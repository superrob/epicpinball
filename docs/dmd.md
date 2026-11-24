# The scoreboard, the messages, and a DMD

A 128x32 DMD picture of every table, in a window or on a real DMD (a ZeDMD,
through libdmdutil), with the messages tables 1-8 put on the playfield moved
to it. Below: how to turn it on, what each table shows
where (read from its lifted code and checked by running it), and how the
picture is put together.

## Using it

```sh
python3 tools/dmd_survey.py                    # once: work/dmd/epN.map, from your copy of the game
./epicpinball.sh --dmd                         # a DMD window beside the game
./epicpinball.sh --cabinet --dmd               # in a cabinet: a DMD window, no scoreboard window
dmd/build.sh                                   # once, for a real DMD (fetches libdmdutil)
./epicpinball.sh --cabinet --dmd-device        # the DMD on a ZeDMD (or Pixelcade, PinDMD3), no scoreboard window
```

On Windows the helper is built by
`powershell -ExecutionPolicy Bypass -File dmd\build.ps1` (Visual Studio 2022
and CMake; [dmd/](../dmd/README.md)), and `--dmd-device` is the same option.

`--dmd` and `--dmd-device` go together or alone. In a cabinet with either,
no scoreboard window opens: the DMD shows the scoreboard. With `--dmd` the
second window is the DMD's (borderless, on top, dragged and remembered like
the scoreboard's, as `[cabinet] dmd_window`), dark when no table is up and
never the game's bar. Either takes the messages
of tables 1-8 off the playfield, since the DMD shows them. Closing the DMD
window hides it; the game goes on.

### Messages a cabinet does not show

With `--cabinet`, `--dmd` or `--dmd-device`, messages about keys a cabinet has
not got are left out everywhere: the DMD, the playfield (1-8) and the
scoreboard (9-13). Found in every table's code and data:

| Left out | What it is | Tables |
|---|---|---|
| USE UP/DOWN ARROW KEYS TO / VIEW ALL OF THE PLAYFIELD | help before a game | all but 3, 5 |
| USE SHIFT KEYS TO CONTROL / THE FLIPPERS | help before a game | all but 3, 5 |
| PRESS ENTER TO REMOVE / THE SCORE BOARD (8: THE SCOREBOARD) | help before a game | 1, 2, 6, 8 |
| PRESS SPACE TO BEGIN | before every game | all |
| PRESS ENTER | the line under the bonus | 1, 2, 4, 7 |

Kept: the gameplay tips between the help screens, and what keys answer
(QUIT? [Y/N], ENTER PARAMS THEN PRESS ESC, BALL TRACKING SPEED with
S)LOW F)AST I)NSTANT, MUSIC ON/OFF, NO MUSIC AVAILABLE, SOUND FX OFF), PAUSE
(9-13), DEMO and GO PLAYER n.

A message whose first line is listed goes with all its lines; a listed
appended line goes alone. `[cabinet] hide_messages` in `epicpinball.ini`
replaces the list: first lines, `|` between them, spaces round them not
counted; empty hides none. On the DMD the board shows meanwhile (on 9-13 the
last one, kept, as the message replaces it in the game's list); on 9-13 the
cabinet's scoreboard is blank meanwhile, as the game draws nothing else
there.

`--dmd-device` starts `dmd/build/epicpinball-dmd` once for the run (GPLv3,
like libdmdutil, in [dmd/](../dmd/README.md); the game does not link it).
Without settings libdmdutil looks for a ZeDMD on every serial port, over
WiFi at `zedmd-wifi.local`, and for a Pixelcade and a PinDMD3; it says on
stderr what it found. To name the port, or set the brightness, write
libdmdutil's own settings into `saves/dmd/DMDUtil.ini`, for example:

```ini
[ZeDMD]
Enabled = 1
Device = /dev/ttyUSB0
Brightness = 6
```

The user running the game needs to be able to open the serial port (on most
distributions, the `uucp` or `dialout` group). On Windows the port is a COM
port (`Device = COM3`), which any user may open.

## The bar is not a DMD

Every table keeps a dot-matrix scoreboard at video address 0, below the
playfield the CRTC splits off by line compare ([lifting](lifting.md#12-the-scoreboard-is-the-split-screen)):

| | Tables 1-8 | Tables 9-13 |
|---|---|---|
| shown | 320x19 pixels (scanline 441) | 320x29 (scanline 421), a 30th row kept |
| what it is | solid text plus score digit pictures | all dots, 2 pixels apart: about 160x15 dots |
| drawn | when something changed (a dirty flag) | from a list of dots, every frame |
| messages | on the playfield | in the bar |

A 128x32 DMD is 4:1; the bar is 17:1 or 11:1. Scaling it down loses the
text. So the DMD image has to be put together from what the game draws,
not copied from where it draws it.

## One message engine in all 13 tables

Each table has a *message function* and two or three *line appenders*
(found by instruction signature in all 13: `tools/dmd_survey.py`):

- the message function takes BX = the text, AL = an effect, AH = a font (0
  small 5x5, 1 large 7x7, 2 tiny: a stub on tables 1 and 8, 4x4 on 2-7, 4x5
  on 9-13; 3 more for the same without centring) and DI = the place, as
  y\*320+x. It centres the text across 320 pixels, drops it if it is longer
  than 30 characters or wider than the screen, and turns it into a **list of
  dots**: one word per lit font pixel, its screen offset. Font pixels are 2
  screen pixels apart, both ways; characters advance 16 pixels (large), 11
  (small) or 8 (tiny). The glyphs are read column by column, top to bottom,
  so the list's order is fixed by the text and font;
- an appender adds a further line to the same list, at its own DI, without
  centring it unless the caller did. The bonus screen, the high scores, the
  instructions before a game and "SUPER JACKPOT! 100M" are a message and its
  appended lines. Watching only the message function misses those;
- every frame an **effect** moves the dots, by the id in AL: hold, a wave,
  explode (with gravity) and fade, slide left or right, scroll up, and on
  tables 9-13 a colour shimmer. A frame counter ends it, or it stays until
  the next message;
- the dots' colour is DAC entry FFh, which the message function sets: white
  on tables 1, 2, 3, 5 and 6, green on 4 and 8, yellow on 7, red on 9-13.
  Tables 9-13 also keep a colour per dot, so one message can be two colours
  (table 10's "T-REX BONUS NOW" in green over its value in red).

On table 1: the message function is `ep1_3380E`; the dot list is at ds:10C2,
the effect id at ds:0B3A and its frame counter at ds:0B3D; the appenders are
`ep1_37B56` (large) and `ep1_37BDC` (small), which continue the list from the
end pointer at 68F2:050C; `ep1_36065` animates and plots it every frame into
the page being drawn, saving what is under each dot (ds:1B56 and ds:306E, a
list per page) and putting it back the next frame. Table 9's message function
is `ep9_320D5`, its list at ds:1823 with the colours 960h after it, plotted
into the bar by `ep9_34022`.

### Tables 1-8: the bar

Table 1's bar, from video memory:

- row 0 a line; text in a solid 8x7 font, 8 pixels a character, on two text
  lines (rows 2-8 and 10-16): "BALL n" at x 0 and "PLAYER n" at x 64, exactly
  128 pixels together;
- the score at the right, x 196-315, rows 2-18: ten 12x17 digit pictures,
  each a pattern of dots (lit, unlit and background colours), 120 pixels in
  all;
- a 66x13 picture at (64, 3) (not yet looked at).

The other tables of 1-8 have the same pieces in their own colours and their
own digit style: dotted digits on 1, 2, 7 and 8, metal blocks on 3 and 5,
grey segments on 4, a green LCD on 6 (as captured; tables 3 and 5 say little
else, see below).

### Tables 9-13: the bar is the message list

The idle board is a message: "BALL n PLAYER n" in the tiny font at the right
of row 1, then the score appended in the large font at row 3, then the other
players' scores. Each score change rebuilds it. A gameplay message replaces
the whole list, and the board comes back when it ends.

## What each table says

`python3 tools/dmd_survey.py` lists every call, with the text, effect, font
and place where they are fixed, into `work/dmd/epN.txt` (from your copy of
the game; not committed). A run with every call logged (a scripted game of
500 s, 13 tables) gave:

| Table | Name | Message function | Appenders | Calls in the code | Seen in a game: calls / distinct |
|---|---|---|---|---|---|
| 1 | Super Android | `ep1_3380E` | 2 | 83 | 41 / 22 |
| 2 | Pot of Gold | `ep2_3484E` | 2 | 85 | 27 / 10 |
| 3 | Excalibur | `ep3_3217E` | 2 | 25 | 9 / 6 |
| 4 | Crash and Burn | `ep4_31B29` | 2 | 84 | 44 / 21 |
| 5 | Magic | `ep5_2F411` | none found | 13 | 6 / 4 |
| 6 | Jungle Pinball | `ep6_3515F` | none found | 39 | 27 / 9 |
| 7 | Deep Sea | `ep7_3226A` | 2 | 96 | 160 / 84 |
| 8 | Enigma | `ep8_36965` | 2 | 86 | 17 / 12 |
| 9 | Cyborgirl | `ep9_320D5` | 3 | 55 | 277 / 110 |
| 10 | Pangaea | `ep10_32FA3` | 3 | 78 | 688 / 33 |
| 11 | Space Journey | `ep11_2E7A4` | 3 | 79 | 529 / 40 |
| 12 | Toy Factory | `ep12_2FCDD` | 3 | 65 | 684 / 33 |
| 13 | African Safari | `ep13_2C88E` | 3 | 62 | 638 / 35 |

On 9-13 most calls are the idle board rebuilt as the score changes.

Kinds of message, by where they land on a 128x32 frame:

- **short**: "JACKPOT!!!", "POWER UP", "TILT", "NO BONUS": the large font fits
  16 characters at one DMD dot per font pixel, the small font 21;
- **long**: up to 30 characters ("SHOOT RIGHT HOLE TO ACTIVATE", "HIT TARGETS
  TO LITE WEAPON HOLES"): too wide for one line, so they wrap;
- **several lines**: the instructions before a game, "SUPER JACKPOT!" with
  "100M", the bonus screen (BONUS, five counts, the total), the scores at game
  over: up to eight lines, more than 32 dots tall;
- **the board** on 9-13, and on 1-8 the bar itself.

## Per-table checklist

Done for all 13: the message function and appenders by signature, the call
list, a logged game, the bar captured. Still open:

- [ ] read each message function and its renderer, not only its signature
  (done: 1 and 9; the rest work in a scripted game);
- [x] the dot list, the effect and its counter, every font the message
  function and the appenders draw with, and on 1-8 the save-under lists, for
  every table: `work/dmd/epN.map`, by `tools/dmd_survey.py`, so none is
  written down as an address;
- [ ] tables 5 and 6: no appender matched the signature; how they add lines
  (table 6 does: its second lines are missing on the DMD);
- [ ] tables 3 and 5 say very little through messages (6 and 4 distinct in a
  game): what else tells the player what happened;
- [ ] table 1's 66x13 picture in the bar, and the like on 2-8;
- [ ] the effects of each table beside table 1's (11 ids; 9-13 add one);
- [ ] screens the scripted game did not reach: jackpots, modes, high score
  entry, matches.

## How the DMD picture is put together

Mock frames, from the games' own fonts and table 1's bar from video memory,
for choosing a layout: `work/dmd/mockups-1.png`. Chosen from them:

- a message takes the whole DMD; the board comes back when it ends;
- a short message is doubled when it fits (8 large characters, 128 dots),
  otherwise one DMD dot per font pixel;
- a line too wide for 128 dots wraps at words, in the small font;
- on 9-13 the board is BALL/PLAYER in the tiny font over the score doubled;
- several lines more than 32 dots tall (the bonus) are shown a page at a time.

How (`src/dmd.c`):

- **text laid out again, dots moved as the game moves them.** A message is
  set in the table's own font at one DMD dot per font pixel, wrapped to 128
  dots. Done in the game's order, dot *i* of that layout is dot *i* of the
  game's list, so the game's own effect (explode, wave, slide) is applied by
  moving each dot as far as the game moved its own;
- **the board** from the bar's own pieces: on 1-8 the BALL/PLAYER line and
  the score digits, pixel for pixel, one above the other; on 9-13 the board's
  lines set again;
- **the game's colours**: DAC FFh and the per-dot colours for messages, the
  bar's own for the board;
- **messages off the playfield** on 1-8 in DMD mode: when the playfield is
  composed for the screen, each message dot gets back what the game saved
  from under it, and is put back before the game runs on. A dot something
  was drawn over since (the ball) is left alone. The game itself is not
  changed;
- **a message the game never ends** (effects 0, 3, 7, and ids with no
  effect: "JACKPOT!!!", "KICKER IS NOW ACTIVE" on table 1) gives way to the
  board after 4 s of play, counted while the physics runs. A screen that
  waits on the player ("QUIT? [Y/N]", the bonus with "PRESS ENTER", the
  scores at game over) stops the physics, so it stays. One that has
  exploded, with no dot left on the DMD, gives way at once;
- **a waving message** (effects 2-5 and 7 move dots up to 12 font pixels
  down and back; 24 rows when doubled) is raised as far as its lowest dot
  goes past the bottom, never past the top, and stays raised while it is up.
  Effect 2 (TILT on table 1) also pulls its dots off to the left, as on the
  playfield;
- **the real DMD** gets a frame when the picture changes, and the last one
  again each second, so a display found late is not left dark.

`EPIC_DMD_CHECK=1` says, for each message, whether the game's list holds the
dots rasterised here: where it ends against how many there are, and how many
are in place. Scripted games of 200 s on all 13 tables (4,778 messages): the
count right and every dot in place on twelve tables, but for one dot an
effect had already moved on a few messages of 9-13. Table 6 adds lines with
an appender the survey does not find, so its second lines (the instructions,
the bonus) are missing.

The window costs 0.1 ms to redraw (only when the picture changes) and 0.04
ms for a cabinet's scoreboard copy, measured here.

### Where it stands

On a real cabinet, table 1: it works.

Every table, in a scripted game, headless, with the picture captured:

- **table 1**: messages wrapped, doubled and paged as chosen; explosions
  carried over; waves kept on the display; the board from the bar; no
  message left on the playfield, the keyboard ones nowhere;
- **tables 2-8**: the same engine and board, in their own colours (green on
  4 and 8, yellow on 7) and digit styles. Not looked at message by message;
  table 6 lacks its appended lines (above);
- **tables 9-13**: messages in their colours, a dot each (table 10's "T-REX
  BONUS NOW" green over its value in red), the board as chosen. The board's
  other players' scores (several players) not seen yet;
- the real DMD: `dmd/build/epicpinball-dmd` fed by the game on table 1, with
  no display attached: libdmdutil was given the frames the game composed,
  each one (its own RGB888 dump, `EPICPINBALL_DMD_DUMP=DIR`); then on the
  real cabinet, table 1;
- Windows' end of the pipe (`src/dmd_pipe.c`) under Wine, with a stand-in
  helper that reads slower than the game sends: frames arrive whole, in
  order, the newest last. The helper itself and `dmd/build.ps1` were only
  syntax-checked and written, not built: not yet on a Windows cabinet.


## libdmdutil

[libdmdutil](https://github.com/vpinball/libdmdutil) drives ZeDMD,
Pixelcade, PIN2DMD and others. It is GPLv3 and C++20 only, so the game does
not link it: `dmd/`'s helper, as `dof/`'s is for libdof, takes frames over a
socket pair of packets (a frame each, never half of one, never waited for;
`src/dmd_pipe.c`; on Windows an anonymous pipe whose write end never waits,
frames back to back and each sent whole before the next) and calls `UpdateRGB24Data`, which queues and does not
block. `FindDisplays()` looks for the displays on its own thread. The build
leaves out Serum, VNI, PUP and PIN2DMD (colourisation, and libusb).
