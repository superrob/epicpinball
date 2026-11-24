# The settings tool

```sh
./epicpinball-config.sh                 # the save folder is saves/, as the game's
./epicpinball-config.sh --save DIR      # another one
```

`epicpinball-config` sets up what would otherwise be typed into
`saves/epicpinball.ini` and `saves/controller.ini`: the modes the game starts
in, the keys, a cabinet's buttons, its plunger and nudging (with
calibration), SSF, the DMD and DOF. It can import all of that from
VPinball's settings. The game's Linux menu (right-click or F10) opens it with
*Settings...*.

It is built with the game (`build-linux.sh`): Dear ImGui on SDL2, fetched
pinned by CMake. `cmake -DEPIC_CONFIG=OFF` builds the game without it, with no
network needed. Linux for now; Windows gets it with the Win32 host's
DirectInput and Direct3D 11 work.

Nothing is written until **Save**, and then only the settings that changed,
each in place: comments and anything the tool does not know stay as they
are. A running game takes the keys, buttons, plunger and nudging when its
window next comes to the front, and the modes when it next starts.

## Pages

**Modes and picture.** The modes every run with a window starts with:
cabinet, the DMD window, the DMD device, SSF and DOF, as if `--cabinet`,
`--dmd`, `--dmd-device`, `--ssf` and `--dof` were given (`[modes]` in
`epicpinball.ini`). A helper that is not built is pointed out.
`--no-cabinet` and the others leave one out for a run. Also scaling,
colours, the bars, the whole table, fullscreen, and the cabinet's turn,
stretch and playfield display (listed by name).

**Keys.** Each of the game's actions, its own key (which always works), and
keys added to it: *+ add* takes the next key pressed, clicking a key removes
it (`[keys]`).

**Buttons.** A cabinet board's buttons: the board, its buttons lighting as
they are pressed, and per action *+ press*, which takes the next button
pressed on any board and picks that board (`[buttons]`).

**Plunger.** The board and axis, with every axis as a live bar. *Find the
axis*: pull the plunger right back and let it go within five seconds, and the
axis that moved most is taken, with where it rested and how far it went.
*Set* takes the axis as it is now for rest or pulled. A drawn plunger, its
spring and knob, moves with the reading, and shows how far pulled the game
would see it (the game's own rule, `plunger_map`). Under 1000 between rest
and pulled is pointed out: the game turns such a plunger off.

**Nudging.** The board, the axes across and along the cabinet, inverting,
**Velocity input** (`is_velocity`, for boards set to report velocity), the
dead zone, strength and tilt (0 to 10), and the playfield indicator and its
range (the reading that takes the dot to the ring). *Calibrate* goes
through four steps:

1. *Keep the cabinet still* (3 s): the noise on the axes gives the dead zone,
   its peak and a half again, plus 300. A reading that rests far from 0 is
   pointed out.
2. *Shove the cabinet from the front*: the axis that moves most is along the
   cabinet, and the way it moves first says whether to invert it.
3. *Shove it from the left side*: the same, across.
4. *One hard shove*: its peak sets the strength so a hard shove pushes about
   as the game's own nudge (0.5 to 10), and the indicator's range, so a hard
   shove takes the dot to the ring.

Then *Try it*: the indicator as the game draws it, larger, and what the ball
feels and the tilt meter, worked out by the game's own `nudge.c` on a pretend
table, 200 steps a second, so a shove that would tilt the table shows TILT.

**SSF.** The effects and rolling volumes, and a speaker test on each of the
7.1 channels the game uses, with the game's own sounds: front left and right
(the game's own mix) play a piece of Super Android's music, side and rear one
of its playfield sounds, picked at random, at the effects volume. They are
recorded when the page first opens, by the game itself run headless for a
moment on Super Android with `--ssf` (the board's effects cut out of the
exciters' channels, rolling off), and kept only while the tool runs; without
the game's files a low knock stands in. A sound device with fewer than 8
channels is pointed out.

**DMD and DOF.** The ZeDMD's settings for libdmdutil
(`saves/dmd/DMDUtil.ini`: enabled, serial port, brightness), and a DOF test:
each button fires one event through the helper's `--test`, on table 1's row,
with its output, which starts with the DOF files it uses.

## Import from VPinball

On its first start, if VPinball's settings are where VPinball keeps them, the
tool asks whether to import them, once (`[config] asked_import`). *Import
from VPinball* on the first page does it at any time. A window shows what
was found and what it becomes, in five groups that can each be left out, and
what was there but cannot be imported; *Apply* puts it into the pages, and
nothing is written until Save.

Where it looks: `~/.local/share/VPinballX/<newest version>/VPinballX.ini`
(or under `$XDG_DATA_HOME`), then `~/.vpinball/VPinballX.ini`;
`%APPDATA%\VPinballX\<version>\` and `%APPDATA%\VPinballX\` on Windows.

Every format VPinball has written is read (`config/vpx_import.c`):

| | VPinball 10.8.1 and newer, `[Input]` | VPinball 10.8.0, `[Player]` |
|---|---|---|
| Keys | `Mapping.LeftFlipper = Key;225 \| ...`, SDL scancodes; empty is VPinball's default | `LFlipKey = 42`, DirectInput codes; empty is the default |
| Buttons | `SDLJoy_<GUID>_1;3`, from 0, on that board | `JoyLFlipKey = 4`, from 1, on the one board |
| Plunger | `Mapping.Plunger0.Position` (or `PlungerPos`): axis, reversed by a negative scale | `PlungerAxis`, `ReversePlungerAxis`, `PlungerSpeedAxis` |
| Nudging | `Mapping.Nudge0.AccX/AccY`, `VelX/VelY` (velocity), `X/Y` (a game controller's stick: velocity or acceleration as the nightlies' `NudgeX1/NudgeY1` still in the file say, else acceleration) or `NudgeX1/NudgeY1`: axes, dead zone, inverted by a negative scale, turned by `NudgeOrientation0` | `LRAxis`, `UDAxis`, `LRAxisFlip`, `UDAxisFlip`, `DeadZone`, `AccelVelocityInput` (velocity) |
| Sound | `Sound3D` 4 or 5 (an exciter setup) turns SSF on; `SoundVolume` scales its volumes | the same |

VPinball's actions become the game's:

| VPinball | The game |
|---|---|
| Left / right flipper | `left_flipper`, `right_flipper` |
| Launch ball (plunger key) | `launch` (Space) |
| Left, right, centre nudge | `nudge_left`, `nudge_right`, `nudge_up` |
| Start | `start` (Enter) |
| Exit game, Escape | `exit` (Esc) |
| UI up, down, left, right | `up`, `down`, `left`, `right` |

A key that is already the action's own is left out; one that is another
action's own key (VPinball launches with Enter, the game's start key) is
shown as taking its place, so it can be left out. A board is found among
those plugged in by its GUID, else by its vendor and product, else the only
joystick or a Pinscape; the plunger and nudging still want calibrating, as
VPinball keeps no rest and pulled values. Not imported, and said so: keys held
together, an axis used as a button, hats, mouse buttons, a second board,
VPinball's gain and its sound device (the game plays to the default one).

## Without the tool

Everything here is in the two files, by hand: [remaster.md](remaster.md)
(the display, `[modes]`, the controller) and
[cabinet-feedback.md](cabinet-feedback.md) (`[keys]`, `[buttons]`,
`[plunger]`, `[nudge]`).

## What was tested

On Linux, with no one there: every page drawn (`--page N --shot FILE.bmp`),
an import of this machine's VPinball 10.8.1 settings, and the self-test's
imports of all three formats. Not yet with a board plugged in, nor on a
cabinet.
