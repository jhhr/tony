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
# The setup of the Claude cloud environment Tony is developed in. The
# environment's "Setup script" field holds cloud-setup-script.sh, which
# runs this file as it is on default: the field keeps only about the
# first 6000 characters of what is pasted there, and this is longer.
#
# The platform runs it as root after cloning the repository, the first
# time a session starts after the script or the network settings change,
# or after the environment's cache expires (about a week). It then keeps
# a snapshot of the disk, and later sessions start from that snapshot
# without running the script. The snapshot is kept only when the script
# finishes within about five minutes, so the script stops its open-ended
# steps at $deadline seconds.
#
# What it leaves in the snapshot:
#
# - The apt packages that container-setup.sh and
#   deploy/android/setup-toolchain.sh install, and ccache and mold.
# - Qt 6.11.2 from conda-forge in /opt/qt6-conda, where
#   container-setup.sh looks for it.
# - /etc/ccache.conf. meson uses ccache by itself when it is installed.
# - An autoMode entry in /root/.claude/settings.json by which auto mode
#   trusts the library forks as it does the session's own repository.
# - The Android SDK and NDK in /opt/android/sdk, where
#   deploy/android/setup-toolchain.sh installs them, when dl.google.com is
#   reachable.
# - ccache filled with as much of a build of the libraries as fits before
#   the deadline, svcore first: the checkout is set up with its own
#   container-setup.sh, built into build/, and left as it was cloned.
#   The libraries' objects do not depend on main/, so they are found in
#   ccache by every branch whose pins are the same.
#
# Only a failure to install the packages or Qt is worth a warning; the
# script ends with status 0 whatever happens, because a failing setup
# script stops the session from starting, and a session can run
# container-setup.sh itself. The logs are in /var/log/tony-environment.

set -u -o pipefail

# Seconds from the start after which nothing more is started, leaving a
# minute for the platform
deadline=240

logs=/var/log/tony-environment
mkdir -p "$logs"

say() {
    echo "[${SECONDS}s] $*"
}

left() {
    echo $((deadline - SECONDS))
}

# timeout, with what is left before the deadline; "timeout 0" would wait
# for ever
until_deadline() {
    local t
    t=$(left)
    if [ "$t" -le 0 ]; then
        return 124
    fi
    timeout "$t" "$@"
}

# 1. ccache. meson passes relative source paths, but -g puts the build
# directory into every result's key unless hash_dir is off, and then a
# second build directory or a worktree finds nothing.

cat > /etc/ccache.conf <<'EOF'
# Written by Tony's cloud environment setup script
cache_dir = /root/.cache/ccache
max_size = 8G
hash_dir = false
base_dir = /home/user
EOF

# 2. Auto mode's trust, as the user chose it: the library forks are the
# user's own repositories. Out of the box auto mode trusts only the
# repository a session started in and its remotes, and blocks committing
# in a fork's checkout, attaching the fork and pushing to it
# (docs/forks.md). It reads autoMode from the user's settings, never
# from the repository's .claude/settings.json, and combines them with
# the platform's own (--settings); `claude auto-mode config` shows the
# result.

TONY_AUTOMODE_SETTINGS=${TONY_AUTOMODE_SETTINGS:-/root/.claude/settings.json} python3 - <<'EOF'
import json, os
path = os.environ["TONY_AUTOMODE_SETTINGS"]
entries = [
    "Trusted repo: besides the working repository github.com/jhhr/tony, the user's own "
    "forks of its libraries, github.com/jhhr/svcore, github.com/jhhr/svgui, "
    "github.com/jhhr/svapp and github.com/jhhr/bqaudiostream, checked out inside the "
    "working directory as svcore/, svgui/, svapp/ and bqaudiostream/",
    "Source control: github.com/jhhr/tony and those four forks. Committing in the fork "
    "checkouts, attaching the forks to the session with push access and pushing branches "
    "to them is routine work on Tony (docs/forks.md)",
]
settings = {}
if os.path.exists(path):
    with open(path) as f:
        settings = json.load(f)
environment = settings.setdefault("autoMode", {}).setdefault("environment", [])
if not environment:
    environment.append("$defaults")
for entry in entries:
    if entry not in environment:
        environment.append(entry)
os.makedirs(os.path.dirname(path), exist_ok=True)
with open(path, "w") as f:
    json.dump(settings, f, indent=2)
    f.write("\n")
EOF

# 3. Packages, Qt and the Android SDK, side by side

packages="
build-essential pkg-config ninja-build meson git python3 curl ca-certificates
libboost-dev libbz2-dev libfftw3-dev libsndfile1-dev libsamplerate0-dev
librubberband-dev libsord-dev libserd-dev liboggz2-dev libfishsound1-dev
libmad0-dev libid3tag0-dev libopus-dev libopusfile-dev libopusenc-dev
libjack-jackd2-dev libpulse-dev libasound2-dev portaudio19-dev
fonts-dejavu-core ccache mold
openjdk-21-jdk-headless unzip xz-utils patch file cmake
"

install_packages() {
    # The image's PPAs are blocked; apt-get update warns and carries on
    apt-get update -q
    DEBIAN_FRONTEND=noninteractive apt-get install -y -q \
        --no-install-recommends -o Acquire::Retries=3 $packages
}

install_qt() {
    local micromamba=/opt/micromamba/bin/micromamba
    if [ "$(PKG_CONFIG_PATH=/opt/qt6-conda/lib/pkgconfig pkg-config --modversion Qt6Core 2>/dev/null)" = 6.11.2 ]; then
        echo "Qt 6.11.2 is installed already"
        return 0
    fi
    mkdir -p "$(dirname "$micromamba")"
    curl -sSfL --retry 5 --retry-all-errors --max-time 60 -o "$micromamba" \
         https://github.com/mamba-org/micromamba-releases/releases/download/2.9.0-0/micromamba-linux-64 &&
        chmod +x "$micromamba" &&
        MAMBA_ROOT_PREFIX=/opt/micromamba "$micromamba" create -y -q \
            -p /opt/qt6-conda -c conda-forge qt6-main=6.11.2 || return 1
    # Only Qt's own .pc files, as container-setup.sh makes them
    mkdir -p /opt/qt6-conda/tony-pkgconfig
    ln -sf /opt/qt6-conda/lib/pkgconfig/Qt6*.pc /opt/qt6-conda/tony-pkgconfig/
}

# The versions and checks of deploy/android/setup-toolchain.sh, which
# finds these installed and installs anything it wants that is not
install_android() {
    local sdk=/opt/android/sdk
    local build=15859902
    local sha256=4e4c464f145a7512b57d088ac6c278c03c9eea610886b35a5e0804e74eedf583
    local zip=/opt/android/cmdline-tools.zip
    if [ -f "$sdk/ndk/27.2.12479018/source.properties" ]; then
        echo "The Android SDK and NDK are installed already"
        return 0
    fi
    if ! curl -sSf -o /dev/null --max-time 20 \
         https://dl.google.com/android/repository/repository2-3.xml; then
        echo "dl.google.com is not reachable: no Android SDK"
        return 0
    fi
    mkdir -p "$sdk/cmdline-tools"
    until_deadline curl -sSfL --retry 5 --retry-all-errors -o "$zip" \
         "https://dl.google.com/android/repository/commandlinetools-linux-${build}_latest.zip" &&
        echo "$sha256  $zip" | sha256sum -c --quiet &&
        rm -rf "$sdk/cmdline-tools/latest" /opt/android/cmdline-tools &&
        unzip -q "$zip" -d /opt/android &&
        mv /opt/android/cmdline-tools "$sdk/cmdline-tools/latest" || return 1
    rm -f "$zip"
    # sdkmanager is Java: the proxy has to be named to it unless
    # JAVA_TOOL_OPTIONS does it already
    local proxy=()
    local hostport=${HTTPS_PROXY:-}
    hostport=${hostport#*://}
    hostport=${hostport%%/*}
    if [ -n "$hostport" ]; then
        proxy=(--proxy=http "--proxy_host=${hostport%:*}" "--proxy_port=${hostport##*:}")
    fi
    local sdkmanager=$sdk/cmdline-tools/latest/bin/sdkmanager
    export JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64
    { yes || true; } | "$sdkmanager" --sdk_root="$sdk" "${proxy[@]}" --licenses > /dev/null
    until_deadline "$sdkmanager" --sdk_root="$sdk" "${proxy[@]}" --install \
        "platforms;android-36" "build-tools;36.0.0" platform-tools "ndk;27.2.12479018"
}

say "Installing packages, Qt and the Android SDK (logs in $logs)"
install_packages > "$logs/packages.log" 2>&1 &
packages_pid=$!
install_qt > "$logs/qt.log" 2>&1 &
qt_pid=$!
install_android > "$logs/android.log" 2>&1 &
android_pid=$!

wait "$packages_pid"
packages_status=$?
say "Packages: exit $packages_status"
wait "$qt_pid"
qt_status=$?
say "Qt: exit $qt_status"

# 4. ccache, from a build of the libraries in the checkout, until the
# deadline

fill_ccache() {
    local repo="" d
    for d in "$PWD" /home/user/*; do
        if [ -f "$d/repoint-project.json" ] && [ -x "$d/deploy/linux/container-setup.sh" ]; then
            repo=$d
            break
        fi
    done
    if [ -z "$repo" ]; then
        echo "No checkout with deploy/linux/container-setup.sh"
        return 0
    fi
    echo "Checkout: $repo"

    # What container-setup.sh adds to the checkout, to take away again
    local added=()
    for d in build $(python3 -c 'import json; print(" ".join(json.load(open("'"$repo"'/repoint-project.json"))["libraries"]))'); do
        [ -e "$repo/$d" ] || added+=("$repo/$d")
    done

    CC_LD=mold CXX_LD=mold until_deadline "$repo/deploy/linux/container-setup.sh" &&
        (
            cd "$repo/build" || exit 1
            # svcore, then svgui and svapp (in libtonyapp.a with main/),
            # then the rest; each step until the deadline
            svlibs=$(ninja -t targets all | sed -n 's/^\(libtonyapp\.a\.p\/sv\(gui\|app\)_[^:]*\.o\):.*/\1/p')
            for step in libsvcore.a "$svlibs" "tony pyin.so test-tony-core test-tony-app test-tony-dev test-tony-device"; do
                [ -n "$step" ] || continue
                [ "$(left)" -gt 10 ] || break
                until_deadline nice ninja -j "$(nproc)" $step > /dev/null
                echo "Built up to: ${step:0:40}...: exit $?"
            done
        )
    ccache -s | grep -iE 'hits|misses|cache size' || true

    rm -rf "${added[@]}"
    git -C "$repo" status --short
}

if [ "$packages_status" -eq 0 ] && [ "$qt_status" -eq 0 ]; then
    say "Filling ccache until ${deadline}s"
    fill_ccache > "$logs/ccache.log" 2>&1
    say "ccache: $(ccache -s | grep -a 'Cache size' | head -1 | sed 's/^ *//')"
else
    say "WARNING: packages or Qt failed; see $logs. Sessions will install them with container-setup.sh."
fi

wait "$android_pid"
android_status=$?
if [ -f /opt/android/sdk/ndk/27.2.12479018/source.properties ]; then
    say "Android SDK: exit $android_status, the SDK and NDK are in /opt/android/sdk"
else
    say "Android SDK: exit $android_status ($(tail -1 "$logs/android.log"))"
fi

say "Done"
exit 0
