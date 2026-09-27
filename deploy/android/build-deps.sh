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
# Cross-compiles the libraries Tony links for Android (arm64-v8a, API
# 28) with NDK r27c, in the Ubuntu 24.04 cloud container:
#
#   /opt/android/deps-arm64-v8a        the libraries: include/, lib/ and
#                                      lib/pkgconfig/
#   /opt/android/cross-arm64-v8a.ini   the meson cross file for the NDK,
#                                      used here and for Tony itself
#
# Run deploy/android/setup-toolchain.sh first, for the NDK and the
# build tools.
#
# All are static libraries of position-independent code, so that they
# end up inside Tony's own shared library and add no .so files to the
# APK. When Tony links them through pkg-config it needs their private
# dependencies too: meson's dependency(..., static: true) asks for them.
#
# What Tony needs, from meson.build's Linux branch and the library
# directories:
#   libsndfile     WAV, for reading and for writing takes; without its
#                  codec libraries (FLAC, Vorbis, Opus, MPEG)
#   libsamplerate  bqresample: playback and takes at another rate
#   fftw3          bqfft; double precision only, as meson.build defines
#                  FFTW_DOUBLE_ONLY on every platform
#   Rubber Band 3  playback at another speed (svapp), with its built-in
#                  FFT and resampler
#   serd, sord     dataquay's RDF store, with zix, which sord needs
#   libmad         MP3 reading; with libid3tag and the NDK's zlib
#   libid3tag      MP3 tags
#   libogg, opus,  Opus reading, read-only (HAVE_OPUS_READ_ONLY; no
#   opusfile       libopusenc); opusfile without its HTTP support
#   bzip2          svcore's BZipFileDevice, which includes bzlib.h
#                  whatever the defines say; the NDK has no bzip2
#   Boost          headers only: pYIN's boost/math
#   Oboe           Google's audio library over AAudio and OpenSL ES, for
#                  Tony's audio backend (main/OboeAudioIO)
# Left out, as on a phone they have no use: oggz and fishsound (Ogg
# Vorbis), JACK, PulseAudio, ALSA and PortAudio, liblo (never enabled).
#
# Sources: release tarballs from GitHub or from Ubuntu's archive, whose
# pool keeps a file unchanged for good, each checked against the SHA-256
# recorded below; Rubber Band is its Git tag, checked against the
# commit. (xiph.org, fftw.org, codeberg.org, download.drobilla.net and
# breakfastquay.com, the upstream sites, cannot be reached from the
# container.)
#
# Safe to run again. A library whose stamp in <prefix>/share/tony-deps
# names the version below is left alone; delete the stamp to build it
# again. The cross file is written every time. It ends by building a
# small shared library that uses every library, found through
# pkg-config with the cross file as Tony's build will, and checking
# that it is an arm64 shared object with none of them left to load.
#
# Usage, from anywhere:
#   deploy/android/build-deps.sh

set -eu -o pipefail

if [ "$#" -ne 0 ]; then
    echo "Usage: $0" 1>&2
    exit 2
fi

android=/opt/android
ndk=$android/sdk/ndk/27.2.12479018
logs=$android/logs

abi=arm64-v8a
api=28
triple=aarch64-linux-android
prefix=$android/deps-$abi
cross=$android/cross-$abi.ini

toolchain=$ndk/toolchains/llvm/prebuilt/linux-x86_64
cc=$toolchain/bin/$triple$api-clang
cxx=$toolchain/bin/$triple$api-clang++

jobs=$(nproc)

ubuntu=https://archive.ubuntu.com/ubuntu/pool
github=https://github.com

if [ ! -x "$cc" ]; then
    echo "ERROR: no NDK at $ndk: run deploy/android/setup-toolchain.sh first" 1>&2
    exit 1
fi

mkdir -p "$logs" "$prefix/share/tony-deps"
work=$(mktemp -d "$android/tmp.XXXXXX")
trap 'rm -rf "$work"' EXIT

# 1. The cross file. c_args and link args apply to what is built for
# Android. The page size is for Android 15's 16 KB pages, which NDK r27
# does not set by itself. There is no sys_root: meson would prefix it
# to every path in the prefix's .pc files. boost_root keeps meson's
# Boost lookup out of the build machine's /usr.

cat > "$cross" <<EOF
# Meson cross file for Android $abi, API $api, NDK r27c.
# Written by deploy/android/build-deps.sh: edit that, not this.

[constants]
toolchain = '$toolchain'
prefix = '$prefix'

[binaries]
c = toolchain / 'bin/$triple$api-clang'
cpp = toolchain / 'bin/$triple$api-clang++'
ar = toolchain / 'bin/llvm-ar'
nm = toolchain / 'bin/llvm-nm'
ranlib = toolchain / 'bin/llvm-ranlib'
strip = toolchain / 'bin/llvm-strip'
objcopy = toolchain / 'bin/llvm-objcopy'
pkg-config = '$(command -v pkg-config)'

[built-in options]
c_args = ['-fPIC']
cpp_args = ['-fPIC']
c_link_args = ['-Wl,-z,max-page-size=16384']
cpp_link_args = ['-Wl,-z,max-page-size=16384']

[properties]
pkg_config_libdir = prefix / 'lib/pkgconfig'
boost_root = prefix

[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
EOF
echo "Wrote $cross"

# 2. How each kind of library is fetched and built. These run in a
# subshell with -e, with their output in the library's log.

# Downloads and unpacks a tarball; sets src to the directory in it
fetch() {
    local name="$1" url="$2" sha256="$3"
    local archive
    archive=$work/$(basename "$url")
    # The container's proxy now and then answers 502 for a moment
    curl -sSfL --retry 5 --retry-all-errors -o "$archive" "$url"
    echo "$sha256  $archive" | sha256sum -c --quiet
    mkdir "$work/$name"
    tar xf "$archive" -C "$work/$name"
    rm "$archive"
    src=$(echo "$work/$name"/*)
}

autotools() {
    local src="$1"
    shift
    cd "$src"
    CC="$cc" CXX="$cxx" AR="$toolchain/bin/llvm-ar" \
      RANLIB="$toolchain/bin/llvm-ranlib" NM="$toolchain/bin/llvm-nm" \
      STRIP="$toolchain/bin/llvm-strip" \
      CFLAGS="-O2 -fPIC" CXXFLAGS="-O2 -fPIC" \
      PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig" PKG_CONFIG_PATH="" \
      ./configure --host="$triple" --prefix="$prefix" --libdir="$prefix/lib" \
      --enable-static --disable-shared --with-pic "$@"
    make -j"$jobs"
    make install
}

cmake_build() {
    local src="$1"
    shift
    cmake -S "$src" -B "$src/build" -G Ninja \
          -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
          -DANDROID_ABI="$abi" -DANDROID_PLATFORM="android-$api" \
          -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_INSTALL_LIBDIR=lib \
          -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
          -DCMAKE_FIND_ROOT_PATH="$prefix" "$@"
    cmake --build "$src/build" --parallel "$jobs"
    cmake --install "$src/build"
}

meson_build() {
    local src="$1"
    shift
    meson setup "$src/build" "$src" --cross-file "$cross" \
          --prefix "$prefix" --libdir lib --buildtype release \
          --default-library static --wrap-mode nodownload "$@"
    ninja -C "$src/build" -j "$jobs"
    meson install -C "$src/build" --no-rebuild
}

# 3. The libraries, in the order they need each other

build_bzip2() {
    fetch bzip2 "$ubuntu/main/b/bzip2/bzip2_1.0.8.orig.tar.gz" \
          ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
    make -C "$src" -j"$jobs" libbz2.a CC="$cc" AR="$toolchain/bin/llvm-ar" \
         RANLIB="$toolchain/bin/llvm-ranlib" \
         CFLAGS="-O2 -fPIC -D_FILE_OFFSET_BITS=64 -Wall -Winline"
    install -D -m 644 "$src/bzlib.h" "$prefix/include/bzlib.h"
    install -D -m 644 "$src/libbz2.a" "$prefix/lib/libbz2.a"
    # bzip2's Makefile writes no .pc file
    mkdir -p "$prefix/lib/pkgconfig"
    cat > "$prefix/lib/pkgconfig/bzip2.pc" <<EOF
prefix=$prefix
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: bzip2
Description: Lossless, block-sorting data compression
Version: 1.0.8
Libs: -L\${libdir} -lbz2
Cflags: -I\${includedir}
EOF
}

build_ogg() {
    fetch ogg "$github/xiph/ogg/releases/download/v1.3.5/libogg-1.3.5.tar.xz" \
          c4d91be36fc8e54deae7575241e03f4211eb102afb3fc0775fbbc1b740016705
    autotools "$src"
}

build_opus() {
    fetch opus "$github/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz" \
          65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
    autotools "$src" --disable-extra-programs --disable-doc
}

build_opusfile() {
    fetch opusfile "$github/xiph/opusfile/releases/download/v0.12/opusfile-0.12.tar.gz" \
          118d8601c12dd6a44f52423e68ca9083cc9f2bfe72da7a8c1acb22a80ae3550b
    autotools "$src" --disable-http --disable-examples --disable-doc
}

build_sndfile() {
    fetch sndfile "$github/libsndfile/libsndfile/releases/download/1.2.2/libsndfile-1.2.2.tar.xz" \
          3799ca9924d3125038880367bf1468e53a1b7e3686a934f098b7e1d286cdb80e
    autotools "$src" --disable-external-libs --disable-mpeg --disable-sqlite \
              --disable-alsa --disable-full-suite
}

build_samplerate() {
    fetch samplerate "$github/libsndfile/libsamplerate/releases/download/0.2.2/libsamplerate-0.2.2.tar.xz" \
          3258da280511d24b49d6b08615bbe824d0cacc9842b0e4caf11c52cf2b043893
    autotools "$src" --disable-fftw --disable-sndfile --disable-alsa
}

build_fftw3() {
    # Debian's repack of the release tarball, with the generated
    # codelets (FFTW's Git repository has none)
    fetch fftw3 "$ubuntu/main/f/fftw3/fftw3_3.3.10.orig.tar.gz" \
          5499874012aa6ea6c597b8c3807494e271ab2426a889e95d5cc4d154b825e8a0
    local fftw=$src
    # NEON is always there on arm64, but FFTW's run-time test for it
    # executes an ARMv7 instruction, which on arm64 is another,
    # valid, instruction that clobbers a register. Debian's patch
    # returns 1 on arm64 instead.
    fetch fftw3-debian "$ubuntu/main/f/fftw3/fftw3_3.3.10-2fakesync1build3.debian.tar.xz" \
          13968a263db24bd87d5f8c613d8fc9b6a2660e41cb731758e19941b2c466c107
    patch -d "$fftw" -p1 < "$src/patches/fix-runtime-neon-detection.patch"
    autotools "$fftw" --enable-neon --disable-fortran --disable-doc
}

build_mad() {
    # The maintained fork of libmad 0.15.1b (Tenacity's), from Ubuntu
    # 25.04: CMake, a mad.pc, and the fixes Debian carried as patches.
    # On 64-bit targets it uses 64-bit fixed-point arithmetic.
    fetch mad "$ubuntu/universe/libm/libmad/libmad_0.16.4.orig.tar.xz" \
          cd4c3aa4bf894c957b9b8662eca109927b43b19adafc4ff67eb5349d29d45198
    cmake_build "$src" -DEXAMPLE=OFF
}

build_id3tag() {
    # The same fork's libid3tag, with an id3tag.pc; zlib is the NDK's
    fetch id3tag "$ubuntu/universe/libi/libid3tag/libid3tag_0.16.3.orig.tar.gz" \
          e335334c4504c5def4c0bc04833a34a8ef032767a078cb93429a9094481a9925
    cmake_build "$src"
}

# zix, serd and sord at the versions of Ubuntu 24.04, which the desktop
# build uses
build_zix() {
    fetch zix "$ubuntu/universe/z/zix/zix_0.4.2.orig.tar.xz" \
          0c071cc11ab030bdc668bea3b46781b6dafd47ddd03b6d0c2bc1ebe7177e488d
    meson_build "$src" -Dbenchmarks=disabled -Ddocs=disabled -Dhtml=disabled \
                -Dsinglehtml=disabled -Dtests=disabled -Dtests_cpp=disabled
}

build_serd() {
    fetch serd "$ubuntu/universe/s/serd/serd_0.32.2.orig.tar.xz" \
          df7dc2c96f2ba1decfd756e458e061ded7d8158d255554e7693483ac0963c56b
    meson_build "$src" -Ddocs=disabled -Dhtml=disabled -Dsinglehtml=disabled \
                -Dman=disabled -Dman_html=disabled -Dtests=disabled -Dtools=disabled
}

build_sord() {
    fetch sord "$ubuntu/universe/s/sord/sord_0.16.16.orig.tar.xz" \
          257f876d756143da02ee84c9260af93559d6249dd87f317e70ab5fffcc975fd0
    meson_build "$src" -Ddocs=disabled -Dtests=disabled -Dtools=disabled
}

build_rubberband() {
    # 3.3.0, as Ubuntu 24.04 and so the desktop build
    git clone -q -c advice.detachedHead=false --depth 1 --branch v3.3.0 \
        "$github/breakfastquay/rubberband.git" "$work/rubberband"
    if [ "$(git -C "$work/rubberband" rev-parse HEAD)" != 2be46b0dffb13273a67396c77bc9278736bb03d2 ]; then
        echo "ERROR: tag v3.3.0 of rubberband is not at the expected commit" 1>&2
        exit 1
    fi
    meson_build "$work/rubberband" -Dfft=builtin -Dresampler=builtin \
                -Djni=disabled -Dladspa=disabled -Dlv2=disabled -Dvamp=disabled \
                -Dcmdline=disabled -Dtests=disabled
}

build_boost() {
    # Ubuntu 24.04's Boost, as the desktop build. The tarball is Boost's
    # modular layout, each library's headers under libs/<name>/include
    # (numeric/<name> for four): merged here as Boost's "b2 headers"
    # would. Tony uses only headers.
    local archive=$work/boost.tar.xz
    curl -sSfL --retry 5 --retry-all-errors -o "$archive" \
         "$ubuntu/main/b/boost1.83/boost1.83_1.83.0.orig.tar.xz"
    echo "404df4b4072fc7f2d4483d4fc2d61ff6f554dd80c9a812652684d5952e881c91  $archive" |
        sha256sum -c --quiet
    tar xf "$archive" -C "$work" --wildcards 'boost/libs/*/include/boost/*'
    rm "$archive"
    rm -rf "$prefix/include/boost"
    mkdir -p "$prefix/include/boost"
    local include
    for include in "$work"/boost/libs/*/include "$work"/boost/libs/numeric/*/include; do
        cp -R "$include/boost/." "$prefix/include/boost/"
    done
    grep -q '^#define BOOST_LIB_VERSION "1_83"' "$prefix/include/boost/version.hpp"
}

build_oboe() {
    # The newest 1.x release. Oboe opens libaaudio.so and libOpenSLES.so
    # with dlopen() and defines OpenSL ES's interface IDs itself, so it
    # links against liblog alone. Its CMake install puts the library in
    # lib/<abi>/, moved here to lib/ beside the others; it writes no .pc
    # file.
    git clone -q -c advice.detachedHead=false --depth 1 --branch 1.11.0 \
        "$github/google/oboe.git" "$work/oboe"
    if [ "$(git -C "$work/oboe" rev-parse HEAD)" != b115f47593969fd67a21e9f63640ffef749b5067 ]; then
        echo "ERROR: tag 1.11.0 of oboe is not at the expected commit" 1>&2
        exit 1
    fi
    rm -rf "$prefix/include/oboe" "$prefix/lib/$abi"
    cmake_build "$work/oboe"
    mv "$prefix/lib/$abi/liboboe.a" "$prefix/lib/liboboe.a"
    rmdir "$prefix/lib/$abi"
    cat > "$prefix/lib/pkgconfig/oboe.pc" <<EOF
prefix=$prefix
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: oboe
Description: C++ library for low-latency audio on Android
Version: 1.11.0
Libs: -L\${libdir} -loboe
Libs.private: -llog
Cflags: -I\${includedir}
EOF
}

install_lib() {
    local name="$1" version="$2"
    local stamp=$prefix/share/tony-deps/$name
    local log=$logs/deps-$name.log
    if [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$version" ]; then
        echo "  $name $version: installed already"
        return
    fi
    echo "  $name $version: building (log: $log)"
    rm -f "$stamp"
    # Not in a condition, or -e would not hold inside the subshell
    set +e
    ( set -e; "build_$name" ) > "$log" 2>&1
    local status=$?
    set -e
    if [ "$status" -ne 0 ]; then
        tail -30 "$log" 1>&2
        echo "ERROR: $name failed; the whole log is $log" 1>&2
        exit 1
    fi
    rm -rf "${work:?}"/*
    echo "$version" > "$stamp"
}

echo
echo "Libraries in $prefix:"
install_lib bzip2 1.0.8
install_lib ogg 1.3.5
install_lib opus 1.5.2
install_lib opusfile 0.12
install_lib sndfile 1.2.2
install_lib samplerate 0.2.2
install_lib fftw3 3.3.10
install_lib mad 0.16.4
install_lib id3tag 0.16.3
install_lib zix 0.4.2
install_lib serd 0.32.2
install_lib sord 0.16.16
install_lib rubberband 3.3.0
install_lib boost 1.83.0
install_lib oboe 1.11.0

shared=$(find "$prefix/lib" -name "*.so*")
if [ -n "$shared" ]; then
    echo "ERROR: shared libraries in $prefix/lib, where there should be none:" 1>&2
    echo "$shared" 1>&2
    exit 1
fi

# 4. Check: a shared library using every library, found as Tony's
# build will find them. meson links shared libraries with
# --no-undefined, so a missing library fails the link.

echo
echo "Checking"
check=$work/check
mkdir -p "$check"
cat > "$check/meson.build" <<'EOF'
project('tony-deps-check', 'cpp', default_options: ['cpp_std=c++17'])
deps = [dependency('boost')]
foreach name : ['sndfile', 'samplerate', 'fftw3', 'rubberband', 'sord-0',
                'serd-0', 'mad', 'id3tag', 'opusfile', 'bzip2', 'oboe']
  deps += dependency(name, static: true)
endforeach
shared_library('tonydepscheck', 'check.cpp', dependencies: deps)
EOF
cat > "$check/check.cpp" <<'EOF'
#include <sndfile.h>
#include <samplerate.h>
#include <fftw3.h>
#include <rubberband/RubberBandStretcher.h>
#include <sord/sord.h>
#include <serd/serd.h>
#include <mad.h>
#include <id3tag.h>
#include <opusfile.h>
#include <bzlib.h>
#include <boost/math/distributions.hpp>
#include <oboe/Oboe.h>

// Something from each library, so that the static linker has to find it
extern "C" int tony_deps_check(const unsigned char *data, int size)
{
    int n = sf_version_string()[0];
    n += src_is_valid_ratio(2.0);

    double *in = fftw_alloc_real(16);
    fftw_complex *out = fftw_alloc_complex(9);
    fftw_plan plan = fftw_plan_dft_r2c_1d(16, in, out, FFTW_ESTIMATE);
    fftw_execute(plan);
    fftw_destroy_plan(plan);
    fftw_free(out);
    fftw_free(in);

    RubberBand::RubberBandStretcher stretcher
        (44100, 1, RubberBand::RubberBandStretcher::OptionEngineFiner);
    n += int(stretcher.getStartDelay());

    SordWorld *world = sord_world_new();
    n += int(sord_num_nodes(world));
    sord_world_free(world);
    n += serd_strerror(SERD_SUCCESS)[0];

    struct mad_stream stream;
    struct mad_frame frame;
    mad_stream_init(&stream);
    mad_frame_init(&frame);
    mad_stream_buffer(&stream, data, size);
    n += mad_frame_decode(&frame, &stream);
    mad_frame_finish(&frame);
    mad_stream_finish(&stream);

    struct id3_tag *tag = id3_tag_parse(data, size);
    if (tag) id3_tag_delete(tag);

    int error = 0;
    OggOpusFile *opus = op_open_memory(data, size, &error);
    if (opus) op_free(opus);
    n += error;

    n += BZ2_bzlibVersion()[0];

    boost::math::normal_distribution<double> normal(0.0, 1.0);
    n += int(boost::math::cdf(normal, double(size)) * 10);

    std::shared_ptr<oboe::AudioStream> audio;
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output);
    if (size > 0 && builder.openStream(audio) == oboe::Result::OK) {
        n += audio->getSampleRate();
        audio->close();
    }

    return n;
}
EOF
log=$logs/deps-check.log
set +e
( set -e
  meson setup "$check/build" "$check" --cross-file "$cross" --buildtype release
  ninja -C "$check/build" ) > "$log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    tail -30 "$log" 1>&2
    echo "ERROR: the check did not build; the whole log is $log" 1>&2
    exit 1
fi

so=$check/build/libtonydepscheck.so
kind=$(file -b "$so")
needed=$("$toolchain/bin/llvm-readelf" -d "$so" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | tr '\n' ' ')
align=$("$toolchain/bin/llvm-readelf" -lW "$so" | awk '$1 == "LOAD" { print $NF }' | sort -u | tr '\n' ' ')
echo "  $(basename "$so"): $(echo "$kind" | cut -d, -f1-2)"
echo "  it loads: $needed"
echo "  segment alignment: $align"
case "$kind" in
    "ELF 64-bit LSB shared object, ARM aarch64"*) ;;
    *) echo "ERROR: not an arm64 shared object" 1>&2; exit 1 ;;
esac
for lib in $needed; do
    case "$lib" in
        libc.so|libm.so|libdl.so|libz.so|liblog.so|libc++_shared.so) ;;
        *) echo "ERROR: it loads $lib, which is not part of Android or the NDK" 1>&2; exit 1 ;;
    esac
done
if [ "$align" != "0x4000 " ]; then
    echo "ERROR: its segments are not aligned for 16 KB pages" 1>&2
    exit 1
fi

cat <<EOF

Done:
  prefix:          $prefix
  cross file:      $cross
  pkg-config path: $prefix/lib/pkgconfig
EOF
