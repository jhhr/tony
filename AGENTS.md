# Instructions for AI agents

This is a fork of [Tony](https://github.com/sonic-visualiser/tony) (Qt6 / C++17, meson +
ninja, built on the Sonic Visualiser libraries) that turns it into a **singing practice
aid**: a reference pitch track and a singing pitch track in one pane, live pitch dots
while recording, partial recordings, takes, undo. Development happens on Windows with
MSYS2 MinGW-w64. The default branch is `default`.

All of the fork's own code is in `main/`. Detailed documentation is in [`docs/`](docs/);
**read the page for the area you are about to change before changing it** — the rules
there were each learned from a crash or a wrong result, and most cannot be seen in the
code.

| Read | Before |
| --- | --- |
| [docs/building.md](docs/building.md) | building for the first time in a session |
| [docs/testing.md](docs/testing.md) | running or writing tests |
| [docs/architecture.md](docs/architecture.md) | touching layers, models, the document, commands, playback or the session file |
| [docs/recording.md](docs/recording.md) | touching `record()`, the Stop path, latency, pre-roll, the live tracker |
| [docs/takes.md](docs/takes.md) | touching takes, the audio swap, ranged analysis, undo, the coverage strip, save/restore |
| [docs/calibrate-audio.md](docs/calibrate-audio.md) | touching Calibrate Audio (`AudioCheckRunner`, `CalibrateAudioDialog`), the dev checks (`main/dev/`) or the measured latency (`LatencyCheck`, `LatencyCalibration`) |
| [docs/audio-drivers.md](docs/audio-drivers.md) | touching the Audio Driver or Audio Latency menus (`AudioDriverSettings`, `AudioDriverMenus`), how the device is opened (`MainWindow::createAudioIO()`), or the bqaudioio fork |
| [docs/forks.md](docs/forks.md) | needing a change in `svcore/`, `svgui/`, `svapp/`, `bqaudiostream/`, `bqaudioio/` |
| [docs/open-points.md](docs/open-points.md), [docs/manual-checklist.md](docs/manual-checklist.md) | choosing what to do next, or saying what the user should try by hand |
| [docs/mobile-port.md](docs/mobile-port.md), then [docs/port-android.md](docs/port-android.md) or [docs/port-sailfish.md](docs/port-sailfish.md) | starting or working on a phone port |

## Build and test

From **Git Bash** (the usual agent shell) on the Windows machine. `build.bat` does not run
from sh. A Linux cloud session builds otherwise: see the end of this section.

```sh
export PATH="/c/msys64/mingw64/bin:$PATH" MINGW_PREFIX="C:/msys64/mingw64"
ninja -j 4 -C build_mingw Tony.exe test-tony-core.exe test-tony-app.exe test-tony-dev.exe > tmp/build.log 2>&1
echo "exit:$?" >> tmp/build.log; tail -20 tmp/build.log
```

- Only `mingw64/bin` on PATH (never `/c/msys64/usr/bin`); always set `MINGW_PREFIX`,
  spelled exactly so; always `-j 4`; always log to a file and never pipe ninja; targets
  need `.exe`. The reasons are in [docs/building.md](docs/building.md).
- Incremental builds take under a minute, a few minutes after `MainWindow.cpp`; a clean
  build up to 30 minutes. If `cc1plus.exe` runs out of memory, run the command again.

Run tests from `build_mingw/` with the same environment:

```sh
mkdir -p ../tmp/tl
TONY_TEST_LOG_DIR=../tmp/tl ./test-tony-core.exe > ../tmp/test.log 2>&1; echo "exit:$?"
TONY_TEST_LOG_DIR=../tmp/tl ./test-tony-app.exe  > ../tmp/test.log 2>&1; echo "exit:$?"   # ~12 min, real time
TONY_TEST_LOG_DIR=../tmp/tl ./test-tony-app.exe undo_two_takes_in_order > ../tmp/test.log 2>&1
grep -a "^FAIL\|^   Loc\|^Totals" ../tmp/tl/*.txt
```

- Read results from the per-suite files in `TONY_TEST_LOG_DIR`, not from stdout.
- A test name on the command line goes to **every** suite in the executable; the suites
  that lack it fail, so the exit status is only meaningful for a run with no names.
- Run named tests while working; run **both whole suites** before calling anything done.
- `test-tony-dev.exe` (development builds only) holds the development checks' suite,
  about seven minutes of real-time takes. Run it as well, whole, when a change touches
  the take path (`record()`, Stop, latency, pre-roll), `AudioCheckRunner`,
  `CalibrateAudioDialog` or `main/dev/`; "both whole suites" then means all three.
- From PowerShell or cmd, `.\build.bat test` runs everything through `meson test`.
- Give the app suite a tool timeout of 20 minutes, or run it in the background.

In a **Linux cloud session** the commands are others. The session's hook has started a
build into `build/` in the background; run `deploy/linux/cloud-session.sh wait` before the
first build or test (if it says none was started, run `deploy/linux/cloud-session.sh start`
first). The rest is in [docs/building.md](docs/building.md#building-on-linux).

### CI's jobs, run here

The workflows in `.github/workflows/` run only when started by hand, so nothing checks a
push on GitHub. **Before every push, run here each CI job this machine can run**, on the
tree being pushed. A change to Markdown alone needs none. The desktop jobs differ from the
builds above: they build `release`, which has none of the development checks, and the Linux
job builds against Ubuntu's Qt 6.4, where a string-based connect fails silently. So they
build in a directory of their own, `build_ci/`.

| Job | Runs on | Here |
| --- | --- | --- |
| Windows (`windows.yml`) | the Windows machine | a release build, `meson test` |
| Linux (`linux.yml`) | a Linux cloud session | a release build against Qt 6.4, `meson test` |
| Android (`android.yml`) | a Linux cloud session | the APK; not needed for a change to tests alone |
| macOS (`macos.yml`) | nowhere | always left to the user |

On the Windows machine, from Git Bash (a first build of `build_ci/` takes up to 30 minutes;
the tests about 13, in the background):

```sh
export PATH="/c/msys64/mingw64/bin:$PATH" MINGW_PREFIX="C:/msys64/mingw64"
[ -f build_ci/build.ninja ] || meson setup build_ci --buildtype release > tmp/ci.log 2>&1
ninja -j 4 -C build_ci >> tmp/ci.log 2>&1; echo "exit:$?" >> tmp/ci.log; tail -5 tmp/ci.log
mkdir -p tmp/tl-ci
TONY_TEST_LOG_DIR=$(cygpath -m "$PWD/tmp/tl-ci") meson test -C build_ci --print-errorlogs --num-processes 1 > tmp/ci-test.log 2>&1; echo "exit:$?"
grep -a -E -A3 "^(FAIL!|XPASS|QFATAL)" tmp/tl-ci/*.txt; tail -20 tmp/ci-test.log
```

In a Linux cloud session (Qt 6.4 is installed once per session; `meson setup` must report
`qt6 ... found: YES 6.4.2`; a first build takes about 5 minutes, the tests about 12, in the
background, with nothing else building):

```sh
apt-get install -y -q --no-install-recommends qt6-base-dev qt6-base-dev-tools qt6-svg-dev qt6-pdf-dev > tmp/ci-apt.log 2>&1
[ -f build_ci/build.ninja ] || CC_LD=mold CXX_LD=mold meson setup build_ci --buildtype release > tmp/ci.log 2>&1
ninja -j 4 -C build_ci >> tmp/ci.log 2>&1; echo "exit:$?" >> tmp/ci.log; tail -5 tmp/ci.log
meson test -C build_ci --print-errorlogs --num-processes 1 > tmp/ci-test.log 2>&1; echo "exit:$?"
grep -a -E -A3 "^(FAIL!|XPASS|QFATAL)|Received signal" build_ci/meson-logs/testlog.txt; tail -20 tmp/ci-test.log
```

Then, in the same session, the Android job. Its scripts take about 20 minutes the first
time in a session, for Qt and the C libraries, and a few after that
([docs/building.md](docs/building.md#building-for-android)):

```sh
deploy/android/setup-toolchain.sh && deploy/android/build-qt.sh && deploy/android/build-deps.sh &&
  deploy/android/build-tony.sh && deploy/android/build-apk.sh
```

- The `grep` prints each failed test with its details, as CI's `test-failures` step does,
  and nothing when all passed; the `tail` is meson's line per test executable.
- A job that fails here is a red CI run: fix it before pushing.
- CI checks the libraries out at `repoint-lock.json`'s pins. A fork change that is not
  pushed and pinned there is in the tree built here and not in CI's
  ([docs/forks.md](docs/forks.md)).

## Rules for working here

### Scope

- Build what was asked. Where the request is silent, take the simpler option and say so.
  No drive-by refactors. If the plan turns out wrong or impossible, do not improvise
  another design: finish what can be finished, leave the tree building and green, report.
- `main/MainWindow.cpp` and `main/test/TestRecordWorkflow.h` are each over 9000 lines.
  Never read them whole: search, then read a range.
- `svcore/`, `svgui/`, `svapp/`, `pyin/` and the other top-level library directories are
  **separate git repositories, gitignored here**, so ripgrep-based search tools skip them
  unless given the directory explicitly. Five are forks that may be changed
  ([docs/forks.md](docs/forks.md)); the rest are upstream and stay untouched. A sub-agent
  does not edit a fork: it reports the change it needs.

### Code

- Match the surrounding code: naming, comment density, idiom. Comments say *why*, in plain
  words. Every source file starts with the project's GPL header.
- State gets a class and files of its own; `MainWindow` only wires it
  (`AlternatePitchTrack`, `CoverageStrip`, `TakeLayers`, `TakeCommands` are the pattern).
- Logic that needs no window goes in `tony_core` as pure functions or plain structs
  (`TakeTiming`, `TakeEvents`), where it is cheap to test.
- The handful of rules most often broken — each explained in
  [docs/architecture.md](docs/architecture.md):
  - Tear a layer down with `m_document->deleteLayer(layer, true)` and nothing else. Never
    `removeLayerFromView()` first; never `ModelById::release()` a model the document knows.
  - Add Tony's own layers with `Document::attachLayerToView()`, never `addLayerToView()`.
  - Never push a command, or call `openPath()`, from anything reachable from an undo or
    redo. Commands hold values, never layer or model pointers.
  - Remove a pane only with `pruneExtraPane()`, and only after another layer holds the model.
  - Temporary mute/hide goes straight to the play parameters / `showLayer()`;
    `Analyser::setAudible()` / `setVisible()` write settings shared by both analysers.
  - Use member-pointer `connect`; string-based connects with `sv::` types fail silently.
  - Stop `RealtimePitchTracker` before releasing the model it reads. All model writes
    happen on the GUI thread.

### Tests

- Every behaviour gets a test **that can fail**. For the ones that matter, prove it: break
  the code briefly, see the test fail, put the code back, and say which you checked.
- Never weaken or delete a test to get green. If the behaviour was meant to change, change
  the test and say so.
- App tests run in real time against a fake audio device: keep them short.

### Git

- Commit only when asked, and only with both whole suites green. One commit per coherent
  step, staged by file name (never `git add -A`; `tmp/` and all of `.claude/` but
  `settings.json` stay out).
- Push only after the CI jobs this machine can run have passed here
  ([CI's jobs, run here](#cis-jobs-run-here)).
- Messages: `feat:` / `fix:` / `test:` / `docs:`, lower case, then a short what-and-why
  body. End with a `Co-Authored-By` trailer naming the model that wrote the code.
- Use the `gh` CLI for anything on GitHub.

### Reporting

Say what was built, by file; copy the `Totals` lines of the final full test runs verbatim;
say which tests were seen to fail; name the CI jobs run here, with their results, and those
left for the user to start by hand (macOS always); list deviations and anything fragile or
unfinished. A problem reported is cheap, one found later is not. If a change needs a real
device or real eyes to judge, name the items of
[docs/manual-checklist.md](docs/manual-checklist.md) the user should try.

### Keeping the docs true

`docs/` records what the code cannot say: why, in what order, and what must not be done.
When a change makes a statement there false, fix it in the same commit. Do not add
narratives of fixed bugs, lists of members or methods, or commit hashes — the code and
`git log` have those. `tmp/` is gitignored scratch space for logs and working notes.
