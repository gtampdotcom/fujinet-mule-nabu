/*
 * hcca.h -- the NABU's HCCA port as a plain byte pipe, for src/nabu/nstrans.c.
 *
 * The RetroNET protocol (docs: NABU RetroNET Command Protocol Specification)
 * has no framing and answers every request at once, so nstrans.c does each
 * request as one short transaction with interrupts off: hcca_tx the request,
 * hcca_rx the reply. The three calls below are only ever made inside such a
 * transaction (between hcca_begin and hcca_end), and are the only code in the port
 * that touches the HCCA registers (src/nabu/hcca.c).
 */
#ifndef HCCA_H
#define HCCA_H

#include <stdint.h>

void hcca_init(void);
uint8_t ay_vdp_int_on(void);    /* the VDP's bit into the AY interrupt mask (plat.c); the mask now */
void hcca_begin(void);          /* a transaction starts: interrupts off, mask on receive */
void hcca_end(void);            /* ... ends: the mask and interrupts back */
uint8_t hcca_rx_ready(void);    /* non-zero: a byte waits */
uint8_t hcca_rx(void);          /* that byte (only after hcca_rx_ready) */
void hcca_tx(uint8_t c);        /* send one, waiting for the transmitter */

/* Reply bytes arrive every 90 us, faster than C can poll and store them one
 * call at a time (a byte not read before the next lands is lost: the
 * version reply came back 6 or 7 bytes of 13). hcca_rx_n (nabu_hw.asm) is a
 * tight loop that reads hcca_cnt bytes into hcca_dst, waiting up to
 * hcca_tmo1 polls (~97 T-states each) for the first and hcca_tmoN for each
 * next; it returns how many it got. Inside a transaction, like the rest. */
extern uint8_t hcca_pre, hcca_npre, hcca_want, hcca_hdr[4];    /* prefix mode, see nabu_hw.asm */
extern uint8_t *hcca_dst;
extern uint8_t hcca_cnt;
extern uint16_t hcca_tmo1, hcca_tmoN;
extern uint16_t hcca_left;      /* hcca_tmo1 less the polls the first byte took (~27 ms a 1000) */
uint8_t hcca_rx_n(void);
void kb_poll(void);             /* nabu_hw.asm: with interrupts off, take a waiting keyboard code */
void snd_hush(void);            /* nabu_hw.asm: the AY's volumes to 0 till the next frame (sound.c) */


#endif
