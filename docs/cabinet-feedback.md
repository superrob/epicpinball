# Cabinet feedback and the analog plunger

For a virtual pinball machine (Linux). Two separate things, each off until
you set it up:

- **`--dof`**: the game tells [libdof](https://github.com/vpinball/libdof)
  which table is up and what happens on it. Your DOF files say what that
  fires.
- **`[plunger]`** in `saves/controller.ini`: a plunger position sensor, read
  as a joystick axis, pulls and launches the game's plunger.

Both work on a real cabinet with a Pinscape Pico, on table 1: its outputs
from a Config Tool export, and its plunger. Tables 2 to 13 have only run in a
sandbox ([what was tested](#what-was-tested)).

## No output limits here: set them in your controller

**The game and its helper limit nothing.** No pulse is cut short, no held
output times out, no duty is counted: what your rows ask for is what the
board is sent, and a frozen game or a lost USB write leaves an output as it
was. A coil held on burns out, and can start a fire, so limits belong in the
controller. On a Pinscape Pico, for each solenoid port:

```js
timeLimit: 100,     // ms at full power; omitted or 0 means no limit
powerLimit: 0,      // what it drops to when that runs out, not a maximum: 0 is off
coolingTime: 100,   // ms after switching off before full power again
```

**This matters from the first run.** A DOF folder from the Config Tool fires
outputs even before you write a row for this game: the helper makes one from
the folder's header ([below](#no-row-one-is-made-from-the-header)), and that
row holds a flipper's output on for as long as the flipper is up, as VPinball
tables do.

Suspending output management in the board's console disables these, and they
have not been checked in its Safe Mode. This is from the firmware's reference
(`Firmware/JSONConfigRef.txt` in
[mjrgh/PinscapePico](https://github.com/mjrgh/PinscapePico)) and source, not
from a board: try it with the coil supply off first.

## Setting up DOF

### 1. Build the helper

```sh
dof/build.sh
```

It needs g++, CMake 3.25, autoconf, automake, libtool, patch, curl and
libudev. It downloads libdof and four libraries at pinned commits (about
1.5 MB, checked by SHA-256), patches one libdof bug
([dof/README.md](../dof/README.md)) and builds in `dof/build/`. Nothing is
installed elsewhere. The helper is GPLv3 like libdof and is a program of its
own; the game stays MIT and does not link it.

### 2. Let your user reach the board

libdof talks to the Pico over hidraw, which is root's by default. As
`/etc/udev/rules.d/70-pinscape.rules`:

```
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="1209", ATTRS{idProduct}=="eaea|eaeb", TAG+="uaccess"
```

The file's number has to be below 73. Then:

```sh
sudo udevadm control --reload
```

and plug the board in again.

### 3. Which DOF files are used

1. **VPinball's**, if it is installed and has a `directoutputconfig` folder:

   ```
   ~/.local/share/VPinballX/10.8/directoutputconfig/
   ```

   That is `$XDG_DATA_HOME` in place of `~/.local/share` if set, and the
   newest version folder with a `VPinballX.ini`, even if only an older one
   has DOF files. A `DOFPath` under `[Plugin.DOF]` in that ini wins (a setting
   of newer VPX builds; where it is saved was inferred, not seen). VPX's DOF
   plugin does not need to be switched on.
2. **Otherwise** `saves/directoutputconfig/`.
3. **The game's own rows:** `directoutputconfig*.ini` files in
   `saves/directoutputconfig/` take the place of VPinball's ini files, for
   this game only. Its `Cabinet.xml` and `GlobalConfig_B2SServer.xml` still
   apply. Keep the rows there: a new Config Tool export replaces the files
   it goes into.

The helper prints which it took. It writes nothing into VPinball's folder;
libdof itself writes a log wherever that folder's GlobalConfig asks for one,
as it does under VPX. Without one, the log is `saves/dof/DirectOutput.log`.

### 4. Write rows

Copy [the example](../dof/example/directoutputconfig120.ini) to
`saves/directoutputconfig/` and make it your own, or put the same rows into
VPinball's files.

```ini
[Colors DOF]
White=48,48,48

[Config DOF]
# One column per output, from 1 (an RGB column takes three).
epicpinball,E101 50,E102 50,E103 50,E104 50
epicpinball_ep07,E101 50,E102 50,E103 50 I32,E104 50 I32
```

- **The file's number** is the DOF unit: 120 is Pinscape Pico unit 1, 121 is
  unit 2, and so on.
- **The table's name** is `epicpinball_ep01` to `epicpinball_ep13`. A table
  without a row of its own uses `epicpinball`. With neither, one is made from
  the file's header ([below](#no-row-one-is-made-from-the-header)).
- **The events:**

| Number | `--test` name | When | Signal |
|---|---|---|---|
| E101, E102 | `flipper_left`, `flipper_right` | the flipper starts up (not the key: never while tilted). EP4's and EP12's extra flippers share their side | on while it is up |
| E103, E104 | `sling_left`, `sling_right` | the slingshot kicks, by where the ball touched it. EP8 has none, EP9 only a left one | instant |
| E105, E106, E107 | `bumper_1`, `bumper_2`, `bumper_3` | that bumper kicks: a table's bumpers are numbered from the left ([below](#the-bumpers)) | instant |
| E108 | `launch` | the game launches the ball | instant |
| E109 | `ball_release` | a ball arrives in the shooter lane for a new game or the next ball | instant |
| E110 | `drain` | a ball is lost | instant |
| E111, E112 | `nudge`, `tilt` | each one | instant |
| E113 | `knocker` | an extra ball. The game has no replay or match, and EP1 and EP5 never give one | instant |
| E114 | `ball_ready` | a ball waits in the shooter lane, as the plunger sees it: from its release until it leaves the lane, so a plunge too weak to get out keeps it on. Enigma: while no ball is up | on while it waits |
| E115 | `multiball` | two balls or more rolling: each gone over 24 pixels in the last 5 seconds, so a ball parked in a lock or kickback, or waiting in the lane, does not count. A ball cradled on a flipper longer than that drops out until it moves | on while it lasts |
| E116 | `game_start` | a game begins, with its first ball's release (Enigma: its first launch) | instant |
| E117 | `game_over` | a game ends | instant |
| E118, E119, E120 | `bumper_4`, `bumper_5`, `bumper_6` | as `bumper_1`; only table 5 has more than three | instant |

E105 to E107 meant the left, centre and right of the table until the per-bumper
events came; on a table with three bumpers that is still what they are.

#### The bumpers

Numbered from the left, each on a side (L, C, R) by its rank from the left,
which is where a made row puts it:

| Table | Bumpers | Sides |
|---|---|---|
| 4 | 1 | C |
| 1, 10, 12, 13 | 2 | L R |
| 2, 3, 6, 7, 9 | 3 | L C R |
| 11 | 3 (the first half hidden behind the left wall) | L C R |
| 5 | 6 | L L C C R R |
| 8 (Enigma) | 3, placed while it runs: by where they stand | L C R |

Table 2's bumpers reach under its ramp; what shows of them there counts as the
bumper. No table has a kicking rubber of its own.

An instant event is a 1 and at once a 0, so the row gives it its length:

| Setting | What comes out |
|---|---|
| `E103` | libdof's default minimum, 60 ms |
| `E103 50` | 50 ms; a hit during it starts the 50 ms again |
| `E103 M1` | **nothing**: 1 ms is over before it is sent |
| `E103 50 I32` | 50 ms at two thirds of full |
| `E101 50` | a 50 ms pulse as the flipper starts up |
| `E101` | on for as long as the flipper is up |

### No row: one is made from the header

The Config Tool writes, as the line after `[Config DOF]`, which device is on
each output, the first after `"Rom"` on output 1. An RGB device takes three
outputs, so it is followed by two blank names:

```ini
[Config DOF]
# "Rom","Flipper Left","Flipper Right","Slingshot Left","Slingshot Right","ZB Launch Ball","Exit","RGB Flippers","","","Knocker"
```

When no row for the table and no `epicpinball` row is found, the helper makes
one from that line, for each file that has one, and gives it to libdof in
place of those files, for this game only. It is written to `saves/dof/auto/`
at every table start; nothing is written into the DOF folder. For the header
above:

```ini
epicpinball_ep07,E101,E102,E103,E104,0,ON,ON White,E113/E117
```

| Device | Gets |
|---|---|
| `Flipper Left`, `Flipper Right` | `E101`, `E102`: on while the flipper is up |
| `Slingshot Left`, `Slingshot Right` | `E103`, `E104` |
| `10 Bumper Back Left`, `Center`, `Right` | every bumper of the table on that side: on table 5, `E105/E106` on the left |
| `10 Bumper Middle ...`, `8 Bumper Left`, `Center`, `Right` | the same, only where there is no such back-row device |
| `10 Bumper Middle Right`, `8 Bumper Right` | also `E108`: the launch |
| `Launch Button` | `E114`: lit while a ball waits in the lane |
| `Knocker` | `E113/E117`: an extra ball, and game over |
| `Beacon` | `E113 2000` |
| `Shaker` | `E111 300 I32/E112 600 I32` |
| `Start Button`, `Exit`, `Coin` | `ON` |
| `RGB Flippers` | `ON White` |
| `5 Flasher ...`, `3 Flasher ...`, `Back MX` | colours for events: [flashers](#flashers) |
| everything else | `0` |

- **`ZB Launch Ball` is always 0.** It is not a lamp: it switches a Pinscape's
  plunger into a launch button, which would stop the analog plunger.
- **Ball release has no device,** and drain, tilt, multiball, game start and
  game over reach a made row only through flashers (below): write a row if you
  want them on something else.
- **The table's bumpers come from the game,** which tells the helper their
  sides when it starts it (`--bumpers LLCCRR` for table 5). A side the table
  has no bumper on gets none.
- **Only the header is read.** Intensities and times the Config Tool puts in
  other games' rows are not copied.
- **The helper says** which file it used, how many outputs got something, and
  which events have no device.
- **To switch it off,** write `epicpinball,0` into
  `saves/directoutputconfig/directoutputconfig120.ini` (with a `[Colors DOF]`
  entry). In VPinball's files the next export would erase it. A row for only
  some tables leaves the others made.

#### Flashers

Flashers and an LED strip get colours, as five spots across the back of the
cabinet, left to right:

| Device | Spots |
|---|---|
| `5 Flasher Outside Left`, `Left`, `Center`, `Right`, `Outside Right` | one each |
| `3 Flasher Left`, `Center`, `Right` | the outer two, the middle one, the outer two |
| `Back MX` (an LED strip) | all five, each a fifth of its width |

| Event | Spots | Effect |
|---|---|---|
| `multiball` E115 | all | white, blinking while it lasts |
| a bumper | the spots on its side: left two, centre, right two | yellow, 80 ms |
| `knocker` E113 (extra ball) | all | white, three flashes |
| `game_start` E116 | all | green, three flashes |
| `game_over` E117 | all | blue, three flashes |
| `drain` E110 | centre | red, 300 ms |
| `tilt` E112 | all | red, 600 ms |

Lower in the list draws over higher: a bumper's flash shows through the
multiball blink, and a tilt covers everything. A flasher device's own effects
in other games' rows are not used; `Strobe` stays 0. A device name with
"flasher" in it that is none of these is left off, and the helper says so.

The strip lights the way it does for the Config Tool's own games: in
`Cabinet.xml`, the output of that file's `LedWizEquivalent` names a `LedStrip`
toy on your LED controller, and the made row's effects go to that toy. Tested
on a real cabinet's `Back MX` strip, on table 1. Where the output leads to no
strip, nothing lights.

The names are the Config Tool's, as listed in the Pinscape build guide's
[device appendix](http://mjrnet.org/pinscape/BuildGuideV2/BuildGuide.php?sid=dofDeviceList),
in any case. The `8 Bumper` ones are taken from the guide alone. Of the
flashers, the guide shows `5 Flasher Outside Left`; the other `5 Flasher` and
the `3 Flasher` names follow its pattern and were not seen in an export.
`Back MX` is from a real export.

### 5. Try a row

With the game closed:

```sh
dof/build/epicpinball-dof --table ep5 --bumpers LLCCRR --test bumper_4
```

It sends that one event (a flipper or another level is held half a second),
waits three seconds and switches off. `--bumpers` is the table's sides from the
table [above](#the-bumpers); without it a made row assumes three, `LCR`.

### 6. Play

```sh
./epicpinball.sh --cabinet --dof
```

`--dof` also works in the ordinary window, and headless with `--realtime`.
On Windows it says that it is not supported yet.

Events from before libdof is up are dropped, not sent late. A whole Config
Tool export (4 MB) took it one to two seconds here; a ball arrives about
two and a half seconds into a table.

The demo and the menus send no events, though `ON` outputs (button lamps in
a made row) stay lit whenever a table is up, the demo's included. A pause, a
prompt and the settings dialog let go of held flippers. `--feedback-log FILE` lists every event with
its time, marked `felt` or `muted`, and needs neither a board nor the helper.

## What to watch for in DOF

- **libdof looks for boards itself.** Opening it already asks any Pinscape
  for its identity and sets its clock, and with auto-config (no Cabinet.xml,
  or one that does not switch it off) it probes every controller type it
  knows.
- **`Cabinet.xml` needs a GlobalConfig.** libdof reads it only when
  `GlobalConfig_B2SServer.xml` in the same folder names it:

  ```xml
  <GlobalConfig>
    <CabinetConfigFilePattern>{GlobalConfigDir}/Cabinet.xml</CabinetConfigFilePattern>
  </GlobalConfig>
  ```

- **`IniFilesPath`** in a GlobalConfig beats the game's own rows.
- **A table's own row in any ini file** switches off the `epicpinball` row in
  all the others, on that table. Give the table a row in each.
- **Only `#` starts a comment**, and a file without a `[Colors DOF]` entry
  is ignored whole.
- **With the game's own rows**, a GlobalConfig's `TableConfigFilePatterns`
  and `{TableName}` see a table file named `epicpinball_epNN`. If such a
  pattern matches a file, libdof 0.5.4 crashes (it reads the table file in
  that file's place): keep table XMLs out of `saves/directoutputconfig/`.
- **A Config Tool export has settings libdof cannot read**, in other games'
  rows. It skips them, as under VPinball, and the helper only counts them.
- **libdof 0.5.4, which the helper pins**, ignores `Cabinet.xml` toys other
  than `LedWizEquivalent` and `LedStrip` (a shaker works as a plain output
  with an intensity), and its schedules and table overrides do not act on a
  Pico. A Cabinet.xml it cannot parse can crash it; the game goes on.
- **VPX bundles its own libdof** (0.4.7 in builds of 2026), so the same files
  can behave a little differently there.

## What still switches outputs off

These are the ends of things, not limits.

| What happens | What switches off |
|---|---|
| the table ends, the game quits | libdof, finishing |
| the game crashes or is killed | the helper sees the pipe close and finishes libdof |
| pause, a prompt, the settings dialog | held flippers are let go; effects already running play out |
| the helper dies, or hangs starting or stopping | it is killed, `--all-off` runs once, that table goes on without feedback |
| libdof hangs and the game is gone | the helper restarts itself as `--all-off` after 10 to 20 seconds |
| the game freezes | **nothing: your controller's limits** |
| the helper freezes in play | **nothing until the table ends** or the pipe to it fills: your controller's limits |
| a USB write fails or never returns | **nothing: your controller's limits** |
| the computer or the cable fails | **nothing: your controller's limits** |

`--all-off` is a fresh process that connects with the same cabinet and no
table, so no row can fire, and finishes, which switches a Pico's ports off.

## The plunger

```sh
./epicpinball.sh --joysticks
```

lists the joysticks and prints their axes as they move. Pull the plunger and
note the axis and its two ends. Then, in `saves/controller.ini` (a file from
before this feature has no such section: add it at the end):

```ini
[plunger]
device = Pinscape Pico       ; the name or GUID from that list
axis = 2                     ; the number from that list, or X Y Z RX RY RZ
rest = 0
pulled = 32767               ; swap the sign if yours reads backwards
speed_axis = none            ; optional
```

Pico configurations commonly use Z. The names assume a board with all six
axes; the number is always right. The file is read again when the window
comes back to the front.

- **It follows** while a ball rests in the lane. The drawn plunger moves a
  pixel a frame, as the keyboard moves it, so it can trail your hand by up
  to a third of a second.
- **Let it go** and the ball launches at the strength you pulled to, rounded
  to one the keyboard can produce, however far the drawing had got. Letting
  go is a fall from nine tenths of the pull to rest within 90 ms, judged on
  the last 150 ms. Easing it home does nothing, and neither does a pull under
  15%. With `speed_axis` set, a reading of a quarter of that axis's range
  also counts as letting go (not tried with a real board's units).
- **Enigma** (EP8) has one strength: a release from 25% or more fires it.
- **Nothing fires** on unplugging, losing focus, pausing, the settings
  dialog or the table ending. After any of those, and at the start, the
  plunger counts again once it has been seen at rest.
- **Space and Ctrl** still work.
- A cabinet board's axes (and the plunger device's) no longer act as a pad's
  triggers and stick. Its buttons still press what `controller.ini` says.
  A centred axis used to read as both triggers pulled, holding both flippers.

This is the one feature that writes the game's memory: the pull value and
the Ctrl-held flag, only with a plunger configured, and only on a table whose
file matches its map.

## The maps

Both features find their way around the tables with `work/feedback/epN.map`,
made from your own copy and lifted code by `tools/feedback_map.py`.
`build-linux.sh` and `setup.sh` run it. A table whose map is missing, or of
another file, plays normally without either feature, and says so. The
signatures are proven on GOG 1.01 only.

## Troubleshooting

| Message or symptom | What to do |
|---|---|
| `--dof needs dof/build/epicpinball-dof` | run `dof/build.sh` |
| `no epicpinball row in ..., and no header line` | write rows there, or in `saves/directoutputconfig/` |
| `no epicpinball_epNN and no epicpinball row` | that table has no row |
| `VPinball has no .../directoutputconfig` | put the Config Tool's export there, or ignore it: `saves/directoutputconfig/` is used |
| `Error searching for legacy PinOne COM port` | harmless without a PinOne: libdof could not list serial ports while looking for one |
| `no Pico answers` | the udev rule, the cable, the board's unit number |
| a row does nothing | the file's number, `M1`, the traps [above](#what-to-watch-for-in-dof), `saves/dof/DirectOutput.log` |
| `made one from the header of ...` | no row was found, so one was made ([how](#no-row-one-is-made-from-the-header)); write one to change it |
| `the headers in ... name no device` | the header has no device this game has an event for |
| `libdof does not see the row it was given` | an `IniFilesPath` in the GlobalConfig: write a row there |
| `the helper stopped (signal 11)` | a Cabinet.xml libdof cannot read, or a file matching `TableConfigFilePatterns` |
| `the helper stopped`, `did not start` | feedback is off until the table is started again; the lines before say why |
| `no work/feedback/epN.map`, `... of another copy` | `python3 tools/feedback_map.py` |
| `the table's stack has left its bounds` | a fault in the game, seen on African Safari: feedback and the plunger stop for that table until it is started again |
| no joysticks listed | your user must be able to read `/dev/input` |
| the plunger is ignored | let it rest first; check `rest` and `pulled` with `--joysticks` |

## What was tested

On a real cabinet with a Pinscape Pico, on table 1: the outputs, from a
Config Tool export with no Epic Pinball row (so from the row made from its
header), the colours on its `Back MX` LED strip, and the analog plunger.

Without hardware, in a sandbox with no devices:

- events against the game's own instructions on all 13 tables, over 100,000
  of them. Knockers were never seen on EP3, 4, 6, 10, 11, 12 and 13 (the
  code that awards them is there; no scripted game earned one), and one
  kicker on EP5 and one on EP13 were never hit;
- the helper on ArtNet over loopback and against a simulated Pico's HID
  interface: file lookup, rows, effects, holds, kills, hangs;
- flashers in made rows: five RGB flashers and a 20-LED `Back MX` strip
  through the helper for every flasher event, and five flashers from the game
  on tables 1 and 5 (multiball and the extra ball did not come up there). On
  the real cabinet, the `Back MX` strip on table 1; no flasher devices;
- the plunger by scripted sensor (`--plunger-trace "ms:percent,..."`) on all
  13 tables, and by a virtual joystick through SDL. Its launch strengths are
  the keyboard's and so are the speeds the ball leaves with. A ball's later
  path still differs with the millisecond of release, as it does from the
  keyboard;
- 70 frames a second in cabinet mode with the helper, a hung helper, and a
  sensor present and absent.

Not tested: tables 2 to 13 on a cabinet, other controllers and other
cabinets' Config Tool headers, Flatpak and portable VPX, Windows (its halves
were only syntax-checked), other releases of the game.
