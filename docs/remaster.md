# The remaster

Everything here sits around the game's own code and changes nothing it does:
the tables, their physics and their timing are the recompiled originals. The
pieces follow the other pcrecomp recomps: the presenter is the Gunman
Chronicles recomp's, the blurred bars and the settings menu are Red Alert 2's,
`controller.ini` is Gunman's. `--classic` turns all of it off.

| Key | |
|---|---|
| **F5** | with `--cabinet`: stretch the table to fill its display, or back ([below](#cabinet-mode)) |
| **F6** | with `--cabinet`: turn the playfield ([below](#cabinet-mode)) |
| **F7** | whole table, or the game's scrolling view |
| **F9** | colour: natural, vivid |
| **F10**, right-click | the settings menu |
| **F11**, **Alt+Enter** | fullscreen (borderless, on the window's monitor) |
| **F12** | scaling: sharp → smooth → CRT → nearest → integer |
| **PrtSc** | screenshot of what is on screen, to `screenshots\` |

The game never uses F7–F12 or the mouse, so taking them costs it nothing. Nor
does it use F5 and F6 in play (in the menus, like any key, they end the intro),
and only cabinet mode takes those.

## The whole table

![The whole of Super Android in a 16:9 window](screenshots/whole-table.png)

Epic Pinball's tables are taller than the screen. The game draws each one
whole into video memory and scrolls by moving the CRTC's start address, so a
320×240 monitor sees 221 rows (211 on tables 9–13) of a table of nearly 400, with the dot-matrix
scoreboard under them (the line-compare split, see
[architecture](architecture.md)).

Video memory holds the table twice, in two pages that share what the
scoreboard leaves of the 64 KB plane, for flicker-free page flipping:

| | scoreboard | page 1 | page 2 | apart |
|---|---|---|---|---|
| tables 1–8 | 19 rows shown, 20 kept | offset 1600 | offset 33600 | 400 rows |
| tables 9–13 | 29 rows shown, 30 kept | offset 2400 | offset 34000 | 395 rows |

The second page runs to the end of the 64 KB plane, which makes a page 399
rows on tables 1–8 and 394 on 9–13. The start address says which page is in
front; `vga_compose_table` (src/vga.c) draws that page from its first row and
the scoreboard under it, 320×418 or 320×423. Enter slides the scoreboard
away by moving the split; the pages don't move, so their places come from
its full height, and with it gone the screen shows as usual. The ball is in the page wherever
it is on the table, because the game draws it there before it flips. Nothing
is redrawn or guessed: it is the game's own picture, more of it.

The layout comes from the start addresses every table actually writes
(logged over a scripted game on all thirteen) and is derived from the CRTC
registers at run time, so no table has a constant of its own. Menus, the
intro and anything that is not a table in Mode X 240 with a split show as
usual.

## Cabinet mode

```sh
./epicpinball.sh --cabinet
```

For a virtual pinball machine, on Linux (on Windows it says it is not
supported yet). Two windows, neither with decorations:

**The playfield** fills a display. It is the table's front page alone, 320×399
(tables 9–13: 320×394) in square pixels: no scoreboard, whatever the game is
doing with its own. **F6** turns it 90° clockwise, round to upright again,
with everything in it: menus, intro, scanlines, bars. Turned on a 16:9 screen
the table fills the height and about 70% of the length. **F5** stretches it to
every pixel of the display, out of its shape, and back; turned or not, with
the scaling and the CRT look as they are. Only a table is stretched: the menus,
before a table and after leaving one, keep their shape, and so does the
scoreboard. F5 stretching works on a real cabinet.

**The scoreboard** is the rows the game keeps at the top of video memory,
320×19 (tables 9–13: 320×29), in their own shape with black round them. The
game goes on drawing them when Enter hides its scoreboard, so this window
stays live. It is black when no table is up. Drag it to move it, drag an edge
to resize it; right-click is still the settings.

For exciters, `--ssf` puts the board's sounds on them, placed on the table
([ssf](ssf.md)).

With a DMD (`--dmd` or `--dmd-device`, [dmd](dmd.md)) there is no scoreboard
window: the DMD shows it, `--dmd` in a window of its own placed the same way
(`dmd_window` in `[cabinet]`).

Messages are where the game puts them: on the playfield for tables 1–8, in
the scoreboard for 9–13. Those about keys a cabinet has not got are left
out: the help screens before a game (USE UP/DOWN ARROW KEYS TO VIEW ALL OF
THE PLAYFIELD, USE SHIFT KEYS TO CONTROL THE FLIPPERS, PRESS ENTER TO REMOVE
THE SCORE BOARD), PRESS SPACE TO BEGIN, and the PRESS ENTER under the bonus.
The gameplay tips between the help screens stay. On 9-13 the scoreboard is
blank while one of them would be up ([dmd](dmd.md#messages-a-cabinet-does-not-show)).

To set a machine up:

1. Start it once. It lists the displays:
   `[cabinet] display 1: DP-2 1280x390 at 1920,0`.
2. If the playfield is on the wrong one, find its number there and set
   `playfield_display` in `saves/epicpinball.ini` (or pass
   `--playfield-display N` each time). Numbers can change when monitors are
   plugged in differently.
3. Press F6 until the table reads from the front of the machine.
4. Drag and resize the scoreboard into place, or pass
   `--score-window X,Y,W,H`. With one display it starts at the top, over the
   playfield; with more, in the middle of another.

```ini
[cabinet]
rotation=90           ; 0, 90, 180, 270: F6 writes it
stretch=0             ; 1: a table fills its display; F5 writes it
playfield_display=0
score_window=1920,0,1280,116
dmd_window=1920,0,1024,256  ; with --dmd: the DMD window's place
; hide_messages=USE UP/DOWN ARROW KEYS TO|PRESS SPACE TO BEGIN   ; replaces the list; empty: hide none
```

F5, F6 and dragging are remembered there. `--stretch`, `--rotate`,
`--playfield-display` and `--score-window` win for their run and
are not written. Cabinet runs leave the ordinary window's settings alone
(`whole_table`, `fullscreen`, `window`), and F7, F11 and Alt+Enter do nothing
in one. The keys and the pad work whichever window has the focus, and closing
either quits. F6 is a keyboard key: `controller.ini` presses the game's keys
and cannot press it.

Solenoids and an analog plunger are set up separately:
[cabinet feedback](cabinet-feedback.md).

It has run on a real cabinet under KDE: the playfield on one display and the
scoreboard placed on the second. Other desktops are tested on virtual displays
only.

- **Wayland** does not let a program place a window, so cabinet mode uses
  X11 (XWayland) when there is one. `SDL_VIDEODRIVER=wayland` overrides that:
  the playfield still fills its display, and the scoreboard, which cannot be
  placed there, fills another, in its own shape. With only one display it
  goes where the compositor puts it, which may be behind the playfield.
- **Moving the focus** from one window to the other lets go of held keys
  (SDL does this), so a click on the scoreboard with the plunger held back
  launches the ball.
- `--trace` logs any frame that took over 1 ms to put on screen
  (`[present] slow`). Two windows are drawn on the game's thread, and the
  retrace it waits for lasts 1.4 ms.

## The presenter

![The scrolling view, sharp-bilinear, with the blur beside it](screenshots/presenter.png)

`src/present.c` scales the frame on the GPU with Direct3D 11 (WARP when there
is no GPU), into a window of any size:

- **sharp** (the default): sharp-bilinear. Whole pixels stay crisp and only
  their edges blend, so the picture neither blurs nor shimmers at any size.
- **smooth**: bilinear.
- **CRT**: scanlines and an aperture grille, faded in from 1.5× (below that
  they beat against the screen's pixels).
- **nearest**, **integer** (nearest at whole multiples only).
- **Vivid**: a little more saturation and contrast.
- **Beside the picture**: a dark blur of the picture itself, or black.

![CRT at 4K](screenshots/crt.png)

The 4:3 picture keeps the 4:3 a 1993 monitor gave it; the whole table keeps
square pixels, as Mode X 320×240 has. If Direct3D 11 cannot start, the window
falls back to a GDI stretch (nearest, or halftone for smooth) and says so on
stderr.

No glow. The SimCity 2000 recomp lights up bright, saturated pixels; here the
brightest saturated colour is the yellow plastic, not the lamps, and it
bloomed the whole playfield.

## Sound

The Sound Blaster plays the game's music and effects at 22,050 Hz; the host
plays 44,100. **Smooth** (the default) draws a straight line between the
DSP's samples; **original** holds each one, the steps a real card's output
filter took away. Settings menu → Sound.

## Settings

Changes made in the window are kept in `saves\epicpinball.ini`, with the
window's place:

```ini
[display]
scale=sharp           ; sharp, smooth, crt, nearest, integer
look=natural          ; natural, vivid
bars=blur             ; blur, black
whole_table=0
fullscreen=0
window=130,130,976,759
[sound]
output=smooth         ; smooth, original
```

The command line (`--scale`, `--look`, `--bars`, `--whole-table`,
`--fullscreen`, `--window N`, `--classic`, and the cabinet's three) wins over the file for that run and
is never written to it. Headless runs read only the command line, so a test
never depends on anybody's preferences.

The game sets only the keys it changed, in place, so the file's comments
and anything else in it stay. That includes `[modes]`, which the settings
tool ([config.md](config.md)) writes: the modes every run with a window
starts with, as if their options were given.

```ini
[modes]
cabinet = 1           ; --cabinet
dmd = 0               ; --dmd
dmd_device = 1        ; --dmd-device
ssf = 1               ; --ssf
dof = 1               ; --dof
```

`--no-cabinet`, `--no-dmd`, `--no-dmd-device`, `--no-ssf` and `--no-dof` leave
one out for a run. A mode from the file that cannot run (a helper that is not
built, `--cabinet` with `--window`) is left out with a line saying why; the
game still starts.

## The controller

An Xbox controller (XInput) presses keys, so it does whatever the key does.
`saves\controller.ini` is written with GOG's mapping the first time:

| | |
|---|---|
| LT / RT | left / right flipper (Shift) |
| LB / RB | nudge left / right (Z, /) |
| A | Space: start, plunger, nudge up |
| X | Enter: the scoreboard |
| Y / B | Y / N |
| Back, Start | Esc |
| d-pad, left stick | arrows |

Each line is `input = KEY` (`none` for nothing); `trigger` and `stick` set how
far they go before they count. Settings menu → *Controller mapping...* opens
the file (on Linux, *Settings...* opens the settings tool); it is read again
when the window comes back to the front. The same file's `[keys]` adds
keyboard keys to the game's actions, and `[buttons]` a cabinet board's
buttons ([cabinet-feedback.md](cabinet-feedback.md#keys-and-buttons)). What the
pad holds is let go when the window loses the front or the pad is unplugged.
On Linux a cabinet board's axes are never taken for triggers or a stick (only
its buttons count), and `[plunger]` in the same file sets up an
[analog plunger](cabinet-feedback.md#the-plunger). Not yet
tried on a real pad.

## On Linux

All of the above, with SDL2 doing what Win32 and Direct3D do on Windows
(`./setup.sh`, README):

- The presenter is SDL's renderer, OpenGL when there is a GPU. Sharp is a
  nearest prescale to whole multiples and bilinear the rest of the way, which
  is what the shader computes; CRT is the shader's beam and grille laid over
  it as a multiply (SDL's software renderer cannot brighten with it, so there
  CRT comes out darker); vivid and the blur are the same CPU code.
- The settings menu (right-click, F10) is a dialog: what is set, the keys for
  it, and buttons for the bars, the sound and the settings tool
  ([config.md](config.md); `controller.ini` in an editor, by `xdg-open`, when
  the tool is not built).
- The pad is SDL's GameController, which gives any pad Xbox names, so
  `controller.ini` maps it as it does on Windows.
- The files are the same: `saves/epicpinball.ini`, `saves/controller.ini`,
  `screenshots/`.

## Testing it

With `--cabinet` each shot is the playfield, upright, and beside it
`NAME-score.bmp`, the scoreboard; `--present` turns the playfield by
`--rotate` as the window would. `--record` is always the game's own picture.

`--present WxH` sends every `--shot-at` frame through the presenter,
offscreen, at that size (on Linux with SDL's software renderer), so all of the above runs headless — over RDP and in
CI. The conformance harness ([conformance](conformance.md)) checks, on a table
with each scoreboard height, that the whole table is the very page the game
scrolls over (the playfield the game shows at the same moment is a band of
it),
that the scoreboard is under it, and that the presenter letterboxes it with
the blur beside it. `tests/selftest.c` checks the page arithmetic and the
sound interpolation without any game data.
