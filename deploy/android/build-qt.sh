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
# Builds Qt for Android and the host Qt it needs, from source, in the
# Ubuntu 24.04 cloud container:
#
#   /opt/android/qt/6.11.2/gcc_64              host Qt: moc, rcc, uic,
#                                              qmake, androiddeployqt,
#                                              and the CMake packages
#                                              for them
#   /opt/android/qt/6.11.2/android_arm64_v8a   qtbase and qtsvg for
#                                              arm64-v8a, API 28
#
# the layout of Qt's own installers. Run deploy/android/setup-toolchain.sh
# first, for the SDK, the NDK, the JDK and the compilers.
#
# From source, because Qt's binary packages cannot be fetched in the
# container: download.qt.io answers every archive request with a
# redirect to a mirror, and the container's proxy denies all of Qt's
# mirrors and master.qt.io. The sources are Qt's own repositories on
# GitHub (github.com/qt/qtbase and qtsvg), whose newest 6.11 tag is
# 6.11.2: the version the desktop build uses too (conda-forge).
#
# The host Qt is built only for its tools, so it leaves out what they
# do not need (SQL, printing, D-Bus, OpenGL, ICU) and uses Qt's bundled
# third-party libraries, to depend on nothing the container may lack.
# Qt for Android leaves out SQL and printing, which Tony does not use,
# and has no OpenSSL: Qt Network works, without TLS.
#
# Takes about 20 minutes on 4 cores; the logs are kept in
# /opt/android/logs. Safe to run again: a Qt that is installed at this
# version already is left alone.
# It ends by checking the result: qmake's view of both Qts, a small
# Widgets and Svg program built for arm64-v8a with qt-cmake, and
# androiddeployqt starting.
#
# Usage, from anywhere:
#   deploy/android/build-qt.sh

set -eu -o pipefail

if [ "$#" -ne 0 ]; then
    echo "Usage: $0" 1>&2
    exit 2
fi

android=/opt/android
sdk=$android/sdk
ndk=$sdk/ndk/27.2.12479018
logs=$android/logs

qt_version=6.11.2
qtbase_commit=ef55f427f2c8b410d34f8a7681020a3000cf6866
qtsvg_commit=17ca512f903f935282ebeca496aac5d11ba4199a

qt=$android/qt/$qt_version
qt_host=$qt/gcc_64
qt_android=$qt/android_arm64_v8a

jobs=$(nproc)

export JAVA_HOME=/usr/lib/jvm/java-21-openjdk-$(dpkg --print-architecture)

# Qt's tools warn at every start in the container's C locale
export LC_ALL=C.UTF-8

if [ ! -x "$JAVA_HOME/bin/javac" ] || [ ! -f "$ndk/source.properties" ] ||
       [ ! -d "$sdk/platforms/android-36" ]; then
    echo "ERROR: no JDK 21, NDK or SDK platform: run deploy/android/setup-toolchain.sh first" 1>&2
    exit 1
fi

mkdir -p "$logs"
work=$(mktemp -d "$android/tmp.XXXXXX")
trap 'rm -rf "$work"' EXIT

# Runs a command with its output in a log file, and shows the end of
# the log if it fails
logged() {
    local log="$1"
    shift
    if ! "$@" >> "$log" 2>&1; then
        tail -30 "$log" 1>&2
        echo "ERROR: failed; the whole log is $log" 1>&2
        exit 1
    fi
}

fetch() {
    local name="$1" commit="$2"
    if [ ! -d "$work/$name" ]; then
        echo "  fetching $name $qt_version"
        git clone -q -c advice.detachedHead=false --depth 1 \
            --branch "v$qt_version" "https://github.com/qt/$name.git" "$work/$name"
        if [ "$(git -C "$work/$name" rev-parse HEAD)" != "$commit" ]; then
            echo "ERROR: tag v$qt_version of $name is not at $commit" 1>&2
            exit 1
        fi
    fi
}

qt_at() {
    if [ -x "$1/bin/qmake" ]; then
        "$1/bin/qmake" -query QT_VERSION 2>/dev/null || true
    fi
}

# Configures, builds and installs one Qt module from source directory
# $3, in build directory $2, logging to $1; the rest are the
# configure arguments
build_module() {
    local log="$1" build="$2" configure="$3"
    shift 3
    rm -f "$log"
    mkdir -p "$build"
    (cd "$build" && logged "$log" "$configure" "$@")
    logged "$log" cmake --build "$build" --parallel "$jobs"
    logged "$log" cmake --install "$build"
    rm -rf "$build"
}

# 1. The host Qt

echo
if [ "$(qt_at "$qt_host")" = "$qt_version" ]; then
    echo "Host Qt $qt_version: in $qt_host already"
else
    echo "Building host Qt $qt_version in $qt_host (log: $logs/qt-host.log)"
    start=$SECONDS
    fetch qtbase "$qtbase_commit"
    rm -rf "$qt_host"
    build_module "$logs/qt-host.log" "$work/build-host" "$work/qtbase/configure" \
        -prefix "$qt_host" -release -nomake tests -nomake examples \
        -qt-zlib -qt-pcre -qt-doubleconversion -qt-freetype -qt-harfbuzz \
        -qt-libpng -qt-libjpeg -no-icu -no-glib -no-dbus -no-opengl \
        -no-xcb -no-gtk -no-feature-sql -no-feature-printsupport
    echo "  done in $(( (SECONDS - start) / 60 )) min"
fi

# 2. Qt for Android: qtbase, then qtsvg on top of it

echo
if [ "$(qt_at "$qt_android")" = "$qt_version" ] &&
       [ -f "$qt_android/lib/libQt6Core_arm64-v8a.so" ]; then
    echo "Qt $qt_version for Android: in $qt_android already"
else
    echo "Building Qt $qt_version for Android in $qt_android (log: $logs/qt-android.log)"
    start=$SECONDS
    fetch qtbase "$qtbase_commit"
    rm -rf "$qt_android"
    build_module "$logs/qt-android.log" "$work/build-android" "$work/qtbase/configure" \
        -platform android-clang -prefix "$qt_android" \
        -android-sdk "$sdk" -android-ndk "$ndk" -android-abis arm64-v8a \
        -qt-host-path "$qt_host" -release -nomake tests -nomake examples \
        -no-feature-sql -no-feature-printsupport
    echo "  done in $(( (SECONDS - start) / 60 )) min"
fi

echo
if [ -f "$qt_android/lib/libQt6Svg_arm64-v8a.so" ]; then
    echo "Qt Svg for Android: in $qt_android already"
else
    echo "Building Qt Svg $qt_version for Android (log: $logs/qtsvg-android.log)"
    fetch qtsvg "$qtsvg_commit"
    build_module "$logs/qtsvg-android.log" "$work/build-svg" \
        "$qt_android/bin/qt-configure-module" "$work/qtsvg"
fi

# 3. Check. qt-cmake configures and builds a small program using the
# modules Tony uses, as the Android build will: this needs Qt for
# Android, the host Qt, the NDK and the SDK to agree.

echo
echo "Checking"
echo "  host Qt:    $("$qt_host/bin/qmake" -query QT_VERSION) at $("$qt_host/bin/qmake" -query QT_INSTALL_PREFIX)"
echo "  Android Qt: $("$qt_android/bin/qmake" -query QT_VERSION) at $("$qt_android/bin/qmake" -query QT_INSTALL_PREFIX), host prefix $("$qt_android/bin/qmake" -query QT_HOST_PREFIX)"

check=$work/check
mkdir -p "$check/src"
cat > "$check/src/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.22)
project(check LANGUAGES CXX)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Xml Network Svg Test)
qt_add_executable(check check.cpp)
target_link_libraries(check PRIVATE Qt6::Widgets Qt6::Xml Qt6::Network Qt6::Svg)
EOF
cat > "$check/src/check.cpp" <<'EOF'
#include <QApplication>
#include <QDomDocument>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QSvgRenderer>
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QDomDocument doc;
    QNetworkAccessManager network;
    QSvgRenderer svg;
    QLabel label("Tony");
    label.show();
    return app.exec();
}
EOF
log=$logs/qt-check.log
rm -f "$log"
logged "$log" "$qt_android/bin/qt-cmake" -S "$check/src" -B "$check/build" -G Ninja \
       -DCMAKE_BUILD_TYPE=Release -DANDROID_SDK_ROOT="$sdk" -DANDROID_NDK_ROOT="$ndk"
# The library only: the default target goes on to run androiddeployqt
# and Gradle, which is the APK build's business
logged "$log" cmake --build "$check/build" --target check
echo "  qt-cmake:   $(file -b "$check/build/libcheck_arm64-v8a.so" | cut -d, -f1-2)"
# The deployment settings Qt's CMake support wrote, kept as a model for
# the file Tony's build has to write for androiddeployqt
cp "$check/build/android-check-deployment-settings.json" "$logs/"

# It prints its usage and exits with 1
log=$logs/androiddeployqt-help.log
"$qt_host/bin/androiddeployqt" --help > "$log" 2>&1 || true
if ! grep -q -- "--output <destination>" "$log"; then
    cat "$log" 1>&2
    echo "ERROR: androiddeployqt does not run" 1>&2
    exit 1
fi
echo "  androiddeployqt: runs"

cat <<EOF

Done:
  Qt for Android: $qt_android
  host Qt:        $qt_host
Next: deploy/android/build-deps.sh
EOF
