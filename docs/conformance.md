# Conformance

```
py tools/conformance.py            # needs your game data in original/ and a build
py tools/conformance.py --update   # record the current count as the new baseline
```

Current figure (2026-11-22, Linux): **152/152.** The whole run takes about 33 seconds:
headless time is deterministic and runs far faster than real time
([architecture](architecture.md#time)). Frames, audio and logs of the last run
stay in `work/conformance/` to explain any FAIL.

It runs `build\Release\epicpinball.exe` on Windows and `build-linux/epicpinball`
on Linux (`python3 tools/conformance.py`), or whatever `EPIC_EXE` names. The
two builds' frames matched bit for bit when last compared (2026-10-22): the
lifted C and the machine are the same code under either compiler. The 9 cabinet checks are Linux only; Windows
prints them as skipped and does not count them against the baseline. The
feedback checks are written for both but have only been run on Linux, so the
baseline of 152 is unproven on Windows. They need `work/feedback`, which the
harness makes first (`tools/feedback_map.py`).

## What it checks, and why that is the ground truth

There is no reference build to diff frames against — the original only runs
under DOSBox — so the checks are things the original visibly does, run headless
with scripted keys (`--keys`) and fixed-time frame grabs (`--shot-at`). Each
table is reached the way a player reaches it: BEGIN, down the list, ENTER, PLAY
GAME, then SPACE to start, the plunger every few seconds, and both flippers.

| Check | Fails when |
|---|---|
| lift *image*: every instruction form handled (16 images) | `tools/lift.py` met an instruction lift16 emitted as `UNHANDLED` |
| boot: clean exit, no dispatch misses | the process crashed, or the guest reached an address with no lifted function |
| boot: SETUP check answered, sound drivers placed | PINBALL did not get past its SETUP check, or never loaded the MASI mixer and SB driver |
| boot: menu on screen | 22 s in (past the notice and the intro), the frame is not a graphics picture with the menu drawn |
| table *n*: clean exit, PINBALL ran EP*n*.EXE, no dispatch misses (×13) | the menu did not EXEC the table chosen, or anything along the way missed |
| table *n*: 320×240 playfield with 32+ colours | the table is not in its Mode X 320×240 view, or it is drawn in a handful of colours |
| table *n*: scoreboard in the split screen | the rows below line compare are dark: the split is wrong or the dot matrix was not drawn |
| table *n*: the view moves in play | no frame of play (one a second) differs from the one 12 s in by over 2% of the playfield — no ball, no scrolling |
| table *n*: SB16 stereo stream started | the SB driver never started an SB16 stereo auto-init transfer |
| table *n*: audio | the WAV the run wrote is near silent |
| remaster table 1, 9: whole table is the page the screen scrolls over | `--whole-table`'s frame does not contain, row for row, the playfield the game showed at the same moment: the wrong page, or offset ([remaster](remaster.md#the-whole-table)) |
| remaster table 1, 9: scoreboard under the whole table | the whole-table frame's last rows are dark |
| remaster table 1, 9: presenter letterboxes it at 1280x720 | `--present 1280x720` did not draw the picture where a square-pixel table belongs (no Direct3D 11, or the shader is wrong) |
| remaster table 1, 9: a dark blur of the picture beside it | the bars are black, or as bright as the picture |
| cabinet menu: on the playfield, the scoreboard black | `--cabinet` with no table up changed the menu, or showed something on the scoreboard |
| cabinet table 1, 9: the playfield is the front page, no scoreboard | it is not 320×399 (394) rows that start exactly at one page's first row (read through `EPIC_VRAM0`, past the composer) and hold the view the game shows |
| cabinet table 1, 9: the scoreboard is the rows under the split | it is not, pixel for pixel, the game's split rows at the same moment |
| cabinet table 1, 9: both the same with the scoreboard hidden (Enter) | Enter took the scoreboard off the cabinet's second screen, or put part of it on the playfield |
| cabinet table 1, 9: --rotate 90 presents the same picture, turned | a pixel of `--present 1280x720 --rotate 90` differs from `--present 720x1280` turned |
| feedback table 1, 9: a flipper event for each press unless tilted, within 20 ms | a press was missed, doubled or late |
| feedback table 1, 9: ball release, launch and bumpers are felt | the map or the watcher no longer sees the game's events |
| feedback table 1, 9: watching changes no pixel and no sample | `--feedback-log` altered the run: the watcher is no longer read-only |
| plunger table 1, 9: a sensor at rest changes no pixel and no sample | the plunger wrote to the table with nobody touching it |
| plunger table 1, 9: a full pull launches as hard as Space | a scripted sensor's release and a held Space give different strengths |
| determinism: two runs, identical frames | the same script twice gave different frames — time leaked into the run |

## Without the game

The data is not redistributable, so on a machine without `original\PINBALL.EXE`
or without a build the harness prints

```
SKIP: no game data in original/ or no build -- conformance needs your own copy (README, Getting Started)
```

and exits 0. A count below `tests/conformance_baseline.json` exits 1.
