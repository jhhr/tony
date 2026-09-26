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
# Builds Tony in the background at the start of a cloud session, so that
# the build runs while the session reads and edits instead of after: the
# library directories at their pins and build/ configured
# (container-setup.sh), then Tony, the pYIN plugin and the three test
# executables.
#
# The environment's setup script (cloud-environment.sh) has installed the
# packages and Qt already and filled ccache with the libraries' objects,
# so this takes a few minutes at most, and ninja carries on from there
# when the session builds again. The build runs at low priority, so that
# the session's own commands come first.
#
# build/ links with mold when it is installed: the four executables link
# in about a second instead of about twelve with GNU ld, which every
# change to main/ pays.
#
# Usage, from anywhere:
#   deploy/linux/cloud-session.sh start   start the build and return at
#                                         once; nothing if it is running
#   deploy/linux/cloud-session.sh wait    wait for it to finish, show the
#                                         end of its log, exit as it did
#
# "start --if-cloud" does nothing outside a cloud session, for a
# SessionStart hook. The log is tmp/cloud-session.log.

set -u -o pipefail

cd "$(dirname "$0")/../.."
root=$(pwd)
mkdir -p tmp
log=$root/tmp/cloud-session.log
lock=$root/tmp/cloud-session.lock
script=$root/deploy/linux/cloud-session.sh

targets="tony pyin.so test-tony-core test-tony-app test-tony-device"

case "${1:-} ${2:-}" in
    "start "|"start --if-cloud")
        if [ "${2:-}" = "--if-cloud" ] && [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
            exit 0
        fi
        if ! flock -n "$lock" true; then
            echo "The background build is running already (log: tmp/cloud-session.log)."
            exit 0
        fi
        # Detached, so that it outlives the command that started it
        setsid flock -n "$lock" "$script" run < /dev/null > /dev/null 2>&1 &
        # Until it holds the lock, so that a wait straight after this waits for it
        for i in $(seq 50); do
            flock -n "$lock" true || break
            sleep 0.1
        done
        echo "Started the background build: libraries, build/, then $targets (log: tmp/cloud-session.log)."
        echo "Run deploy/linux/cloud-session.sh wait before building or testing: two ninjas must not share build/."
        ;;
    "wait ")
        if [ ! -f "$log" ]; then
            echo "No background build was started (deploy/linux/cloud-session.sh start)."
            exit 1
        fi
        flock "$lock" true
        tail -5 "$log"
        status=$(sed -n 's/^exit:\([0-9]*\)$/\1/p' "$log" | tail -1)
        exit "${status:-1}"
        ;;
    "run ")
        # Holding the lock, from start
        exec > "$log" 2>&1
        echo "Started $(date -u '+%Y-%m-%d %H:%M:%S') UTC"
        start=$SECONDS
        if command -v mold > /dev/null; then
            # Read by meson setup only: a build/ configured already keeps its linker
            export CC_LD=mold CXX_LD=mold
        fi
        nice -n 10 deploy/linux/container-setup.sh
        status=$?
        if [ "$status" -eq 0 ]; then
            echo
            echo "Building $targets"
            nice -n 10 ninja -j "$(nproc)" -C build $targets
            status=$?
        fi
        echo "Finished in $((SECONDS - start)) s"
        echo "exit:$status"
        ;;
    *)
        echo "Usage: $0 start [--if-cloud] | wait" 1>&2
        exit 2
        ;;
esac
