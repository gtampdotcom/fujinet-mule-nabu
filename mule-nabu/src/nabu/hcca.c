/*
 * hcca.c -- the NABU's HCCA port: the only code that touches its registers.
 *
 * The method is the boot ROM's own (DJ Sures' annotated U53 ROM:
 * check_kbd_interrupt / hcca_wait_response / the OUT (80h) sites), which is
 * known to work, in MAME too, as it is what loaded this program:
 *   - data port 80h; the AY-3-8910 at 41h (latch) and 40h (data);
 *   - AY register 14 (port A) is the interrupt mask: 80h HCCA receive,
 *     40h HCCA send; register 15 (port B) is the status;
 *   - to ask "is a byte waiting?" the mask is written 80h and port B's
 *     BIT 0 read: SET = a byte waits. To ask "may I send?" the mask is
 *     written 40h and bit 0 read: SET = the transmitter is ready.
 *     (An earlier version of this file read other bits, from NABU-LIB's
 *     transmit loop, and never saw a byte arrive.)
 *   - a transaction runs with interrupts off (hcca_begin); the mask we
 *     found (z88dk's: VDP and keyboard) is put back by hcca_end.
 */
#include "hcca.h"
#include "dbg.h"

__sfr __at 0x80 IO_HCCA;
__sfr __at 0x40 IO_AY_DATA;
__sfr __at 0x41 IO_AY_LATCH;

#define PSG_REG_IO_A 14
#define PSG_REG_IO_B 15
#define INT_MASK_HCCARX 0x80
#define INT_MASK_HCCATX 0x40
#define ST_READY   0x01         /* port B bit 0: set = the masked-in event (rx byte / tx free) */

static uint8_t saved_mask;

uint8_t *hcca_dst;                  /* hcca_rx_n's arguments (nabu_hw.asm) */
uint8_t hcca_cnt;
uint8_t hcca_pre, hcca_npre, hcca_want, hcca_hdr[4];
uint16_t hcca_tmo1, hcca_tmoN, hcca_left;

static void mask(uint8_t m)
{
    IO_AY_LATCH = PSG_REG_IO_A;
    IO_AY_DATA = m;
}

void hcca_init(void)
{
}

/* The VDP's frame interrupt is wired to the Z80 through the AY's interrupt
 * mask (port A, bit 10h): on, for plat.c's frame clock (nabu_hw.asm's vdp_isr
 * is in the IM 2 table by now). */
#define INT_MASK_VDP 0x10
uint8_t int_mask;                   /* the mask, VDP on (nabu_hw.asm's vdp_isr / vdp_catch_up) */
uint8_t ay_vdp_int_on(void)
{
    uint8_t m;

    __asm__("di");
    IO_AY_LATCH = PSG_REG_IO_A;
    m = IO_AY_DATA;
    int_mask = (uint8_t)(m | INT_MASK_VDP);
    IO_AY_DATA = int_mask;
    m = IO_AY_DATA;                 /* (the latch stays at register 14) */
    __asm__("ei");
    return m;
}

void hcca_begin(void)
{
    __asm__("di");
    IO_AY_LATCH = PSG_REG_IO_A;
    saved_mask = IO_AY_DATA;
    mask(INT_MASK_HCCARX);
}

void hcca_end(void)
{
    mask(saved_mask);               /* (leaves the latch at register 14) */
    __asm__("ei");
}

uint8_t hcca_rx_ready(void)
{
    mask(INT_MASK_HCCARX);              /* (the ROM writes it on every look) */
    IO_AY_LATCH = PSG_REG_IO_B;
    return IO_AY_DATA & ST_READY;
}

uint8_t hcca_rx(void)
{
    return IO_HCCA;
}

void hcca_tx(uint8_t c)
{
    uint16_t t = 0;
    mask(INT_MASK_HCCATX);
    IO_AY_LATCH = PSG_REG_IO_B;
    while (!(IO_AY_DATA & ST_READY))
        if (++t == 0)
            break;                  /* (never: but never hang) */
    IO_HCCA = c;
    mask(INT_MASK_HCCARX);
}

#ifdef NABU_DEBUG
/* What port B reads when idle, under no mask, the receive mask and the send
 * mask: if the receive reading has bit 0 set with nothing sent, polling for
 * a byte can never tell (and drain() would never end). Logged after the
 * transaction. */
#endif
