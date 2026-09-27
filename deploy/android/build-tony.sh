#!/bin/bash
#
#   Tony
#   An intonation analysis and annotation tool
#   Centre for Digital Music, Queen Mary, University of London.
#
#   This program is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2 of the
#   License, or (at your option) any later version.  See the file
#   COPYING included with this distribution for more information.
#
# Builds Tony for Android (arm64-v8a, API 28) in the Ubuntu 24.04 cloud
# container, into build-android/ in the repository:
#
#   build-android/libTony_arm64-v8a.so   the application: a shared
#                                        library, which Qt for Android's
#                                        launcher loads to call its main()
#   build-android/pyin.so, chp.so        the Vamp plugins
#
# Run deploy/android/setup-toolchain.sh, build-qt.sh and build-deps.sh
# first. meson gets two cross files: the one build-deps.sh writes, for
# the NDK and the libraries, and deploy/android/qt-arm64-v8a.ini, for
# Qt. The build type is the desktop builds' (build.bat, container-setup.sh):
# optimised, with asserts and debug information. The libraries here keep
# that information, for symbolising a crash from the phone; the copies
# in an APK are to be stripped.
#
# The build directory is configured once; after that ninja configures it
# again by itself when meson.build or a cross file changes. --wipe
# configures it from scratch, for a new NDK or Qt. The logs are
# /opt/android/logs/tony-setup.log and tony-build.log. It ends by checking
# the application library: an arm64 shared object, aligned for 16 KB
# pages, exporting main(), and loading nothing but Android's system
# libraries, the NDK's C++ library and Qt's; and the plugins likewise,
# exporting their Vamp entry point.
#
# Usage, from anywhere:
#   deploy/android/build-tony.sh [--wipe]

set -eu -o pipefail

wipe=no
if [ "$#" -eq 1 ] && [ "$1" = "--wipe" ]; then
    wipe=yes
elif [ "$#" -ne 0 ]; then
    echo "Usage: $0 [--wipe]" 1>&2
    exit 2
fi

android=/opt/android
ndk=$android/sdk/ndk/27.2.12479018
logs=$android/logs
cross=$android/cross-arm64-v8a.ini

repo=$(cd "$(dirname "$0")/../.." && pwd)
qt_cross=$repo/deploy/android/qt-arm64-v8a.ini
build=$repo/build-android

readelf=$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf

jobs=$(nproc)

# Qt's qmake, which meson runs, warns at every start in the container's
# C locale
export LC_ALL=C.UTF-8

if [ ! -f "$cross" ]; then
    echo "ERROR: no $cross: run deploy/android/build-deps.sh first" 1>&2
    exit 1
fi
qmake=$(sed -n "s/^qmake6 = '\(.*\)'$/\1/p" "$qt_cross")
if [ ! -x "$qmake" ]; then
    echo "ERROR: no Qt for Android at $qmake: run deploy/android/build-qt.sh first" 1>&2
    exit 1
fi

mkdir -p "$logs"

# 1. Configure

setup_args=(--buildtype=debugoptimized --cross-file "$cross" --cross-file "$qt_cross")

log=$logs/tony-setup.log
if [ -f "$build/build.ninja" ] && [ "$wipe" = "no" ]; then
    echo "build-android/: configured already"
else
    if [ -f "$build/build.ninja" ]; then
        echo "Configuring build-android/ from scratch"
        setup_args+=(--wipe)
    else
        echo "Configuring build-android/"
    fi
    if ! meson setup "$build" "$repo" "${setup_args[@]}" > "$log" 2>&1; then
        tail -30 "$log" 1>&2
        echo "ERROR: meson setup failed; the whole log is $log" 1>&2
        exit 1
    fi
fi

# 2. Build: the application library and the plugins, which on Android
# are all there is (meson.build leaves the tests out)

echo "Building"
log=$logs/tony-build.log
if ! ninja -j "$jobs" -C "$build" > "$log" 2>&1; then
    if grep -q "error:" "$log"; then
        grep "error:" "$log" | head -30 1>&2
    else
        tail -30 "$log" 1>&2
    fi
    echo "ERROR: the build failed; the whole log is $log" 1>&2
    exit 1
fi

# 3. Check

echo
echo "Checking"

# check <library> <symbol it must export> <the libraries it may load>
check() {
    local so="$build/$1" symbol="$2" allowed="$3"
    local kind needed align
    kind=$(file -b "$so")
    needed=$("$readelf" -d "$so" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | tr '\n' ' ')
    align=$("$readelf" -lW "$so" | awk '$1 == "LOAD" { print $NF }' | sort -u | tr '\n' ' ')
    echo "  $1: $(echo "$kind" | cut -d, -f1-2)"
    echo "    it loads: $needed"
    case "$kind" in
        "ELF 64-bit LSB shared object, ARM aarch64"*) ;;
        *) echo "ERROR: not an arm64 shared object" 1>&2; exit 1 ;;
    esac
    for lib in $needed; do
        if ! echo "$lib" | grep -qxE "$allowed"; then
            echo "ERROR: it loads $lib, which is not among the libraries it may load" 1>&2
            exit 1
        fi
    done
    if [ "$align" != "0x4000 " ]; then
        echo "ERROR: its segments are not aligned for 16 KB pages" 1>&2
        exit 1
    fi
    if ! "$readelf" --dyn-syms -W "$so" |
            awk -v s="$symbol" '$8 == s && $5 == "GLOBAL" && $6 == "DEFAULT" && $7 != "UND" { found = 1 }
                                END { exit !found }'; then
        echo "ERROR: it does not export $symbol" 1>&2
        exit 1
    fi
    echo "    it exports $symbol"
}

system_libraries='lib(c|m|dl|log|z|android|mediandk|c\+\+_shared)\.so'

check libTony_arm64-v8a.so main "$system_libraries|libQt6[A-Za-z]+_arm64-v8a\.so"
check pyin.so vampGetPluginDescriptor "$system_libraries"
check chp.so vampGetPluginDescriptor "$system_libraries"

cat <<EOF

Done:
  application: $build/libTony_arm64-v8a.so
  plugins:     $build/pyin.so, $build/chp.so
EOF
