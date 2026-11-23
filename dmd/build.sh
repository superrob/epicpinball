#!/bin/sh
# Build epicpinball-dmd, the program that shows the game's DMD picture on a
# real DMD (ZeDMD, Pixelcade, PinDMD3) through libdmdutil, into dmd/build/.
# GPLv3 like libdmdutil (dmd/LICENSE); the game never links it, and nothing
# builds it unless this is run.
#
#   dmd/build.sh           # fetch what is missing, then build
#   dmd/build.sh --fetch   # only fetch: the rest then needs no network
#
# Fetched, each at a pinned commit and checked by SHA-256: libdmdutil, the
# libzedmd its own script pins, and the four libraries libzedmd's pins.
# Serum, VNI, PUP and PIN2DMD are left out (colourisation and libusb): the
# game's picture is already in colour. Nothing is installed outside dmd/build.
#
# Needs g++ (C++20), cmake 3.25, autoconf, automake, libtool, curl.
set -eu
cd "$(dirname "$0")"
HERE=$(pwd)
B=$HERE/build
S=$B/src
L=$B/lib

[ "$(uname -s)-$(uname -m)" = Linux-x86_64 ] || { echo "dmd/build.sh: x86-64 Linux only for now" >&2; exit 1; }
for t in g++ cmake autoreconf libtoolize curl sha256sum; do
    command -v "$t" > /dev/null || { echo "dmd/build.sh: $t is missing" >&2; exit 1; }
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
        echo "dmd/build.sh: $tgz is not the pinned archive: delete it and run again" >&2
        exit 1
    }
    if [ ! -d "$S/$1" ]; then
        t=$(mktemp -d "$S/.$1.XXXXXX")
        tar -xzf "$tgz" -C "$t" --strip-components=1 --no-same-owner
        mv "$t" "$S/$1"
    fi
}
fetch libdmdutil vpinball/libdmdutil 6b8f711a5346cf2e70562872ebc1a57b210906df \
    9c3589e173a8c882f6fcb22ceba6accc05762afc22652f971e9b7750f7b0283a
fetch libzedmd PPUC/libzedmd ed69d499d87d4d328bd328773cc6bbd3dadd64c3 \
    9a278ce2bd08b0fab79d791c815ce5f2cade04bf18bccf6689a02c5a874137bb
fetch cargs likle/cargs 0698c3f90333446d0fc2745c1e9ce10dd4a9497a \
    c948f6d3a4f347ed6440d474a82d1a5ce7201c5e6c87df1f3fd9399581a0914a
fetch libserialport sigrokproject/libserialport 21b3dfe5f68c205be4086469335fd2fc2ce11ed2 \
    66c5eb23811b58938dc97168ec9acb0568dcebb02e3d97dcec30ae22a524eaf6
fetch libframeutil ppuc/libframeutil 711ab21dcf9ad33d8db8339dfd3b1548bcc34cdb \
    3dd0809ace8353fbd1f1b070f4665461ed5acae11578808a0112a0676c811dfe
fetch sockpp fpagliughi/sockpp e6c4688a576d95f42dd7628cefe68092f6c5cd0f \
    fabb73eb4a86a3314b0639740e1d77073775d8c98960654b0e9f2e4474695491
[ "${1:-}" = --fetch ] && exit 0

J=$(nproc)
OFF="-DENABLE_SERUM=OFF -DENABLE_VNI=OFF -DENABLE_PUP=OFF -DENABLE_PIN2DMD=OFF"

# What libzedmd's platforms/linux/x64/external.sh does, minus its downloads
Z=$S/libzedmd/third-party
mkdir -p "$Z/include" "$Z/build-libs/linux/x64" "$Z/runtime-libs/linux/x64"
if [ ! -f "$S/.cargs-built" ]; then
    ( cd "$S/cargs" && cmake -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release -B build && cmake --build build -j "$J" )
    cp "$S/cargs/include/cargs.h" "$Z/include/"
    cp "$S/cargs/build/libcargs.so" "$Z/runtime-libs/linux/x64/"
    touch "$S/.cargs-built"
fi
if [ ! -f "$S/.libserialport-built" ]; then
    ( cd "$S/libserialport" && ./autogen.sh && ./configure && make -j"$J" )
    cp "$S/libserialport/libserialport.h" "$Z/include/"
    cp "$S/libserialport/.libs/libserialport.a" "$Z/build-libs/linux/x64/"
    cp -a "$S"/libserialport/.libs/libserialport.so "$S"/libserialport/.libs/libserialport.so.* "$Z/runtime-libs/linux/x64/"
    touch "$S/.libserialport-built"
fi
cp "$S"/libframeutil/include/* "$Z/include/"
if [ ! -f "$S/.sockpp-built" ]; then
    ( cd "$S/sockpp" && cmake -DCMAKE_BUILD_TYPE=Release -B build && cmake --build build -j "$J" )
    cp -r "$S/sockpp/include/sockpp" "$Z/include/"
    cp -a "$S"/sockpp/build/libsockpp.so "$S"/sockpp/build/libsockpp.so.* "$Z/runtime-libs/linux/x64/"
    touch "$S/.sockpp-built"
fi
if [ ! -f "$S/.libzedmd-built" ]; then
    ( cd "$S/libzedmd" &&
      cmake -DPLATFORM=linux -DARCH=x64 -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -DPOST_BUILD_COPY_EXT_LIBS=OFF \
            -DCMAKE_BUILD_TYPE=Release -B build &&
      cmake --build build -j "$J" --target zedmd_shared )
    touch "$S/.libzedmd-built"
fi

# What libdmdutil's platforms/linux/x64/external.sh does with libzedmd, minus its downloads
T=$S/libdmdutil/third-party
R=$T/runtime-libs/linux/x64
mkdir -p "$T/include" "$R"
cp "$S/libzedmd/src/ZeDMD.h" "$Z/include/libserialport.h" "$Z/include/cargs.h" "$Z/include/FrameUtil.h" "$T/include/"
cp -r "$Z/include/komihash" "$Z/include/sockpp" "$T/include/"
cp "$Z/runtime-libs/linux/x64/libcargs.so" "$R/"
cp -a "$Z"/runtime-libs/linux/x64/libserialport.so* "$Z"/runtime-libs/linux/x64/libsockpp.so* "$R/"
cp -a "$S"/libzedmd/build/libzedmd.so "$S"/libzedmd/build/libzedmd.so.* "$R/"
if [ ! -f "$S/.libdmdutil-built" ]; then
    ( cd "$S/libdmdutil" &&
      cmake -DPLATFORM=linux -DARCH=x64 -DBUILD_SHARED=ON -DBUILD_STATIC=OFF -DPOST_BUILD_COPY_EXT_LIBS=OFF $OFF \
            -DCMAKE_BUILD_TYPE=Release -B build &&
      cmake --build build -j "$J" --target dmdutil_shared )
    touch "$S/.libdmdutil-built"
fi
cp -a "$R"/*.so "$R"/*.so.* "$L/" 2>/dev/null || cp -a "$R"/*.so "$L/"
cp -a "$S"/libdmdutil/build/libdmdutil.so "$S"/libdmdutil/build/libdmdutil.so.* "$L/"

cmake -S "$HERE" -B "$B/helper" -DCMAKE_BUILD_TYPE=Release
cmake --build "$B/helper" -j "$J"
cp "$B/helper/epicpinball-dmd" "$B/epicpinball-dmd.new"
mv "$B/epicpinball-dmd.new" "$B/epicpinball-dmd"
echo "built $B/epicpinball-dmd"
