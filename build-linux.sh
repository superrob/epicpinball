#!/bin/sh
# Configure and build the native Linux program, build-linux/epicpinball: the
# lifted C (work/gen) on the DOS machine in src/, with SDL2 for the window,
# sound and pad. setup.sh does this and the lift; this rebuilds after a change.
#
#   ./build-linux.sh               # then: build-linux/epicpinball
#
# Needs gcc, cmake and SDL2's development files (setup.sh lists the packages)
# and the toolkit: PCRECOMP_HOME, else ../pcrecomp.
set -e
cd "$(dirname "$0")"
cmake -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j "$(nproc)"
# where the tables keep what cabinet feedback and the analog plunger use
if [ -d work/gen/ep1 ] && [ -d original ]; then
    python3 tools/feedback_map.py || echo "build-linux.sh: no feedback map; the game itself is fine" >&2
fi
