# epicpinball-dof

The program that tells [libdof](https://github.com/vpinball/libdof) what
happens on Epic Pinball's tables, recompiled, for a virtual pinball
machine's feedback devices. It passes events on and limits nothing: limits
are the controller's. Setting it up:
[docs/cabinet-feedback.md](../docs/cabinet-feedback.md).

**Licence: GPL-3.0-or-later** ([LICENSE](LICENSE)), because it links libdof.
Everything outside this folder is MIT. The game does not link this program or
libdof; it starts it and writes lines of text to it. Nothing builds it unless
you run `build.sh`.

```sh
dof/build.sh           # fetch what is missing, build into dof/build/
dof/build.sh --fetch   # fetch only
dof/build/helper/autorow_test
```

| | |
|---|---|
| `src/main.cpp` | the pipe protocol, events to libdof by name, a watchdog for a hung start or finish |
| `src/locate.h` | whose DOF files: VPinball's, or the game's own. No libdof in it |
| `src/autorow.h` | a row made from a Config Tool file's device header, when there is none; a table's own features' names (`--features`). No libdof in it |
| `src/events.h` | the events and their DOF names (`$FlipperLeft`) |
| `tests/autorow_test.cpp` | those rows, from made-up headers |
| `patches/` | what is changed in libdof |
| `example/` | rows to start from |

Pinned: libdof `49b6947` (0.5.4, master of 26 November 2026) with the libusb, hidapi, libftdi and
libserialport commits its own `platforms/config.sh` pins. They keep their own
licences and are downloaded, not included.

What that libdof makes the helper do:

- it can hang or crash, and carries queued events into the next table, so it
  gets a process for each table;
- it takes a row whose name the table's merely begins with, so the helper
  wants the exact row or `epicpinball`, and the names have their zero;
- it reads `$Name` in a row as a named table element, but its `DOF` class
  takes only a type letter and a number. `patches/named-data-receive.patch`
  adds `DataReceive(name, value)`, which the helper sends every event with;
- before `839624f` a named element lost its name and never fired, and with a
  Pico of more than 128 outputs it read past its value buffer: both fixed
  upstream, so the pin is at least that.
