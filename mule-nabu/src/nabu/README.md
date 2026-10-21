# M.U.L.E. on the NABU PC (`src/nabu/`)

Built from the MSX client: the same Z80 + TMS9918A + AY-3-8910, so the game
(`src/*.c`) and the screen (`src/tms9918/`) are shared. What differs:

| MSX | NABU |
|---|---|
| FujiNet cartridge ROM, ASCII8 banks, overlays | no cartridge: a `.nabu` homebrew in 64K RAM, **no overlays** (`bank.c`) |
| VDP ports 98h/99h, BIOS interrupt clock | VDP A0h/A1h, our own VDP frame interrupt (`vdp_isr`, below) counts the frames and plays the music; if it never comes, `frame_wait` makes frames up from a spin count |
| PSG A0h/A1h, reg 7 = BEh | PSG 41h latch / 40h data, reg 7 = **7Eh** (port A is the interrupt mask), latch left at reg 14 (`sound.c`) |
| BIOS keyboard/stick | our own keyboard interrupt handler (`nabu_hw.asm`): every code into a 16-code queue, also polled from the 8251 while an HCCA transaction has interrupts off; `input.c` parses keys, arrows (E0h-E3h down, F0h-F3h up) and joystick bytes (A0h-BFh) |
| FujiNet N:TCP over FujiBus | NABU Internet Adapter **RetroNET TCP** (D0-D4) over HCCA (`nstrans.c`, `hcca.c`) |
| appkeys (name, table) | not kept (`appkey.c`): type a name at boot |

Build: `build-nabu.bat` (z88dk nightly; see the README at the top) ->
`r2r/nabu/mule.nabu`, to be served by the Internet Adapter (or MAME with a
null-modem to an adapter).

## Debug output
`set DEBUG=1` before `build-nabu.bat` (every run recompiles everything) adds `-DNABU_DEBUG`: the port then sends lines to the Internet
Adapter's console with its own command, `0xBA, 0x1F, <byte length of msg>, <msg>`
(`dbg.c`, `dbg.h`). Each line starts `MULE `; it is cut at 72 characters. Without
the flag every `dbg*` call compiles to nothing.

What it says: start/quit (`plat.c`); in `nstrans.c` the adapter's version, each
open (host, port, handle or why not), close, the first of any run of "no
answer" / "socket gone" calls, short writes, a read cut short (and how many bytes
were dropped), a refused frame, and the link being declared lost. Not one line a
frame: each is a ~1 ms transaction. Add your own with `dbg(msg)`,
`dbg_s(msg, str)`, `dbg_n(msg, int16)`, `dbg_x(msg, uint16)`, but never inside
an `hcca_begin`..`hcca_end` transaction.

## Music
The song, `music.c` (4,006 bytes: one 66.6 s play of the tune, as AY register writes), plays all the
time: the title, the lobby and the game. **M** turns it off and on (once the name is typed: before,
M is a letter of it). It is played by the VDP's frame interrupt (`plat.c`'s `vdp_frame` ->
`sound.c`'s `snd_frame`), as are the effects' lengths, so nothing hangs while the game is busy.
Effects borrow channel A and give it back. While interrupts are off (an HCCA transaction), a
reply that keeps us waiting ~45 ms has the receive loop set the volumes to 0 (`snd_hush`,
`nabu_hw.asm`; `drain` and `plat_init` call it too) and the next frame puts the music back.
For a build without music, set `nabu_music_len` in `music.c` to 0 (the player does nothing then).

## Frame interrupt
`vdp_isr` (`nabu_hw.asm`) is in the IM 2 table's VDP entry and the VDP is on in the AY's mask. Its
status read resets the VDP's address latch, so a VRAM burst sets `vdp_busy` (`vdp.h`'s VDP_LOCK,
as the Adam's): the handler then takes the VDP out of the mask and sets `vdp_pend`, and
VDP_UNLOCK (`vdp_catch_up`) puts it back, when the frame is handled at once. Interrupts stay on
through bursts, for the keyboard. `frame_tick` has nothing to do; `frame_wait` waits for the
handler's count (made-up frames if it never comes).

## Size
Linked small: `CRT_TRIM_BSS` (no zero bytes in the
file), `CLIB_DEFAULT_SCREEN_MODE=-1`, `CRT_ENABLE_STDIO=0` and `fputc_cons` redirected to a stub
(no z88dk console driver or font). Release about 55.8K with the music, debug about 57.3K (limit 60,401). The
variables end at F16Bh: the stack (from FF00h) went no lower than FE8Dh in play.

## Receive path (hcca_rx_n)
Replies come a byte every ~90 us. Polling and storing them one C call at a time
lost every other byte (the 13-byte version reply came back as 6 or 7), so a
reply is read by `hcca_rx_n` (nabu_hw.asm): the receive mask set once, port B
polled in a tight loop, `hcca_cnt` bytes into `hcca_dst`. 
