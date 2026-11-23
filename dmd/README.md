# epicpinball-dmd

The program that puts Epic Pinball's 128x32 DMD picture on a real DMD
(ZeDMD, Pixelcade, PinDMD3) through
[libdmdutil](https://github.com/vpinball/libdmdutil). The picture is the
game's own ([docs/dmd.md](../docs/dmd.md)); this only hands it on.

**Licence: GPL-3.0-or-later** ([LICENSE](LICENSE)), because it links
libdmdutil. Everything outside this folder is MIT. The game does not link this
program or libdmdutil; it starts it and sends it frames. Nothing builds it
unless you run `build.sh`.

```sh
dmd/build.sh           # fetch what is missing, build into dmd/build/
dmd/build.sh --fetch   # fetch only
```

| | |
|---|---|
| `src/main.cpp` | frames in (a socket pair of packets on stdin), libdmdutil out |

Pinned: libdmdutil `6b8f711` (0.14.0), libzedmd `ed69d49` (0.11.1), and
libzedmd's cargs, libserialport, libframeutil and sockpp, each checked by
SHA-256. Serum, VNI, PUP and PIN2DMD are not built.
