# Building

The development machine builds with meson + ninja under MSYS2's `mingw64` toolchain
(default prefix `C:\msys64\mingw64`) into `build_mingw/`: that is most of this page. An
agent in a cloud session builds on Linux instead, into `build/`: see
[Building on Linux](#building-on-linux). The Android app is built on Linux as well, in a
cloud session or on CI, into `build-android/`: see
[Building for Android](#building-for-android) at the end. The Windows CI workflow in
`.github/workflows/` builds as the development machine does, from MSYS2's packages; the
Linux and macOS ones build the upstream way. All three run the test suites
([testing.md](testing.md)). The Android one builds the APK with the scripts a cloud
session uses, and runs no tests. None runs on a push or a pull request: each is started by
hand, from its page under the repository's Actions tab (Run workflow), on a branch chosen
there.

# Building on Windows (MSYS2 MinGW-w64)

## From cmd or PowerShell: `build.bat`

```
.\build.bat           build Tony.exe
.\build.bat run       build, then launch
.\build.bat launch    launch without building
.\build.bat test      meson test: tony-core, tony-app, tony-dev and four svcore suites
.\build.bat clean     wipe build_mingw and reconfigure (only for a broken build directory)
```

It reads `MSYS2_MINGW` and puts `mingw64\bin` and msys2's `usr\bin` on `PATH` itself.
From PowerShell its output is safe to capture: `.\build.bat *> tmp\build.log`.

## From Git Bash (what an agent's Bash tool usually is)

`build.bat` cannot be called from sh. Call ninja directly:

```sh
export PATH="/c/msys64/mingw64/bin:$PATH" MINGW_PREFIX="C:/msys64/mingw64"
ninja -j 4 -C build_mingw Tony.exe test-tony-core.exe test-tony-app.exe test-tony-dev.exe > tmp/build.log 2>&1
echo "exit:$?" >> tmp/build.log
tail -20 tmp/build.log
```

Each part of that is there because of something that went wrong:

- **Only `mingw64/bin` on PATH, not `/c/msys64/usr/bin`.** With the latter, msys2's
  coreutils shadow Git's and run under a different msys runtime: quoted arguments held in
  variables arrive empty (`grep: Usage...`, `mkdir: missing operand`). ninja, meson, gcc,
  moc and the Qt DLLs are all under `mingw64`.
- **`MINGW_PREFIX` must be set.** `meson.build` reads it, and Git Bash sets it to Git's
  own `/mingw64`, which fails with `Include dir C:/Program Files/Git/mingw64/include/opus
  does not exist`. ninja reconfigures by itself whenever `meson.build` changed (a new
  source file is enough), so this bites on ordinary incremental builds.
- **Spell `MINGW_PREFIX` exactly `C:/msys64/mingw64`.** A reconfigure with a different
  spelling than the build directory was set up with changes the include flags and
  rebuilds everything (about 560 steps).
- **`-j 4`**, one job per core. It relies on Windows' page file being on. Without one,
  Windows can promise programs no more memory than the RAM, and with an editor and a
  browser open a large rebuild ran out (`cc1plus.exe: out of memory`, bash cannot fork)
  while RAM was still free. If it happens, run the same command again; ninja carries on
  where it stopped.
- **Redirect to a log and never pipe ninja.** The output is large and can stall or time out
  the tool. `tmp/` is gitignored and is the place for logs.
- **Write ninja's exit status into the log.** The status of a `ninja ...; tail ...` chain
  is that of `tail`.
- **Targets carry the extension**: `test-tony-app.exe`, not `test-tony-app`.
- `Tony.exe` cannot be relinked while it is running. Close the app first.

Success is `[N/N] Linking target ...` and `exit:0`; nothing to do is `ninja: no work to
do.` and `exit:0`.

Timings: incremental under a minute, a few minutes after touching `MainWindow.cpp` or a
widely included header, up to 30 minutes from clean. Give long builds a 30-minute timeout.

Reconfigure from scratch (rarely needed):

```sh
export PATH="/c/msys64/mingw64/bin:$PATH" MINGW_PREFIX="C:/msys64/mingw64"
meson setup --wipe build_mingw > tmp/build.log 2>&1 && ninja -j 4 -C build_mingw Tony.exe >> tmp/build.log 2>&1
echo "exit:$?" >> tmp/build.log
```

## What is particular about this `meson.build`

- A MinGW/GCC win64 branch that upstream does not have: msys2 system libraries,
  `-include main/mingw_byte_fix.h` on every translation unit (the C++17 `std::byte` versus
  `rpcndr.h`'s `byte` clash), `--export-dynamic-symbol` for the vamp entry point, explicit
  include directories for `opus`, `sord-0`, `serd-0`.
- `-DHAVE_MEDIAFOUNDATION` with `-lmfplat -lmfreadwrite -lmfuuid -lpropsys`; needs the
  `bqaudiostream` fork.
- `general_defines` goes on every target, svcore and the plugins included, so a change to
  it recompiles everything (about 620 steps, 20 minutes). A define that only Tony's code
  reads goes in `tony_defines`, which only Tony's own targets get. `tony_app` compiles
  svgui and svapp too, so a change there still recompiles those, but not svcore.
- `tony_core` / `tony_app` static libraries and the test executables; see
  [architecture.md](architecture.md) for what goes where. A new source file goes into
  `tony_core_files` or `tony_app_files` (an Android-only one into the lists' additions
  under `if system == 'android'`), and its header into the matching moc list only if it
  declares `Q_OBJECT`.
- Any build type but `release` (`build.bat`'s is `debugoptimized`) is a development build:
  `-DTONY_DEV_CHECKS` for the compiler and for moc, `main/dev/` compiled into `tony_app`,
  and `test-tony-dev` built. `meson.build`'s default and the desktop CI workflows use
  `release`, which has none of it; the Android build, on CI too, is `debugoptimized`.
  After a change to how the dev checks are wired in, set up a `release` build directory
  and build it: it must compile with no `main/dev/` file
  ([calibrate-audio.md](calibrate-audio.md), §6).
- Windows headers define macros named `near` and `far` (empty), `min`, `max`, `ERROR`, `IN`
  and `OUT`. Do not use them as identifiers: a build on Linux does not catch it.
- The macOS SDK's `MacTypes.h` declares `normal`, `bold`, `italic`, `underline`,
  `outline`, `shadow`, `condense` and `extend` in the global namespace. A function of one
  of those names, even in an anonymous namespace, makes each unqualified call to it
  ambiguous on macOS, and nothing else catches it.

# Building on Linux

For a cloud session: Ubuntu 24.04, 4 cores, 16 GB, root, no sound card, and no Windows.
repoint does not run there, and hg.sr.ht, where five of the libraries live, cannot be
reached. Three scripts in `deploy/linux/` do the work:

- **`cloud-environment.sh` is the cloud environment's setup script.** Its text is pasted
  into the environment's settings, with the network access and variables below; the copy in
  the repository does nothing by itself. The platform runs it once and keeps a snapshot of
  the disk, which later sessions start from, until the script or the allowed hosts change
  or about a week has passed. It installs the packages, Qt, ccache and mold, the Android SDK
  and NDK when `dl.google.com` is reachable, and spends what is left of four minutes filling
  ccache from a build of the libraries. Qt for Android and the Android build's C libraries
  are not in the snapshot: they take far longer than it allows. It also writes an
  `autoMode` entry to `/root/.claude/settings.json` by which auto mode trusts four of the
  library forks (all but `bqaudioio`) as it does Tony's own repository
  ([forks.md](forks.md#changing-a-fork)): auto mode reads that from the user's settings,
  never from the repository's `.claude/settings.json`. The snapshot is kept only when the
  script ends within about five minutes, so any change to it has to keep to that. Its logs
  are in `/var/log/tony-environment/`.
- **`container-setup.sh`** makes any fresh Ubuntu 24.04 able to build: packages, Qt, the
  library directories at their pins, `meson setup build`. Safe to run again. After the
  environment's snapshot it only checks out the libraries and configures.
- **`cloud-session.sh start` runs at the start and resumption of every cloud session**,
  from the SessionStart hook in `.claude/settings.json` (`start --if-cloud`, which does
  nothing outside the cloud). It runs `container-setup.sh` and then builds everything into
  `build/` in the background, at low priority: about 4 minutes, while the session reads.
  It builds nothing for Android. The hook itself returns at once. The log is
  `tmp/cloud-session.log`. `cloud-session.sh wait` waits for it and exits as it did.
  **Wait before the first build or test**: two ninjas must not work in one build
  directory. A session with several repositories runs no repository's hooks; there, run
  `cloud-session.sh start` by hand.

The environment's settings:

- Network access **Custom**, with the default list of package hosts, and `dl.google.com`
  added for the Android build (the SDK, the NDK, and Gradle's Google repository, which
  `maven.google.com` redirects to). Without it there is no Android SDK, and Gradle cannot
  fetch the Android Gradle plugin: `build-apk.sh` then names the host it was refused.
  GitHub, conda-forge, PyPI, Ubuntu's archive and Maven Central are reachable. Not
  reachable, and not to be looked for elsewhere: hg.sr.ht; Qt's download mirrors, to which
  download.qt.io redirects every archive (so no Qt from Qt's own archives or installer);
  `breakfastquay.com` and the other upstream sites of the Android build's C libraries
  (`xiph.org`, `fftw.org`, `codeberg.org`, `download.drobilla.net`); the image's own PPAs
  (`ppa.launchpadcontent.net`), about which `apt-get update` warns and carries on. The
  scripts fetch only from reachable hosts, and retry, because the proxy now and then
  answers 502 for a moment. A push to one of the forks is another matter: with this Custom
  access it is refused (HTTP 403) until the fork is attached to the session, and goes
  through once it is ([forks.md](forks.md#changing-a-fork)).
- Variables `BASH_DEFAULT_TIMEOUT_MS=600000` and `BASH_MAX_TIMEOUT_MS=1800000`, so that a
  build or a suite run is not moved to the background after the tool's default two minutes,
  and a 30-minute timeout can be given at all.

Then, from the repository root:

```sh
ninja -j 4 -C build tony pyin.so test-tony-core test-tony-app test-tony-dev > tmp/build.log 2>&1
echo "exit:$?" >> tmp/build.log; tail -20 tmp/build.log
deploy/linux/run-tests.sh test-tony-core     # a few seconds
deploy/linux/run-tests.sh test-tony-app      # under two minutes
deploy/linux/run-tests.sh test-tony-dev      # when AGENTS.md says to run it; two minutes
```

No `.exe` on Linux; the plugin target is `pyin.so`. `run-tests.sh` runs an executable as
several processes, each with a shard of every suite ([testing.md](testing.md#running)).
The one-process runs of AGENTS.md work too, from `build/`.

Measured on 2026-09-26 (the sharded runs on 2026-09-27 as well):

| | |
| --- | --- |
| Full build, nothing in ccache | 6.6 minutes: 1570 CPU-seconds, nearly all compiling |
| Full build, everything in ccache | 4 to 6 seconds |
| A session's first build, with the setup script's ccache | 3.7 minutes, in the background |
| Linking `tony` and the three test executables | 3 s with mold, 12 s with GNU ld |
| Core suite | 5 to 8 s in eight processes |
| App suite | 628 s in one process; 98 to 113 s in eight |
| Development checks' suite | 444 s in one process; 110 to 121 s in eight |

Why each part is as it is:

- **Qt 6.11 from conda-forge, not Ubuntu's 6.4**: the Qt of the Windows machine. Qt 6.4
  does not match a `SIGNAL()`/`SLOT()` string naming `ModelId` or `sv_frame_t` against a
  slot moc recorded with `sv::`, so such a connection fails silently there and works on
  Windows (member-pointer connects, which AGENTS.md asks for, work on both).
  download.qt.io's mirrors are blocked by the session's proxy; conda-forge is not. Only
  Qt's own `.pc` files are put on meson's pkg-config path (`/opt/qt6-conda/tony-pkgconfig`),
  so that nothing else of conda's is picked up; meson puts Qt's library directory in the
  executables' RPATH.
- **Rubber Band is Ubuntu's** (3.3.0), which meets `meson.build`'s `>= 3.0.0`: the Linux
  CI workflow builds it from a tarball on `breakfastquay.com`, which is blocked.
- **The Mercurial libraries come from their GitHub mirrors.** hg.sr.ht is blocked, and
  `repoint-lock.json` pins them by Mercurial hash, which the mirrors do not carry.
  `container-setup.sh` has a table from pin to mirror commit and stops at a pin it does not
  know.
- **ccache**, which meson uses by itself when it is installed, with `hash_dir = false`
  (`/etc/ccache.conf`). With `-g` every result's key otherwise holds the build directory,
  and a second build directory or a worktree found 0.4 % of a full cache. The compiler's
  name is part of the key too: a directory configured with `CC=gcc CXX=g++` finds nothing
  that meson's own `cc` and `c++` put there. Configure through `container-setup.sh`.
- **mold**: `cloud-session.sh` has `meson setup` take it (`CC_LD=mold CXX_LD=mold`) for a
  new `build/`; a build directory keeps the linker it was set up with. Every change to
  `main/` relinks all four executables.
- **Run the app suite with nothing else building**: it records in real time.
- `TestTakesFile` checks Windows paths (backslashes, drive letters, case) on Windows only;
  elsewhere it checks that names are case-sensitive and uses a POSIX absolute path.
- Measured and left alone: `-g1` compiles svcore in 19 % less time than `-g`, but Windows
  builds `debugoptimized`, with full debug information; clang is no faster than GCC; and a
  unity build fails in the libraries, which define the same names in several files.

## Checking the fork's Windows code

The bqaudioio fork's drivers ([forks.md](forks.md#bqaudioio)) are under `#ifdef _WIN32`,
so the Linux build compiles none of them. Here they are checked by compiling the two files
for Windows with MinGW-w64, against the headers of PortAudio 19.7.0, the version of MSYS2's
package:

```sh
apt-get install -y g++-mingw-w64-x86-64-posix
mkdir -p tmp/pa197
for h in portaudio.h pa_win_wasapi.h pa_win_waveformat.h; do
  curl -sSfo tmp/pa197/$h https://raw.githubusercontent.com/PortAudio/portaudio/v19.7.0/include/$h
done
for f in PortAudioIO AudioFactory; do
  x86_64-w64-mingw32-g++ -std=c++17 -fsyntax-only -DHAVE_PORTAUDIO -Itmp/pa197 \
    -Ibqaudioio/bqaudioio -Ibqaudioio/src -Ibqvec bqaudioio/src/$f.cpp
done
```

Both must compile without a word. WASAPI's stream info is included only where
`__has_include` finds `pa_win_wasapi.h`, and a missing header drops it without an error:
run the line for `PortAudioIO` with `-E` in place of `-fsyntax-only` and look for
`wasapiInfo.flags` in the output. Nothing more of the Windows part can be tried here: it
runs only on the user's PC.

# Building for Android

The Android app is built on Linux, by the scripts in `deploy/android/`: in a cloud
session, or on CI by `.github/workflows/android.yml`. They run in this order. Each is safe
to run again (the first three leave alone what is there already at its version), ends by
checking what it made, and shows the end of its log when it fails. All but Tony's own build
goes under `/opt/android`, the logs in `/opt/android/logs`.

| Script | What it makes |
| --- | --- |
| `setup-toolchain.sh` | JDK 21 and the host's build tools from apt; the SDK's command-line tools, platform `android-36`, build tools 36.0.0, the platform tools and NDK r27c (27.2.12479018), in `/opt/android/sdk` |
| `build-qt.sh` | Qt 6.11.2 from source: a host Qt for its tools, and qtbase and qtsvg for Android, in `/opt/android/qt/6.11.2/gcc_64` and `android_arm64_v8a` |
| `build-deps.sh` | the C libraries, static, for arm64-v8a at API 28, in `/opt/android/deps-arm64-v8a`, and the NDK's meson cross file `/opt/android/cross-arm64-v8a.ini` |
| `build-tony.sh` | `build-android/`: `libTony_arm64-v8a.so` and the plugins `pyin.so` and `chp.so`; `--wipe` configures afresh, for a new NDK or Qt |
| `build-apk.sh` | `build-android/apk/Tony-debug.apk`, signed with the debug key, through androiddeployqt and Gradle |

The first three took 21 minutes in a fresh container, 18 of them Qt's; `build-tony.sh`
takes about 4 minutes from scratch. The cloud environment's snapshot holds the SDK and the
NDK, not Qt for Android or the libraries, so a new session runs the first three, about 20
minutes, before its first APK. After that, a change to Tony needs only:

```sh
deploy/android/build-tony.sh && deploy/android/build-apk.sh
```

The APK is installed on the phone by hand. Why each part is as it is:

- **meson with two cross files**, not an Android-only `CMakeLists.txt`, which is Qt's
  supported route: a CMake copy would have to keep `meson.build`'s source lists in step.
  One cross file is the NDK's, which `build-deps.sh` writes; the other,
  `deploy/android/qt-arm64-v8a.ini`, names Qt for Android's `qmake`. Qt for Android
  installs no `.pc` files, so meson finds Qt through that qmake, and takes moc, rcc and uic
  from the host Qt it names. The meson issues feared for this (13018, 6089) did not arise
  with Ubuntu's meson 1.3.2.
- **`meson.build`'s `android` branch** is the Linux branch less what a phone has no use
  for (JACK, PulseAudio, ALSA and RtMidi's ALSA defines, PortAudio, oggz and fishsound),
  with Opus read-only. Every library is found with `static: true`: only then does
  pkg-config name their private libraries (ogg, opus, zix, serd). Tony is the shared
  library `libTony_arm64-v8a.so`, the name androiddeployqt looks for, which Qt's Java
  launcher loads to call its `main()`; `--exclude-libs,ALL` keeps everything from the
  static libraries out of its symbol table. `link_whole` is what keeps
  `AndroidMediaReadStream` in it: the reader registers itself with bqaudiostream, and
  nothing refers to it. The test executables are not built for Android.
- **`debugoptimized`**, as the desktop builds: asserts on, and the development checks
  compiled in ([calibrate-audio.md](calibrate-audio.md)). The libraries in `build-android/`
  keep their debug information (Tony's is over 100 MB), for the NDK's `ndk-stack` on a
  crash from the phone. `build-apk.sh` strips the APK's copies and checks that every
  library in the APK is stripped.
- **Qt from source.** download.qt.io answers every archive with a redirect to a mirror,
  and the mirrors are blocked. The sources are Qt's own repositories on GitHub at tag
  `v6.11.2`, checked against the commit: the Qt of the desktop builds. The host Qt is built
  for its tools only, with Qt's bundled third-party libraries, and **without zstd**: where
  zstd is found (CI's runner has `libzstd-dev`), its rcc compresses Tony's resources with
  it, which Qt for Android cannot read, and Tony's link fails. Qt for Android leaves out SQL
  and printing, and has no OpenSSL: Qt Network works, without TLS.
- **The C libraries** are release tarballs from GitHub or from Ubuntu's archive pool, which
  keeps a file unchanged for good, each checked against a SHA-256 in the script; Rubber
  Band and Oboe are Git tags checked against the commit. Rubber Band, zix, serd, sord and
  Boost are at the versions of Ubuntu 24.04, which the Linux build uses. Beyond what
  `meson.build` names: bzip2, because svcore's `BZipFileDevice` includes `bzlib.h`
  whatever the defines say, and the NDK has none; zix, which sord needs; Boost's headers,
  for pYIN. fftw3 is double only
  (`meson.build` defines `FFTW_DOUBLE_ONLY` everywhere), with NEON, and with Debian's patch
  to its NEON probe, which runs an ARMv7 instruction that on arm64 is another one and
  clobbers a register. libmad and libid3tag are the maintained fork, 0.16, which has `.pc`
  files. Rubber Band uses its built-in FFT and resampler. Oboe opens AAudio and OpenSL ES
  with `dlopen()`, so it links `liblog` alone. zlib is the NDK's. A library is built again
  when its version in the script changes, or when its stamp in
  `/opt/android/deps-arm64-v8a/share/tony-deps/` is deleted.
- **16 KB pages**, which Android 15 can have: NDK r27 does not align for them by itself,
  so the cross file links with `-z max-page-size=16384`, and `build-deps.sh`,
  `build-tony.sh` and `build-apk.sh` each check the alignment.
- **The SDK's command-line tools are 22.0**: in 23.0 `sdkmanager` became a wrapper around
  a new tool, with other package names. `sdkmanager` takes the newest revision of platform
  `android-36` and of the platform tools, so those two are not pinned. Java tools need the
  proxy named to them: in a session `JAVA_TOOL_OPTIONS` does it, for sdkmanager and Gradle;
  `cloud-environment.sh` names it to sdkmanager itself.

What `build-apk.sh` does, and why:

- **It writes the deployment settings** that androiddeployqt reads, which CMake and qmake
  write for their own builds (`build-android/android-Tony-deployment-settings.json`). They
  follow the file Qt's CMake wrote for `build-qt.sh`'s check, kept as
  `/opt/android/logs/android-check-deployment-settings.json`, and name the Qt plugins that
  file names: without a list, androiddeployqt packages every plugin of every module, test
  platforms and all. androiddeployqt then makes `build-android/android-build/`, a Gradle
  project, and runs Gradle.
- **The manifest** is `deploy/android/package/AndroidManifest.xml`: Qt's template with the
  package name `io.github.jhhr.tony`, `RECORD_AUDIO` (androiddeployqt adds it only for Qt
  Multimedia, which Tony does not use), `MANAGE_EXTERNAL_STORAGE`, and `sensorLandscape`.
  Lint's `ScopedStorage` check, which is for Google Play's sake, is told to ignore the
  storage permission: Tony is installed by hand.
- **The plugins** go in as `libpyin.so` and `libchp.so`, beside Tony's library in
  `lib/arm64-v8a/`: Android installs nothing else from an APK. Not as `android-extra-libs`,
  which Qt's launcher would load at every start. The libraries are packaged the legacy way,
  compressed and unpacked at install, so that they exist as files for svcore's plugin scan
  (how Tony finds them: [port-android.md](port-android.md)).
- **The version.** The version name is `TONY_VERSION` and the short commit, with a `+` when
  tracked files differ from that commit; the version code is the number of commits. The
  first line of the phone's log names the version name, so that a report from the phone
  says which commit ran. Hence:
  - CI's checkout fetches the whole history (`fetch-depth: 0`). A cloud session's clone is
    shallow and counts fewer commits. Android refuses an APK with a lower version code than
    the one installed, so an APK from a session does not install over one from CI without
    uninstalling first, which loses the app's settings and log.
  - `repoint-lock.json` ends with a newline, as repoint writes it. CI runs `./repoint
    install`, which writes the file afresh: without the newline its tree counted as changed,
    and every APK it built said `+`.
- **It deletes Gradle's last APK first.** Gradle packages a debug APK over its last one,
  and a library that changed leaves the old one's space in the file: 7 MB more each time
  Tony's library changes.
- **It runs Gradle again**, up to three runs in all, when one fails on 429 Too Many
  Requests, a reset or a timeout: Gradle's first run fetches the Android Gradle plugin from
  Google's repository and more from Maven Central, which has answered 429. On a 403 it
  names the host that was refused.

On CI, `android.yml` runs the five scripts on GitHub's `ubuntu-24.04`, after `./repoint
install` as the other workflows do. It first deletes the runner's own Android SDK and .NET,
for the disk the Qt build needs, and points the `ANDROID_*` variables at the scripts' SDK
and NDK, the runner's being other versions. Qt for Android and the C libraries are cached,
each under a key made from the script that builds it and `setup-toolchain.sh`, and saved as
soon as they are built, so that a later step's failure does not cost them; a change to one
of those scripts builds it again, and the two from nothing take most of an hour there. The
APK is kept as the run's artifact `Tony-debug-apk`; after a failure, `/opt/android/logs` as
`android-logs`.
