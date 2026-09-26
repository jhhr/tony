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
# Packages what deploy/android/build-tony.sh built into an APK signed
# with the debug key, which can be installed on a phone by hand:
#
#   build-android/apk/Tony-debug.apk
#
# androiddeployqt reads what to package from a deployment settings file,
# which Qt's CMake support writes for a CMake build; this writes it for
# the meson one (build-android/android-Tony-deployment-settings.json).
# androiddeployqt then adds Qt's libraries and plugins and the NDK's C++
# library to build-android/android-build/, a Gradle project, and runs
# Gradle, which strips the libraries (it knows the NDK's version) and
# signs the APK with ~/.android/debug.keystore, made on its first run.
#
# The manifest is deploy/android/package/AndroidManifest.xml; the icon is
# added from icons/.
#
# The Vamp plugins go in as libpyin.so and libchp.so, beside the
# application's library in lib/arm64-v8a/: Android installs nothing else
# from an APK. They are put there directly rather than named as
# android-extra-libs, which Qt's launcher would load at every start. The
# libraries are packaged the legacy way, compressed and unpacked when the
# app is installed, so that they exist as files for svcore's scan to open;
# main.cpp links the plugins under their own names (see AndroidFiles).
#
# Gradle fetches the Android Gradle plugin and its dependencies from
# Google's repository and Maven Central on its first run, and Maven
# Central has answered 429 Too Many Requests: a Gradle run that fails for
# want of the network is tried again, three times at most. The log is
# /opt/android/logs/tony-apk.log. It ends by checking the APK.
#
# Usage, from anywhere:
#   deploy/android/build-apk.sh

set -eu -o pipefail

if [ "$#" -ne 0 ]; then
    echo "Usage: $0" 1>&2
    exit 2
fi

android=/opt/android
sdk=$android/sdk
ndk=$sdk/ndk/27.2.12479018
build_tools=$sdk/build-tools/36.0.0
platform=android-36
qt=$android/qt/6.11.2
qt_android=$qt/android_arm64_v8a
qt_host=$qt/gcc_64
logs=$android/logs
abi=arm64-v8a

repo=$(cd "$(dirname "$0")/../.." && pwd)
build=$repo/build-android
out=$build/android-build
package=$build/android-package
settings=$build/android-Tony-deployment-settings.json
apk_dir=$build/apk
apk=$apk_dir/Tony-debug.apk

llvm=$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin

export JAVA_HOME=/usr/lib/jvm/java-21-openjdk-$(dpkg --print-architecture)

# Qt's tools warn at every start in the container's C locale
export LC_ALL=C.UTF-8

if [ ! -x "$JAVA_HOME/bin/java" ] || [ ! -d "$build_tools" ] ||
       [ ! -d "$sdk/platforms/$platform" ] || [ ! -f "$ndk/source.properties" ]; then
    echo "ERROR: no JDK 21, SDK or NDK: run deploy/android/setup-toolchain.sh first" 1>&2
    exit 1
fi
if [ ! -x "$qt_host/bin/androiddeployqt" ] || [ ! -d "$qt_android/src/android/templates" ]; then
    echo "ERROR: no Qt for Android at $qt: run deploy/android/build-qt.sh first" 1>&2
    exit 1
fi
for f in libTony_arm64-v8a.so pyin.so chp.so version.h; do
    if [ ! -f "$build/$f" ]; then
        echo "ERROR: no build-android/$f: run deploy/android/build-tony.sh first" 1>&2
        exit 1
    fi
done

mkdir -p "$logs"

# 1. Gather the libraries, the manifest and the icon

echo "Gathering the libraries"

# Emptied first, so that nothing from an earlier run is packaged by
# mistake; androiddeployqt puts Qt's libraries back
rm -rf "$out/libs/$abi"
mkdir -p "$out/libs/$abi"
cp "$build/libTony_arm64-v8a.so" "$out/libs/$abi/"
cp "$build/pyin.so" "$out/libs/$abi/libpyin.so"
cp "$build/chp.so" "$out/libs/$abi/libchp.so"

# Stripped here as Gradle strips them, so that the APK is small whatever
# Gradle does (Qt's libraries come without debug information). Those in
# build-android/ keep theirs, for symbolising a crash with the NDK's
# ndk-stack
for so in libTony_arm64-v8a.so libpyin.so libchp.so; do
    "$llvm/llvm-strip" --strip-unneeded "$out/libs/$abi/$so"
done

rm -rf "$package"
cp -r "$repo/deploy/android/package" "$package"
# 128 pixels is about the 48 dp of a launcher icon at xxhdpi (3x)
mkdir -p "$package/res/drawable-xxhdpi"
cp "$repo/icons/tony-128x128.png" "$package/res/drawable-xxhdpi/icon.png"

# 2. The deployment settings

# The version name says which commit this is, with a + if the working
# tree had changes; the version code grows with each commit, so that a
# newer build installs over an older one
version=$(sed -n 's/^#define TONY_VERSION "\(.*\)"$/\1/p' "$build/version.h")
commit=$(git -C "$repo" rev-parse --short HEAD)
if ! git -C "$repo" diff --quiet HEAD --; then
    commit="$commit+"
fi
version_name="$version ($commit)"
version_code=$(git -C "$repo" rev-list --count HEAD)

# The Qt plugins are those Qt's CMake support chose for an application
# using the same Qt modules (the model in $logs): without the list,
# androiddeployqt takes every plugin of each module, test platforms and
# all
plugins=
for p in platforms/libplugins_platforms_qtforandroid \
         styles/libplugins_styles_qandroidstyle \
         imageformats/libplugins_imageformats_qgif \
         imageformats/libplugins_imageformats_qico \
         imageformats/libplugins_imageformats_qjpeg \
         imageformats/libplugins_imageformats_qsvg \
         iconengines/libplugins_iconengines_qsvgicon \
         networkinformation/libplugins_networkinformation_qandroidnetworkinformation \
         tls/libplugins_tls_qcertonlybackend; do
    f=$qt_android/plugins/${p}_$abi.so
    if [ ! -f "$f" ]; then
        echo "ERROR: no Qt plugin $f" 1>&2
        exit 1
    fi
    plugins="$plugins${plugins:+;}$f"
done

cat > "$settings" <<EOF
{
   "description": "Written by deploy/android/build-apk.sh for androiddeployqt",
   "qt": { "$abi": "$qt_android" },
   "qtDataDirectory": { "$abi": "." },
   "qtLibExecsDirectory": { "$abi": "libexec" },
   "qtLibsDirectory": { "$abi": "lib" },
   "qtPluginsDirectory": { "$abi": "plugins" },
   "qtQmlDirectory": { "$abi": "qml" },
   "sdk": "$sdk",
   "sdkBuildToolsRevision": "$(basename "$build_tools")",
   "ndk": "$ndk",
   "toolchain-prefix": "llvm",
   "tool-prefix": "llvm",
   "useLLVM": true,
   "toolchain-version": "clang",
   "ndk-host": "linux-x86_64",
   "abi": "$abi",
   "architectures": { "$abi": "aarch64-linux-android" },
   "stdcpp-path": "$ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/",
   "application-binary": "Tony",
   "android-app-name": "Tony",
   "android-package-source-directory": "$package",
   "android-version-name": "$version_name",
   "android-version-code": "$version_code",
   "android-min-sdk-version": "28",
   "android-target-sdk-version": "36",
   "android-legacy-packaging": true,
   "android-deploy-plugins": "$plugins",
   "qml-skip-import-scanning": true,
   "rcc-binary": "$qt_host/libexec/rcc",
   "extraPrefixDirs": [ "$qt_android" ],
   "extraLibraryDirs": [ ],
   "zstdCompression": false
}
EOF

# 3. androiddeployqt and Gradle

log=$logs/tony-apk.log
rm -f "$log"
mkdir -p "$apk_dir"
rm -f "$apk"

attempt=1
while true; do
    echo "Packaging (androiddeployqt and Gradle), attempt $attempt"
    echo "=== attempt $attempt" >> "$log"
    if "$qt_host/bin/androiddeployqt" \
           --input "$settings" --output "$out" \
           --android-platform "$platform" --jdk "$JAVA_HOME" \
           --apk "$apk" --verbose >> "$log" 2>&1; then
        break
    fi
    this_attempt=$(sed -n "/^=== attempt $attempt\$/,\$p" "$log")
    if [ "$attempt" -lt 3 ] &&
           echo "$this_attempt" |
               grep -qE "status code 429|Too Many Requests|Connection reset|timed out"; then
        echo "  Gradle was turned away for now; trying again in $attempt minute(s)"
        sleep $((60 * attempt))
        attempt=$((attempt + 1))
        continue
    fi
    echo "$this_attempt" | grep -E "error|FAILED|What went wrong|Could not GET" |
        sort -u | tail -20 1>&2 || true
    refused=$(echo "$this_attempt" | grep "status code 403" |
                  grep -o "https://[^/']*" | sort -u | tr '\n' ' ' || true)
    if [ -n "$refused" ]; then
        echo "ERROR: refused with 403 (is the host allowed on this network?): $refused" 1>&2
    fi
    echo "ERROR: packaging failed; the whole log is $log" 1>&2
    exit 1
done

# 4. Check

echo
echo "Checking $apk"

listing=$(unzip -Z1 "$apk")
for f in lib/$abi/libTony_arm64-v8a.so lib/$abi/libpyin.so lib/$abi/libchp.so \
         lib/$abi/libc++_shared.so lib/$abi/libQt6Core_arm64-v8a.so \
         lib/$abi/libplugins_platforms_qtforandroid_arm64-v8a.so; do
    if ! echo "$listing" | grep -qxF "$f"; then
        echo "ERROR: the APK has no $f" 1>&2
        exit 1
    fi
done
echo "  it holds $(echo "$listing" | grep -c "^lib/$abi/.*\.so$") libraries in lib/$abi/"

# Stripped, and each loading nothing that is neither in the APK nor one
# of Android's own libraries
unpacked=$(mktemp -d)
trap 'rm -rf "$unpacked"' EXIT
unzip -q "$apk" "lib/$abi/*" -d "$unpacked"
system_libraries='lib(c|m|dl|log|z|android|jnigraphics|EGL|GLESv2|GLESv3|vulkan|mediandk)\.so'
for so in "$unpacked/lib/$abi/"*.so; do
    name=$(basename "$so")
    if "$llvm/llvm-readelf" -SW "$so" | grep -q "\.debug_info"; then
        echo "ERROR: $name in the APK is not stripped" 1>&2
        exit 1
    fi
    for needed in $("$llvm/llvm-readelf" -d "$so" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
        if [ ! -f "$unpacked/lib/$abi/$needed" ] && ! echo "$needed" | grep -qxE "$system_libraries"; then
            echo "ERROR: $name loads $needed, which is neither in the APK nor Android's" 1>&2
            exit 1
        fi
    done
done
echo "  they are stripped, and load nothing but each other and Android's libraries"

"$build_tools/apksigner" verify "$apk"
echo "  apksigner: verified"
"$build_tools/zipalign" -c -P 16 4 "$apk"
echo "  zipalign: aligned for 16 KB pages"

badging=$("$build_tools/aapt2" dump badging "$apk")
echo "$badging" | grep -E "^package:|^application-label:|^sdkVersion:|^targetSdkVersion:|^uses-permission:" | sed 's/^/  /'
if ! echo "$badging" | grep -q "name='android.permission.RECORD_AUDIO'"; then
    echo "ERROR: the APK does not ask for RECORD_AUDIO" 1>&2
    exit 1
fi
manifest=$("$build_tools/aapt2" dump xmltree --file AndroidManifest.xml "$apk")
if ! echo "$manifest" | grep -q 'screenOrientation.*=6'; then
    echo "ERROR: the activity is not sensorLandscape" 1>&2
    exit 1
fi
if echo "$manifest" | grep -q 'extractNativeLibs.*=false'; then
    echo "ERROR: the libraries would not be unpacked on install" 1>&2
    exit 1
fi
echo "  landscape, and the libraries are unpacked on install"

cat <<EOF

Done: $apk ($(du -h "$apk" | cut -f1))
EOF
