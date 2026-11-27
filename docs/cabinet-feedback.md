# Cabinet feedback, the analog plunger and nudging

For a virtual pinball machine (Linux). Three separate things, each off until
you set it up:

- **`--dof`**: the game tells [libdof](https://github.com/vpinball/libdof)
  which table is up and what happens on it. Your DOF files say what that
  fires.
- **`[plunger]`** in `saves/controller.ini`: a plunger position sensor, read
  as a joystick axis, pulls and launches the game's plunger.
- **`[nudge]`** in `saves/controller.ini`: an accelerometer's shove pushes
  the ball by how hard, and nudge buttons are the game's own nudge
  ([nudging](#nudging)).

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
epicpinball,$FlipperLeft 50,$FlipperRight 50,$SlingshotLeft 50,$SlingshotRight 50
epicpinball_ep07,$FlipperLeft 50,$FlipperRight 50,$SlingshotLeft 50 I32,$SlingshotRight 50 I32
```

- **The file's number** is the DOF unit: 120 is Pinscape Pico unit 1, 121 is
  unit 2, and so on.
- **The table's name** is `epicpinball_ep01` to `epicpinball_ep13`. A table
  without a row of its own uses `epicpinball`. With neither, one is made from
  the file's header ([below](#no-row-one-is-made-from-the-header)).
- **The events** have names, as a table without a ROM names its own: a row
  writes them with a `$`, `$FlipperLeft`, in any case. `--test` takes the same
  name without the `$`. These are the same on every table:

| Event | When | Signal |
|---|---|---|
| `FlipperLeft`, `FlipperRight` | the flipper starts up (not the key: never while tilted). EP4's and EP12's extra flippers share their side | on while it is up |
| `SlingshotLeft`, `SlingshotRight` | the slingshot kicks, by where the ball touched it. EP8 has none, EP9 only a left one | instant |
| `Bumper1` … `Bumper6` | that bumper kicks: a table's bumpers are numbered from the left ([below](#the-bumpers)); only table 5 has more than three | instant |
| `Launch` | the game launches the ball | instant |
| `BallRelease` | a ball arrives in the shooter lane for a new game or the next ball | instant |
| `Drain` | a ball is lost | instant |
| `Nudge`, `Tilt` | each one | instant |
| `ExtraBall` | an extra ball, for the knocker. The game has no replay or match, and EP1 and EP5 never give one | instant |
| `BallReady` | a ball waits in the shooter lane, as the plunger sees it: from its release until it leaves the lane, so a plunge too weak to get out keeps it on. Enigma: while no ball is up | on while it waits |
| `Multiball` | two balls or more rolling: each gone over 24 pixels in the last 5 seconds, so a ball parked in a lock or kickback, or waiting in the lane, does not count. A ball cradled on a flipper longer than that drops out until it moves | on while it lasts |
| `GameStart` | a game begins, with its first ball's release (Enigma: its first launch) | instant |
| `GameOver` | a game ends | instant |

On a table with features of its own, also:

| Event | When | Signal |
|---|---|---|
| `BallHeld` | a hole holds a ball | on while it does |
| `LanesLit` | a set of lanes complete, lit by the ball | instant |
| `SkillShot` | a skill shot | instant |
| `Jackpot` | a jackpot | instant |
| `SuperJackpot` | a bigger one: double, super | instant |
| `Award` | a mode or progress award the scoreboard announces | instant |

A table's own holes, kickback, drop targets, rollovers and ramps are named by
the table: `t`, its number, and the feature's name, so `$t1PowerUpHole` is
table 1's power-up hole and nothing on any other table. Every table has them
([below](#table-1-super-android)); tables 3 and 5 show no texts of their own,
so they have no `Jackpot`, `SkillShot` or `Award`.

| Feature | When | Signal |
|---|---|---|
| a hole | it kicks its ball out (also when multiball takes it) | instant |
| a kickback | it fires | instant |
| a drop target | it goes down | instant |
| a bank's targets all down (its name and `Down`) | the bank's last one goes down and the bank resets | instant |
| a rollover | the ball rolls over it, every pass | instant |
| a ramp | made, as the game counts it (or, on a table that counts none, its switch) | instant |
| another switch, a lane, a loop, a lock | the ball rolls over it, or the scoreboard says it happened | instant |

#### Table 1 (Super Android)

| Event | Is |
|---|---|
| `t1TestHole` | the test hole, top left |
| `t1LeftHole` | the left hole (android millions), on the left side |
| `t1PowerUpHole` | the power-up hole, centre |
| `t1PhysicalSystemsHole` | the physical-systems hole, right |
| `t1RightHole` | the right hole, at the end of the centre ramp |
| `t1Kickback` | the left outlane |
| `t1DropTarget1` … `t1DropTarget3` | the drop targets 1, 2, 3 on the left; all three down is `t1DropTargetsDown` |
| `t1Star` | the star rollover, left of centre |
| `t1LeftLane` | the left lane (its jackpot) |
| `t1TopLane1` … `t1TopLane4` | the four top lanes, left to right; all four lit is `LanesLit` |
| `t1LeftRamp`, `t1CentreRamp`, `t1RightRamp` | the ramps. The centre one also kicks out of `t1RightHole` |
| `Jackpot` | JACKPOT |
| `SuperJackpot` | DOUBLE JACKPOT, SUPER JACKPOT |
| `Award` | POWER UP, FULL POWER, I.Q. UPGRADE, BUMPER VALUE, COMPUTER LINK, the physical systems, BASIC IO / AI / AI2 / DATABASE INSTALLED, ANDROID IS ACTIVATED, VIRUS FLUSHED OUT, ANDROID MILLIONS, each arm, leg, head and torso, and each LEVEL 1 test |

They come from the game's own code: the switch handler it calls with the colour
of the switch under the ball, what that code keeps (a hole's hold timer, the
targets' flags, the lane lights, the ramp count) and the scoreboard messages.
Holes, ramps, the left lane, the drop target bank and the lanes add up to the
game's own counts (its bonus screen's TESTS, POWER UPS, PHYS. SYS. HOLES,
RAMPS and LEFT LANES among them) over four hours of its demo. What the handler
turns back is not sent either: tilted it handles only a few colours, and with
the ball on a ramp's wire, over the top lanes, only the ramps' own.

#### Table 2 (Pot of Gold)

| Event | Is |
|---|---|
| `t2TreasureHole` | the treasure hole, centre, under the bumpers |
| `t2Cave` | the cave, far left: where the balls are locked |
| `t2SkyRamp` | the crossing ramps made, as the scoreboard says SKY RAMP |
| `t2LeftCloverLane`, `t2RightCloverLane` | the lanes down each side (the 4 leaf clover) |
| `t2CaveLane` | the lane into the cave |
| `t2TopLeprechaun`, `t2RightLeprechaun` | the leprechauns' switches, at the top left and on the right |
| `t2RainbowRight` | the rainbow lane's right end |
| `t2RainbowLane` | both ends of the rainbow lane made: RAINBOW LANE |
| `t2BallLock` | BALL 1 LOCKED, BALL 2 LOCKED |
| `t2LeprechaunCaught` | YOU CAUGHT A LEPRECHAUN! |
| `t2RabbitFoot` | FOUND RABBIT FOOT, RABBIT FOOT FOUND IN CAVE |
| `SkillShot`, `Jackpot` | SKILL SHOT, JACKPOT!!! |
| `LanesLit` | the four top lanes lit |
| `Award` | LEPRECHAUN BONUS, LUCKY RABBIT FOOT, TREASURE - 300K, 4 LEAF CLOVER BONUS, LEPRECHAUNS - SHOOT TREASURE |

Four hours of `--autoplay` (111 games) fired every one of them but
`Jackpot`: the locks came, multiball did not.

#### Table 3 (Excalibur)

It shows no texts of its own, so it has no `Jackpot`, `SkillShot` or
`Award`.

| Event | Is |
|---|---|
| `t3BlackHole` | the black hole, at the head of the right lane |
| `t3LaneS`, `t3LaneW`, `t3LaneO`, `t3LaneR`, `t3LaneD` | SWORD, the top lanes; all five lit is `LanesLit` |
| `t3LeftLoop`, `t3RightLoop` | the lanes by the axe and the spear; both made advance the multiplier |
| `t3LeftLane`, `t3RightLane` | the lanes that light the kickbacks |
| `t3LeftOutlane`, `t3RightOutlane` | the outlanes, which kick a ball back when lit |
| `t3LeftOrbit`, `t3RightOrbit` | the top corners, by the shields |
| `t3LeftTarget`, `t3RightTarget` | the red capsule, left; the blue one by XTRA BALL, right |
| `t3Lane1k`, `t3Lane5k`, `t3Lane10k` | the switches down the right U lane |
| `t3Spinner` | the spinner, left, under the top bumper |

Four hours of `--autoplay` (154 games) fired every one of them.

#### Table 4 (Crash and Burn)

| Event | Is |
|---|---|
| `t4LeftScoop`, `t4TopScoop` | the scoops left and top right, toward the extra ball |
| `t4PitStop` | the pit, centre |
| `t4DeadManCurve` | the hole at the foot of Dead Man Curve, right: the ball lock |
| `t4BlowOut` | the tyre's hub, top |
| `t4RaceTrack` | ONTO THE RACE TRACK: the race track ramp |
| `t4DriverD` … `t4DriverR2` | DRIVER, the targets down the right, top to bottom |
| `t4LeftOrbit`, `t4RightOrbit` | the orbits |
| `t4LeftOutlane`, `t4RightOutlane` | the outlanes |
| `t4LeftGreenLane`, `t4RightGreenLane` | the lanes by the slingshots: GO - GREEN LIGHT |
| `t4BallLock` | BALL 1 LOCKED |
| `Jackpot`, `SuperJackpot` | JACKPOT!!, DOUBLE JACKPOT |
| `Award` | ALL GREEN LIGHTS - 5M, MPH MULTIPLIER INCREASE, REFUELED-15M, NEW TIRES-10M, TUNE UP-5M, MYSTERY BONUS, PIT STOP |

Four hours of `--autoplay` (139 games) fired every one of them but the
jackpots.

#### Table 5 (Magic)

It shows no texts of its own, so it has no `Jackpot`, `SkillShot` or
`Award`. Its holes and lanes are drawn into its collision map as it runs.

| Event | Is |
|---|---|
| `t5TopHole` | the hole under the top arch |
| `t5LaneM`, `t5LaneA`, `t5LaneG`, `t5LaneI`, `t5LaneC` | MAGIC, the top lanes |
| `t5LeftLane` | the lane on the left: 50, 75, 100 |
| `t5CentreTarget` | the red target under the top hat |
| `t5YellowBumpers` | the target that lights the yellow bumpers, left |
| `t5LeftOutlane`, `t5RightOutlane` | the outlanes |
| `t5RightOrbit` | the orbit by the scarf, top right |

Four hours of `--autoplay` (153 games) fired every one of them.

#### Table 6 (Jungle Pinball)

| Event | Is |
|---|---|
| `t6StarHole` | the hole by the star, top left |
| `t6SwordHole` | the hole by the swords, top right |
| `t6NestHole` | the nest, right: FROG and ANT millions |
| `t6LeftRamp`, `t6RightRamp` | the vine ramps |
| `t6TopLane1` … `t6TopLane4` | the top lanes, left to right |
| `t6SerpentPath` | the serpent's path, top left |
| `t6SwordTarget1` … `t6SwordTarget3` | the targets under the nest, I I I |
| `t6BlueTarget1` … `t6BlueTarget4` | the blue targets down the right, top first |
| `t6LeftTarget1`, `t6LeftTarget2` | the targets on the left (the death blade hole) |
| `t6LeftOutlane`, `t6RightOutlane`, `t6LeftInlane`, `t6RightInlane` | the lanes by the flippers |
| `t6AppleFalls` | AN APPLE FALLS |
| `Jackpot` | JACKPOT!!! |
| `Award` | CONSECUTIVE TARGETS- 5MIL, FROG MILLIONS, ANT MILLIONS, BONUS WILL BE DOUBLED |

Its targets are switches here, each hit: three banks of them would each need
the game's flags watched. Four hours of `--autoplay` (169 games) fired every
one of them but `Jackpot`.

#### Table 7 (Deep Sea)

| Event | Is |
|---|---|
| `t7EelNest`, `t7SunkenShip` | the holes left, by the eel, and right, the wreck |
| `t7ReefHole` | the hole in the reef, centre |
| `t7AirTanks` | the hole on the right that fills the air tanks |
| `t7LeftRamp`, `t7RightRamp` | the ramps made: 1 OF 3 RAMPS ... |
| `t7Seahorse1` … `t7Seahorse4` | the top lanes, left to right: the skill shot |
| `t7Stingray` | the stingray, top left |
| `t7LeftInlane`, `t7RightInlane`, `t7LeftOutlane`, `t7RightOutlane` | the lanes by the flippers |
| `t7TopLoop`, `t7RightOrbit` | the loop at the top, the orbit on the right |
| `t7TreasureTarget1` … `t7TreasureTarget4` | 1M ADDED TO TREASURE, left first |
| `t7DiverEnters` | ANOTHER DIVER ENTERS |
| `SkillShot`, `Jackpot` | SKILL SHOT, JACKPOT! |
| `Award` | ALL RAMPS COMPLETE, SUNKEN SHIP GOLD FOUND, SECRET TREASURE, STINGRAY, MYSTERY HOLE FOUND, REFILL OF AIR, SHARK LEAVES |

Four hours of `--autoplay` (120 games) fired every one of them but
`Jackpot`.

#### Table 8 (Enigma)

Each level draws switches of its own as it runs, so most of Enigma's events
are its scoreboard's.

| Event | Is |
|---|---|
| `t8LockHole` | the hole that locks the ball: BALL LOCKED is `t8BallLock` |
| `t8SpikedPit`, `t8Spring` | the spiked pit and the spring, as their levels draw them |
| `t8AlphaRamp`, `t8BetaRamp` | the wire loops on the left, as the scoreboard names them |
| `t8DeltaRamp`, `t8GammaRamp` | the loops on the right |
| `t8Mine` | DESTROYED BY A MINE |
| `t8EnigmaComes` | THE ENIGMA HAS COME |
| `SuperJackpot` | SUPER JACKPOT AWARDED |
| `Award` | LEVEL 1, 2, 3 COMPLETE, MYSTIC SCROLL, GOLD BEAR, LOLLY POP, SPRING, TEDDY WAS RUN OVER, BRAINS!!, A HAND FOR 12M |

Eight hours of `--autoplay` (224 games) fired every one of them but
`t8Spring`, `t8EnigmaComes` and `SuperJackpot`, from levels it did not
reach.

#### Table 9 (Cyborgirl)

| Event | Is |
|---|---|
| `t9TenseDeck` | the hole bottom left |
| `t9CpuHog` | the hole on the right |
| `t9LaneI`, `t9LaneC`, `t9LaneE` | ICE, the top lanes |
| `t9Reboot` | sync and reboot, right |
| `t9CentreLane`, `t9RightLane` | the lanes under the top lanes |
| `t9IslandLeft`, `t9IslandRight` | the switches under the bumpers' island |
| `t9LeftUpperTarget`, `t9LeftLowerTarget`, `t9RightUpperTarget`, `t9RightLowerTarget` | the side targets: MULTIPLIER ADVANCE, KERNEL PANIC! |
| `t9LeftInlane`, `t9RightInlane` | the inlanes |
| `t9IceBreaker` | the ice breaker, left |
| `t9ImplantLane` | the lanes into the cybernetic implants, top left |
| `t9Implant` | CYBERNETIC IMPLANT |
| `t9KernelPanic` | KERNEL PANIC! |
| `Award` | SUPER IMPLANT, MULTIPLIER ADVANCE, CPU MAXED OUT, SUPER TENSE DECK |

Four hours of `--autoplay` (129 games) fired every one of them but
`t9IceBreaker` and `t9KernelPanic`.

#### Table 10 (Pangaea)

| Event | Is |
|---|---|
| `t10Cavern` | the cavern, top right: the ball lock |
| `t10Stegosaurus`, `t10Allosaurus` | the holes left and right |
| `t10LeftSaucer`, `t10JackpotHole` | the holes top left and centre |
| `t10LeftTargets`, `t10RightTargets` | the columns of lights down each side |
| `t10LeftOrbit`, `t10RightOrbit` | the orbits |
| `t10Bullseye` | the bullseye, right |
| `t10LeftOutlane`, `t10RightOutlane` | the outlanes |
| `t10LeftLoop`, `t10RightLoop` | the volcano loop's ends |
| `t10VolcanoLoop` | VOLCANO LOOP 2M |
| `t10BallLock` | BALL LOCKED |
| `t10Egg` | PTERODACTYL EGG, T-REX EGG |
| `SkillShot`, `Jackpot` | SKILL SHOT 2M, JACKPOT  JACKPOT |
| `Award` | MILLION CAVERN, STEGOSAURUS, ALLOSAURUS, EARTHQUAKE, T-REX ATTACK, EGG NEST, VOLCANO ERUPTION, BULLSEYE MILLIONS, MULTIPLIER ADVANCE, COLLECT T-REX BONUS |

Four hours of `--autoplay` (172 games) fired every one of them but
`SkillShot` and `Jackpot`.

#### Table 11 (Space Journey)

| Event | Is |
|---|---|
| `t11LockHole` | the lock, centre right: BALL LOCKED is `t11BallLock` |
| `t11ExtraBallHole` | the hole on the left, by Mars |
| `t11Abyss` | INTO THE ABYSS, bottom left |
| `t11AsteroidRamp`, `t11Hyperspace` | the ramps, as the scoreboard names them |
| `t11LaneS`, `t11LaneT`, `t11LaneA`, `t11LaneR` | STAR, the lanes under Pluto |
| `t11SpaceStation` | SPACE STATION FLYBY |
| `t11RightOrbit` | the lane top right |
| `t11LeftOutlane`, `t11RightOutlane` | the outlanes |
| `t11LeftTarget`, `t11CentreTarget`, `t11RightTarget` | the targets that light the lock, the comet and the asteroid |
| `Jackpot`, `SuperJackpot` | JACKPOT  JACKPOT, SUPER JACKPOT! 50MIL |
| `Award` | 10 MILLION, PLANET ADVANCED, MULTIPLIER ADVANCE, 2 MILLION AND ADVANCE, 1 MILLION! |

Four hours of `--autoplay` (163 games) fired every one of them but the
jackpots.

#### Table 12 (Toy Factory)

| Event | Is |
|---|---|
| `t12JunkHeap` | INTO THE JUNK HEAP, left |
| `t12LockHole` | the lock, centre: BALL LOCKED is `t12BallLock` |
| `t12Superball` | the hole on the right: SUPERBALL |
| `t12Joystick` | the joystick, centre: the extra ball |
| `t12SkateboardRamp` | SKATEBOARD RAMP |
| `t12TopLane1` … `t12TopLane4` | the top lanes, left to right |
| `t12ExpressLeft`, `t12ExpressRight` | the toy express loop's ends |
| `t12ToyExpress` | TOY EXPRESS-2M: the loop made |
| `t12ToyChest` | the toy chest's targets, left |
| `t12DisplayCase` | HIT THE DISPLAY CASE!, left |
| `t12PaintShop` | the paint shop |
| `t12LeftOrbit` | the orbit top left |
| `t12LeftOutlane`, `t12RightOutlane` | the outlanes |
| `Jackpot`, `SuperJackpot` | JACKPOT  JACKPOT, SUPER JACKPOT! 50MIL |
| `Award` | SKATEBOARD MILLIONS, MULTIPLIER ADVANCE, SUPERBALL |

Four hours of `--autoplay` (163 games) fired every one of them but the
jackpots.

#### Table 13 (African Safari)

| Event | Is |
|---|---|
| `t13WaterHole`, `t13BaseCamp` | the water hole, right, and the base camp, left |
| `t13LockHole` | the lock by MULTI BALL: BALL LOCKED is `t13BallLock` |
| `t13LionsDen` | the lion's den |
| `t13LaneB`, `t13LaneI`, `t13LaneR`, `t13LaneD` | BIRD, the top lanes |
| `t13MountainLeft`, `t13MountainRight` | the mountain path's ends |
| `t13MountainPath` | MOUNTAIN PATH-2M: the path made |
| `t13TargetC`, `t13TargetA`, `t13TargetT` | CAT, on the left |
| `t13SnakePit` | the snake pit |
| `t13LostCaves` | the lost caves, right |
| `t13BaseCampLane` | GO FOR THE BASE CAMP, the lane on the right |
| `t13LeftOutlane`, `t13RightOutlane` | the outlanes |
| `Jackpot`, `SuperJackpot` | JACKPOT  JACKPOT, SUPER JACKPOT! 50MIL |
| `Award` | WATER HOLE 1M, SNAKE PIT MILLIONS, LION`S MILLIONS, MULTIPLIER ADVANCE |

Four hours of `--autoplay` (172 games) fired every one of them but
`t13BallLock` and the jackpots: the lock hole fired 71 times, but it never
said BALL LOCKED.

The names are in `FEATURES` in `tools/feedback_map.py`, which writes them into
the table's map; the game passes them to the helper with `--features`.

#### Finding a table's features

Every table has one switch handler: the ball scan hands it the collision map's
colour under the ball, and it branches to the table's own code for that
colour, through a jump table. `python3 tools/feedback_map.py --switches epN`
prints each colour that has code of its own: where it is drawn, and that code,
with the scoreboard texts it shows. A colour's code often tells places apart by
the ball's X and Y. Then a long run of play shows where the ball meets each:

```sh
./epicpinball.sh --headless --mute --table 2 --autoplay --seconds 14400 \
  --feedback-log events.log --switch-log switches.log
```

`--autoplay` flips at a ball coming down near the flippers, launches a ball
waiting in the lane, nudges a ball that has not moved for ten seconds, and
after a game over types initials and plays again: four hours of a table in a
couple of minutes. A feature is then a zone (a colour,
where the ball is), a hole's hold timer, or a scoreboard text: the game's own
"BALL 1 LOCKED" or "SKY RAMP" fires a named feature with no switch of its
own.

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
| `$SlingshotLeft` | libdof's default minimum, 60 ms |
| `$SlingshotLeft 50` | 50 ms; a hit during it starts the 50 ms again |
| `$SlingshotLeft M1` | **nothing**: 1 ms is over before it is sent |
| `$SlingshotLeft 50 I32` | 50 ms at two thirds of full |
| `$FlipperLeft 50` | a 50 ms pulse as the flipper starts up |
| `$FlipperLeft` | on for as long as the flipper is up |

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
epicpinball_ep07,$FlipperLeft,$FlipperRight,$SlingshotLeft,$SlingshotRight,0,ON,ON EpMain,$ExtraBall
```

| Device | Gets |
|---|---|
| `Flipper Left`, `Flipper Right` | `$FlipperLeft`, `$FlipperRight`: on while the flipper is up |
| `Slingshot Left`, `Slingshot Right` | `$SlingshotLeft`, `$SlingshotRight` |
| `10 Bumper Back Left`, `Center`, `Right` | every bumper of the table on that side: on table 5, `$Bumper1/$Bumper2` on the left |
| `10 Bumper Middle ...`, `8 Bumper Left`, `Center`, `Right` | the same, only where there is no such back-row device |
| `10 Bumper Middle Right`, `8 Bumper Right` | also `$Launch` |
| `Launch Button` | `$BallReady`: lit while a ball waits in the lane |
| `Knocker` | `$ExtraBall` (not game over) |
| `10 Bumper Middle Left`, `Center`, `Right` | also the table's own solenoids on that side: hole kick-outs, the kickback, the drop target bank resetting. On table 1, Middle Left gets `$t1TestHole/$t1LeftHole/$t1Kickback/$t1DropTargetsDown`, Center `$t1PowerUpHole`, Right `$Launch/$t1PhysicalSystemsHole/$t1RightHole` |
| `8 Bumper Left`, `Center`, `Right` | the same, where there is no `10 Bumper Middle` device on that side |
| `Beacon` | `$ExtraBall 2000`; on a table with features of its own, jackpots too: `$ExtraBall 2000/$Jackpot 2000/$SuperJackpot 2000` |
| `Shaker` | `$Nudge 300 I32/$Tilt 600 I32` |
| `Start Button`, `Exit`, `Coin` | `ON` |
| `RGB Flippers` | `ON EpMain` |
| `5 Flasher ...`, `3 Flasher ...`, `Back MX` | colours for events: [flashers](#flashers) |
| `Left MX`, `Right MX` | slingshots, ramps, kick-outs and the kickback on their side; ball ready breathing at the bottom of the right one: [the side MXs](#the-side-mxs) |
| everything else | `0` |

- **`ZB Launch Ball` is always 0.** It is not a lamp: it switches a Pinscape's
  plunger into a launch button, which would stop the analog plunger.
- **Ball release has no device,** and drain, tilt, multiball, game start and
  game over reach a made row only through flashers (below): write a row if you
  want them on something else.
- **Not in made rows:** single drop targets, rollovers and `BallHeld`. They
  are there for your own rows.
- **The table's bumpers come from the game,** which tells the helper their
  sides when it starts it (`--bumpers LLCCRR` for table 5). A side the table
  has no bumper on gets none. So do its own features' names and sides, by
  where each is drawn in thirds of the table (`--features "hole1=TestHole:L
  hole2=LeftHole:L ... bank=DropTargets"` for table 1).
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
| `Back MX` (an LED strip) | all five: the Config Tool's flasher circles, or a fifth of its width each ([below](#shapes-on-the-back-mx)) |

| Event | Spots | Effect |
|---|---|---|
| `Multiball` | all | *glow*, blinking while it lasts |
| a bumper | the spots on its side: left two, centre, right two | *main*, 80 ms |
| a hole kicking out, the kickback | the spots on its side | *feature*, 120 ms |
| a ramp made | the spots on its side | *ramp*, 150 ms |
| `SkillShot` | all | *ramp*, three flashes |
| `Award` | all | *feature*, three flashes |
| `LanesLit` | all | *glow*, three flashes |
| `Jackpot` | all | *jackpot*, three flashes |
| `SuperJackpot` | all | *jackpot*, six flashes |
| `ExtraBall` | all | *glow*, three flashes |
| `GameStart` | all | *start*, three flashes |
| `GameOver` | all | *over*, three flashes |
| `Drain` | centre | *danger*, 300 ms |
| `Tilt` | all | *danger*, 600 ms |

#### Colours

A made row names its colours by what they are for, `EpGlow` to `EpDanger`
in its `[Colors DOF]`, and each table's file gives them that table's own
colours, from its playfield art, bright enough for LEDs. Danger stays red on
every table, so a drain and a tilt read as a warning. A name not of the game
(`saves/dof/auto/` with no table, a test) gets plain colours: white, yellow,
orange, cyan, magenta, green, blue, red.

| Table | *glow* | *main* | *feature* | *ramp* | *jackpot* | *start* | *over* |
|---|---|---|---|---|---|---|---|
| 1 Super Android | `#FFD27A` | `#FF9A00` | `#FF4000` | `#2848FF` | `#6A3CFF` | `#2848FF` | `#FF9A00` |
| 2 Pot of Gold | `#FFE680` | `#FFC000` | `#FF5A00` | `#1E7BFF` | `#00E040` | `#00E040` | `#1E7BFF` |
| 3 Excalibur | `#FFE0A0` | `#FFA030` | `#FF6A10` | `#B8C8FF` | `#9A30FF` | `#FFA030` | `#9A30FF` |
| 4 Crash and Burn | `#FFFFFF` | `#FF6A00` | `#FFD000` | `#1048FF` | `#FF0040` | `#FFFFFF` | `#1048FF` |
| 5 Magic | `#FFF0D0` | `#FFB830` | `#FF1E1E` | `#2040FF` | `#20C040` | `#FFB830` | `#2040FF` |
| 6 Jungle Pinball | `#C0FF80` | `#40E000` | `#FF2000` | `#2060FF` | `#FFC000` | `#40E000` | `#FF6000` |
| 7 Deep Sea | `#A0F0FF` | `#00C8FF` | `#FF8040` | `#0040FF` | `#00FFB0` | `#00C8FF` | `#0040FF` |
| 8 Enigma | `#FFC8A0` | `#FF4A10` | `#FF8A30` | `#FFC060` | `#FF2000` | `#FF8A30` | `#B01800` |
| 9 Cyborgirl | `#E0E8FF` | `#FF3A20` | `#FFB040` | `#40A0FF` | `#FF4080` | `#40A0FF` | `#FF3A20` |
| 10 Pangaea | `#FFC080` | `#FF6A00` | `#20E020` | `#6020FF` | `#FF1040` | `#20E020` | `#FF6A00` |
| 11 Space Journey | `#D8C8FF` | `#7A3CFF` | `#FF1020` | `#2040FF` | `#FF30D0` | `#2040FF` | `#7A3CFF` |
| 12 Toy Factory | `#FFD0F8` | `#FF40E0` | `#80D020` | `#40A0FF` | `#8030FF` | `#80D020` | `#8030FF` |
| 13 African Safari | `#FFE0A0` | `#FF8000` | `#FFD800` | `#00C000` | `#FF4000` | `#00C000` | `#FF8000` |

Lower in the list draws over higher: a bumper's flash shows through the
multiball blink, and a tilt covers everything. The table's own rows are on
table 1 only. A hole, the kickback and a ramp leave the `Back MX` when there
is a side MX for their side ([below](#the-side-mxs)); flasher devices keep
them. A flasher device's own effects
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
`Back MX`, `Left MX` and `Right MX` are from a real export.

#### Shapes on the Back MX

Where the DOF folder libdof reads has `DirectOutputShapes.xml` and
`DirectOutputShapes.png` (a Config Tool export brings both), the `Back MX`
draws its five spots as the Config Tool's own flasher circles: `SHPCircle3` at
`AL0`, `AL20`, `AL40`, `AL60` and `AL80`, `AW19 AH100`, as its `[Variables
DOF]` define `flshemulo` to `flshemuro`. The made file spells them out, since
it carries no variables. The colours and times above stay; these change:

| Event | On the Back MX |
|---|---|
| a bumper | its own circles: a side's bumpers share its spots from the left, so table 1's left bumper has the two left circles and table 5's have one each, but for the two in the centre |
| `SkillShot` | `SHPCirclePulse`, *ramp*, 600 ms, on all five |
| `Award` | `SHPDiamondBoxPulse`, *feature*, 1 s, on all five |
| `LanesLit` | `SHPFillLeftRight`, *glow*, 600 ms, across the whole width |
| `Jackpot` | `SHPRoundAndRound`, *jackpot*, 1.2 s, on all five |
| `SuperJackpot` | the same for 2.4 s, and the two strobe circles (`strblft`, `strbrgt`) flashing *glow* six times |
| `ExtraBall` | `SHPCrossPulse`, *glow*, 900 ms, on all five |

Without the shapes file and its `.png`, a shape draws nothing, so the `Back MX`
gets the bands instead, and the helper says `no DirectOutputShapes.xml and .png
in ...: plain bands on the Back MX`. A `ShapeDefinitionFilePattern` in your `GlobalConfig` is not
looked at.

#### The side MXs

`Left MX` and `Right MX`, the LED strips down the sides, get what the Config
Tool's own games put there, top (the back) to bottom (the player), lower in
the list drawing over higher:

| Event | MX | Effect |
|---|---|---|
| `BallReady` | right | the bottom 15% breathes *main*, dark to full every two seconds, while a ball waits: `AT85 AH15 F500 BLINK 2000` |
| `SlingshotLeft`, `SlingshotRight` | its side | *main*, rising up the side: `M50 MAX50 ADU AS300` |
| a hole kicking out, the kickback | its side | *feature*, down the side: `75 ADD AS300` |
| a ramp made | its side | *ramp*, down the side: `75 ADD AS300` |

A feature in the centre third of the table goes down both. On table 1 the
left MX gets the test and left holes, the power-up hole, the kickback and the
left ramp; the right MX the power-up hole, the physical-systems hole, the
right hole and the centre and right ramps.

The breathe is the launch-ball effect the Config Tool's games use, at the
bottom of the right MX, slowed: measured on libdof 0.5.4, a fade takes about
twice the time it is given, so their common `F500 BLINK 1000` only swings
between dim and about two thirds. `RGB Undercab Complex MX` stays 0.

### 5. Try a row

With the game closed:

```sh
dof/build/epicpinball-dof --table ep5 --bumpers LLCCRR --test Bumper4
```

It sends that one event (a flipper or another level is held half a second),
waits three seconds and switches off. `--bumpers` is the table's sides from the
table [above](#the-bumpers); without it a made row assumes three, `LCR`. For
table 1's own events, also give its features, as the game does (from the
`zone`, `target` and `bank` lines of `work/feedback/ep1.map`):

```sh
dof/build/epicpinball-dof --table ep1 --bumpers LR --test t1PowerUpHole --features "hole1=TestHole:L \
  hole2=LeftHole:L hole3=PowerUpHole:C hole4=PhysicalSystemsHole:R hole5=RightHole:R kickback1=Kickback:L \
  rollover1=Star:L rollover2=LeftLane:L rollover3=TopLane1:C rollover4=TopLane2:C rollover5=TopLane3:R \
  rollover6=TopLane4:R ramp1=LeftRamp:L ramp2=CentreRamp:R ramp3=RightRamp:R target1=DropTarget1:L \
  target2=DropTarget2:L target3=DropTarget3:L bank=DropTargets"
```

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
It uses the game's own names, `flipper_left`, `knocker`, `hole_3`, which the
helper turns into the DOF names above (`dof/src/events.h`): `FlipperLeft`,
`ExtraBall`, and table 1's third hole `t1PowerUpHole`.

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

The settings tool ([config.md](config.md)) finds the plunger's axis as you
pull it, calibrates it, and shows a drawn plunger moving with it. By hand:

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

This feature writes the game's memory: the pull value and the Ctrl-held
flag, only with a plunger configured, and only on a table whose file matches
its map. So does nudging from an accelerometer (below).

## Nudging

The game's own nudge is three keys: **Z** left, **/** right, **Space** up
(the pad's bumpers press Z and /). A board that types those keys, as
VPinball's default nudge keys are the same, needs nothing set up.

A cabinet's accelerometer, and nudge buttons a board reports as joystick
buttons, are set in `saves/controller.ini`. The settings tool
([config.md](config.md)) sets them and calibrates the accelerometer;
`--joysticks` shows the axes as you shove the cabinet, and each button's
number as you press it:

```ini
[nudge]
device = Pinscape Pico       ; the name or GUID from --joysticks
x_axis = X                   ; the accelerometer across the cabinet
y_axis = Y                   ; and along it (Pinscape: X and Y)
dead = 1500                  ; readings within this of 0 are the cabinet at rest
strength = 1                 ; 1: a full shove pushes as the game's own nudge
tilt = 1                     ; how fast shoves fill the tilt meter; 0 never
invert_x = 0                 ; 1 if a shove sends the ball the wrong way
invert_y = 0
is_velocity = 0              ; 1 if the board reports velocity, not acceleration
indicator = 1                ; the ring on the playfield showing the reading
indicator_range = 8000       ; the reading that takes it to the ring
left_button = none           ; button numbers: the game's own nudge, Z / and Space
right_button = none
up_button = none
```

- **A shove pushes by how hard.** At the start of each of the table's physics
  steps, about 200 a second, the accelerometer's reading of that moment,
  past `dead`, changes the speed of every ball in play by up to 14h, the
  game's own push for a step, times `strength`. The push is against the
  cabinet's movement: the cabinet shoved left sends the ball right, as Z does.
  Small readings add up rather than round away.
- **Where the game pushes.** The game's own nudge pushes a ball only while a
  value its physics works out for the ball lies within 4 to 2Ah (the same on
  every table); a shove pushes by the same rule, as the last step left it.
  Never while tilted, nor in the demo.
- **Tilt by the game's rule.** Every game-nudge's worth of push (14h a step
  for 24 steps) fills the tilt meter by 23h, as one press of Z does. The
  meter drains as the game drains it, and the game tilts above 50h: hard
  shaking tilts the table, soft bumps never do. `tilt` scales this.
- **Velocity input.** Some boards (a Pinscape can be set either way) report
  the cabinet's velocity instead of its acceleration: set `is_velocity = 1`.
  Each step then pushes by how much the velocity changed since the step
  before, against it, all at once: a full-scale change (0 to 32767) is a
  whole game nudge, 24 steps of 14h, times `strength`, and fills the tilt
  meter by 23h. A velocity held steady pushes nothing. `--nudge-trace` with
  `--nudge-velocity` tries it without a board.
- **The indicator.** While an accelerometer is read and a table is up (not
  the menus), a ring at the top right of the playfield shows the reading, as
  VPinball does: the dot is where the cabinet is going (with velocity input,
  how fast), a fainter ring is the dead zone, and a short trail shows a
  quick shove. The dot reaches the ring at `indicator_range` (more stays on
  it): lower shows small shoves bigger. It only draws; the push is
  `strength`'s. It turns with the playfield in a cabinet. Shots and
  recordings leave it out; `indicator = 0` hides it.
- **Buttons** press Z, / and Space while held, the game's nudge exactly,
  with its own tilt rules. Any of the game's actions can have buttons too:
  `[buttons]` below.
- Out of focus, unplugged, or while `--table` drives the menus, there is no
  shove and the buttons let go.

Writes into the game: each ball's speed and the tilt meter, from the step
seam, only with an accelerometer configured and a table whose file matches
its map.

## Keys and buttons

Each of the game's actions has its own key, which always works. More keys can
do the same, for a board that types VPinball's keys, and a board's joystick
buttons can hold them, in `saves/controller.ini` (the settings tool sets both):

```ini
[keys]
launch = ENTER              ; VPinball's launch key, as well as Space
start = 1 | KP_ENTER        ; Enter's job: PLAY GAME, and the scoreboard in play

[buttons]
device = Pinscape Pico      ; the name or GUID from --joysticks
left_flipper = 0            ; button numbers from --joysticks, | between
right_flipper = 1
launch = 2
```

The actions are `left_flipper`, `right_flipper`, `launch`, `nudge_left`,
`nudge_right`, `nudge_up`, `start`, `exit`, `up`, `down`, `left`, `right`,
`yes` and `no`; the keys are names like `LSHIFT`, `RCTRL`, `A`, `F1`,
`KP_ENTER` (`src/keys.c` lists them). A listed key sends its action's key
instead of its own; every other key is passed on as it is. A key and a
button held on one action press it once and let go when both are released.
Buttons are read only while the window is in front. Linux only for now.

## The maps

These features find their way around the tables with `work/feedback/epN.map`,
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
header), table 1's own events, the colours and shapes on its `Back MX` LED
strip, the effects on its `Left MX` and `Right MX` and the ball-ready breathe,
and the analog plunger.

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
- the MXs in made rows, through the helper and libdof on ArtNet to a 40x4
  `Back MX` and 40-LED `Left MX` and `Right MX` with the Config Tool's shapes
  file: each wipe on its side and going its way, the ball-ready breathe from
  dark to full on the right MX's bottom rows only, the bumpers' circles in
  their spots, the animated shapes changing on all five, and bands without the
  shapes file. From the game on table 1 with the real cabinet's file 30
  header: every lit side pixel matched an event, the breathe on every ball,
  and no hole or ramp on the `Back MX`. Then on the real cabinet;
- table 1's own events against the game's own counts, over four hours of its
  demo: every ramp, hole, left lane, drop target bank and set of lanes
  matched, each switch fired where its feature is drawn. Jackpots, double and
  super jackpots never came up, in the demo or in scripted play: they are read
  from the same message function as the awards, which did. Every one of them,
  through the helper to flashers, the middle-row devices and a beacon on
  ArtNet; from the game, skill shot, lanes lit and a ramp. Then in play on
  the real cabinet;
- nudging by a scripted accelerometer (`--nudge-trace "ms:x,y,..."`) on
  tables 1, 5, 9 and 13: a shove left speeds a rolling ball rightwards by 14h
  a step at full strength; two seconds of hard shaking tilts table 1, of
  quarter-strength shaking never does; the game's gate holds some steps back.
  Through SDL, a virtual board (uinput) with X and Y axes and three buttons:
  `--joysticks` shows its axes and buttons, its X axis pushed the ball in a
  window, and its button gave the game's own nudge (the feedback log's
  `nudge`, the meter up 23h). Then on a real cabinet's accelerometer, a
  Pinscape Pico's X and Y, at the default settings. Nudge buttons not yet on
  a real board;
- the plunger by scripted sensor (`--plunger-trace "ms:percent,..."`) on all
  13 tables, and by a virtual joystick through SDL. Its launch strengths are
  the keyboard's and so are the speeds the ball leaves with. A ball's later
  path still differs with the millisecond of release, as it does from the
  keyboard;
- 70 frames a second in cabinet mode with the helper, a hung helper, and a
  sensor present and absent;
- events by name, through libdof's master and the helper on ArtNet over
  loopback: `--test` with an event of every table, a table 1 feature
  (`t1PowerUpHole`, any case), the bank's `t1DropTargetsDown` and
  `ExtraBall`, each on its own output for its row's time, and an old `E101`
  column left dark. A row made from a header on table 1, and from the game on
  table 1, `Bumper1` and `t1TopLane1` lighting their outputs each time the
  game sent them. Named events are not yet tried on the real cabinet.

Not tested: tables 2 to 13 on a cabinet, other controllers and other
cabinets' Config Tool headers, Flatpak and portable VPX, Windows (its halves
were only syntax-checked), other releases of the game.
