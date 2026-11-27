# epicpinball-dof

The program that tells [libdof](https://github.com/vpinball/libdof) what
happens on Epic Pinball's tables, recompiled, for a virtual pinball
machine's feedback devices. It passes events on and limits nothing: limits
are the controller's. Setting it up:
[docs/cabinet-feedback.md](../docs/cabinet-feedback.md).

**Licence: GPL-3.0-or-later** ([LICENSE](LICENSE)), because it links libdof.
Everything outside this folder is MIT. The game does not link this program or
libdof; it starts it and writes lines of text to it. Nothing builds it unless
you run `build.sh` (Linux) or `build.ps1` (Windows).

```sh
dof/build.sh           # fetch what is missing, build into dof/build/
dof/build.sh --fetch   # fetch only
dof/build/helper/autorow_test
```

On Windows, with Visual Studio 2022's C++ tools, CMake 3.25 and git:

```powershell
powershell -ExecutionPolicy Bypass -File dof\build.ps1          # into dof\build\: epicpinball-dof.exe and its DLLs
powershell -ExecutionPolicy Bypass -File dof\build.ps1 -Fetch   # fetch only
dof\build\helper-win\Release\autorow_test.exe
```

`build.ps1` fetches the same commits, checks the same SHA-256 sums and
applies the same patches (`git apply`). It builds all of it with Visual
Studio, libdof with its own `PLATFORM=win ARCH=x64`. libdof's own Windows
script builds libusb, libserialport, hidapi and libftdi with MSYS2's MinGW;
here libusb and libserialport build from their own Visual Studio projects and
hidapi and libftdi with CMake, so no MSYS2 is needed. libftdi gets
`win/msvc-gettimeofday.h` (its stream code calls `gettimeofday`, which MSVC
lacks; libdof never calls that code) and every function exported (it has no
export list of its own). Not yet run on Windows: if one of those four fails
to build, that is where to look.

| | |
|---|---|
| `src/main.cpp` | the pipe protocol, events to libdof by name, a watchdog for a hung start or finish (Linux and Windows) |
| `src/locate.h` | whose DOF files: VPinball's, the game's own, or on Windows DirectOutput's. No libdof in it |
| `src/autorow.h` | a row made from a Config Tool file's device header, when there is none; a table's own features' names (`--features`). No libdof in it |
| `src/events.h` | the events and their DOF names (`$FlipperLeft`) |
| `tests/autorow_test.cpp` | those rows, from made-up headers |
| `patches/` | what is changed in libdof |
| `win/` | what Windows' build adds to a dependency |
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
