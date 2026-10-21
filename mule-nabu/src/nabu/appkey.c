/*
 * appkey.c -- the lobby's appkeys (name, table) are FujiNet's; a NABU has no
 * FujiNet, so they are not kept: the game asks for a name at boot and lists
 * the server's tables. (Keeping them in the adapter's file store, RetroNET
 * E7/E8 by file name, would be the way to remember them; not done yet.)
 */
#ifdef __NABU__
#include <string.h>
#include "mule.h"
#include "config.h"

void appkey_load(void)
{
    uint8_t n;
    sc_name[0] = 0;
    sc_table[0] = 0;
    strncpy(sc_endpt, DEFAULT_ENDPOINT, 64);
    sc_endpt[64] = 0;
    n = (uint8_t)strlen(sc_endpt);
    if (n && sc_endpt[n - 1] == '/')
        sc_endpt[n - 1] = 0;
}

void appkey_save_room(void)
{
}

void appkey_clear_room(void)
{
    sc_table[0] = 0;
}
#endif
