# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[SemVer](https://semver.org/).

## [Unreleased]

### Added
- **`--cabinet`**, for virtual pinball machines (Linux): two windows without
  decorations. The playfield fills a display with the whole table, never the
  scoreboard, and F6 turns it a quarter at a time, menus and all. The
  scoreboard has a window of its own, dragged and resized where the cabinet's
  score display is, and stays live while the game hides its own. `--rotate`,
  `--playfield-display` and `--score-window` set them; `[cabinet]` in
  `epicpinball.ini` keeps them. Conformance 142 (9 of them cabinet's).
- **F5 in cabinet mode** stretches the table to fill its display, out of its
  shape, and back; `[cabinet] stretch` keeps it and `--stretch` sets it for a
  run. The menus and the scoreboard keep their shape. Conformance 154.
- **`--dof`**, cabinet feedback (Linux): flippers, slingshots, bumpers,
  launch, ball release, drain, nudge, tilt and the extra ball's knocker go to
  libdof as named events (`$FlipperLeft`, `$Bumper2`, `$ExtraBall`) of
  tables `epicpinball_ep01` to `ep13`:
  each bumper its own (table 5 has six), a ball waiting in the lane,
  multiball, game start and game over among them,
  through a helper in `dof/` (GPLv3, built only by `dof/build.sh`; the game
  does not link it). It uses VPinball's DOF files where there are some, and
  the game's own rows in `saves/directoutputconfig/` before those. With no
  row for the game, it makes one from the device list the Config Tool writes
  after `[Config DOF]`: flippers, slingshots, back-row bumpers, knocker
  (the extra ball, never game over), beacon, shaker and steady button lamps, and colours on flashers and a
  `Back MX` strip for bumpers, extra ball, game start and over, drain, tilt
  and multiball; never `ZB Launch Ball`. It sets
  **no output limits**: they are the controller's. Tested on a real cabinet
  with a Pinscape Pico, an LED strip and a Config Tool export, on table 1
  only: `docs/cabinet-feedback.md`.
- **Table 1's own feedback**, named by the table (`$t1PowerUpHole`,
  `$t1LeftRamp`, `$t1DropTargetsDown`), and `$Jackpot` and the like: its five holes kicking out and
  holding a ball, the kickback, drop targets and their bank, rollovers, the
  top lanes complete, the skill shot, three ramps, jackpots and awards. Read
  from the switch handler every table has, what it keeps and the scoreboard's
  messages; each counted as the game counts it. Made rows put kick-outs, the
  kickback and the bank on the middle-row solenoids by side, jackpots on the
  beacon, and colours on flashers. The other twelve tables can follow table by
  table. Tested on a real cabinet.
- **Table 2's own feedback** (Pot of Gold): its treasure hole and cave, the
  Sky Ramp, the clover lanes, the cave lane, the leprechauns and the rainbow
  lane, its locks, the top lanes, the skill shot, the jackpot and its awards
  (`$t2Cave`, `$t2SkyRamp` ...). Tested by four hours of `--autoplay`.
- **For each table's own feedback:** a feature can be any switch the table
  has (`switch1` to `switch16`: lanes, loops, locks), or have no switch at
  all and be fired by the table's own scoreboard text ("BALL 1 LOCKED"); a
  ramp a table does not count fires on its switch. `--switches epN` in
  `tools/feedback_map.py` prints what each switch colour's code does, and for
  testing `--autoplay` plays game after game of a table by itself and
  `--switch-log` lists where the ball meets each switch colour.
- **Made rows use the side MXs and DOF's shapes.** `Left MX` and `Right MX`
  get slingshots, ramps, hole kick-outs and the kickback on their side, as
  wipes up or down it, and the bottom of the right one breathes while a ball
  waits, as the Config Tool's games use them; those features leave the
  `Back MX`. With `DirectOutputShapes.xml` in the DOF folder, the `Back MX`
  draws the Config Tool's flasher circles, a bumper's on its own spots, and
  animated shapes for the awards, jackpots and the extra ball; without it,
  bands as before. Tested on a real cabinet with all three MXs, on table 1.
- **A DMD** (`--dmd`, `--dmd-device`): a 128x32 picture
  of every table, in a window of its own (in a cabinet, in place of the
  scoreboard window, which then never opens) or on a ZeDMD through
  libdmdutil, by a GPLv3 helper in `dmd/`
  the game does not link. The messages are the game's own, followed through
  the one message engine all 13 tables share and set again for 128x32 in the
  table's fonts and colours: wrapped, doubled when they fit, paged when tall,
  and moved as the game moves its dots (explosions, waves, slides). Between
  them, the table's board: on 1-8 the scoreboard's own BALL/PLAYER line over
  its score digits, on 9-13 BALL/PLAYER over the score doubled. Tables 1-8's
  messages leave the playfield for it. `tools/dmd_survey.py` finds what it
  needs in each table and lists every message a table can show. In cabinet
  mode and with a DMD, messages about keys a cabinet has not got (the help
  screens before a game, PRESS SPACE TO BEGIN, PRESS ENTER) are left out;
  `[cabinet] hide_messages` changes the list. Tested on a real cabinet on
  table 1; tables 2-13 only headless.
- **Nudging from a cabinet** (Linux): `[nudge]` in `controller.ini`. An
  accelerometer's shove (a Pinscape's X and Y axes) pushes every ball by how
  hard, against the cabinet's movement, at the start of each physics step and
  only where the game's own nudge would push; it fills the tilt meter by as
  much, so hard shaking tilts by the game's rule. Nudge buttons reported as
  joystick buttons press the game's own nudge keys (Z, /, Space).
  `--joysticks` shows buttons; `--nudge-trace` scripts an accelerometer. The
  feedback map is version 5 (the nudge's gate). Tested on a real cabinet's
  accelerometer.
- **Velocity nudging**: `[nudge] is_velocity = 1` for a board set to report
  the cabinet's velocity instead of its acceleration. Each physics step
  pushes by how much the velocity changed, all at once; a full-scale change
  is a whole game nudge. `--nudge-velocity` makes a `--nudge-trace` velocity.
- **The nudge indicator**: while an accelerometer is read and a table is up,
  a ring at the top right of the playfield shows the reading, as VPinball
  does, with the dead zone and a short trail; it turns with the playfield in
  a cabinet, and shots and recordings leave it out. `[nudge] indicator_range`
  is the reading that takes it to the ring (8000); `indicator = 0` hides it.
- **`--ssf-volumes FX,ROLL`**: SSF's effects and rolling volumes for one run,
  over `[ssf]`'s.
- **The settings tool**, `epicpinball-config` (Linux; [docs](docs/config.md)):
  Dear ImGui pages for the modes, the picture, keys, a cabinet's buttons, the
  plunger (finding its axis, calibrating it, a drawn plunger that moves with
  it), nudging (a four-step calibration, and a live test with the game's own
  push and tilt meter), SSF's volumes and a speaker test with the game's own
  sounds (Super Android's music, its playfield sounds), the ZeDMD and a DOF
  test. It imports VPinball's keys, buttons, plunger, nudging and sound, from
  10.8.0's and 10.8.1's settings alike, asking on its first start. The game's
  Linux menu opens it.
- **Permanent modes**: `[modes]` in `epicpinball.ini` starts every run with a
  window with `--cabinet`, `--dmd`, `--dmd-device`, `--ssf` and `--dof` as
  set; `--no-cabinet` and the others leave one out for a run, and a mode that
  cannot run is left out, saying why.
- **Keys and buttons for the game's actions**: `[keys]` in `controller.ini`
  adds keyboard keys to each action (the action's own key keeps working), and
  `[buttons]` a cabinet board's joystick buttons (Linux). Every key has a
  name now (`src/keys.c`), for `--keys` and `controller.ini` too.
- **`--table N`**: straight to table N with a one-player game, for a
  frontend. PINBALL's own menus are driven to it unseen and unheard, a key at
  a time from where PINBALL is (its code and memory, never a clock), in
  about half a second; the game is shown as its ball is served. Quitting the
  table quits the program by PINBALL's own way out, so the high scores are
  saved; initials are yours, and after a game over so is the table's screen
  (PLAY GAME again, or EXIT). `tools/feedback_map.py` finds PINBALL's menus in
  your copy (`work/feedback/pin.map`). Tested on a real cabinet, and headless
  on all 13 tables.
- **Replays in a cabinet** (`--cabinet --table N`): a game over no longer
  needs the program started again, nor a keyboard. The playfield stays,
  dimmed; a high score's initials are chosen on the DMD with the flipper
  buttons and Start, and typed to PINBALL unseen; then GAME OVER and the
  table's high scores until Start plays it again, served at once, or Exit
  leaves. A button held while the game drives PINBALL is not pressed again
  afterwards. With no DMD, the scoreboard's window shows it. The
  high scores are written to `hiscore.pin` after each entry, not only when
  PINBALL quits. `pin.map` is version 2 (run `tools/feedback_map.py`, which
  `build-linux.sh` does). Not yet tried on a real cabinet.
- **SSF** (`--ssf`, Linux): 7.1 for a cabinet with
  exciters, wired as vpinball wires one. The front pair keeps the game's mix;
  the board's sounds (bumpers, slingshots, flippers, drop targets, plunger,
  holes) leave it and play on the exciters, the side pair at the top of the
  table and the rear pair at the bottom, placed where they happen, with
  vpinball's gains. A rolling sound follows each ball, louder and
  brighter with its speed. Each effect is caught as the table starts it, played from the
  game's own sample, and its voice in the sound driver set silent.
  `[ssf]` in `epicpinball.ini` sets the exciters' volumes and a table's
  board effects; `--wav` writes all eight channels. Tested on a cabinet's
  exciters.
- **An analog plunger** (Linux): `[plunger]` in `controller.ini` names a
  joystick axis; the drawn plunger follows it and a release launches at the
  strength pulled to. `--joysticks` lists devices and axes. It is the first
  feature to write the game's memory, and only when configured.
- `--feedback-log` and `--plunger-trace`, for testing both. Conformance 152.

### Changed
- **Made DOF rows take each table's colours.** The flashers, the Back MX and
  the side MXs used the same white, yellow, orange, cyan, magenta, green and
  blue on every table; each table now has its own, from its playfield art
  (Deep Sea's aqua and coral, Pot of Gold's gold and clover green, ...),
  named by role in the made file (`EpMain`, `EpFeature`, `EpRamp`, ...).
  Drain and tilt stay red.
- **DOF events are names, not E numbers.** `E101` to `E145` could not be
  read without a list, nor a table's own feature told from an event of every
  table: rows now say `$FlipperLeft`, `$SlingshotLeft 50`, `$Bumper1`,
  `$ExtraBall`, and a table's own holes, ramps, lanes and targets carry its
  number, `$t1PowerUpHole`. Their names are in `FEATURES` in
  `tools/feedback_map.py`, so the feedback maps are version 6 (run
  `tools/feedback_map.py`, which `build-linux.sh` does), and the helper takes
  `--features` for `--sides`. `--test` takes the new names. Rows with E
  numbers no longer fire: write the names, or delete the row to have one
  made again.
- libdof is pinned at its master of 26 November 2026 (0.5.4), for its fix of
  named elements, which never fired before it, and with a patch of ours that
  sends one: `DOF::DataReceive(name, value)`. Its own Pinscape Pico bound
  replaces ours. `dof/build.sh` fetches a new pin by itself, and the libraries
  it builds once are not built again for it.
- `epicpinball.ini` is updated in place, a setting at a time, so its comments
  and the settings tool's keys stay (it was written anew each time).

### Fixed
- The Windows self-test did not link: the DMD and SSF tests were inside the
  POSIX-only part.
- A cabinet board in gamepad mode held both flippers up: SDL reads its
  centred sensor axes as half-pulled triggers. Such boards now count for
  their buttons only. Keys a pad held are let go when it is unplugged or the
  window loses the front; they used to stay down.
- The whole table shook up and down on tables 9-13: their two pages are 395
  rows apart, not the 400 of tables 1-8, so every other frame was drawn five
  rows off. It also jumped on every table while Enter slid the scoreboard in
  or out, because the pages were placed from the height it had at that moment.
- The menus ran as fast as the host: PINBALL's page flip waits for "in
  retrace", and up to a hundred of them fitted in one retrace. One retrace
  answers one flip now, so the main menu waits its 13 seconds before the demo
  instead of about one, and its animations run at 70 frames a second.
  Headless too, so scripted keys (`--keys`, conformance, discover) wait until
  22 s for the menu.
- Conformance's "the view moves in play" compared two moments, and failed one
  run in five when the keys moved by milliseconds; it looks every second now.

## [0.2.0-alpha.2] - 2026-10-22

### Fixed
- `setup.sh` given a folder took the shareware demo in its `DEMO/` (it has
  `EP1.EXE` too) when `find` listed it first; it takes the shallowest now.

## [0.2.0-alpha.1] - 2026-10-22

### Added
- **The installer**, for Windows and Linux: recomp-launcher built from
  `launcher/launcher.json` by `.github/workflows/installer.yml`. Every push
  to main leaves it as an artifact; a `v*` tag puts it on that release, and
  the tag is the version it shows. It finds the game, builds, and plays.
- `setup.sh` takes `--yes --game-dir DIR`; both setups print `[n/5]` steps,
  end a failure with `Setup stopped:`, find GOG's installer in the folder
  they are given, and need no git when pcrecomp is already beside them (the
  installer downloads it, at the pinned commit).
- **Linux**, natively: the same lifted C and DOS machine built with gcc,
  with SDL2 for the window, sound and pad (`src/host.c`, `src/present.c`,
  `src/pad.c`); `setup.sh` (the tools, the game, pcrecomp, lift, build, a
  launcher) and `build-linux.sh`, after the Red Alert 2 recomp's. Every
  remaster feature works there; the settings menu is a dialog. Conformance
  133/133 on Debian 13, from a clean `./setup.sh`, and its frames are
  bit-identical to the Windows build's.
- The remaster (docs/remaster.md), all optional and off with `--classic`:
  - **The whole table** (F7): `vga_compose_table` draws the page of video
    memory the game scrolls over -- the full table, ball included -- with the
    scoreboard under it, from the CRTC registers, for every table.
  - `src/present.c`: a Direct3D 11 presenter, from the Gunman Chronicles
    recomp: sharp-bilinear, smooth, CRT, nearest, integer (F12), a vivid look
    (F9), a dark blur of the picture beside it (from Red Alert 2), GDI if
    Direct3D will not start. `--present WxH` renders `--shot-at` frames
    through it headless.
  - A settings menu (right-click, F10) and `saves\epicpinball.ini`, which
    keeps the choices and the window's place; F11 and Alt+Enter fullscreen,
    PrtSc screenshots. Command-line choices win for the run and are not saved.
  - Smooth sound: the DSP's 22 kHz samples interpolated to 44.1 kHz (Settings
    → Sound → Original for the held steps).
  - `src/pad.c`: the Xbox controller mapped by `saves\controller.ini`, GOG's
    mapping by default.
- Conformance: the whole table and the presenter on a table with each
  scoreboard height; selftest: the page arithmetic and the interpolation.

### Changed
- `--scale` is now the scaling mode; the window size is `--window N`.
  `--display` is gone (F12 and `--scale` replace it).

### Removed
- The INT 33h mouse driver and mouse capture, left from Blake Stone: the game
  never asks for a mouse. Right-click is the settings menu.

### Changed
- Pinned to pcrecomp 1dfdaef, now that the toolkit changes this needs are merged
  (pcrecomp#74, #75).

## [0.1.0] - 2026-10-10

### Added
- `tools/lift.py`: lifts `PINBALL.EXE`, the thirteen table EXEs and the MASI
  mixer and Sound Blaster driver, each as its own image at the segment it runs
  at. Finds code through decoded calls and jumps, bounded and unbounded jump
  tables (`mov reg, cs:[bx+T] / jmp reg`), unrolled-loop entry tables, routines
  stored in called-through variables, the MASI driver header, and stops at
  `int 21h/4Ch`.
- `tools/discover.py`: lift, build, play every table headless, repeat until
  every address reached is lifted (four rounds from nothing).
- `src/images.c`: which lifted program or driver owns an address, which are
  live, and where each was placed (`work/layout.txt`, `work/misses/`).
- `src/dos.c`: DOS EXEC, so PINBALL runs the tables; program loading from the
  game folder; the `SETUP.EXE -cd` check answered; drivers noticed as PINBALL
  reads them in; a 1024-block arena.
- `src/vga.c`: CRTC line compare, for the tables' split-screen scoreboard.
- `src/audio.c`: the Sound Blaster 16 DSP (4.05: `41h` rate, `C0h`–`C6h` 8-bit
  mono/stereo, signed/unsigned) with stereo output; IRQ 7.
- Polled `3DAh` reads cost emulated time, so retrace waits inside the timer
  interrupt end in deterministic runs.
- Interrupt state reset when a program exits from inside an interrupt handler.
- Trace builds: lifted code at `/Od` (seconds to build) and a watchdog that
  prints the lifted-call ring and the main thread's generated-C line when the
  guest stops polling.
- `tools/conformance.py`: boot, menu, all thirteen tables played, scoreboard,
  scrolling, SB16 stream, audio, determinism — 125/125.
- `tests/selftest.c` (split screen, SB16 stereo, the image registry; no game
  data) and CI on Windows: lint, selftest, conformance (which skips there).
- `Setup.cmd` / `scripts/setup.ps1`: the Quick start, from a GOG install or
  GOG's offline installer (unpacked with innoextract) to a shortcut.
- The DOS machine started from the Blake Stone recomp's (same author, MIT),
  without its hi-res renderer and OPL2.
