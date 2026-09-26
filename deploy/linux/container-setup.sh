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
# Makes a fresh Ubuntu 24.04 cloud container able to build Tony and run
# both test suites: installs the apt packages and Qt, checks out the
# library directories at the revisions pinned in repoint-lock.json, and
# configures build/ with meson.
#
# It exists because "./repoint install" cannot work in the cloud
# container: the Mercurial libraries are on hg.sr.ht, which cannot be
# reached from there. Their Git mirrors on GitHub are
# checked out instead, at the commits in the table below. The Git
# libraries are cloned from GitHub at their pins, as repoint would.
# sv-dependency-builds is skipped: only the macOS and Windows branches
# of meson.build use it.
#
# Qt is conda-forge's qt6-main, not Ubuntu's Qt 6.4. Tony builds with
# 6.4, but its analysis never completes there: Analyser connects by
# SIGNAL()/SLOT() strings naming ModelId and sv_frame_t to slots that
# moc records as sv::ModelId and sv::sv_frame_t, and only Qt 6.5 and
# later match those by their registered metatypes rather than by name.
# The development machine and the Android build use Qt 6.11.
#
# Safe to run again. Packages already installed, a Qt of the right
# version and libraries already at their pins are left alone, and a
# library with local changes or local commits is never moved.
#
# Usage, from anywhere:
#   deploy/linux/container-setup.sh           set up and configure build/
#   deploy/linux/container-setup.sh --build   the same, then build Tony,
#                                             the pYIN plugin and both
#                                             test executables

set -eu -o pipefail

build=no
case "${1:-}" in
    "") ;;
    --build) build=yes ;;
    *) echo "Usage: $0 [--build]" 1>&2; exit 2 ;;
esac

cd "$(dirname "$0")/../.."
root=$(pwd)
echo "Setting up $root"

sudo=""
if [ "$(id -u)" -ne 0 ]; then
    sudo=sudo
fi

qt_version=6.11.2
qt_prefix=/opt/qt6-conda
micromamba_version=2.9.0-0
micromamba=/opt/micromamba/bin/micromamba

# Only Qt's own .pc files, so that pkg-config finds everything else
# (alsa, for one, which the conda prefix has too) on the system
qt_pkgconfig=$qt_prefix/tony-pkgconfig

# 1. Packages. The names are those of .github/workflows/linux.yml where
# meson.build needs them, as spelled on Ubuntu 24.04, less Qt. Rubber
# Band is Ubuntu's librubberband-dev (3.3), which meets meson.build's
# ">= 3.0.0", so the CI's tarball from breakfastquay.com (blocked here
# too) is not needed.

packages="
build-essential pkg-config ninja-build meson git python3 curl ca-certificates
libboost-dev libbz2-dev libfftw3-dev libsndfile1-dev libsamplerate0-dev
librubberband-dev libsord-dev libserd-dev liboggz2-dev libfishsound1-dev
libmad0-dev libid3tag0-dev libopus-dev libopusfile-dev libopusenc-dev
libjack-jackd2-dev libpulse-dev libasound2-dev portaudio19-dev
fonts-dejavu-core
"

missing=""
for p in $packages; do
    if ! dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q "install ok installed"; then
        missing="$missing $p"
    fi
done

if [ -n "$missing" ]; then
    echo
    echo "Installing packages:$missing"
    # Some of the container's own apt sources (PPAs) are blocked too;
    # apt-get update warns about them and carries on.
    $sudo apt-get update -q
    $sudo env DEBIAN_FRONTEND=noninteractive \
          apt-get install -y -q --no-install-recommends $missing
else
    echo
    echo "Packages: all installed"
fi

# 2. Qt, from conda-forge through micromamba (both from hosts the
# container can reach: GitHub releases and conda.anaconda.org).

echo
if [ -x "$micromamba" ]; then
    echo "micromamba: installed already"
else
    echo "Installing micromamba $micromamba_version in $(dirname "$micromamba")"
    $sudo mkdir -p "$(dirname "$micromamba")"
    # The container's proxy now and then answers 502 for a moment
    $sudo curl -sSfL --retry 5 --retry-all-errors -o "$micromamba.part" \
          "https://github.com/mamba-org/micromamba-releases/releases/download/$micromamba_version/micromamba-linux-64"
    $sudo chmod +x "$micromamba.part"
    $sudo mv "$micromamba.part" "$micromamba"
fi

installed_qt=""
if [ -f "$qt_prefix/lib/pkgconfig/Qt6Core.pc" ]; then
    installed_qt=$(PKG_CONFIG_PATH="$qt_prefix/lib/pkgconfig" pkg-config --modversion Qt6Core)
fi

if [ "$installed_qt" = "$qt_version" ]; then
    echo "Qt: $qt_version in $qt_prefix already"
else
    echo "Installing Qt $qt_version (conda-forge qt6-main) in $qt_prefix"
    if [ -d "$qt_prefix/conda-meta" ]; then
        action=install
    else
        action=create
    fi
    $sudo env MAMBA_ROOT_PREFIX="$(dirname "$(dirname "$micromamba")")" \
          "$micromamba" $action -y -q -p "$qt_prefix" -c conda-forge \
          "qt6-main=$qt_version"
fi

$sudo mkdir -p "$qt_pkgconfig"
for pc in "$qt_prefix"/lib/pkgconfig/Qt6*.pc; do
    $sudo ln -sf "$pc" "$qt_pkgconfig/"
done

# 3. Libraries.
#
# repoint-lock.json pins the Mercurial libraries by Mercurial hash,
# which their Git mirrors (github.com/breakfastquay/<name>) do not
# carry, and a conversion back to Mercurial does not reproduce it. Each
# pin was matched to a mirror commit by hand, by date and message:
#
# - The pins were taken by upstream Tony's "Update Repoint
#   locations and revisions" (2024-06-25). For each, the commit is the
#   last one on the mirror's master before that date, and Sonic
#   Visualiser's repoint-lock.json took the same Mercurial pin shortly
#   after that commit's date:
#     dataquay        "Fix warning", 2024-01-04 (SV pinned it the same
#                     day; the mirror's head is newer, from 2024-09)
#     bqvec           "Adjust local include policy", 2023-06-27 (head)
#     bqfft           "Update CI for SLEEF", 2022-08-09 (head)
#     bqresample      "Adjust local include policy", 2023-06-27 (head)
#     bqthingfactory  "Copyright dates", 2021-01-08 (head)
#
# When a pin in repoint-lock.json changes, this table must change with
# it; the script stops if they disagree.

mirror_commit() {
    case "$1 $2" in
        "dataquay 79623fb778da")       echo 2dbf1bed112c1a7eaaf43335abbe5ddb5c03d0ff ;;
        "bqvec 291cde50db9d")          echo ddfcd1716576c6bb44218c5f5696bf24a248960a ;;
        "bqfft d41a117b8cbe")          echo 68dc4c5735c1e0da099e8473fc4562acf2895cb8 ;;
        "bqresample 38c3e524416a")     echo 6cef06961f15399f8cecc414f1364dac7d83c3db ;;
        "bqthingfactory 2e4bd170f57f") echo 8468b3f98d9d1768561d6f1fc95d1689af193a3e ;;
        *) echo "" ;;
    esac
}

warnings=0

checkout() {
    local name="$1" url="$2" branch="$3" commit="$4"
    local short="${commit:0:12}"
    if [ -e "$name/.git" ]; then
        local head
        head=$(git -C "$name" rev-parse HEAD)
        if [ "$head" = "$commit" ]; then
            echo "  $name: at $short already"
            return
        fi
        if [ -n "$(git -C "$name" status --porcelain --untracked-files=no)" ]; then
            echo "  $name: WARNING: has local changes and is not at $short; left alone"
            warnings=1
            return
        fi
        # A library that has moved, as bqaudioio did from its mirror to
        # the fork, is fetched from where it is now
        if [ "$(git -C "$name" remote get-url origin 2>/dev/null)" != "$url" ]; then
            echo "  $name: origin is now $url"
            git -C "$name" remote set-url origin "$url"
        fi
        if ! git -C "$name" cat-file -e "$commit^{commit}" 2>/dev/null; then
            echo "  $name: fetching from $url"
            git -C "$name" fetch -q origin
        fi
        # Local commits that are on no remote branch would be lost from
        # sight by moving the checkout
        if [ -z "$(git -C "$name" branch -r --contains HEAD)" ]; then
            echo "  $name: WARNING: has local commits and is not at $short; left alone"
            warnings=1
            return
        fi
        # Detached, so that no local branch is moved
        git -C "$name" checkout -q --detach "$commit"
    elif [ -e "$name" ] && [ -n "$(ls -A "$name")" ]; then
        echo "ERROR: $name exists but is not a Git checkout; move it away and run again" 1>&2
        exit 1
    else
        echo "  $name: cloning $url"
        git clone -q ${branch:+--branch "$branch"} "$url" "$name"
        # As repoint does: the local branch at the pin, where there is one
        if [ -n "$branch" ]; then
            git -C "$name" checkout -q -B "$branch" "$commit"
        else
            git -C "$name" checkout -q --detach "$commit"
        fi
    fi
    echo "  $name: checked out $short"
}

echo
echo "Libraries:"

# One line per library: name, vcs, owner, repository, branch, pin
libraries=$(python3 - <<'EOF'
import json
project = json.load(open("repoint-project.json"))["libraries"]
lock = json.load(open("repoint-lock.json"))["libraries"]
for name, lib in project.items():
    print(name, lib["vcs"], lib["owner"], lib.get("repository", name.split("/")[-1]),
          lib.get("branch", "-"), lock[name]["pin"])
EOF
)

while read -r name vcs owner repository branch pin; do
    [ "$branch" = "-" ] && branch=""
    if [ "$name" = "sv-dependency-builds" ]; then
        echo "  $name: skipped, not used on Linux"
        continue
    fi
    if [ "$vcs" = "git" ]; then
        checkout "$name" "https://github.com/$owner/$repository.git" "$branch" "$pin"
    else
        commit=$(mirror_commit "$name" "$pin")
        if [ -z "$commit" ]; then
            echo "ERROR: repoint-lock.json pins $name at $pin, which this script does not map to a commit of its Git mirror; update the table in $0" 1>&2
            exit 1
        fi
        checkout "$name" "https://github.com/$owner/$repository.git" "" "$commit"
    fi
done <<< "$libraries"

# 4. Configure. The same build type as the Windows build (build.bat):
# optimised, with asserts. meson keeps the pkg-config path in build/, so
# the reconfigures ninja runs by itself find the same Qt, and it puts
# Qt's library directory in the executables' RPATH.

setup_args="--buildtype=debugoptimized --pkg-config-path=$qt_pkgconfig"

echo
if [ -f build/build.ninja ] &&
       grep -q "$qt_pkgconfig" build/meson-info/intro-buildoptions.json; then
    echo "build/: configured already"
elif [ -f build/build.ninja ]; then
    echo "build/ is configured with another Qt: configuring it again from scratch"
    meson setup --wipe build $setup_args
else
    echo "Configuring build/"
    meson setup build $setup_args
fi

if [ "$build" = "yes" ]; then
    echo
    echo "Building"
    ninja -j 4 -C build tony pyin.so test-tony-core test-tony-app
fi

echo
if [ "$warnings" -ne 0 ]; then
    echo "Done, with warnings above"
else
    echo "Done"
fi
