<div align="center">

# Epic Pinball, recompiled

**Epic Pinball** (Epic MegaGames, 1993) — all thirteen tables of the GOG
*Complete Collection*, taken apart from their shipping DOS executables and
rebuilt as one native program for Windows or Linux. No DOSBox. No emulator.

<img src="docs/screenshots/play.gif" width="480" alt="Super Android running as recompiled native code: the ball comes off the plunger and up through the top lanes">

<sub>*Super Android*, recompiled, mid-game. Recorded headless with the built-in recorder.</sub>

</div>

---

## What it is

A static recompilation of Epic Pinball. `tools/lift.py` lifts the 16-bit x86
of the menu program, every table and the sound drivers to C with the
[pcrecomp](https://github.com/sp00nznet/pcrecomp) toolkit; a small DOS machine
in `src/` answers the DOS, VGA, Sound Blaster, keyboard and timer the game
expects, including running one program from another the way DOS does. You
supply the game; the build produces `epicpinball.exe`.

It is part of the pcrecomp family and follows its house style: the toolkit
ships, the output never does.

## Status

**v0.1.0 — alpha. The menus and all thirteen tables boot and play; the
conformance harness passes 154/154 on Linux.**

| | |
|---|---|
| Version recompiled | GOG *Epic Pinball: The Complete Collection* 1.01 |
| Programs lifted | `PINBALL.EXE`, `EP1.EXE`–`EP13.EXE`, the MASI mixer and Sound Blaster driver |
| Intro, menus, table select, high scores | ✅ |
| All 13 tables: playfield, scrolling, flippers, plunger, tilt | ✅ |
| Dot-matrix scoreboard (the VGA split screen) | ✅ |
| Music and effects: Sound Blaster 16, 8-bit stereo, 22 kHz | ✅ measured, see below |
| Windowed play at real speed | ✅ |
| Linux, natively (gcc, SDL2) | ✅ played on a real Linux desktop and a cabinet; 154/154 conformance on Arch (133/133 on Debian 13, before cabinet mode) |
| Gamepad (XInput, `controller.ini`, GOG's DOSBox mapping by default) | implemented, untested on a real pad |
| `SETUP.EXE` (the sound card menu) | not lifted: its start-up check is answered, and GOG's `SOUNDCRD.INF` (Sound Blaster 16) is used as shipped |
| The `DEMO\` folder's shareware version | not supported |
| A full game, start to final ball, played by hand | ✅ |
| Cabinet mode, `--cabinet` (Linux only) | ✅ on a real cabinet (table 1) and on virtual displays |
| Nudging from a cabinet: an accelerometer's shove by how hard, nudge buttons as the game's nudge (`[nudge]`, Linux) | ✅ on a real cabinet's accelerometer (a Pinscape Pico); by a scripted accelerometer on four tables and a virtual board in a window; nudge buttons not yet on a real board |
| Cabinet feedback through libdof (`--dof`) and an analog plunger (Linux only) | ✅ on a real cabinet with a Pinscape Pico, on table 1, with table 1's own events (holes, ramps, targets, jackpots); tables 2-13, each with events of its own, and every table's lamps, only in a sandbox (loopback, a simulated board, a virtual joystick, four hours of `--autoplay` each) |
| SSF: the board's sounds on a cabinet's exciters, 7.1 (`--ssf`, Linux) | ✅ headless on all 13 tables (eight channels measured), and on a cabinet's exciters |
| Straight to a table, `--table N` | ✅ on a real cabinet; headless on all 13 tables (quit, high score, game over, play again); not on Windows |
| Between games with `--cabinet --table N`: initials on the DMD, Start plays again | implemented: headless on table 1 and in an offscreen window; not yet on a real cabinet |
| A 128x32 DMD (`--dmd` in a window, `--dmd-device` on a real DMD through libdmdutil; Linux) | ✅ on a real cabinet with a ZeDMD, on table 1; all 13 tables headless |
| **Conformance** ([docs](docs/conformance.md)) | **154/154** on Linux (Windows skips the 11 cabinet checks, and has not run the 10 feedback ones) |

Everything marked ✅ was seen working in scripted headless runs, which the
conformance harness repeats. The sound was checked by measurement — a stereo
stream whose two channels are each smooth signals with a music-shaped
spectrum — not yet by ear. A full game has been played by hand; most tables
have still seen only scripted play: treat this as an alpha.

## What the remaster adds

The game plays as it shipped; everything here is around it, optional, and
off with `--classic`. Details in [docs/remaster.md](docs/remaster.md).

![The whole of Super Android at once, in a 16:9 window](docs/screenshots/whole-table.png)

- **The whole table** (F7): the full playfield on screen at once, no
  scrolling, with the scoreboard under it. Not redrawn — the game keeps the
  entire table in video memory and only ever showed part of it; this shows
  the rest. Made for a tall window or a monitor turned on its side.
- **Cabinet mode** (`--cabinet`, Linux): for a virtual pinball machine. The
  whole table fills the playfield display, turned with F6 and stretched to
  every pixel of it with F5 while a table is up; the scoreboard is
  a window of its own. Messages about keys a cabinet has not got (the help
  screens, PRESS SPACE TO BEGIN) are left out.
  [How to set one up](docs/remaster.md#cabinet-mode).
- **Straight to a table** (`--table N`): table N with a one-player game, in
  half a second, for a frontend; quitting the table quits the program, with
  PINBALL saving the high scores as it always does. In a cabinet it plays as
  a pinball machine: initials on the DMD with the flipper buttons, then
  GAME OVER until Start plays the table again. [How](docs/table.md).
- **SSF** (`--ssf`, Linux): 7.1 for a cabinet with exciters. The backbox
  speakers keep the music and the callouts; bumpers, slingshots, flippers,
  drop targets and the plunger play on the exciters, top or bottom, left or
  right, where on the table they happen, and a rolling sound follows the
  ball. [How](docs/ssf.md).
- **A DMD** (`--dmd`, `--dmd-device`, Linux): a 128x32 dot-matrix picture of
  every table, in a window of its own or on a real DMD (any libdmdutil
  drives; tested with a ZeDMD), taking the scoreboard window's place in a cabinet. The
  messages are the game's own, followed through the message engine all 13
  tables share and set again for 128x32 in each table's fonts and colours,
  with its explosions and waves; tables 1-8's leave the playfield for it.
  Between them, the table's score. [How it works](docs/dmd.md).
- **Cabinet feedback** (`--dof`, Linux): flippers, slingshots, bumpers,
  launch, drain, nudge and the rest go to libdof as DOF events (and each
  table's own holes, ramps, lanes, targets, loops, jackpots, awards and
  lamps), with
  VPinball's DOF files if there are any (a Config Tool export works as it
  is: the game's row is made from its device list, solenoids, flashers and
  MX strips with DOF's shapes included) (no limits: those are the
  controller's), and a **plunger position sensor** can work the game's plunger.
  [Read this before connecting a coil](docs/cabinet-feedback.md).
- **Its own window**, drawn with Direct3D 11 (SDL2 on Linux): resizable, borderless
  fullscreen (F11, Alt+Enter), remembered where you left it.
- **Scaling** (F12): sharp-bilinear (default), smooth, CRT, nearest, integer.
  **Vivid** colour (F9). A dark blur of the picture beside it instead of
  black bars.
- **A settings menu** on right-click or F10; choices are kept in
  `saves\epicpinball.ini`.
- **Smooth sound**: the 22 kHz Sound Blaster stream interpolated up to
  44.1 kHz instead of held in steps (Settings → Sound for the original).
- **Xbox controller** mapped by `saves\controller.ini`.
- **A settings tool** (Linux), `./epicpinball-config.sh`: modes, keys, a
  cabinet's buttons, plunger and nudge calibration, SSF, DMD and DOF, and an
  import of VPinball's settings ([docs](docs/config.md)).

## Screenshots

Every one of these is the recompiled game running.

![All thirteen tables in play](docs/screenshots/tables.png)

<sub>All thirteen tables, each mid-game with its scoreboard across the bottom:
Android, Pot of Gold, Excalibur, Crash and Burn, Magic, Jungle Pinball, Deep
Sea, Enigma, Cyborgirl, Pangaea, Space Journey, Toy Factory, African Safari.</sub>

| | | |
|---|---|---|
| ![Main menu](docs/screenshots/menu.png) | ![Table select](docs/screenshots/select.png) | ![Excalibur's high scores](docs/screenshots/excal.png) |
| The main menu | Choosing a table | A table's own screen |

## The game

Epic Pinball came out in 1993 from Epic MegaGames — the company that became
Epic Games. Most tables' own credits say it plainly: *design and graphics by
James Schmalz, music by Robert Allen*. It was shareware: the free table was
*Super Android*, and PINBALL.EXE's closing text still promises that "eleven
more worlds of pinball await". The sound system under it is MASI, Joshua C. Jensen's — its
drivers sign themselves *Licensed to: Epic MegaGames*.
[[Wikipedia]](https://en.wikipedia.org/wiki/Epic_Pinball)

It was written in assembly, and it shows: the tables scroll a 320×240 Mode X
playfield by moving the CRTC start address, put their dot-matrix scoreboard in
the VGA's split screen, lock their timer to the display's vertical retrace, and
run as children of a front end that keeps the music playing under them. The
sound driver's mixer jumps into the middle of unrolled loops. All of that is
lifted as-is; [architecture](docs/architecture.md) and
[lifting](docs/lifting.md) say how.

## Getting Started

You need your own copy of the game. GOG's *Epic Pinball: The Complete
Collection* works, installed or as the offline installer
(`setup_epic_pinball_the_complete_collection_*.exe`). Other versions are untested.

### Quick start

1. Download this repository (Code → Download ZIP) and unzip it anywhere.
2. Double-click **`Setup.cmd`**.

Setup checks for Python 3, CMake, Visual Studio 2022's C++ build tools and Git,
and **asks** before installing any that are missing (it says what, why and how
big). It finds the game (a GOG install, or GOG's installer in your Downloads,
which it unpacks with innoextract without running it — asking first), copies the
game files into `original\`, fetches pcrecomp next to this folder, lifts and
builds, and leaves a shortcut here: **Epic Pinball**. On a fast machine that
takes under two minutes. If anything fails it stops with one sentence on what to
do and writes the details to `setup.log`; running it again carries on from
where it stopped.

### On Linux

The same game, built natively: gcc and SDL2, no Wine.

```sh
./setup.sh                  # or: ./setup.sh /the/folder/holding/PINBALL.EXE
./epicpinball.sh
```

`setup.sh` takes the same steps as Setup.cmd. If gcc, CMake, Python 3, git
or SDL2's development files are missing, it names the packages for Debian,
Ubuntu, Fedora and Arch and stops. It finds the game in `~/GOG Games` or
`~/Games`, or uses GOG's offline Windows installer from `~/Downloads`
(unpacked with innoextract). It copies the game into `original/` with
upper-case names, fetches pcrecomp next to this folder, and runs
`tools/discover.py`, which builds into `build-linux/`. On Debian 13 it
settles in four rounds, about 85 seconds. After a change, `./build-linux.sh`
rebuilds. `python3 tools/conformance.py` runs the checks against
`build-linux/epicpinball`.

## Usage

```
epicpinball [options] [-- arguments for PINBALL.EXE]
```

| Keys | |
|---|---|
| Left / right **Shift** | flippers |
| **Space** | start; hold to pull the plunger; nudge |
| **Z** / **/** | nudge left / right |
| **Enter** | show or hide the scoreboard |
| **↑ ↓** | menus; look up and down the table |
| **Esc** | back, quit |
| **F5** | with `--cabinet`: stretch the table to fill its display, or back (menus keep their shape) |
| **F6** | with `--cabinet`: turn the playfield 90° clockwise |
| **F7** | whole table / scrolling view |
| **F9** | colour: natural / vivid |
| **F10**, right-click | settings menu |
| **F11**, **Alt+Enter** | fullscreen |
| **F12** | scaling: sharp → smooth → CRT → nearest → integer |
| **PrtSc** | screenshot to `screenshots\` |

A pad: triggers flip, bumpers nudge, A plunges, d-pad for menus; remap it in
`saves\controller.ini`, where `[keys]` also adds keyboard keys to the game's
actions ([docs](docs/cabinet-feedback.md#keys-and-buttons)).

| Option | |
|---|---|
| `--data DIR` | the game's files (default `original`) |
| `--save DIR` | where `CONFIG.PIN` and high scores go (default `saves`); the install is never written to |
| `--window N` | window size, multiples of 320×240 (default 3, or where you left it) |
| `--scale sharp\|smooth\|crt\|nearest\|integer`, `--look natural\|vivid`, `--bars blur\|black`, `--whole-table`, `--fullscreen` | this run's picture; wins over `epicpinball.ini` and is not saved to it |
| `--classic` | the game's own picture: nearest, no extras |
| `--cabinet` | a virtual pinball machine (Linux): the whole table on one display, the scoreboard in its own window, both borderless |
| `--rotate 0\|90\|180\|270` | with `--cabinet`: the playfield turned clockwise, this run |
| `--stretch` | with `--cabinet`: the playfield stretched to fill its display, this run |
| `--playfield-display N` | with `--cabinet`: the display the playfield fills; they are listed at start |
| `--score-window X,Y,W,H` | with `--cabinet`: where the scoreboard (or with `--dmd`, the DMD) window goes, this run |
| `--table N` | straight to table N (1-13) and a one-player game; quitting it quits the program; with `--cabinet`, initials on the DMD and Start for the next game ([docs](docs/table.md)) |
| `--ssf` | 7.1 for a cabinet with exciters: the board's sounds on the side (top) and rear (bottom) pairs; `[ssf]` in `epicpinball.ini` sets their volumes (Linux) ([docs](docs/ssf.md)) |
| `--ssf-volumes FX,ROLL` | SSF's effects and rolling volumes for this run, over `[ssf]`'s (4 is the default, 0 none) |
| `--dmd` | a 128x32 DMD in a window of its own; in a cabinet, in place of the scoreboard window (Linux) ([docs](docs/dmd.md)) |
| `--dmd-device` | the DMD on a real one, any libdmdutil drives (ZeDMD, Pixelcade, PinDMD3, ...; tested with a ZeDMD; PIN2DMD is left out of this build): build `dmd/build.sh` first; settings in `saves/dmd/DMDUtil.ini` (Linux) |
| `--dof` | DOF events through libdof (Linux): build `dof/build.sh` first; rows in VPinball's DOF files or `saves/directoutputconfig/`; no output limits ([docs](docs/cabinet-feedback.md)) |
| `--no-cabinet`, `--no-dmd`, `--no-dmd-device`, `--no-ssf`, `--no-dof` | leave a mode out this run that `[modes]` in `epicpinball.ini` turns on (the settings tool sets it; [docs](docs/config.md)) |
| `--joysticks` | list joysticks, their live axes and buttons pressed, to set up `[plunger]`, `[nudge]` and `[buttons]` in `saves/controller.ini` (Linux); the settings tool does it with pictures |
| `--feedback-log FILE`, `--plunger-trace "ms:percent,..."`, `--nudge-trace "ms:x,y,..."`, `--nudge-velocity` | testing: what a cabinet would feel; a scripted plunger sensor; a scripted accelerometer, or with `--nudge-velocity` its velocity |
| `--autoplay`, `--switch-log FILE` | testing: a player that flips at a ball coming down, launches, and starts game after game (with `--table`); each switch colour the ball is handled on and where, and each effect the table plays ([docs](docs/cabinet-feedback.md#finding-a-tables-features)) |
| `--mute` | |
| `--headless` | no window, no audio device, deterministic time — works over RDP |
| `--record out.mp4` | video and audio through ffmpeg (with `--headless`: unattended) |
| `--keys "ms:KEY[+holdms],..."` | scripted input at emulated times, e.g. `"22000:ENTER,24000:ENTER"` |
| `--seconds N`, `--shot-at "ms:file.bmp,..."`, `--wav out.wav`, `--trace` | testing |
| `--present WxH` | `--shot-at` frames through the presenter at that size, headless |

```powershell
# a minute of Super Android, recorded without a window
build\Release\epicpinball.exe --headless --seconds 60 --record android.mp4 `
  --keys "22000:ENTER,24000:ENTER,26000:ENTER,35000:SPACE,37000:SPACE+1500"
```

```sh
# a cabinet: the playfield on display 0, turned; the scoreboard on the screen right of it
./epicpinball.sh --cabinet --rotate 90 --playfield-display 0 --score-window 1920,0,1280,116

# a cabinet with a real DMD for the scoreboard, and DOF feedback
./epicpinball.sh --cabinet --dmd-device --dof
```

## Building from source

`Setup.cmd` and `setup.sh` (above) are the build: they fetch pcrecomp and run
`tools/discover.py`, which lifts, builds and plays the game until every address
it reaches is lifted. What else is here:

| | |
|---|---|
| `tools/lift.py` | one program or driver → C, at the segment it runs at |
| `tools/discover.py` | lift, build, play, repeat |
| `tools/conformance.py` | the scripted checks; `--update` records a new baseline |
| `tools/feedback_map.py` | where each table keeps what cabinet feedback and the plunger use |
| `tools/dmd_survey.py` | each table's message engine for the DMD, and every message a table can show |
| `dof/` | the optional output helper: its own program, GPLv3, built by `dof/build.sh` |
| `dmd/` | the optional DMD helper for a real DMD: its own program, GPLv3, built by `dmd/build.sh` |
| `setup.sh`, `build-linux.sh` | the Linux quick start and rebuild |
| `tests/selftest.c` | the split screen, SB16 stereo, the image registry, the feedback watcher, the plunger, the helper's pipe, the DMD's layout and SSF's placing, with no game data — what CI runs (`cmake --build build --target selftest`) |
| `src/` | the DOS machine: `dos.c` (DOS, BIOS, EXEC), `images.c` (which lifted code is where), `machine.c` (PIC, PIT, keyboard, interrupts), `vga.c`, `audio.c` (SB16, DMA), `host.c` (window, input, recording: Win32, or SDL2 on Linux), `present.c` (Direct3D 11, or SDL2), `pad.c`, `watch.c` and `feedback.c` (table events), `dof_pipe.c`, `plunger.c`, `dmd.c` and `dmd_pipe.c` (the DMD), `ssf.c` (the exciters), `autostart.c` (`--table`), `nudge.c` (a shove) |
| `docs/` | [architecture](docs/architecture.md), [remaster](docs/remaster.md), [cabinet feedback](docs/cabinet-feedback.md), [dmd](docs/dmd.md), [ssf](docs/ssf.md), [table](docs/table.md), [lifting](docs/lifting.md), [conformance](docs/conformance.md) |

A trace build (`cmake -B build-dbg -DEPIC_TRACE=ON`, then
`cmake --build build-dbg --config RelWithDebInfo`) keeps a ring of the lifted
functions entered and adds a watchdog that names the generated C line the game
is stuck on; [lifting](docs/lifting.md#debugging-kit) lists the rest.

This needs pcrecomp 1dfdaef or later: 32-bit addressing in the 16-bit decoder
and lifter (pcrecomp#74) and `jmp reg` tables lifted as switches (pcrecomp#75).

**The generated source is not distributed.** Everything lifted from the game —
`work/gen/`, the layout and entry lists — is produced on your machine from your
copy, and `.gitignore` keeps it out of this repository.

## License

MIT for this repository's code ([LICENSE](LICENSE)), except `dof/` and `dmd/`,
separate programs under the GPLv3 ([dof/LICENSE](dof/LICENSE),
[dmd/LICENSE](dmd/LICENSE)), as libdof and libdmdutil are; the game links
neither. Epic Pinball is commercial
software; no part of the game, and nothing generated from it, is in this
repository ([NOTICE](NOTICE)).
