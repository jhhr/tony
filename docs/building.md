# Building on Windows (MSYS2 MinGW-w64)

The development machine builds with meson + ninja under MSYS2's `mingw64` toolchain
(default prefix `C:\msys64\mingw64`) into `build_mingw/`. The CI workflows in
`.github/workflows/` build the upstream way on Linux, macOS and MSVC and are not what is
described here.

## From cmd or PowerShell: `build.bat`

```
.\build.bat           build Tony.exe
.\build.bat run       build, then launch
.\build.bat launch    launch without building
.\build.bat test      meson test: tony-core, tony-app and four svcore suites
.\build.bat clean     wipe build_mingw and reconfigure (only for a broken build directory)
```

It reads `MSYS2_MINGW` and puts `mingw64\bin` and msys2's `usr\bin` on `PATH` itself.
From PowerShell its output is safe to capture: `.\build.bat *> tmp\build.log`.

## From Git Bash (what an agent's Bash tool usually is)

`build.bat` cannot be called from sh. Call ninja directly:

```sh
export PATH="/c/msys64/mingw64/bin:$PATH" MINGW_PREFIX="C:/msys64/mingw64"
ninja -j 3 -C build_mingw Tony.exe test-tony-core.exe test-tony-app.exe test-tony-device.exe > tmp/build.log 2>&1
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

## On Linux (a cloud session)

Not how the project is developed, but it builds and both suites run; this is how it was done
on 2026-09-25 (Ubuntu 24.04, no sound card):

- Packages: the `apt-get install` list of `.github/workflows/linux.yml` (`smlnj` and
  `mercurial` are not needed, and `libboost-dev` does for `libboost-all-dev`), plus
  `librubberband-dev`, `libjack-jackd2-dev`, `libasound2-dev`, `libopusenc-dev`, `meson`.
- **Qt 6.11 from conda-forge, not Ubuntu's 6.4.** Under 6.4 the string-based connects of
  `Analyser` with `sv::` types do not resolve ("No such slot
  Analyser::layerCompletionChanged(ModelId)"), so pYIN's completion never arrives and every
  analysing test times out. download.qt.io's mirrors are blocked by the session's proxy;
  conda-forge is not:
  `micromamba create -p /opt/qt611 -c conda-forge qt6-main=6.11.1`, then a directory with
  links to only its `Qt6*.pc` files, so that nothing else of conda's is picked up:
  `PKG_CONFIG_PATH=<that directory> meson setup build_qt611`, and
  `LD_LIBRARY_PATH=/opt/qt611/lib` to run.
- The libraries by `git clone` at the pins of `repoint-lock.json`. sourcehut (the `hg`
  ones) was unreachable; their GitHub mirrors (`github.com/breakfastquay/...`) are at the
  same tips.
- `-j 4` on four cores; the whole build takes about 20 minutes. Run the app suite with
  nothing else building: it records in real time.
- Four tests of `TestTakesFile` fail on Linux and nowhere else: they are about Windows
  paths (backslashes, drive letters, case).

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
