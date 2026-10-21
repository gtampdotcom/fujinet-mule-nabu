/*
 * dbg.c -- messages to the NABU Internet Adapter's console (see dbg.h):
 * BA 1F <length> <message>, one short HCCA transaction each. The adapter
 * answers nothing to it, so nothing is read back.
 */
#ifdef NABU_DEBUG

#include <stdint.h>
#include "hcca.h"
#include "dbg.h"

#define RN_IA_CTRL 0xBA
#define RN_IA_MSG  0x1F

static char buf[DBG_MAX];
static uint8_t blen;

static void put_s(const char *s)
{
    while (*s && blen < DBG_MAX)
        buf[blen++] = *s++;
}

static void put_c(char c)
{
    if (blen < DBG_MAX)
        buf[blen++] = c;
}

static void put_dec(int16_t v)
{
    char d[6];
    uint8_t i = 0;
    uint16_t u = (uint16_t)v;

    if (v < 0) {
        put_c('-');
        u = (uint16_t)(0 - u);
    }
    do {
        d[i++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    while (i)
        put_c(d[--i]);
}

static void put_hex(uint16_t v)
{
    uint8_t i, h;

    put_s("0x");
    for (i = 0; i < 4; ++i) {
        h = (uint8_t)((v >> 12) & 0x0F);
        put_c((char)(h < 10 ? '0' + h : 'A' - 10 + h));
        v <<= 4;
    }
}

static void start(const char *msg)
{
    blen = 0;
    put_s("MULE ");
    put_s(msg);
}

static void flush_msg(void)
{
    uint8_t i;

    hcca_begin();
    hcca_tx(RN_IA_CTRL);
    hcca_tx(RN_IA_MSG);
    hcca_tx(blen);
    for (i = 0; i < blen; ++i)
        hcca_tx((uint8_t)buf[i]);
    hcca_end();
}

void dbg(const char *msg)
{
    start(msg);
    flush_msg();
}

void dbg_s(const char *msg, const char *s)
{
    start(msg);
    put_s(": ");
    put_s(s);
    flush_msg();
}

void dbg_n(const char *msg, int16_t v)
{
    start(msg);
    put_s(": ");
    put_dec(v);
    flush_msg();
}

void dbg_x(const char *msg, uint16_t v)
{
    start(msg);
    put_s(": ");
    put_hex(v);
    flush_msg();
}

#endif
