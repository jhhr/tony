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
# Runs in a Linux cloud session what the Linux and Android CI workflows
# run (.github/workflows/linux.yml and android.yml), so that a change
# can be checked without them. The Windows workflow's build and suites
# are "build.bat test" on the Windows machine; the macOS one has no
# stand-in.
#
#   linux     Ubuntu's Qt 6.4, not the conda-forge 6.11 of build/: a
#             release build in build-linux-ci/, then every meson test in
#             one process and a list of the tests that failed, as the
#             workflow's configure, make, test and test-failures steps.
#   android   deploy/android/'s five scripts, in the workflow's order;
#             the APK is build-android/apk/Tony-debug.apk. The first run
#             in a session builds Qt for Android and the C libraries into
#             /opt/android, which the environment's snapshot cannot hold.
#   all       both, Linux first.
#
# --quick runs the linux job's tony-core and tony-app as one process per
# core (run-tests.sh), and the svcore suites through meson: about three
# minutes instead of twelve, but the tests that share a process are
# others than on CI (docs/testing.md, "Running").
#
# Where it differs from the workflows: the libraries are
# container-setup.sh's, the GitHub mirrors at the pins, not repoint's;
# meson and Rubber Band are Ubuntu's, not built from tarballs; and
# libopusenc is installed here and not on the Linux runner, so this
# build can write Opus files (HAVE_OPUS_READ_ONLY is not defined).
#
# It first waits for the session's background build (cloud-session.sh),
# which would load the machine while the app suite records in real
# time. The logs are in tmp/ci-local/. The exit status is 0 only if
# every job asked for passed.
#
# Usage, from anywhere:
#   deploy/linux/ci-local.sh [--quick] linux|android|all

set -u -o pipefail

usage() {
    echo "Usage: $0 [--quick] linux|android|all" 1>&2
    exit 2
}

quick=no
if [ "${1:-}" = "--quick" ]; then
    quick=yes
    shift
fi
[ "$#" -eq 1 ] || usage
case "$1" in
    linux|android|all) what=$1 ;;
    *) usage ;;
esac

cd "$(dirname "$0")/../.."
root=$(pwd)
logs=$root/tmp/ci-local
mkdir -p "$logs"

sudo=""
if [ "$(id -u)" -ne 0 ]; then
    sudo=sudo
fi

# Runs a command with its output in a log file; on failure shows the end
# of the log and returns its status
logged() {
    local log="$1"
    shift
    "$@" > "$log" 2>&1
    local status=$?
    if [ "$status" -ne 0 ]; then
        tail -20 "$log" | sed 's/^/    /'
        echo "    FAILED; the whole log is ${log#$root/}"
    fi
    return "$status"
}

minutes() {
    echo "$(( ($1 + 30) / 60 )) min"
}

# The libraries at their pins and the packages. It is quick once the
# session's hook has run it, and needed when it has not
prepare() {
    if [ -f tmp/cloud-session.log ]; then
        echo "Waiting for the session's background build"
        deploy/linux/cloud-session.sh wait > /dev/null 2>&1
    fi
    echo "Libraries and packages (container-setup.sh)"
    logged "$logs/setup.log" deploy/linux/container-setup.sh
}

linux_job() {
    local build=build-linux-ci
    local start=$SECONDS

    echo
    echo "== Linux: Ubuntu's Qt 6.4, a release build in $build/"

    # Ubuntu's Qt, as the workflow installs it (less what nothing here uses)
    local packages="qt6-base-dev qt6-base-dev-tools qt6-svg-dev" missing="" p
    for p in $packages; do
        if ! dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q "install ok installed"; then
            missing="$missing $p"
        fi
    done
    if [ -n "$missing" ]; then
        echo "Installing$missing"
        logged "$logs/linux-packages.log" \
               bash -c "$sudo apt-get update -q && $sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends$missing" ||
            return 1
    fi

    if [ ! -f "$build/build.ninja" ]; then
        echo "Configuring $build/"
        # No pkg-config path of conda's, so that meson finds Ubuntu's Qt.
        # mold as in build/: the linker changes nothing that is tested
        local linker=()
        if command -v mold > /dev/null; then
            linker=(env CC_LD=mold CXX_LD=mold)
        fi
        logged "$logs/linux-setup.log" \
               env -u PKG_CONFIG_PATH "${linker[@]}" \
               meson setup "$build" --buildtype release || return 1
    fi
    local qt
    qt=$(sed -n 's/^Run-time dependency qt6 (modules: [^)]*) found: YES \([0-9.]*\).*/\1/p' \
             "$build/meson-logs/meson-log.txt" | tail -1)
    case "$qt" in
        6.4.*) ;;
        *) echo "    ERROR: $build/ is configured with Qt ${qt:-(not found in its log)}, not Ubuntu's 6.4; delete it and run again"
           return 1 ;;
    esac

    echo "Building (log: tmp/ci-local/linux-build.log)"
    logged "$logs/linux-build.log" ninja -j "$(nproc)" -C "$build" || return 1

    local status=0
    if [ "$quick" = "yes" ]; then
        # One process per core, half of run-tests.sh's default, so that
        # the timed tests run on a machine loaded less (docs/testing.md)
        echo "Testing, $(nproc) processes"
        local exe
        for exe in test-tony-core test-tony-app; do
            deploy/linux/run-tests.sh -j "$(nproc)" "$build" "$exe" \
                > "$logs/linux-$exe.log" 2>&1 < /dev/null
            if [ "$?" -ne 0 ]; then
                status=1
                grep -v ' 0 failed' "$logs/linux-$exe.log" | tail -30 | sed 's/^/    /'
            else
                echo "    $(head -1 "$logs/linux-$exe.log")"
            fi
        done
        logged "$logs/linux-svcore.log" \
               meson test -C "$build" --no-rebuild --print-errorlogs \
               svcore-base svcore-system svcore-data-model svcore-data-fileio ||
            status=1
    else
        echo "Testing: every meson test in one process, as the workflow (log: tmp/ci-local/linux-test.log)"
        meson test -C "$build" --no-rebuild --print-errorlogs --num-processes 1 \
              > "$logs/linux-test.log" 2>&1 < /dev/null
        status=$?
        sed -n '/^Ok:/,/^Timeout:/p' "$logs/linux-test.log" | sed 's/^/    /'
        if [ "$status" -ne 0 ]; then
            # The workflow's test-failures step
            awk '/^test:/ { print; next }
                 /^Totals:/ { if ($0 !~ /, 0 failed/) print; next }
                 /^(FAIL!|XPASS|QFATAL)|Received signal/ { print; fail = 1; next }
                 /^[A-Z!]+ *: / { fail = 0 }
                 fail && /^   / { print }' "$build/meson-logs/testlog.txt" |
                sed 's/^/    /'
        fi
    fi

    echo "Linux: $([ "$status" -eq 0 ] && echo passed || echo FAILED) in $(minutes $((SECONDS - start)))"
    return "$status"
}

android_job() {
    local start=$SECONDS s

    echo
    echo "== Android: the APK, with deploy/android/'s scripts"
    for s in setup-toolchain build-qt build-deps build-tony build-apk; do
        local step=$SECONDS
        echo "$s (log: tmp/ci-local/android-$s.log)"
        logged "$logs/android-$s.log" "deploy/android/$s.sh" < /dev/null || {
            echo "Android: FAILED in $(minutes $((SECONDS - start)))"
            return 1
        }
        echo "    done in $(minutes $((SECONDS - step)))"
    done
    grep -E '^  package:|^Done:' "$logs/android-build-apk.log" | sed 's/^ */    /'
    echo "Android: passed in $(minutes $((SECONDS - start)))"
}

prepare || exit 1

status=0
if [ "$what" = "linux" ] || [ "$what" = "all" ]; then
    linux_job || status=1
fi
if [ "$what" = "android" ] || [ "$what" = "all" ]; then
    android_job || status=1
fi
exit "$status"
