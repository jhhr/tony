# Building

The development machine builds with meson + ninja under MSYS2's `mingw64` toolchain
(default prefix `C:\msys64\mingw64`) into `build_mingw/`: that is most of this page. An
agent in a cloud session builds on Linux instead, into `build/`: see
[Building on Linux](#building-on-linux) at the end. The CI workflows in
`.github/workflows/` build the upstream way on Linux, macOS and MSVC and are not what is
described here.

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
ninja -j 3 -C build_mingw Tony.exe test-tony-core.exe test-tony-app.exe test-tony-dev.exe test-tony-device.exe > tmp/build.log 2>&1
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
- **`-j 3`.** At ninja's default parallelism a large rebuild runs this machine out of
  memory (`cc1plus.exe: out of memory`, bash cannot fork). If it happens, run the same
  command again; ninja carries on where it stopped.
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
meson setup --wipe build_mingw > tmp/build.log 2>&1 && ninja -j 3 -C build_mingw Tony.exe >> tmp/build.log 2>&1
echo "exit:$?" >> tmp/build.log
```

## What is particular about this `meson.build`

- A MinGW/GCC win64 branch that upstream does not have: msys2 system libraries,
  `-include main/mingw_byte_fix.h` on every translation unit (the C++17 `std::byte` versus
  `rpcndr.h`'s `byte` clash), `--export-dynamic-symbol` for the vamp entry point, explicit
  include directories for `opus`, `sord-0`, `serd-0`.
- `-DHAVE_MEDIAFOUNDATION` with `-lmfplat -lmfreadwrite -lmfuuid -lpropsys`; needs the
  `bqaudiostream` fork.
- `tony_core` / `tony_app` static libraries and the two test executables; see
  [architecture.md](architecture.md) for what goes where. A new source file goes into
  `tony_core_files` or `tony_app_files`, and its header into the matching `*_moc_files`
  only if it declares `Q_OBJECT`.
- Windows headers define `near` and `far` as macros. Do not use them as identifiers.

# Building on Linux

For a cloud session: Ubuntu 24.04, 4 cores, 16 GB, root, no sound card, and no Windows.
repoint does not run there, and hg.sr.ht, where six of the libraries live, cannot be
reached. Three scripts in `deploy/linux/` do the work:

- **`cloud-environment.sh` is the cloud environment's setup.** The environment's "Setup
  script" field holds `cloud-setup-script.sh`, pasted whole, which runs this file as it is
  on `default` (the checkout's copy if GitHub cannot be reached). This file cannot go in
  the field itself: the field keeps only about its first 6000 characters, and the rest was
  cut off ("unexpected end of file"). The platform runs the setup once and keeps a snapshot
  of the disk, which later sessions start from, until the field's text or the allowed hosts
  change or about a week has passed; changing the date in the field's text makes a change
  to `cloud-environment.sh` reach sessions at once. It installs the packages, Qt, ccache and mold, the Android SDK
  and NDK when `dl.google.com` is reachable, and spends what is left of four minutes filling
  ccache from a build of the libraries. It also writes an `autoMode` entry to
  `/root/.claude/settings.json` by which auto mode trusts the four library forks as it does
  Tony's own repository ([forks.md](forks.md#changing-a-fork)): auto mode reads that from
  the user's settings, never from the repository's `.claude/settings.json`. The snapshot is kept only when the script ends
  within about five minutes, so any change to it has to keep to that. Its logs are in
  `/var/log/tony-environment/`.
- **`container-setup.sh`** makes any fresh Ubuntu 24.04 able to build: packages, Qt, the
  library directories at their pins, `meson setup build`. Safe to run again. After the
  environment's snapshot it only checks out the libraries and configures.
- **`cloud-session.sh start` runs at the start of every cloud session**, from the
  SessionStart hook in `.claude/settings.json` (`start --if-cloud`, which does nothing
  outside the cloud). It runs `container-setup.sh` and then builds everything into `build/`
  in the background, at low priority: about 4 minutes, while the session reads. The hook
  itself returns at once. The log is `tmp/cloud-session.log`. `cloud-session.sh wait`
  waits for it and exits as it did. **Wait before the first build or test**: two ninjas
  must not work in one build directory. A session with several repositories runs no
  repository's hooks; there, run `cloud-session.sh start` by hand.

The environment's settings:

- Network access **Custom**, with the default list of package hosts, and `dl.google.com`
  added for the Android branch (the SDK, the NDK, and Gradle's Google repository, which
  `maven.google.com` redirects to). GitHub, conda-forge and Ubuntu's archive are in the
  default list; hg.sr.ht, download.qt.io and Qt's mirrors are not. A push to one of the
  forks is another matter: with this Custom access it is refused (HTTP 403) until the fork
  is attached to the session, and goes through once it is
  ([forks.md](forks.md#changing-a-fork)).
- Variables `BASH_DEFAULT_TIMEOUT_MS=600000` and `BASH_MAX_TIMEOUT_MS=1800000`, so that a
  build or a suite run is not moved to the background after the tool's default two minutes,
  and a 30-minute timeout can be given at all.

Then, from the repository root:

```sh
ninja -j 4 -C build tony pyin.so test-tony-core test-tony-app test-tony-dev > tmp/build.log 2>&1
echo "exit:$?" >> tmp/build.log; tail -20 tmp/build.log
deploy/linux/run-tests.sh test-tony-core     # about a second
deploy/linux/run-tests.sh test-tony-app      # a minute and a half
deploy/linux/run-tests.sh test-tony-dev      # when AGENTS.md says to run it
```

No `.exe` on Linux; the plugin target is `pyin.so`. `run-tests.sh` runs an executable as
several processes, each with a shard of every suite ([testing.md](testing.md#running)).
The one-process runs of AGENTS.md work too, from `build/`.

Measured on 2026-09-26:

| | |
| --- | --- |
| Full build, nothing in ccache | 6.6 minutes: 1570 CPU-seconds, nearly all compiling |
| Full build, everything in ccache | 4 to 6 seconds |
| A session's first build, with the setup script's ccache | 3.7 minutes, in the background |
| Linking `tony`, `test-tony-core`, `test-tony-app` and `test-tony-device` | 3 s with mold, 12 s with GNU ld |
| App suite | 550 s in one process, 95 s in eight |
| Development checks' suite | 69 s in one process, 18 s in eight |

Why each part is as it is:

- **Qt 6.11 from conda-forge, not Ubuntu's 6.4**: the Qt of the Windows machine. Qt 6.4
  does not match a `SIGNAL()`/`SLOT()` string naming `ModelId` or `sv_frame_t` against a
  slot moc recorded with `sv::`, so such a connection fails silently there and works on
  Windows (member-pointer connects, which AGENTS.md asks for, work on both). And with 6.4
  several of the tests that race the analysis against a take fail whatever the change
  ([testing.md](testing.md#running)); with 6.11 none do. download.qt.io's mirrors are
  blocked by the session's proxy; conda-forge is not. Only Qt's own `.pc` files are put on meson's pkg-config path
  (`/opt/qt6-conda/tony-pkgconfig`), so that nothing else of conda's is picked up; meson
  puts Qt's library directory in the executables' RPATH.
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
- Four tests of `TestTakesFile` fail on Linux and nowhere else: they are about Windows
  paths (backslashes, drive letters, case).
- Measured and left alone: `-g1` compiles svcore in 19 % less time than `-g`, but Windows
  builds `debugoptimized`, with full debug information; clang is no faster than GCC; and a
  unity build fails in the libraries, which define the same names in several files.
