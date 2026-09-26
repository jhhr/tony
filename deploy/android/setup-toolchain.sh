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
# Installs the Android SDK, the NDK and JDK 21 for Tony's Android build
# in the Ubuntu 24.04 cloud container, with the apt packages that this
# script, build-qt.sh and build-deps.sh need:
#
#   /opt/android/sdk                       command-line tools, platform
#                                          android-36, build tools 36.0.0,
#                                          platform tools
#   /opt/android/sdk/ndk/27.2.12479018     NDK r27c, where Gradle looks
#                                          for it
#   /usr/lib/jvm/java-21-openjdk-amd64     JDK 21, from apt
#
# The versions are those Qt 6.11 documents for Android
# (docs/port-android.md, "Qt for Android"). Then run, in this order,
# deploy/android/build-qt.sh and deploy/android/build-deps.sh.
#
# Safe to run again: what is installed already is left alone.
#
# Usage, from anywhere:
#   deploy/android/setup-toolchain.sh

set -eu -o pipefail

if [ "$#" -ne 0 ]; then
    echo "Usage: $0" 1>&2
    exit 2
fi

sudo=""
if [ "$(id -u)" -ne 0 ]; then
    sudo=sudo
fi

android=/opt/android
sdk=$android/sdk

# The command-line tools 22.0: the last release whose sdkmanager is
# the Java one. In 23.0 sdkmanager became a wrapper around a new
# "android" tool, with other package names. The SHA-1 is the one
# Google's repository2-3.xml gives; the SHA-256 was recorded here.
cmdline_tools_build=15859902
cmdline_tools_sha1=040d3996a65543d22ec4bf73e4c37aa37a8d4af4
cmdline_tools_sha256=4e4c464f145a7512b57d088ac6c278c03c9eea610886b35a5e0804e74eedf583

ndk_version=27.2.12479018
ndk=$sdk/ndk/$ndk_version

# sdkmanager takes the latest revision of each of these, and checks
# the SHA-1 of what it downloads. The NDK's path names its exact version.
sdk_packages="platforms;android-36 build-tools;36.0.0 platform-tools ndk;$ndk_version"

# 1. Packages: the JDK, the host compiler and build tools for Qt and
# the libraries, and "file" for the checks. The JDK is found by its
# path, not by /usr/bin/java, which another JDK may own.

jdk_package=openjdk-21-jdk-headless
java_home=/usr/lib/jvm/java-21-openjdk-$(dpkg --print-architecture)

packages="
$jdk_package curl ca-certificates unzip xz-utils git patch file
build-essential pkg-config cmake ninja-build meson
"

missing=""
for p in $packages; do
    if ! dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q "install ok installed"; then
        missing="$missing $p"
    fi
done

if [ -n "$missing" ]; then
    echo "Installing packages:$missing"
    # Some of the container's own apt sources (PPAs) are blocked;
    # apt-get update warns about them and carries on.
    $sudo apt-get update -q
    $sudo env DEBIAN_FRONTEND=noninteractive \
          apt-get install -y -q --no-install-recommends $missing
else
    echo "Packages: all installed"
fi

if [ ! -x "$java_home/bin/javac" ]; then
    echo "ERROR: no JDK 21 at $java_home" 1>&2
    exit 1
fi
export JAVA_HOME=$java_home

# Everything below runs as the user who owns /opt/android
if [ ! -d "$android" ]; then
    $sudo mkdir -p "$android"
    $sudo chown "$(id -u):$(id -g)" "$android"
fi
work=$(mktemp -d "$android/tmp.XXXXXX")
trap 'rm -rf "$work"' EXIT

# 2. The SDK's command-line tools, in the layout sdkmanager expects:
# sdk/cmdline-tools/latest.

sdkmanager=$sdk/cmdline-tools/latest/bin/sdkmanager

echo
if [ -x "$sdkmanager" ]; then
    echo "SDK command-line tools: installed already"
else
    echo "Installing the SDK command-line tools ($cmdline_tools_build) in $sdk"
    zip=$work/cmdline-tools.zip
    # The container's proxy now and then answers 502 for a moment
    curl -sSfL --retry 5 --retry-all-errors -o "$zip" \
         "https://dl.google.com/android/repository/commandlinetools-linux-${cmdline_tools_build}_latest.zip"
    echo "$cmdline_tools_sha1  $zip" | sha1sum -c --quiet
    echo "$cmdline_tools_sha256  $zip" | sha256sum -c --quiet
    unzip -q "$zip" -d "$work"
    rm "$zip"
    mkdir -p "$sdk/cmdline-tools"
    rm -rf "$sdk/cmdline-tools/latest"
    mv "$work/cmdline-tools" "$sdk/cmdline-tools/latest"
fi

# 3. SDK packages and the NDK. Installing them means accepting the
# SDK's licences, as every unattended Android build does.

missing=""
for p in $sdk_packages; do
    if [ ! -f "$sdk/${p//;//}/source.properties" ]; then
        missing="$missing $p"
    fi
done

echo
if [ -n "$missing" ]; then
    echo "Installing SDK packages:$missing"
    log=$work/sdkmanager.log
    { yes || true; } | "$sdkmanager" --sdk_root="$sdk" --licenses > "$log" 2>&1 ||
        { tail -20 "$log" 1>&2; exit 1; }
    "$sdkmanager" --sdk_root="$sdk" --install $missing >> "$log" 2>&1 ||
        { tail -20 "$log" 1>&2; exit 1; }
    for p in $missing; do
        if [ ! -f "$sdk/${p//;//}/source.properties" ]; then
            tail -20 "$log" 1>&2
            echo "ERROR: sdkmanager did not install $p" 1>&2
            exit 1
        fi
    done
else
    echo "SDK packages: all installed"
fi

if ! grep -q "^Pkg.Revision = $ndk_version\$" "$ndk/source.properties"; then
    echo "ERROR: $ndk is not NDK $ndk_version" 1>&2
    exit 1
fi

cat <<EOF

Done:
  JAVA_HOME=$JAVA_HOME
  ANDROID_SDK_ROOT=$sdk
  ANDROID_NDK_ROOT=$ndk
Next: deploy/android/build-qt.sh, then deploy/android/build-deps.sh
EOF
