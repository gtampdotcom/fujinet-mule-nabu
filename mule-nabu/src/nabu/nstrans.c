/*
 * nstrans.c -- stream mode's bytes (src/nsnet.c) on the NABU PC: a TCP
 * socket made by the NABU Internet Adapter (RetroNET), spoken to over the
 * HCCA port with the RetroNET TCP client commands.
 *
 *   D0 open   D0 len host[len] port(16) handle(FF = assign) -> handle (FF: failed)
 *   D1 close  D1 handle
 *   D2 avail  D2 handle -> int32 bytes waiting (-1: disconnected)
 *   D3 read   D3 handle len(16) -> int32 n (-1: disconnected), n bytes
 *   D4 write  D4 handle len(16) data -> int32 written (-1: disconnected)
 *
 * (Integers are little-endian; there is no framing: the reply must be read
 * exactly. BA 16 asks the adapter for its version string: nt_detect.)
 *
 * Unlike FujiNet's stream, an adapter's answer comes straight after the
 * request (the adapter, not a socket, is the other end of the wire), so a
 * "call" here is one short transaction with interrupts off, a millisecond or
 * two. They are made at most one status call a frame (and a read only when
 * the status says bytes wait), like nstrans_fb.c's, so the game keeps its
 * pace; a frame or two lost to a slow adapter is put right by main.c's
 * clock_sync, as for any other platform.
 *
 * A reply that doesn't come (the adapter busy or gone) times out; the wire
 * is then drained so the next call starts clean.
 */
#if defined(__NABU__) && defined(PLAT_NETSTREAM)

#include <string.h>
#include <stdint.h>
#include "plat.h"
#include "nstrans.h"
#include "hcca.h"
#include "dbg.h"

/* Every status call is a round trip through the adapter (through an
 * emulator's socket too: several ms), and the game stops while it waits. With
 * nothing arriving the socket is asked about every NT_IDLE_FRAMES frames (4:
 * 15 times a second; a reply to a status call that found bytes is followed
 * by a call the next frame). */
#ifndef NT_IDLE_FRAMES
#define NT_IDLE_FRAMES 4
#endif

#define RN_TCP_OPEN  0xD0
#define RN_TCP_CLOSE 0xD1
#define RN_TCP_AVAIL 0xD2
#define RN_TCP_READ  0xD3
#define RN_TCP_WRITE 0xD4
#define RN_IA_CTRL   0xBA
#define RN_IA_VERSION 0x16

#define H_ANY 0xFF

/* Polls of the HCCA status (hcca_rx_n: ~97 T-states with its looks at the
 * keyboard and the hush, ~37,000 a second) for a
 * reply's first byte, and for each next one: the first ~0.9 s (an emulator
 * relaying the HCCA over a socket to the adapter, or a USB-serial adapter,
 * can take tens of ms a way), the next ~85 ms (a byte is 90 us on the
 * wire). Only a call that fails pays them in full. */
#ifndef RN_FIRST
#define RN_FIRST 34000U
#endif
#ifndef RN_NEXT
#define RN_NEXT  10500U
#endif
#ifndef RN_QUIET
#define RN_QUIET 6000U              /* polls of silence that end a drain: ~0.3 s */
#endif
#ifndef RN_OPEN_WAIT
#define RN_OPEN_WAIT 12U        /* RN_FIRST waits for the reply of an open: ~11 s (DNS, a TCP connect) */
#endif

#define DOWN_AFTER 3            /* statuses saying the socket is shut */
#define BAD_AFTER  8            /* calls in a row without an answer (each ~1.5 s) */
#define WRITE_TRIES 4
#define RBUF       128          /* the most a read takes */
#define FBUF       32           /* a frame being put */
#define QBUF       96           /* flushed frames waiting */

#ifdef NABU_HOST_TEST
#define DI()
#define EI()
#else
#define DI() hcca_begin()
#define EI() hcca_end()
#endif

/* nt_dropped is nsnet.c's (nstrans.h) */

static uint8_t handle = H_ANY;
static uint8_t down, bad, dropped;
static uint16_t drained;                /* (debug: what the last drain took) */
static uint8_t rbuf[RBUF];
static uint8_t said_io, navail;
static uint8_t rd_hdr, rd_n, rd_got;    /* (debug) how far the last read got */                 /* (debug: how many first calls were said) */
static uint8_t rlen, rpos;
static uint16_t polled;                 /* the frame the next status call is due on */
static uint8_t fbuf[FBUF], flen, fover;
static uint8_t qbuf[QBUF], qlen;
static uint16_t want;

/* Debug notes (src/nabu/dbg.h) of a run of failures: the first of a run is
 * said, not each of the ninety. Always made outside a transaction. */
static void note_bad(const char *what)
{
    (void)what;
    if (bad == 0)
        dbg(what);
    if (bad < 255)
        ++bad;
}

static void note_down(const char *what)
{
    (void)what;
    if (down == 0)
        dbg(what);
    if (down < 255)
        ++down;
}

/* ---- the wire ------------------------------------------------------------ */

/* n reply bytes into dst (hcca_rx_n: a tight loop, no byte lost); how many
 * came: fewer than n if one didn't within its time. */
static uint8_t rxn(uint8_t *dst, uint8_t n, uint16_t first)
{
    hcca_pre = 0;
    hcca_dst = dst;
    hcca_cnt = n;
    hcca_tmo1 = first;
    hcca_tmoN = RN_NEXT;
    return hcca_rx_n();
}

/* A reply with a prefix, all in one call (no byte lost between the parts):
 * pre (1 or 4) bytes into hcca_hdr, then as many payload bytes as the
 * prefix's first byte says (an int32 prefix: if it is 0..255), at most max,
 * into dst. Returns the payload bytes; hcca_npre the prefix bytes got. */
static uint8_t rxp(uint8_t pre, uint8_t *dst, uint8_t max, uint16_t first)
{
    hcca_pre = pre;
    hcca_dst = dst;
    hcca_cnt = max;
    hcca_tmo1 = first;
    hcca_tmoN = RN_NEXT;
    return hcca_rx_n();
}

/* A little-endian int32 reply (the Z80 is little-endian too). */
static uint8_t rx32(int32_t *v, uint16_t first)
{
    return rxn((uint8_t *)v, 4, first) == 4;
}

/* Whatever is on the wire is stale (a call that timed out): drop it. */
static void drain(void)
{
    uint16_t quiet = RN_QUIET;
    drained = 0;
#ifndef NABU_HOST_TEST
    snd_hush();                         /* (interrupts are off for a while: no note may hang) */
#endif
    while (quiet--) {
        kb_poll();                      /* (interrupts are off: keep the keyboard's codes) */
        if (hcca_rx_ready()) {
            (void)hcca_rx();
            quiet = RN_QUIET;
            if (++drained == 1024)      /* never: a stuck "ready" must not hang the machine */
                break;
        }
    }
}

static void tx16(uint16_t v)
{
    hcca_tx((uint8_t)v);
    hcca_tx((uint8_t)(v >> 8));
}

/* Each call below is one transaction: DI .. EI. A failed one drains. */

/* D2: bytes waiting; -1 gone. 0 and ok = 0: no answer. */
static uint8_t call_avail(int32_t *n)
{
    uint8_t ok;
    DI();
    hcca_tx(RN_TCP_AVAIL);
    hcca_tx(handle);
    ok = rx32(n, RN_FIRST);
    if (!ok)
        drain();
    EI();
    return ok;
}

/* D3: up to `max` bytes into rbuf; *n their count (-1 gone). */
static uint8_t call_read(uint8_t max, int32_t *n)
{
    uint8_t ok;
    DI();
    hcca_tx(RN_TCP_READ);
    hcca_tx(handle);
    tx16(max);
    rd_got = rxp(4, rbuf, max, RN_FIRST);       /* the int32 n, then n bytes */
    rd_hdr = ok = (hcca_npre == 4);
    rd_n = 0;
    if (ok) {
        memcpy(n, hcca_hdr, 4);
        if (*n > 0) {
            rd_n = (uint8_t)*n;
            if (*n > max || rd_got != rd_n)     /* (never more than max: we asked for max) */
                ok = 0;
        }
    }
    if (!ok)
        drain();
    EI();
    return ok;
}

/* D4: write n bytes; *wrote their count (-1 gone). */
static uint8_t call_write(const uint8_t *p, uint8_t n, int32_t *wrote)
{
    uint8_t ok, i;
    DI();
    hcca_tx(RN_TCP_WRITE);
    hcca_tx(handle);
    tx16(n);
    for (i = 0; i < n; ++i)
        hcca_tx(p[i]);
    ok = rx32(wrote, RN_FIRST);
    if (!ok)
        drain();
    EI();
    return ok;
}

/* ---- the link ------------------------------------------------------------- */

/* Is there an adapter? Its version string comes back (BA 16: length, text);
 * a NABU run any other way (a bare MAME) says nothing. */
uint8_t nt_detect(void)
{
    uint8_t ok = 1, len = 0, got = 0, t;
    dbg("detect: asking BA 16");
    drained = 0;
    DI();
    hcca_init();
    drain();                            /* what an earlier message left */
    hcca_tx(RN_IA_CTRL);
    hcca_tx(RN_IA_VERSION);
    for (t = 0; t < 3; ++t) {                   /* the length byte, then the text */
        got = rxp(1, rbuf, RBUF - 1, RN_FIRST);
        if (hcca_npre)
            break;
    }
    len = hcca_hdr[0];
    /* (the spec's example is 13 bytes; the text is read into rbuf, and what
     * doesn't fit is drained, so the wire stays in step) */
    if (!hcca_npre || !len || got != (len > RBUF - 1 ? RBUF - 1 : len))
        ok = 0;
    if (!ok || len > RBUF - 1)
        drain();
    EI();
#ifdef NABU_DEBUG
    rbuf[got > 32 ? 32 : got] = 0;
    if (ok)
        dbg_s("adapter version", (char *)rbuf);
    else
        dbg("no adapter answered BA 16");
    dbg_n("detect: len", len);
    dbg_n("detect: got", got);
#endif
    return ok;
}

/* The connection is made by the adapter, here (a wait, once). */
uint8_t nt_open(const char *host, uint16_t port)
{
    uint8_t n = 0, i, hb = H_ANY, got = 0;
    uint16_t t;
    int16_t h;

    if (handle != H_ANY)
        (void)nt_close();
    while (host[n] && n < 60)
        ++n;
    dbg_s("open", host);
    dbg_n("open port", (int16_t)port);
    DI();
    drain();
    hcca_tx(RN_TCP_OPEN);
    hcca_tx(n);
    for (i = 0; i < n; ++i)
        hcca_tx((uint8_t)host[i]);
    tx16(port);
    hcca_tx(H_ANY);
    for (t = 0; t < RN_OPEN_WAIT && !got; ++t)
        got = rxn(&hb, 1, RN_FIRST);
    h = got ? (int16_t)hb : -1;
    if (!got)
        drain();
    EI();
    said_io = 0;
    if (h < 0 || h == H_ANY) {
        dbg(h < 0 ? "open: no answer" : "open: adapter refused (FF)");
        return 0;
    }
    dbg_n("open: handle", h);
    handle = (uint8_t)h;
    rlen = rpos = 0;
    flen = fover = qlen = 0;
    down = bad = dropped = 0;
    polled = frame_count;
    return 1;
}

uint8_t nt_close(void)
{
    if (handle != H_ANY) {
        DI();
        hcca_tx(RN_TCP_CLOSE);
        hcca_tx(handle);
        EI();
        dbg_n("close: handle", handle);
        handle = H_ANY;
    }
    qlen = flen = fover = 0;
    rlen = rpos = 0;
    down = bad = dropped = 0;
    return 1;
}

/* Nothing is ever left going between our calls. */
void nt_quiesce(void)
{
}

/* The flushed frames go out (they go whole, or the stream is out of step:
 * a short write sends on what is left). */
static void write_queue(void)
{
    int32_t wrote;
    uint8_t tries;
    for (tries = 0; tries < WRITE_TRIES; ++tries) {
        wrote = 0;
        if (call_write(qbuf, qlen, &wrote)) {
            if (wrote == (int32_t)qlen) {
                qlen = 0;
                bad = 0;
                return;
            }
            if (wrote < 0) {            /* the socket is gone */
                note_down("write: socket gone");
                qlen = 0;               /* (nt_flush said it went: the */
                return;                 /* server's silence is the answer) */
            }
            if (wrote > 0 && wrote < (int32_t)qlen) {
                dbg_n("short write", (int16_t)wrote);
                memmove(qbuf, qbuf + wrote, qlen - (uint8_t)wrote);
                qlen -= (uint8_t)wrote;
            }
        }
    }
    qlen = 0;
    note_bad("write: no answer");
}

/* One look at the adapter: what is queued goes out; else the socket is asked
 * how much waits (once a frame), and read if some does. */
static void step(void)
{
    int32_t n;

    if (handle == H_ANY || rpos < rlen)
        return;
    if (qlen) {
        write_queue();
        return;
    }
    if ((int16_t)(frame_count - polled) < 0)    /* once a frame at most */
        return;
    polled = frame_count + 1;
    if (!call_avail(&n)) {
        note_bad("avail: no answer");
        return;
    }
    bad = 0;
#ifdef NABU_DEBUG
    if (!(++navail & 63))               /* every 64th status call (about 4 s) */
        dbg_n("avail wait, polls (1000 = 27 ms)", (int16_t)(RN_FIRST - hcca_left));
    if (said_io < 6 && n != 0) {        /* the first few non-empty answers */
        ++said_io;
        dbg_n("avail", (int16_t)n);
    }
#endif
    if (n < 0) {
        note_down("avail: socket gone");
        return;
    }
    down = 0;
    if (n == 0) {
        polled = frame_count + NT_IDLE_FRAMES;  /* nothing coming: ask less often */
        return;
    }
    want = n > RBUF ? RBUF : (uint16_t)n;
    if (!call_read((uint8_t)want, &n)) {
        dropped = 1;                    /* some were taken from the socket */
        nt_dropped = want;
        dbg_n("read cut short, dropped", (int16_t)want);
        if (bad == 0) {                 /* the first of a run: how far it got */
            dbg_n("read header got", rd_hdr);
            dbg_n("read n", rd_n);
            dbg_n("read bytes got", rd_got);
            dbg_n("read drained after", (int16_t)drained);
        }
        note_bad("read: no answer");
        return;
    }
    if (n < 0) {
        note_down("read: socket gone");
        return;
    }
    rlen = (uint8_t)n;
    rpos = 0;
#ifdef NABU_DEBUG
    if (said_io < 6) {
        ++said_io;
        dbg_n("read", (int16_t)n);
    }
#endif
}

uint8_t nt_get(char *c)
{
    if (rpos >= rlen) {
        step();
        if (dropped) {
            dropped = 0;
            return NT_DROP;
        }
        if (down >= DOWN_AFTER || bad >= BAD_AFTER) {
            dbg_n(down >= DOWN_AFTER ? "link lost: socket shut" : "link lost: no answers",
                  (int16_t)(down >= DOWN_AFTER ? down : bad));
            return NT_LOST;
        }
    }
    if (rpos < rlen) {
        *c = (char)rbuf[rpos++];
        return 1;
    }
    return 0;
}

/* A frame is gathered, then queued for the next write. */
uint8_t nt_put(uint8_t c)
{
    if (flen >= sizeof(fbuf)) {
        fover = 1;
        return 0;
    }
    fbuf[flen++] = c;
    return 1;
}

uint8_t nt_flush(void)
{
    uint8_t ok = 0;
    if (flen && !fover && qlen + flen <= sizeof(qbuf)) {
        memcpy(qbuf + qlen, fbuf, flen);
        qlen += flen;
        ok = 1;
        if (handle != H_ANY)
            write_queue();              /* it goes now */
    } else if (flen) {
        dbg_n(fover ? "frame too long" : "queue full", (int16_t)flen);
    }
    flen = fover = 0;
    return ok;
}

#endif
