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
```

| | |
|---|---|
| `src/main.cpp` | the pipe protocol, the event numbers, a watchdog for a hung start or finish |
| `src/locate.h` | whose DOF files: VPinball's, or the game's own. No libdof in it |
| `patches/` | what is changed in libdof |
| `example/` | rows to start from |

Pinned: libdof `3d0ad20` (0.5.4) with the libusb, hidapi, libftdi and
libserialport commits its own `platforms/config.sh` pins. They keep their own
licences and are downloaded, not included.

What that libdof makes the helper do:

- it can hang or crash, and carries queued events into the next table, so it
  gets a process for each table;
- it takes a row whose name the table's merely begins with, so the helper
  wants the exact row or `epicpinball`, and the names have their zero;
- with a Pico of more than 128 outputs it read past its value buffer and
  sent what it found to the ports above. `patches/pico-output-bound.patch`
  stops the loop at the buffer.
