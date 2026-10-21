/*
 * bank.c -- the NABU has 64K of RAM and the whole game fits: no overlays,
 * the art and strings stay where they are (the Adam build's phases, art and
 * strings come to about 54K; see the Makefile's nabu section).
 */
#include <string.h>
#include "bank.h"

static uint8_t cur;

const char *bank_init(void)
{
    return 0;
}

void bank_overlay(uint8_t n)
{
    cur = n;
}

uint8_t bank_current(void)
{
    return cur;
}

void bank_get(void *dst, const void *src, uint16_t n)
{
    memcpy(dst, src, n);
}
