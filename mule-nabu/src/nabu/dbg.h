/*
 * dbg.h -- debug output to the NABU Internet Adapter's console.
 *
 * The adapter takes commands from the NABU's software too: send
 *     0xBA, 0x1F, <byte length of msg>, <msg>
 * and msg is shown on the adapter's console. dbg.c does that, with the HCCA
 * port (hcca.h) and nothing else.
 *
 * Built only with -DNABU_DEBUG (make nabu DEBUG=1; set DEBUG=1 for
 * build-nabu.bat); without it every call below is nothing at all, so the
 * 64K image is not a byte bigger. Rebuild clean after changing it.
 *
 * RULES
 *  - Never call these inside a hcca_begin .. hcca_end transaction (a message
 *    in the middle of a request is a request the adapter can't read): each
 *    call here is a transaction of its own.
 *  - Every message starts "MULE ", and is cut at DBG_MAX characters.
 *  - Keep to events (a connect, a failure), not one a frame: each call holds
 *    the machine for ~1 ms per 10 characters.
 */
#ifndef DBG_H
#define DBG_H

#include <stdint.h>

#ifdef NABU_DEBUG
#define DBG_MAX 72

void dbg(const char *msg);                      /* MULE msg */
void dbg_s(const char *msg, const char *s);     /* MULE msg: s */
void dbg_n(const char *msg, int16_t v);         /* MULE msg: -123 */
void dbg_x(const char *msg, uint16_t v);        /* MULE msg: 0x00FF */
#else
#define dbg(m)      ((void)0)
#define dbg_s(m, s) ((void)0)
#define dbg_n(m, v) ((void)0)
#define dbg_x(m, v) ((void)0)
#endif

#endif
