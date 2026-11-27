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
- **Each table's lamps, for DOF:** every lamp the game draws is a level,
  `$t1Lamp1` to `$t13Lamp63`, on while it is lit: blinks and light shows as
  the table shows them, numbered in the game's order (36 on table 7, 82 on
  table 10), and tables 3 and 5's lights that glow on the palette, from 51
  on. `tools/feedback_map.py --lamps epN` says where each is. Read from the
  blitter the lamps' renderer draws them with, so what the player sees is
  what goes out. Made rows leave them to rows of your own. Map version 8.
  `EPIC_LAMP_CHECK=1` for testing.
- **Any key on a cabinet button:** `[buttons]` in `controller.ini` takes a
  key's name besides the game's actions (`F7 = 9`, `P = 10`), up to 16: the
  window's own keys (F5 and F6 in a cabinet, F7, F9-F12) do what they do on
  the keyboard, the others go to the game.
- **The DMD on every table, looked at message by message:** tables 5 and 6
  now show their messages' further lines (SCORES with each player, the bonus
  pages, BEEHIVE-HIT RAMP TO COLLECT 24): their line appenders leave the
  list's end where it is, which the survey did not take for one. A board of
  four players on tables 9-13 keeps the score on the DMD, the other three
  two to a row (they went to a second page). For testing,
  `EPIC_DMD_SHOTS=DIR` saves each message's picture, and
  `EPIC_TABLE_PLAYERS=N` starts `--table`'s game with N players.
- **Table 6's target banks:** each bank all down, as it resets, is an event
  of its own (`$t6SwordTargetsDown`, `$t6BlueTargetsDown`,
  `$t6LeftTargetsDown`), watched by the game's flags for it (`bankset` in the
  map). A table may have 24 switch features now.
- **SSF on every table's own board:** each table's holes (and table 1's,
  8's, 10's and 12's rubber posts, table 11's targets) now go to the exciters
  too, not only the sounds every table shares. Found by their samples: table
  1's hole and post sounds are in the others byte for byte. `--switch-log`
  lists each effect a table plays.
- **Table 13's own feedback** (African Safari): the water hole, base camp,
  lock and lion's den, the BIRD lanes, the mountain path, the CAT targets,
  the snake pit, the lost caves, the base camp lane, the outlanes, the
  jackpots and its awards (`$t13LionsDen` ...). Every table now has events
  of its own. Tested by four hours of `--autoplay`.
- **Table 12's own feedback** (Toy Factory): the junk heap, the lock, the
  superball and joystick holes, the skateboard ramp, the top lanes, the toy
  express, the toy chest, the display case, the paint shop, the orbit, the
  outlanes, the jackpots and its awards (`$t12Joystick` ...). Tested by four
  hours of `--autoplay`.
- **Table 11's own feedback** (Space Journey): the lock, the extra ball hole
  and the abyss, the asteroid and hyperspace ramps, the STAR lanes, the
  space station, the orbit, the outlanes, its targets, the jackpots and its
  awards (`$t11Hyperspace` ...). Tested by four hours of `--autoplay`.
- **Table 10's own feedback** (Pangaea): the cavern and its lock, its four
  other holes, the side targets, the orbits, the bullseye, the outlanes, the
  volcano loop, the eggs, the skill shot, the jackpot and its awards
  (`$t10Cavern` ...). Tested by four hours of `--autoplay`.
- **Table 9's own feedback** (Cyborgirl): the Tense Deck and CPU Hog holes,
  the ICE lanes, its lanes, targets and inlanes, the ice breaker, the
  implants, kernel panic and its awards (`$t9CpuHog` ...). Tested by four
  hours of `--autoplay`.
- **Table 8's own feedback** (Enigma): the lock hole, the spiked pit and the
  spring, its four wire loops as its scoreboard names them, the ball lock,
  the mines, the super jackpot and its awards (`$t8GammaRamp` ...). Tested
  by eight hours of `--autoplay`.
- **Table 7's own feedback** (Deep Sea): the eel nest, the wreck, the reef
  and the air tanks, the ramps, the seahorse lanes, the stingray, the lanes
  by the flippers, the loops, the treasure targets, a diver entering, the
  skill shot, the jackpot and its awards (`$t7SunkenShip` ...). Tested by
  four hours of `--autoplay`.
- **Table 6's own feedback** (Jungle Pinball): the star, sword and nest
  holes, the vine ramps, the top lanes, the serpent path, its targets, the
  lanes by the flippers, a falling apple, the jackpot and its awards
  (`$t6NestHole` ...). Tested by four hours of `--autoplay`.
- **Table 5's own feedback** (Magic): the top hole, the MAGIC lanes, the
  left lane, its targets, the outlanes and the right orbit (`$t5TopHole`
  ...); it has no texts, so no jackpot or awards. Tested by four hours of
  `--autoplay`.
- **Table 4's own feedback** (Crash and Burn): its five holes (the scoops,
  the pit, Dead Man Curve, the tyre), the race track ramp, the DRIVER
  targets, orbits, outlanes and green-light lanes, the ball lock, the
  jackpots and its awards (`$t4PitStop` ...). Tested by four hours of
  `--autoplay`.
- **Table 3's own feedback** (Excalibur): the black hole, the SWORD lanes,
  the loops, the kickback lanes and outlanes, the orbits, the capsules, the
  right lane's switches and the spinner (`$t3BlackHole` ...); it has no texts,
  so no jackpot or awards. Tested by four hours of `--autoplay`.
- **Table 2's own feedback** (Pot of Gold): its treasure hole and cave, the
  Sky Ramp, the clover lanes, the cave lane, the leprechauns and the rainbow
  lane, its locks, the top lanes, the skill shot, the jackpot and its awards
  (`$t2Cave`, `$t2SkyRamp` ...). Tested by four hours of `--autoplay`.
- **For each table's own feedback:** a feature can be any switch the table
  has (`switch1` to `switch24`: lanes, loops, locks), or have no switch at
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
- **The settings tool**, `epicpinball-config` (Linux and Windows; [docs](docs/config.md)):
  Dear ImGui pages for the modes, the picture, keys, a cabinet's buttons, the
  plunger (finding its axis, calibrating it, a drawn plunger that moves with
  it), nudging (a four-step calibration, and a live test with the game's own
  push and tilt meter), SSF's volumes and a speaker test with the game's own
  sounds (Super Android's music, its playfield sounds), the ZeDMD and a DOF
  test. It imports VPinball's keys, buttons, plunger, nudging and sound, from
  10.8.0's and 10.8.1's settings alike, asking on its first start. The game's
  menu opens it (*Settings...*). On Windows it is drawn with Direct3D 11 and
  reads the boards through the game's own DirectInput code
  (`src/dinput_joy.c`); setup adds an *Epic Pinball Settings* shortcut.
- **`[buttons]` on Windows**: a cabinet's buttons through DirectInput, as on
  Linux.
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
- **Levels sent again when the DOF helper is ready:** a flipper held, a ball
  in the lane or a lamp lit while the helper was starting reached its output
  only when it next changed.
- **A ball pushed off the left edge no longer wrecks the table** (seen on
  African Safari, in the game's own code on every table): after the game's
  clamp, a collision can still leave a ball at x = -1, and the save of the
  screen under it then copied some 49,000 bytes over the table's data, its
  timer vector too; the original would crash or hang there. `src/guard.c` puts
  such a ball back where the game's clamp puts it, as that save begins, always
  (the table's feedback map says where: version 7). Nothing changes while x is
  0 or more. Forced in a test: without it the table broke after a game; with
  it, play went on.
- Windows, found on a cabinet: setup stopped at the Visual Studio Build Tools
  (winget was given `--override` and the installer's options as one
  argument), and once they were in, setup, `dmd\build.ps1` and
  `dof\build.ps1` did not see them (vswhere wants `-products *` for Build
  Tools). `dof\build.ps1`'s libdof patch did not apply from a CRLF checkout
  (GitHub Desktop's git); it is applied with LF now, and `.gitattributes`
  keeps patches LF. The maps setup writes with Python on Windows have CRLF
  line ends, which `--table`, the DMD and the feedback refused; they read
  either now, and the tools write LF. The settings tool imports the
  VPinball settings saved last, not the newest version's (VPinball 10.8.0
  keeps its own beside 10.8.1's), and names `build.ps1` for the helpers.
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
