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

In a cabinet (`--cabinet`) it is a pinball machine's way instead, below.

## Between games in a cabinet

With `--cabinet`, PINBALL's screens are never shown after the table either,
and nothing needs a keyboard:

1. **Game over.** The playfield keeps its last picture, dimmed, until the
   next game; the DMD shows the table's own end of game (bonus, scores) until
   the table ends.
2. **A high score.** The DMD shows HIGH SCORE #1 to #3, the score, and three
   places for initials:

   | Button | Does |
   |---|---|
   | left flipper, right flipper | the letter before, after: A-Z, 0-9, space, and a back arrow |
   | Start, or the launch button | takes the letter (the back arrow takes back the last one) |
   | a letter on a keyboard | takes that letter |
   | Exit (Esc) | ends the initials with those taken so far, and exits |

   After three letters they are typed to PINBALL's own initials entry,
   unseen, and the high scores are written to `hiscore.pin` at once.
3. **GAME OVER, PRESS START**, then the table's three high scores, a page each
   three seconds. **Start** plays the table again: PLAY GAME is driven as
   the first time, unseen, and the new game is shown as its ball is served,
   at once (0.05 s in a test). **Exit** (Esc) leaves by PINBALL's own way out.

Quitting a game in the middle (Esc, QUIT? Y) with a high score asks for
initials the same way, then exits. Closing the window during the initials or
at GAME OVER keeps the initials and leaves the same way.

The DMD's pictures go wherever the DMD goes: the DMD window, the ZeDMD
(`--dmd-device`), and with no DMD at all the scoreboard's window, as dots.
PINBALL's own sound is not heard between games. A button held while PINBALL
is driven (the initials being typed, the next game loading) is not pressed
again when it is the player's: Start held after the third letter does not
start a game, nor turn up in it.

PINBALL writes `hiscore.pin` only when it quits, so a cabinet switched off
after many games would lose them. Between games the high scores are written
as PINBALL would write them, the 1000 bytes it keeps in memory, but only
after the first game has shown that the file in `saves/` is that memory byte
for byte (a missing file, as on the first run, waits for PINBALL's own write
when it quits).

Without `--cabinet`, `--table` stays as above: initials are typed on
PINBALL's screen, which is the player's.

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

Between games in a cabinet, it also reads where the new high score is
(PINBALL's initials entry sets it), the high scores, and PINBALL's letter
for each key, and types the initials with the keys for those letters.

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

Between games in a cabinet, headless with `--cabinet --dmd` and every saved
score set to 1 (table 1 unless said):

- **game over, a high score**: the playfield held and dimmed, initials chosen
  with the flipper keys, Start and a typed letter (CAX), typed to PINBALL;
  `hiscore.pin` had CAX and 4,470,000 before the run ended, so written
  between games, and byte for byte the file PINBALL writes itself when it
  quits (the next run reads it as the same); GAME OVER and the high scores;
  Start served the second game, 4 s of emulated time later (unseen, fast);
- **two high scores in one run**: both entered and both written (first and
  second place);
- **no high score** (every saved score above the game's): straight to
  GAME OVER; two games, and Start still taken after 11 minutes at GAME OVER;
- **Exit at GAME OVER**: out by PINBALL's way, exit code 0;
- **quitting a game with a high score**, Exit in the middle of the
  initials: the letter taken so far saved, then out;
- **no DMD**: the pictures on the scoreboard's window;
- **tables 8 and 11**: held and dimmed once (as dark as the table's own game
  over dim), the table's own end of game on the DMD until it ends;
- **without `--cabinet`**: initials typed on PINBALL's screen, as before;
- **in a window with a cabinet board** (SDL's offscreen driver, a virtual
  joystick in `[buttons]`): launched by its button to game over, initials
  CBB by the flipper buttons and Start, the third Start held 4 s into
  GAME OVER without starting a game, then a new press: the second game shown
  0.05 s later; and a game over, the initials and out by Exit;
- the self-test (a whole game over to the second game in fake PINBALL
  memory, `hiscore.pin` written, the initials' rules); conformance 154/154.

- **every table's high scores**, 1-13: twenty minutes of `--autoplay` from a
  fresh save folder, then quit (Esc, Y): `hiscore.pin` had the table's own
  five, new initials among them, and nothing changed in any other table's;
  started again on it and quit, it was written back byte for byte.

Not yet on a real cabinet's DMD, nor on Windows.

On a real cabinet, with `--cabinet --dof --dmd-device --ssf` (before the
between-games part). Not on Windows.
