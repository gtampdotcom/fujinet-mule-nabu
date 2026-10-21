/*
 * input.c -- the NABU's keyboard and joysticks, from the queue of codes that
 * nabu_hw.asm's keyboard handler keeps (every code, also those that came
 * while an HCCA transaction had interrupts off).
 *
 * Joystick messages are a prefix (80h joystick 1, 81h joystick 2) then a
 * byte A0h-BFh; the byte is known by its value, so a lost prefix can't make
 * a key of it. Both joysticks steer (one player per NABU).
 *
 * The joystick is a level (the keyboard sends it when it changes). An arrow
 * sends a code when it goes down (0E0h-0E3h) and 10h more when it comes up
 * (seen in MAME): the direction is held between the two. SPACE has no
 * key-up, so a press holds the button for NABU_KEY_HOLD frames. RETURN ends a
 * turn, ESC leaves, 0 sits out, BS rubs out: typed characters as the MSX's.
 * M turns the music off and on, once the name has been typed (RETURN with a
 * name: before that M is a letter of it).
 *
 * The joystick bits are NABU-LIB's (a status byte A0h-BFh: left 1, down 2,
 * right 4, up 8, button 10h). The arrow key codes are the NABU keyboard's
 * special keys: E0h right, E1h left, E2h up, E3h down (key-ups F0h-F3h), as
 * MAME sends them (arrow_dir below).
 */
#include "mule.h"
#include "dbg.h"

uint8_t inp_dir, inp_btn, ev_btn, ev_key;
uint8_t inp_hook, inp_hook_dir, inp_hook_btn, inp_hook_key;

uint8_t kb_get(void);            /* nabu_hw.asm: the next code, 0 if none */
void snd_music_toggle(void);     /* sound.c */

#define ARROW_HELD      255     /* an arrow is down, till its key-up code */
#ifndef NABU_JOY_LEFT
#define NABU_JOY_LEFT   0x01    /* NABU-LIB's Joy_* */
#define NABU_JOY_DOWN   0x02
#define NABU_JOY_RIGHT  0x04
#define NABU_JOY_UP     0x08
#define NABU_JOY_FIRE   0x10
#endif
#ifndef NABU_KEY_HOLD
#define NABU_KEY_HOLD   8       /* frames a key press counts as held */
#endif

static uint8_t btn_prev, btn_latch, key_latch, hold, hold_dir, space_hold;
static uint8_t joy[2], jsel, tapped;
uint8_t kb_named;                       /* M is the music's (the name is typed) */

/* E0h-E3h: right, left, up, down (as MAME sends them) */
static const uint8_t arrow_dir[4] = { DIR_RIGHT, DIR_LEFT, DIR_UP, DIR_DOWN };

static void key(uint8_t k)
{
    uint8_t d;
    dbg_x("key", k);
    if ((k & 0xEC) == 0xE0) {           /* an arrow: E0h-E3h down, F0h-F3h up */
        d = arrow_dir[k & 3];
        if (!(k & 0x10)) {
            hold_dir = d;
            hold = ARROW_HELD;
            tapped = 1;
        } else if (hold_dir == d)
            hold = tapped;              /* up in the same frame as down (a tap): still this frame */
        return;
    }
    if (k == ' ') {
        space_hold = NABU_KEY_HOLD;
        return;
    }
    if ((k == 'm' || k == 'M') && kb_named) {
        snd_music_toggle();
        return;
    }
    if (k == 13 && sc_name[0])
        kb_named = 1;                    /* the name is typed: M is the music's from now */
    if (k >= 'a' && k <= 'z')
        k -= 32;
    if (k == 0x7F)
        k = KEY_BACK;
    if (k == 13 || k == 27 || k == 8 || (k > 32 && k < 96))
        if (key_latch == KEY_NONE)
            key_latch = k;
}

void input_init(void)
{
}

void input_poll(void)
{
    uint8_t dir = 0, btn, k, j;

    tapped = 0;
    while ((k = kb_get()) != 0) {
        if (k >= 0x80 && k <= 0x83)         /* joystick 1-4: its byte follows */
            jsel = k & 3;
        else if ((k & 0xE0) == 0xA0) {      /* a joystick byte (1 and 2 steer) */
            if (jsel < 2)
                joy[jsel] = k & 0x1F;
        } else if (k < 0x80 || k >= 0xE0)   /* (84h-9Fh: paddles, keyboard status) */
            key(k);
    }
    if (hold) {
        dir = hold_dir;
        if (hold != ARROW_HELD)
            --hold;
    }
    btn = 0;
    if (space_hold) {
        btn = 1;
        --space_hold;
    }

    j = joy[0] | joy[1];
    if (j & NABU_JOY_UP)    dir |= DIR_UP;
    if (j & NABU_JOY_DOWN)  dir |= DIR_DOWN;
    if (j & NABU_JOY_LEFT)  dir |= DIR_LEFT;
    if (j & NABU_JOY_RIGHT) dir |= DIR_RIGHT;
    if (j & NABU_JOY_FIRE)  btn = 1;

    if (inp_hook) {
        dir = inp_hook_dir;
        btn = inp_hook_btn;
        if (inp_hook_key) {
            key_latch = inp_hook_key;
            inp_hook_key = 0;
        }
    }
    inp_dir = dir;
    inp_btn = btn;
    if (btn && !btn_prev)
        btn_latch = 1;
    btn_prev = btn;
}

void input_take(void)
{
    ev_btn = btn_latch;
    btn_latch = 0;
    ev_key = key_latch;
    key_latch = KEY_NONE;
}
