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
# Runs a test executable as several processes at once, each running its
# shard of every suite (TONY_TEST_SHARD, main/test/RunSuite.h), and adds
# up what they report. The app suite spends nearly all of its time
# waiting on FakeAudioIO, which plays in real time: on the cloud
# container's 4 cores, 8 processes run it in about a minute instead of
# six, and the load stays under 2.
#
# Each process has a log directory and XDG directories of its own. The
# suites keep QSettings per user, and processes sharing the file would
# read each other's settings. On Windows QSettings is the registry, which
# XDG_CONFIG_HOME does not move: this is for Linux.
#
# Usage, from anywhere:
#   deploy/linux/run-tests.sh [-j N] [BUILD_DIR] EXECUTABLE
#
#   deploy/linux/run-tests.sh test-tony-app
#
# N defaults to twice the number of cores, BUILD_DIR to build. The
# results are in tmp/tl/EXECUTABLE/SHARD/SUITE.txt; the summary gives
# each suite's counts, less initTestCase and cleanupTestCase, which
# every shard runs, and every failure with its location. The exit status
# is 0 only if every shard's was.

set -u -o pipefail

jobs=$(( $(nproc) * 2 ))
if [ "${1:-}" = "-j" ]; then
    jobs=${2:-}
    shift 2
fi
root=$(cd "$(dirname "$0")/../.." && pwd)
case "$#" in
    1) build=$root/build; exe=$1 ;;
    2) build=$1; exe=$2 ;;
    *) echo "Usage: $0 [-j N] [BUILD_DIR] EXECUTABLE" 1>&2; exit 2 ;;
esac
if ! [ "$jobs" -ge 1 ] 2>/dev/null; then
    echo "Usage: $0 [-j N] [BUILD_DIR] EXECUTABLE" 1>&2
    exit 2
fi

# BUILD_DIR as given, from where this was run
if ! dir=$(cd "$build" 2>/dev/null && pwd); then
    echo "No build directory $build" 1>&2
    exit 2
fi
build=$dir
if [ ! -x "$build/$exe" ]; then
    echo "No executable $build/$exe" 1>&2
    exit 2
fi

out=$root/tmp/tl/$exe
rm -rf "$out"
mkdir -p "$out"

start=$SECONDS
for i in $(seq 0 $((jobs - 1))); do
    dir=$out/$i
    mkdir -p "$dir/xdg/config" "$dir/xdg/data" "$dir/xdg/cache"
    (
        cd "$build" &&
            TONY_TEST_SHARD=$i/$jobs TONY_TEST_LOG_DIR=$dir \
            XDG_CONFIG_HOME=$dir/xdg/config XDG_DATA_HOME=$dir/xdg/data \
            XDG_CACHE_HOME=$dir/xdg/cache \
            "./$exe" > "$dir/stdout.log" 2>&1
        echo $? > "$dir/exit"
    ) &
done
wait
echo "$exe: $jobs processes, $((SECONDS - start)) s"

status=0
for i in $(seq 0 $((jobs - 1))); do
    code=$(cat "$out/$i/exit" 2>/dev/null || echo "none")
    if [ "$code" != 0 ]; then
        status=1
    fi
    # 1 is failed tests, which the summary lists; anything else is a crash
    if [ "$code" != 0 ] && [ "$code" != 1 ]; then
        echo "Process $i ended with status $code: see $out/$i/stdout.log"
        for f in "$out/$i"/*.txt; do
            [ -f "$f" ] || continue
            if ! grep -aq "^Totals" "$f"; then
                echo "  it was running $(basename "$f" .txt): last line $(grep -a '^[A-Z]' "$f" | tail -1)"
            fi
        done
    fi
done

for suite in $(ls "$out"/*/*.txt 2>/dev/null | xargs -n1 basename | sort -u); do
    cat "$out"/*/"$suite" | awk -v suite="${suite%.txt}" '
        /^(PASS|XFAIL) / && !/::(initTestCase|cleanupTestCase)\(\)/ { passed++ }
        /^(FAIL!|XPASS) / { failed++ }
        /^SKIP / { skipped++ }
        END { printf "%s: %d passed, %d failed, %d skipped\n", suite, passed, failed, skipped }'
done

grep -a "^FAIL!\|^XPASS\|^   Loc" "$out"/*/*.txt | sed "s#^$out/##"

exit $status
