# --table: straight to a table

`--table N` starts table N (1-13) with a one-player game, for a frontend or a
launch script:

```sh
./epicpinball.sh --table 5
./epicpinball.sh --cabinet --table 1 --dof --dmd-device --ssf
```

The first thing shown is the table with its ball being served, about half a
second after start. The notice, the intro and PINBALL's menus run unseen and
unheard first. Then:

| You | What happens |
|---|---|
| quit the table (Esc, QUIT? Y) | the program exits |
| quit with a high score | PINBALL asks for your initials first, as always; then it exits |
| play to the end of a game | your initials if it was a high score, then the table's own screen (PLAY GAME, RULES, OPTIONS, EXIT) is yours: PLAY GAME plays it again, and EXIT or Esc there exits the program |
| press keys while it starts | nothing: they are not the game's then, except Esc, which cancels the run |

Every exit goes through PINBALL's own: it is PINBALL that writes the high
scores (`hiscore.pin`) and `config.pin`, when it quits, so `--table` quits it
by its menus, unseen, rather than stopping the program.

`--keys` with `--table` counts from the moment the game is shown.

## How it works

PINBALL.EXE has to run: it loads the sound drivers and the table's music,
then runs the table (DOS EXEC), and the tables use both. So `src/autostart.c`
drives PINBALL's own menus with keys, as a player would. It chooses each key
from where PINBALL is, read from its code and memory through the
`RECOMP_ENTER` seam, never from a clock:

| PINBALL is in | It gets |
|---|---|
| the intro | Enter |
| the main menu | Up until the cursor is on BEGIN, then Enter |
| the table list | Down or Up to table N's place among the installed tables, then Enter |
| table N's screen | Enter on PLAY GAME: the game starts by itself |
| the EXEC | checked to be `EPn.EXE`, its command tail set to one player |
| table N | nothing: shown when its physics first steps (the ball is served) |
| table N's screen after you quit | Esc; then Esc on the list and on the main menu, and PINBALL quits |

Each of PINBALL's menus calls one function a frame while it waits for a key,
with its cursor in AH, and the intro another. A key is pressed on such a
frame and let go 50 ms of emulated time later. A key that leads to another
screen is not pressed again until that screen comes, or a second's worth of
frames later if it has not. Initials entry is seen by its own flag, and a
main menu by the frame counter only it keeps.

While PINBALL is driven, emulated time runs as fast as the game goes, as it
does headless: the 21 seconds to a served ball take about 0.4 s. Nothing is
shown, no sound is queued, and the window's events are still read. When the
game is shown, the clock carries on from there.

Where these functions and variables are in your own PINBALL.EXE is in
`work/feedback/pin.map`. `tools/feedback_map.py` writes it from the lifted
code, by the shape of a few instruction runs, beside the tables' feedback
maps; `build-linux.sh` runs it. A table without its feedback map is shown
from its start rather than at its served ball.

It writes nothing into the game's memory. It sends keys, and sets the one
players digit of the table's command tail.

## What was tested

Headless, with the trace on (`--trace` shows each key and phase):

- **every table, 1-13**: the right `EPn.EXE` runs, its game is shown as the
  ball is served, Esc and Y quit it, and PINBALL quits writing both saves;
  exit code 0;
- **a high score** (every saved score set to 1): at game over, initials ABC
  typed, Esc on the table's screen: `hiscore.pin` has ABC and the score;
- **quitting with a high score**: the initials are asked, typed, then out;
- **OPTIONS** opened from the table's screen after a game: its Esc returns
  to the screen and does not exit; the next Esc does;
- **PLAY GAME again** after a game over: the second game, then a quit exits;
- **one player** whatever the menu's players setting, which is not changed;
- **in a window** (SDL's offscreen driver): 25 s of emulated time, the table
  shown at 21.4 s, took 4.0 s; only the game's sound was queued;
- the self-test (the map, the list place, the keys of a whole drive);
  conformance 154/154.

On a real cabinet, with `--cabinet --dof --dmd-device --ssf`. Not on Windows.
