/*
 * plat.c -- the NABU PC: start-up, the frame clock and quitting.
 *
 * The clock is the VDP's frame interrupt: our handler (nabu_hw.asm's vdp_isr,
 * in the IM 2 table in place of z88dk's) calls vdp_frame below, which reads
 * the status (frame flag, fifth sprite), counts the frame and plays the music
 * (sound.c's snd_frame), so the music keeps time and no note hangs while the
 * game is busy drawing a screen or waiting. The VDP's bit is on in the AY's
 * interrupt mask (hcca.c's ay_vdp_int_on). Reading the status resets the VDP's
 * address latch, so every VRAM burst runs with interrupts off (vdp.h's
 * VDP_LOCK / VDP_UNLOCK: DI / EI, as on the MSX).
 *
 * The shared code calls frame_tick in its long loops (as for the Apple II):
 * here the interrupt counts, so it has nothing to do. A frame missed while the
 * adapter is talked to (interrupts off) is put right by main.c's clock_sync
 * (frame_lost). If the interrupt never comes, frame_wait makes the frames up
 * from a spin count, so the game still runs.
 */
#include "mule.h"
#include "vdp.h"
#include "hcca.h"
#include "dbg.h"

uint8_t vdp_status(void);           /* nabu_hw.asm */
void kb_install(void);
void vdp_install(void);
void snd_frame(void);               /* sound.c: the music, a frame on */
void snd_silence(void);

uint16_t frame_count;
static uint16_t seed = 0x1234;
static uint8_t synth;               /* frames made up in a row (0: the interrupt works) */

#define FC (*(volatile uint16_t *)&frame_count)

/* frame_wait's spins (a spin is ~60 T-states: ~1000 to a 60 Hz frame): before a
 * frame is made up, and, once the interrupt has plainly failed, between the
 * made-up ones (about one frame). */
#define SPINS_WAIT   5000u
#define SPINS_SYNTH  1000u
#define SYNTH_TRUST  3

/* The VDP's frame interrupt (nabu_hw.asm's vdp_isr: registers saved, interrupts off). */
void vdp_frame(void)
{
    uint8_t s = vdp_status();       /* (reading it lets the interrupt go) */
    vdp_seen |= s;
    if (s & 0x80) {
        ++frame_count;
        snd_frame();
    }
}

const char *plat_init(void)
{
    uint8_t m;

    snd_hush();                     /* whatever the boot ROM left sounding */
    hcca_init();
    kb_install();                   /* our keyboard handler (nabu_hw.asm) */
    m = ay_vdp_int_on();            /* int_mask: before the frame's handler can use it */
    vdp_install();                  /* our frame handler */
    gfx_init();
    dbg("start");
    dbg_x("AY int mask set to", m);
    (void)m;
    return 0;
}

/* Back to the boot ROM / adapter menu. NOTE: if the ROM is paged out this
 * needs the control-port write that brings it back (see NABU-LIB); check. */
void plat_quit(void)
{
    dbg("quit");
    snd_silence();                  /* no note hangs on in the boot ROM */
    __asm__("di");
    __asm__("rst 0");
}

void frame_tick(void)
{
}

void frame_wait(void)
{
    static uint16_t seen;
    uint16_t spins = 0, limit;
#ifdef NABU_DEBUG
    static uint8_t said_ok, said_synth;
#endif
    limit = synth >= SYNTH_TRUST ? SPINS_SYNTH : SPINS_WAIT;
    while (FC == seen) {
        if (++spins >= limit) {     /* no interrupt: make the frame up */
            __asm__("di");
            ++frame_count;
            __asm__("ei");
            if (synth < 255)
                ++synth;
#ifdef NABU_DEBUG
            if (!said_synth) {
                said_synth = 1;
                dbg_x("no VDP interrupt, synth", vdp_seen);
            }
#endif
            break;
        }
    }
    if (spins < limit)
        synth = 0;
#ifdef NABU_DEBUG
    if (!synth && !said_ok) {
        said_ok = 1;
        dbg_n("VDP interrupt ok, spins", (int16_t)spins);
    }
#endif
    seen = FC;
    seed += seen;
#ifdef NABU_DEBUG
    if (!(seen & 0x01FF))           /* about every 8 s */
        dbg_n("frames", (int16_t)seen);
#endif
}

void frame_lost(uint8_t jiffies)
{
    __asm__("di");
    frame_count += jiffies;
    __asm__("ei");
}

uint8_t plat_rand(void)
{
    seed ^= seed << 7;
    seed ^= seed >> 9;
    seed ^= seed << 8;
    return (uint8_t)seed;
}
