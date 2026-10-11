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
# Builds pyin_cli from a directory of pYIN sources, so that each
# variant of them gets a runner of its own.
#
# Usage: build.sh <pyin source dir> <output binary> [extra source files...]
#
# The Vamp SDK comes from the vamp-plugin-sdk/ checkout next to
# Tony's sources (the TONY_ROOT environment variable, or two levels
# up from here).

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=${TONY_ROOT:-$(cd "$here/../../.." && pwd)}
src=$1
out=$2
shift 2

sdk=$root/vamp-plugin-sdk
if [ ! -d "$sdk/vamp-sdk" ]; then
    echo "No Vamp SDK at $sdk (set TONY_ROOT to a Tony checkout with its libraries)" 1>&2
    exit 1
fi

# The fork's harmonic salience, which upstream's pYIN does not have
fork=()
if [ -f "$src/HarmonicSalience.cpp" ]; then
    fork=("$src"/HarmonicSalience.cpp)
fi

c++ -O2 -std=c++17 -w \
    -I"$src" -I"$sdk" \
    "$here/pyin_cli.cpp" \
    "$src"/YinUtil.cpp "$src"/Yin.cpp "$src"/SparseHMM.cpp "$src"/MonoPitchHMM.cpp \
    "$src"/MonoNoteParameters.cpp "$src"/MonoNoteHMM.cpp "$src"/MonoNote.cpp \
    "$src"/PYinVamp.cpp ${fork[@]+"${fork[@]}"} \
    "$sdk"/src/vamp-sdk/FFT.cpp "$sdk"/src/vamp-sdk/RealTime.cpp \
    "$@" \
    -lsndfile -o "$out"
