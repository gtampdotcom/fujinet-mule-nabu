# M.U.L.E. for the NABU PC

A port of the FujiNet multiplayer M.U.L.E. client to the NABU PC (Z80, TMS9918A, AY-3-8910).
It plays on a tournament server (the default is fujinet.online:9900) through the
NABU Internet Adapter's RetroNET TCP, and boots as a `.nabu` file.

If you just want to play it, instructions are here: https://nabu.ca/software/185

The game logic (`src/*.c`) and the screen (`src/tms9918/`) are shared with the other
FujiNet M.U.L.E. clients (the MSX one in particular). `src/nabu/` is the NABU's part: the
keyboard and joystick, the frame interrupt, the sound and music, and the HCCA link to the
adapter. See `src/nabu/README.md` for how it works.

## Controls
- Arrow keys or a joystick; SPACE is the button; RETURN ends a turn; ESC leaves.
- **M** turns the music on and off (once your name is entered).
- Table list: up/down and SPACE to join, **R** to fetch the list again, ESC to quit.

## Building (Windows)
You need:
- [z88dk](https://www.z88dk.org) (the nightly build, with sdcc), by default in `c:\z88dk\`
  (edit `Z88DK_DIR` in `build-nabu.bat`);
- Python 3 with Pillow (`pip install pillow`);
- your own Atari 8-bit M.U.L.E. disk image (`.atr`, CRC32 F76D2896). The game's graphics are
  made from it. They are not part of this project and are not included.

```
build-nabu.bat "C:\path\M.U.L.E. (1983)(Electronic Arts)(US).atr"
```

That writes `r2r\nabu\mule.nabu` (about 55.8K; the limit is 60,401 bytes) and copies it to
`c:\nabu\local` if that folder exists (edit `LOCAL_DIR`). Without an `.atr` it builds with
stand-in art. Options, as environment variables:
- `ENDPOINT=http://myhost:8080`: the server used when none was chosen;
- `DEBUG=1`: debug messages on the Internet Adapter's console;
- `build-nabu.bat clean` removes the build folders.

Put `mule.nabu` where your Internet Adapter serves local `.nabu` files and boot it from the
NABU's menu.

## Licence
GPLv3 (see `LICENSE`), as the FujiNet M.U.L.E. project this comes from:
https://github.com/FujiNetWIFI/fujinet-multiplayer-mule. The graphics extracted from your disk
belong to their copyright holders.
