# Roadmap

## Next

- **Play it by hand.** Every table has been played by script for half a minute.
  A real session — multiball, every table's modes and bonus rounds, entering a
  high score — will find what scripts do not.
- **Listen to it.** The sound is verified by measurement (an SB16 stereo stream,
  two smooth channels, music-shaped spectrum), not by ear. It is interpolated
  linearly from 22 kHz to 44.1 kHz; a windowed resampler is the next step if
  that is not enough.
- **Try the remaster's hotkeys and menu by hand.** The whole table, the
  presenter and the settings file are checked headless and in a recorded
  window; F7-F12, the menu and the pad were not pressed by a test.
- **Cabinet feedback on every table.** `--dof` and the analog plunger work on
  a real cabinet on table 1; tables 2 to 13 have only met a sandbox
  (docs/cabinet-feedback.md).
- **A netlab recipe**, so builds and conformance run on the farm like the other
  recomps (repo rules, section 8). Needs the MSVC build to run on the clang-cl
  builder, or an MSVC builder.

## Later

- **Lift `SETUP.EXE`** (Borland C++ 3.x, like Blake Stone's executables) so its
  sound-card menu works and the `-cd` check stops being answered by hand. Until
  then the card is whatever `SOUNDCRD.INF` says; GOG's says Sound Blaster 16.
- **The other MASI drivers**: Gravis Ultrasound (`MDRV003R`), Pro Audio Spectrum
  (`MDRV005R`), PC speaker and no-sound. Each needs its card modelled; the
  SB16 covers everyone today.
- **Gamepad**: `controller.ini`, GOG's mapping by default, untested on hardware.
  Rumble on the flippers and bumpers would be the remaster's next touch.
- **Cabinet mode on Windows**: `--cabinet` is Linux only. Windows needs a
  swap chain per window and the turn in the shader.
- **Cabinet mode on more cabinets**: it works on one real cabinet under KDE,
  on table 1, with the scoreboard placed on the second display, and is tested
  on virtual displays. wlroots compositors and SDL2 proper (not sdl2-compat)
  are untried.
- **More cabinet feedback**: every table has its own events now, tried
  only by `--autoplay`: each on a real cabinet, and the jackpots, multiball
  and later levels the autoplayer never reaches. Lamps (each table's lights
  as levels), Windows, and an entry in the DOF Config Tool's database, which
  is the Config Tool's site to take.
- **The DMD** (works on a real cabinet on table 1; every table's messages
  looked at, headless, docs/dmd.md): tables 2-13 on a real DMD; the
  jackpots, modes and later levels `--autoplay` does not reach; table 1's
  66x13 picture in the bar; Windows.
- **SSF** (works on a real cabinet's exciters): each table's own board
  effects beyond slots 0-6 by ear on a cabinet (they were chosen by sample
  and by when they play: docs/ssf.md); Windows.
- **`--table`**: Windows; a frontend's own start and exit keys.
- **Nudging**: nudge buttons on a board that has them; tables 2-13 by
  hand on a cabinet; Windows.

## Out of scope

- The `DEMO\` folder's shareware build (a different `PINBALL.EXE`/`EP1.EXE`).
- Changing the game: no hi-res playfield. The tables are drawn as 320-pixel
  bitmaps; there is nothing to redraw at higher resolution. The whole-table
  view shows more of the game's own picture, which is as far as it goes.
- Glow on the lamps, as the SimCity 2000 recomp has: the lamps are not the
  brightest colours on these tables, the plastics are (docs/remaster.md).
