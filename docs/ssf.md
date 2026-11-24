# SSF: the board's sounds on the cabinet's exciters

`--ssf` (Linux) puts the game's sound out as 7.1 for a cabinet with exciters,
wired as vpinball wires one:

| Output | 7.1 channel | What plays |
|---|---|---|
| backbox speakers | front left, front right | the game's own mix: music, callouts, everything not the board's |
| exciters at the top of the table | side left, side right | the board's sounds there |
| exciters at the bottom (lockbar) | rear left, rear right | the board's sounds there |
| | centre, LFE | nothing |

The board's sounds (bumpers, slingshots, flippers, drop targets, the
plunger, holes, a rolling sound for the ball) leave the front and play only on
the exciters, placed where on the table they happen: a bumper at the top
mostly on the side pair, the left flipper on the rear left, a ball rolling down
the right lane moving from the top right to the bottom right.

```sh
./epicpinball.sh --cabinet --ssf
./epicpinball.sh --cabinet --ssf --dmd-device --dof
```

The sound card must offer 7.1; with fewer channels SDL folds the eight
together. `--wav FILE` with `--ssf` writes all eight channels, to check the
wiring. At start it says what it found:

```
[ssf] sound out: 7.1 to System default, which has 8 channels
[ssf] ep1: 9 board effects on the exciters, effects x4, rolling x4
```

Fewer than 8 channels there: set the card's profile to 7.1 (in
`pavucontrol`, or `pactl set-card-profile`). No second line: the table has
no feedback map (`[feedback]` says why).

## Settings

In `saves/epicpinball.ini`:

```ini
[ssf]
effects_volume=4        ; the board's effects on the exciters: 1 is as loud as the game's own mix
rolling_volume=4        ; the rolling ball; 0 turns it off
board_ep1=0,1,2,3,4,5,6,B,11   ; a table's board effects (hex slots in its SFXn.PIN)
```

`effects_volume` and `rolling_volume` default to 4, 12 dB above the game's
own mix: the game's effects are quiet beside its music, an effect in the
middle of the table gives each exciter a quarter, and exciters need driving.
Past about -3 dBFS the exciters' mix rounds off (a soft knee) rather than
clips, so a higher volume makes the busy moments denser, not distorted. The
card's own per-channel volumes (side and rear) count too. A table's `board_epN`
replaces what the map lists for it (below). `--ssf-volumes FX,ROLL` sets
both for one run, over the saved ones (headless runs read only that).

## How it works

Every table plays an effect through one function of its own: AL is the effect,
AH a pan nibble (0 pans by the ball, 1-15 are fixed: the flippers, the
plunger). That function asks the Sound Blaster driver, the same lifted code for
all tables, for a voice:

- **the driver's voice setup** gets the sample's 60h-byte header in ECX
  (+2Dh the PCM's address, +36h its length, +45h the rate, +44h the volume,
  loop flags and points) and the class in DL: 1 for effects, 2 for music;
- **its volume call** gets the voice in AL and the volume in CL;
- **its stop** gets a voice in AL, **its stop of a class** a class in AH.

`src/ssf.c` follows these through the `RECOMP_ENTER` seam. For an effect the
table's list calls the board's, the setup starts a voice of its own from the
same header, and the volume call's CL becomes 0: the driver mixes that voice
silently, and stops it when it would have stopped, and so does `ssf.c`. This
is the second thing that writes into the running game, after the plunger,
and it only ever touches the volume of a voice that is playing on the exciters
instead.

Where an effect is heard: a fixed pan (the flippers, the plunger) at the bottom
of the table at that pan's place; a bumper or a slingshot where the kicker last
hit, within 300 ms; the thud where the ball is (it hit a wall); any other
where the table last handled a switch, or the kicker hit, within 300 ms
(`watch.c`'s); otherwise the ball, as the game itself pans. The gains are
vpinball's (`src/audio/MixNode.cpp`): linear across, `left = (1-pan)/2`, and
linear down the table, `bottom = (1+fade)/2`, top the rest. A sound in the
middle gives a quarter to each exciter.

A voice is weighed as the driver weighs it: its volume times the driver's
master volume for effects, the high byte, 0-127. Resampled from the effect's
rate, it is mixed at 44.1 kHz in step with the front.

**Rolling.** The game has no rolling sound: every effect is a one-shot. For
each ball in play, `ssf.c` makes a rolling sound: noise through two band
passes in a row, a narrow band with steep sides, its centre rising with the
ball's speed from 150 Hz to 500 Hz at a launch, and dipping once a turn of
the ball, 4 to 25 times a second. Its gain is the ball's speed squared (full
at 350, about a launch; a ball under 40 is still), eased so it never clicks,
and it is placed where the ball is. On a cabinet a low rumble alone sounded
too bassy, and a wide band up to 1.1 kHz like hiss; the constants are
`ROLL_*` in `src/ssf.c`.

**The board's effects** are listed per table in the feedback map, as hex slots
of its `SFXn.PIN` (`board` in `work/feedback/epN.map`, by
`tools/feedback_map.py`). Slots 0-6 are the same kind on every table:

| Slot | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|---|
| | bumper | drop target | slingshot | thud, kickback | flipper | the ball in the shooter lane | plunger |

Table 1 adds 0Bh (its holes) and 11h (its rubber posts). Award and jackpot
calls are pitch sequences of other slots, and stay on the front.

## What to know

- **Rolling only while the ball is played:** while the game waits (QUIT?,
  a pause, the bonus) its physics stops, and so does the rolling; with the
  game's effects off (SOUND FX OFF) there is none. The attract demo plays its
  board effects, and so do the exciters.
- **The exciters are early, the front late:** the game's sound driver mixes
  about one buffer (46 ms) ahead of what is heard, so an effect reaches the
  backbox 42 ms after the table played it. On the exciters it sounds at once,
  nearer the picture.
- **Hard pans:** the driver softens a hard-panned effect in its stereo mix
  (the right flipper, the plunger); the exciters do not, so those are louder
  there than in the game's mix (below).
- **Callouts made of board sounds:** a few award sequences play a board
  effect's sample at rising pitches (table 1's from the bumper's): those go
  to the exciters too.
- **`--record`** keeps the front pair only: with `--ssf` its video has no
  board sounds. `--wav` has all eight channels.
- **Fewer than eight channels:** SDL is asked for 7.1; how a device with fewer
  folds them was not tried.
- **Timing:** with `--ssf`, a scripted game goes another way after the first
  board effect than without it: what the driver mixes changes how its code
  runs, and the game's interrupts land a little differently, as on the real
  machine. Runs with `--ssf` are deterministic among themselves. Without
  `--ssf` nothing changes.

## What was tested

Without hardware, headless, the eight channels written to a WAV:

- **every table**, a scripted game of 200 s with `EPIC_SSF_CHECK=1` (a line for
  each board effect: slot, place, why there, the voice silenced): 2,118
  board effects, every one's voice silenced in the game's mix; every bumper and
  slingshot effect placed at its kicker hit as the feedback log has it;
- **table 1**, with the music off: the front silent at the board's sounds
  (callouts aside); each flipper on its bottom corner (95% there when nothing
  else sounds), the launch on the bottom right (93%), bumpers near the top
  mostly on the side pair;
- **the waveform:** the plunger on the rear right against the same effect in
  a run without `--ssf` (the runs are alike until it): correlation 0.955, 42 ms
  early (above);
- **loudness** against the game's own front mix, the same events: the left
  flipper 0.99 times, a bumper 1.16, the right flipper 1.66, the plunger 4;
- **rolling** following the ball, silent between balls, at QUIT? and with the
  effects off;
- **7.1 in a window:** SDL opened the device with eight channels (its disk
  driver, written to a file);
- the self-test (placing, the silent voice, stops, rolling, a prompt);
  conformance 154/154 and the feedback game cases without `--ssf`.

On a cabinet (ALC892 line out, 7.1, PipeWire), a scripted game of table 1
with the card's monitor recorded: at volume 1 the exciters' level in play was
a fifth of the front's (peaks -12 dBFS), too quiet to hear; at the default 4
it is about the front's, peaks rounded off at the knee. By ear on that
cabinet: the board on the exciters, and the rolling ball after two retunings
(**Rolling**, above).
