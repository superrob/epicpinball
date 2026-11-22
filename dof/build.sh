#!/bin/sh
# Build epicpinball-dof, the program that drives a cabinet's solenoids through
# libdof, into dof/build/. GPLv3 like libdof (dof/LICENSE); the game never
# links it, and nothing builds it unless this is run.
#
#   dof/build.sh           # fetch what is missing, then build
#   dof/build.sh --fetch   # only fetch: the rest then needs no network
#
# Fetched, each at a pinned commit and checked by SHA-256: libdof and the four
# libraries its own script pins. libdof gets dof/patches/. Nothing is installed
# outside dof/build.
#
# Needs g++ (C++20), cmake 3.25, autoconf, automake, libtool, patch, curl, libudev.
set -eu
cd "$(dirname "$0")"
HERE=$(pwd)
B=$HERE/build
S=$B/src
L=$B/lib

[ "$(uname -s)-$(uname -m)" = Linux-x86_64 ] || { echo "dof/build.sh: x86-64 Linux only for now" >&2; exit 1; }
for t in g++ cmake autoreconf libtoolize patch curl sha256sum; do
    command -v "$t" > /dev/null || { echo "dof/build.sh: $t is missing" >&2; exit 1; }
done
mkdir -p "$S/dl" "$L"

# name owner/repo commit sha256
fetch() {
    tgz=$S/dl/$1.tar.gz
    if [ ! -f "$tgz" ]; then
        echo "fetching $2 $3"
        curl -fsSL --max-time 180 -o "$tgz.part" "https://codeload.github.com/$2/tar.gz/$3"
        mv "$tgz.part" "$tgz"
    fi
    echo "$4  $tgz" | sha256sum -c --quiet - || {
        echo "dof/build.sh: $tgz is not the pinned archive: delete it and run again" >&2
        exit 1
    }
    if [ ! -d "$S/$1" ]; then
        t=$(mktemp -d "$S/.$1.XXXXXX")
        tar -xzf "$tgz" -C "$t" --strip-components=1 --no-same-owner
        mv "$t" "$S/$1"
    fi
}
fetch libdof vpinball/libdof 3d0ad20a4be185aaa3d426e1abff01f9639a2f62 \
    5fb82f9b46a6e55768dc1254fb9d9e9c2d30374486ce1686317eaa4408b1fb40
fetch libusb libusb/libusb 15a7ebb4d426c5ce196684347d2b7cafad862626 \
    4ae78a9c6c540a3ccd7153c77e24947fa2ba50d845714bbbfccf8783832ed704
fetch libserialport sigrokproject/libserialport 21b3dfe5f68c205be4086469335fd2fc2ce11ed2 \
    66c5eb23811b58938dc97168ec9acb0568dcebb02e3d97dcec30ae22a524eaf6
fetch hidapi libusb/hidapi d6b2a974608dec3b76fb1e36c189f22b9cf3650c \
    a93a9bfb599831b82c6cef50f42359e0f5f31abfc8f6954862c328a464cbc4d0
fetch libftdi jsm174/libftdi 8b138364e5577ec6bf3778283347eaddb83f0429 \
    fbd57dce21ce8408dd00593befc075bf84c64b7c1972a1583b83a09ad5dd5637
[ "${1:-}" = --fetch ] && exit 0

J=$(nproc)
T=$S/libdof/third-party
mkdir -p "$T/include/libusb-1.0" "$T/include/libftdi1"
R=$T/runtime-libs/linux/x64
mkdir -p "$R"

# What libdof's platforms/linux/x64/external.sh does, minus its downloads
if [ ! -f "$S/.libusb-built" ]; then
    ( cd "$S/libusb" && NOCONFIGURE=1 ./autogen.sh && ./configure --enable-shared && make -j"$J" )
    cp "$S/libusb/libusb/libusb.h" "$T/include/libusb-1.0/"
    cp -a "$S"/libusb/libusb/.libs/libusb-1.0.so "$S"/libusb/libusb/.libs/libusb-1.0.so.* "$R/"
    touch "$S/.libusb-built"
fi
if [ ! -f "$S/.libserialport-built" ]; then
    ( cd "$S/libserialport" && ./autogen.sh && ./configure && make -j"$J" )
    cp "$S/libserialport/libserialport.h" "$T/include/"
    cp -a "$S"/libserialport/.libs/libserialport.so "$S"/libserialport/.libs/libserialport.so.* "$R/"
    touch "$S/.libserialport-built"
fi
if [ ! -f "$S/.hidapi-built" ]; then
    ( cd "$S/hidapi" && cmake -DHIDAPI_WITH_LIBUSB=OFF -DCMAKE_BUILD_TYPE=Release -B build && cmake --build build -j "$J" )
    cp -r "$S/hidapi/hidapi" "$T/include/"
    cp -a "$S"/hidapi/build/src/linux/libhidapi-hidraw.so "$S"/hidapi/build/src/linux/libhidapi-hidraw.so.* "$R/"
    touch "$S/.hidapi-built"
fi
if [ ! -f "$S/.libftdi-built" ]; then
    ( cd "$S/libftdi" &&
      cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.10 -DFTDI_EEPROM=OFF -DEXAMPLES=OFF -DSTATICLIBS=OFF \
            -DLIBUSB_INCLUDE_DIR="$S/libusb/libusb" \
            -DLIBUSB_LIBRARIES="$S/libusb/libusb/.libs/libusb-1.0.so" \
            -DCMAKE_INSTALL_RPATH='$$ORIGIN' -DCMAKE_BUILD_WITH_INSTALL_RPATH=TRUE \
            -DCMAKE_BUILD_TYPE=Release -B build &&
      cmake --build build -j "$J" )
    cp "$S/libftdi/src/ftdi.h" "$T/include/libftdi1/"
    cp -a "$S"/libftdi/build/src/libftdi1.so "$S"/libftdi/build/src/libftdi1.so.* "$R/"
    touch "$S/.libftdi-built"
fi
# Named by what is in the patch, so a tree built without it is built again
PATCH=$HERE/patches/pico-output-bound.patch
SUM=$(sha256sum "$PATCH" | cut -c1-16)
if [ ! -f "$S/.libdof-patched-$SUM" ]; then
    patch -d "$S/libdof" -p1 --forward --batch --fuzz=0 --reject-file=- < "$PATCH" || {
        echo "dof/build.sh: $PATCH does not apply to $S/libdof: delete that folder and $S/.libdof-*, and run again" >&2
        exit 1
    }
    touch "$S/.libdof-patched-$SUM"
fi
if [ ! -f "$S/.libdof-built-$SUM" ]; then
    ( cd "$S/libdof" &&
      cmake -DPLATFORM=linux -DARCH=x64 -DCMAKE_BUILD_TYPE=Release -DBUILD_STATIC=OFF -DPOST_BUILD_COPY_EXT_LIBS=OFF -B build &&
      cmake --build build -j "$J" --target dof_shared )
    cp -a "$S"/libdof/build/libdof.so "$S"/libdof/build/libdof.so.* "$L/"
    touch "$S/.libdof-built-$SUM"
fi
cp -a "$R"/*.so "$R"/*.so.* "$L/"

cmake -S "$HERE" -B "$B/helper" -DCMAKE_BUILD_TYPE=Release
cmake --build "$B/helper" -j "$J"
cp "$B/helper/epicpinball-dof" "$B/epicpinball-dof.new"
mv "$B/epicpinball-dof.new" "$B/epicpinball-dof"
echo "built $B/epicpinball-dof"
