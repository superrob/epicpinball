# Lifting Epic Pinball: what it took

The things that each cost real time, in the order they turned up, so the next
hand-written-asm, multi-program DOS title costs less. Code: [`tools/lift.py`](../tools/lift.py),
[`src/images.c`](../src/images.c), [`src/dos.c`](../src/dos.c), and in pcrecomp
`decode16.py` / `lift16.py`.

## 1. It is not one program

`PINBALL.EXE` is a 36 KB front end. It loads two MASI driver blobs into memory
it allocates, plays the music, and runs each table with `INT 21h/4B00h`:

```
[dos] exec Setup.exe tail " -cd c:\"
[image] MDRV004R.MUS live at 0A27 (9789 bytes)
[image] MDRV000R.MUS live at 0C8D (4680 bytes)
[dos] exec ep1.exe tail " D<602270:;<0>1001           @513"
```

The table's command tail is its mode (`D` demo, `1`–`4` players) and PINBALL's
settings as digits; started on its own a table prints *Music not connected —
This can only be run through PINBALL.EXE*. So the runtime has an EXEC
(`dos_exec`), and the lift has **images**: each file lifted separately, at the
segment it runs at, with the runtime keeping track of which are live
([architecture](architecture.md#images)).

## 2. SETUP.EXE answers through video memory

Every start, PINBALL runs `SETUP.EXE -cd <its directory>` and then looks for
`pincd=` at `B000:FA00` — an unused corner of monochrome text memory that SETUP
writes its answer into. Without it PINBALL prints *Please run SETUP* and quits.
SETUP is a Borland C++ program with a whole sound-card menu. At PINBALL's
start the runtime still writes the string itself (`dos_exec`): running SETUP
there would move every block PINBALL allocates after it, and with them where
the tables are lifted. `--setup` runs the lifted SETUP on its own, as the menu
it is (section 13).

## 3. The MASI drivers are raw blobs

`MDRV*.MUS` start `FE "REAL16" FE`, a 12-character name, and dword offsets of
their entry points at 28h and 2Ch. PINBALL reads the whole file into a block
and far-calls `seg:[28h]`; the entry sets DS = CS and does
`call word [bx+279h]` — a function table indexed by the call number. So the
lifter seeds a driver from its header, and reads `call/jmp word [bx+table]`
tables in a driver as DS-relative (`ptr_table`).

The runtime notices a driver arriving by watching `INT 21h/3Fh`: a read of a
whole `MDRV*.MUS`, from its start into one run of memory, places it there.

## 4. 32-bit addressing in real mode

The SB driver indexes per-channel arrays as `mov word [ebx*4+7A4h], ax` and
counts its mixing loops in ECX with `67 E2 xx` (`loop` with an address-size
prefix). pcrecomp's decode16 knew the `67h` prefix existed and ignored it, so
`67 89 04 9D A4 07 00 00` came out as `mov [si], ax` followed by garbage, and
the driver's jump table `jmp word [ebx*2+2524h]` as `jmp word [si]` — which
jumped to `0504h`, two bytes inside an instruction. pcrecomp#74
adds the 32-bit ModR/M + SIB forms, `moffs32`, full-width
`lea`, and ECX for `loop`/`jcxz` under `67h`. Checked against real x86 with
`difftest16`: the forms agree except where the effective address passes FFFFh,
which faults on a real CPU in real mode and is not a comparison at all.

## 5. What only running tells you

Two things the binaries do not say:

- **where an image lands.** A table's segment depends on how much PINBALL
  allocated for that table's song first: EP5 goes to `2014h`, EP7 to `542Fh`.
- **some entry points.** The interrupt handlers go through a variable
  (`mov cs:[0A04h], dx` … `mov dx, cs:[0A04h] / int 21h`), the SB driver picks
  routines out of a table of them (`2221h`).

So the runtime writes both down as it meets them (`work/layout.txt`,
`work/misses/<image>.txt`) and `tools/discover.py` loops: lift, build, play
PINBALL and every table headless, lift again. From an empty `work/` it settles
in four rounds, about 80 seconds:

```
round 1  ... PINBALL only; the drivers and 13 tables get placed
round 2  ... everything lifted; their interrupt handlers get reached
round 3  ... the handlers lifted; the SB driver's per-card routines reached
round 4
settled after 4 rounds: every address reached is lifted
```

A miss that is recorded, lifted, and still missed is a bug, and discover says so
instead of looping. Two traps on the way: the runtime used to append the same
miss every run, so "nothing new" never came; and MSBuild did not notice the
regenerated image list when it was a header, so a newly lifted image was linked
but never registered — it is a generated `.c` file now.

## 6. Finding code by its tables

All hand-written, so no compiler's switch shapes. What the lift recognises:

| Form | Where | How it is read |
|---|---|---|
| `cmp bx, N / … / jmp cs:[bx+T]` | the tables | `N+1` words at `T` |
| `mov bx, cs:[bx+T] / jmp bx` | the tables' key handlers; the mixer's command interpreter (`mov bx, [bx+84Dh]`) | words at `T` while they land on plausible code — and **not past the lowest target already read**: the handlers sit right after their table, and reading on turned the handlers' own bytes into 30 bogus arms |
| `jmp ax` into an unrolled loop | the SB driver's mixers | the four tables at `1214h`/`1440h`/`16CCh`/`1958h` are runs of 32 words in arithmetic progression (`1425h, 1416h, 1407h, …`), which numbers almost never are; any such run within 4 KB becomes the `jmp`'s switch arms, so the loops stay gotos in one function |
| `mov [X], imm` … `call [X]` | the SB driver's current output routine (`[223Dh]`) | only when `[X]` is somewhere called through: the same driver stores *table* addresses the same way (`mov [115Bh], 16CCh`), and its volume tables decode as plausibly as code |

A stored pointer was first taken from any `mov`/`push` of an immediate landing
after a `ret`; that lifted a run of zero bytes as `add [bx+si], al` and the SB
driver's volume tables as `sbb` soup.

## 7. Text after `int 21h/4Ch`

PINBALL puts its messages right after the exit call
(`mov ax, 4C00h / int 21h / db 'The executable should be named…'`). Decoding on
into them invented jumps into the middle of real instructions. `int 21h` after
`mov ax, 4Cxxh` or `mov ah, 4Ch` (and `int 20h`) now ends a path.

## 8. Sixty-four DOS blocks were not enough

PINBALL allocates one DOS block per song sample — 60-odd for one song, and the
next song's before the last one's are freed. The arena list held 64; the
65th wrote past it into the image registry next to it in memory, every image
stopped being live, and the next timer interrupt reported *no image there*. It
holds 1024 now and refuses allocation 1025.

## 9. A table quits from inside an interrupt

A table's `INT 21h/4Ch` can come from inside one of its interrupt handlers
(the demo's ends that way). The longjmp
back to EXEC skipped the end of `recomp_tick()`, which left its re-entrance
guard set — and from then on no interrupt was delivered at all, so PINBALL's
next table waited forever for a timer flag. `machine_unwound()` resets that
state after every child exit.

## 10. Waiting for retrace inside the timer interrupt

The tables lock the PIT to the display: the timer handler polls `3DAh` for
vertical retrace and then reprograms channel 0. In deterministic time nothing
advances inside a handler (time moves in `recomp_tick`, which does not re-enter),
so it spun forever. Each `3DAh` read now costs a microsecond of emulated time,
as an ISA read does.

## 11. The sound card is a Sound Blaster 16

The first audio was music under a wall of noise: every other sample belonged to
a different signal. The driver was mixing **stereo** while the DSP model,
claiming to be an SB Pro (DSP 3.02), played the stream as mono — and the driver
turned the SB Pro's stereo bit off again after starting, because GOG's
`SOUNDCRD.INF` was written for the Sound Blaster 16 its DOSBox emulates. With the
DSP answering 4.05 the driver uses the SB16 commands — `41h` (rate 22,050 Hz),
`C6h` mode `20h` (8-bit, stereo, unsigned, auto-init) — on the same 8-bit DMA
channel, and both channels came out as smooth signals. IRQ 7, also from that
file.

## 12. The scoreboard is the split screen

The bottom 19 rows of tables 1-8, and 29 of tables 9-13, are the CRTC's line
compare (`18h`, with bits in `07h` and `09h`: `1B9h`, scanline 441 of 480;
`1A5h`, scanline 421, on 9-13): the CRTC restarts at address 0 there, where the
tables draw the dot matrix, while the playfield above scrolls by start address
([dmd](dmd.md)).

## 13. SETUP.EXE and HELPME.EXE are compiled

The two programs nobody wrote in assembly: SETUP in Borland C++ (1993, with
its exceptions and string classes), and HELPME, which SETUP's Instructions
runs, in Turbo Pascal. What each cost:

- **Code only pointers reach.** Following calls from the entry point found
  275 of SETUP's routines and 55% of its code; the rest is reached through
  vtables and function pointers, and a run only through the keys pressed.
  Every compiler prologue (`push bp / mov bp, sp`: `55 8B EC` in Borland C,
  `55 89 E5` in Turbo Pascal; or `enter`) that follows the end of another
  routine is an entry too (`compiled_seeds`): 486 routines, 93%.
- **Borland's switch on long or sparse values** compares the value with N
  words at T in a `loop`, then `jmp cs:[bx+D]`: the targets are the N words at
  T+D. Unrecognised, SETUP's dispatch on a menu choice reached its arms one run
  at a time (`jump_table`).
- **Turbo Pascal's main program ends in `call Halt`**, which never returns,
  and a few zero bytes pad it to the CRT unit's segment. Walking on, the zeros
  decoded across the boundary and CRT's code was lifted as main's, with CS 0:
  `AssignCrt` then stored `0000:0359` as the screen's routine, a routine of
  main's that waits for a key, and HELPME sat on a blank screen. A walk now
  stops at an instruction that straddles the next segment's start
  (`segment_ends`), in Turbo Pascal only: Borland C's segments overlap by a
  paragraph, and SETUP's `retf` at `0D59:0330` is `0D8C:0000`.
- **What DOS and the BIOS had to answer**: `INT 21h AX=3700h`, the switch
  character, before `system()` builds `COMMAND.COM /c helpme.exe`, which the
  runtime runs as `HELPME.EXE` (there is no COMMAND.COM); an arrow key through
  `AH=06/07/08` as 0 and then its scan code; `INT 10h AX=1130h`, from which
  Turbo Pascal's CRT takes the number of rows (it had none, and wrote into a
  one-row window); scrolling with `INT 10h AH=06/07`, which only cleared; the
  device `PRN` opened as a device, not created as a file; and code page 437's
  accented letters in the text font ("Fran ais").
- **Settings in the driver.** SETUP keeps the card's port, IRQ, DMA and mixing
  rate in the chosen driver: 40h bytes at the offset its header gives at 30h,
  the same layout in every MASI card driver. The check that a driver is the
  one lifted leaves that block out.
- **Where the card driver's block goes moves everything after it.** PINBALL
  allocates the card's driver first, then the mixer, its songs and each table:
  with the PAS driver, 702 bytes shorter than the Sound Blaster's, the mixer
  landed at `0C61` instead of `0C8D`. The block allocated right after a card
  driver is opened gets the room of the largest one (`src/dos.c`), so every
  card leaves the rest where it was lifted. With no music (PC speaker, none)
  PINBALL loads no songs, and table 1 went to `1F7E`, not `42CF`: a lifted
  program is now EXEC'd where it was lifted when that memory is free.
- **The Sound Blaster's mono mixer** was never lifted: GOG's setting is
  stereo. The driver picks its mixing routines out of tables
  (`mov ax, [bx+2217h]` ... `mov [0BC6h], ax` ... `call [0BC6h]`), which
  `table_pointers` now reads, so every routine of every setting is lifted.

## Debugging kit

| | |
|---|---|
| `work/misses/<image>.txt` | every address the guest reached with no lifted function; the next lift makes them entries |
| `work/layout.txt` | where each image was placed |
| `--trace` | DOS calls, EXEC, image placement, DSP commands, DMA and mixer writes, CRTC changes |
| `cmake -B build-dbg -DEPIC_TRACE=ON` | lifted code at `/Od` with a PDB (builds in seconds), a ring of the last lifted functions entered, and a **watchdog**: if no interrupt is polled for 3 s it prints that ring and samples the main thread's stack *with generated-C line numbers* — each line carries its guest instruction |
| `EPIC_WATCH=<linear hex>` | each write to that byte, with the host call stack |
| `EPIC_VRAM0=<offset>` | show video memory from that offset (the scoreboard lives at 0) |
| `--shot-at`, `--record`, `--wav` | frames, video and audio of a run nobody watched |
