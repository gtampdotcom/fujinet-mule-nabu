/*
 * sound.c -- the effects and the music on the NABU's PSG (an AY-3-8910, ports 41h
 * latch, 40h data).
 *
 * Effects: the table is the Apple II's (src/apple2/sound.c) as the Adam plays it
 * (src/adam/sound.c): a delay d there is a tone of 36428 / d Hz, which on the NABU's
 * PSG (the manual: f = 223750 / period) a period of 6.14 d. Channel A plays them;
 * the frame interrupt times them (snd_frame), so an effect ends on time even while
 * the game is busy.
 *
 * Music (the NABU only): the song in music.c, all the time: the title, the lobby and the game. M turns it
 * off and on (input.c). It is played by the VDP's frame interrupt (plat.c's vdp_frame
 * calls snd_frame), so it keeps time and no note hangs while the game is busy (a
 * screen being drawn, a table being joined). The one time the interrupt can't run is
 * an HCCA transaction (interrupts off): if a reply keeps us waiting (~45 ms), the
 * receive loop (nabu_hw.asm) sets the three volumes to 0 and snd_hushed, and the next
 * frame puts the music's registers back.
 *
 * Records: a byte with the number of register writes (high nibble) and the frames to
 * the next record (low nibble), then (register, value) pairs. Every register the music
 * sets is kept (mreg), to put back after a hush or when it is turned on again.
 * An effect borrows channel A: meanwhile the music's writes to it (registers 0, 1, 8)
 * only go into mreg, and when the effect ends they go back to the chip.
 */
#include "sound.h"

extern const uint8_t nabu_music[];
extern const uint16_t nabu_music_len;

__sfr __at 0x41 PSG_ADDR;
__sfr __at 0x40 PSG_DATA;
#define PSG_REG_IO_A 14         /* the latch is left here (z88dk's habit; ours too) */
#define MIXER_ALONE  0x7E       /* tone A only; port A (interrupt mask) out, B (status) in:
                                 * the NABU's, not the MSX's 0xBE */

typedef struct {
    uint8_t d1, f1;         /* delay, frames */
    uint8_t d2, f2;         /* then a second tone (f2 = 0: none) */
} Effect;

static const Effect fx[SND_COUNT] = {
    {  42,  3,   0,  0 },   /* select */
    { 132,  3,   0,  0 },   /* sample */
    { 255,  8,   0,  0 },   /* error (as low as it goes) */
    {  33,  4,   0,  0 },   /* buy */
    {  35,  8,  25, 14 },   /* alert: the only two notes, and the loudest */
    {  70,  6,   0,  0 },   /* auction over */
    {  30,  2,   0,  0 },   /* trade */
    { 165,  8,   0,  0 },   /* event */
    {  47, 10,   0,  0 },   /* founder */
    { 200,  1,   0,  0 },   /* step: a soft click */
    {  13,  2,   0,  0 },   /* tick: the time bar running out (map.c) */
    {  18,  2,   0,  0 },   /* tock */
};

static volatile uint8_t cur_f;      /* frames of the effect left (the interrupt counts) */
static uint8_t next_d, next_f, muted, soft, vol;

volatile uint8_t snd_hushed;        /* nabu_hw.asm set the volumes to 0 (a long wait) */
static volatile uint8_t playing;    /* the music is on: the interrupt plays it */
static uint8_t music_off;           /* M turned it off */
static uint8_t ready;               /* snd_init has run */
static const uint8_t *mp;           /* the music's next record (0: not started) */
static uint8_t mwait;               /* frames to it */
static uint8_t mreg[14];            /* the registers as the music set them */

/* A register, from the game's loop: interrupts off, so the frame's music can't
 * come between the latch and the data. */
static void psg(uint8_t r, uint8_t v)
{
    __asm__("di");
    PSG_ADDR = r;
    PSG_DATA = v;
    PSG_ADDR = PSG_REG_IO_A;
    __asm__("ei");
}

/* ... from the interrupt (interrupts are off already) */
static void psg_i(uint8_t r, uint8_t v)
{
    PSG_ADDR = r;
    PSG_DATA = v;
    PSG_ADDR = PSG_REG_IO_A;
}

/* Channel A to an effect's tone (interrupts off). */
static void tone_i(uint8_t d)
{
    uint16_t n = d * 6u + (d * 36u >> 8);          /* 6.14 d */
    psg_i(0, (uint8_t)n);
    psg_i(1, (uint8_t)(n >> 8));
    psg_i(8, vol);
}

/* Every register the music has set back on the chip (channel A's only if no effect
 * has it); not 13, as writing it restarts the envelope (a drum beat). Interrupts off. */
static void music_restore(void)
{
    uint8_t r;
    for (r = 0; r < 13; ++r)
        if (cur_f && (r <= 1 || r == 8))
            continue;
        else
            psg_i(r, r == 7 ? mreg[7] | 0x40 : mreg[r]);
}

/* Channel A: back to the music, or quiet (interrupts off). */
static void quiet_i(void)
{
    if (playing) {
        psg_i(0, mreg[0]);
        psg_i(1, mreg[1]);
        psg_i(8, mreg[8]);
    } else
        psg_i(8, 0);
}

/* The music on or off as it should be now (from the game's loop). */
static void music_set(void)
{
    uint8_t want = ready && !muted && !music_off && nabu_music_len;
    if (want == playing)
        return;
    __asm__("di");
    if (want) {
        if (!mp) {
            mp = nabu_music;            /* the first record sets the mixer, volumes and envelope */
            mwait = 0;
        } else
            music_restore();            /* on where it was turned off */
        playing = 1;
    } else {
        playing = 0;
        psg_i(7, MIXER_ALONE);
        psg_i(9, 0);
        psg_i(10, 0);
        if (!cur_f)
            psg_i(8, 0);
    }
    __asm__("ei");
}

/* A frame: from the VDP's interrupt (plat.c's vdp_frame). */
void snd_frame(void)
{
    uint8_t n, r, v;
    if (snd_hushed) {                   /* a long wait set the volumes to 0 */
        snd_hushed = 0;
        if (playing)
            music_restore();
        if (cur_f)
            psg_i(8, vol);
    }
    if (cur_f && !--cur_f) {            /* the effect's time is up */
        if (next_f) {
            tone_i(next_d);
            cur_f = next_f;
            next_f = 0;
        } else
            quiet_i();
    }
    if (!playing)
        return;
    if (mwait && --mwait)
        return;
    if (mp >= nabu_music + nabu_music_len)
        mp = nabu_music;                /* (the last record's frames ran to the song's end: it loops) */
    n = *mp >> 4;
    mwait = *mp++ & 15;
    while (n--) {
        r = *mp++;
        v = *mp++;
        mreg[r] = v;
        if (r == 7)
            v |= 0x40;                  /* port A out (the interrupt mask), B in */
        if (cur_f && (r <= 1 || r == 8))
            continue;                   /* an effect has channel A */
        psg_i(r, v);
    }
}

void snd_init(void)
{
    psg(7, MIXER_ALONE);
    psg(8, 0);
    psg(9, 0);
    psg(10, 0);
    ready = 1;
    music_set();
}

void snd_play(uint8_t id)
{
    const Effect *e = &fx[id];
    if (muted || (SND_SOFT(id) && cur_f && !soft))
        return;                     /* a footstep or tick cuts only its own */
    __asm__("di");
    soft = SND_SOFT(id);
    vol = id == SND_ALERT ? 15 : soft ? 8 : 11;  /* the alert loudest */
    cur_f = e->f1;                  /* (the music leaves channel A alone from now) */
    tone_i(e->d1);
    next_d = e->d2;
    next_f = e->f2;
    __asm__("ei");
}

/* The game's loop, once a frame: the effects and the music are the interrupt's;
 * this only turns the music on or off as the settings say. */
void snd_tick(void)
{
    music_set();
}

void snd_toggle_mute(void)
{
    __asm__("di");
    muted ^= 1;
    cur_f = 0;
    quiet_i();
    __asm__("ei");
    music_set();
}

/* M (input.c): the music off, or on again where it was. */
void snd_music_toggle(void)
{
    music_off ^= 1;
    music_set();
}

/* Everything quiet (leaving the game: no note may hang on in the boot ROM). */
void snd_silence(void)
{
    music_off = 1;
    __asm__("di");
    cur_f = 0;
    __asm__("ei");
    music_set();
    psg(8, 0);
}
