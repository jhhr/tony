# Android port — work orders for phase agents

Working document for the `feat/tonyandroid` branch. It is removed, and whatever is worth
keeping folded into the other pages, by the last phase. It is tracked only because the
cloud containers that run the work are thrown away between sessions.

You are one of a line of agents, each building **one small phase** of the Android port. A
lead reviews your work when you report back. You have no memory of earlier phases; what
you need is here.

**Your context is the budget.** Aim to finish well under 200k tokens. The rules below say
how. They are about not reading huge files whole and not maintaining big documents; they
are **not** a licence to skip what you need to understand. Careful, correct work comes
first.

## 1. What to read (and what not to)

1. This file, all of it.
2. [AGENTS.md](../AGENTS.md), all of it (short). Its rules hold, except where section 2
   below replaces them for this container (build commands, who commits).
3. The spec is [mobile-port.md](mobile-port.md) plus [port-android.md](port-android.md).
   Read the sections your work order names; search for the heading, read that range.
4. The docs page for the area you touch, as the table in AGENTS.md says. Named sections
   only, unless your work order says otherwise.
5. Code: `main/MainWindow.cpp` (over 6000 lines) and `main/test/TestRecordWorkflow.h`
   (over 2000) are **never read whole**: search, then read the range. The same goes for
   `meson.build` (about 1400 lines). Read an existing test next to where yours will go and
   copy its shape.

## 2. Rules

**Scope**

- Build your phase only. If something from a later phase is needed, build the smallest
  part of it and say so.
- The spec and the decisions in section 3 are agreed. Where they are silent, choose the
  simpler option and note it. Where they are **wrong or impossible**, do not improvise
  another design: finish what can be finished, leave the tree building and green, report.
- Do not edit the library directories (`svcore/`, `svgui/`, `svapp/`, `bqaudiostream/`,
  `bqaudioio/`, `pyin/`, ...). They are separate repositories. If one needs a change,
  report exactly what; the lead decides.
- Do not edit `.github/` unless your work order says so. Never push, never touch the
  remote, never amend or rebase.
- Match the surrounding code: naming, comment density, idiom, the GPL header. State gets a
  class of its own and `MainWindow` only wires it; window-free logic goes in `tony_core`.
- Android-only code sits behind `#ifdef Q_OS_ANDROID` (or in files the Android build alone
  compiles) and must not change the desktop build's behaviour.

**Build and test in this container** (Linux)

- Not the Windows commands in AGENTS.md: this is an Ubuntu 24.04 container, 4 cores,
  15 GB memory, no swap, running as root. Qt 6.11 is conda-forge's, in `/opt/qt6-conda`.
  In a fresh container run `deploy/linux/container-setup.sh` first (apt, Qt, the library
  directories, `meson setup build`); it is idempotent. From the repo root:

  ```sh
  mkdir -p tmp
  ninja -j 4 -C build tony pyin.so test-tony-core test-tony-app > tmp/build.log 2>&1
  echo "exit:$?" >> tmp/build.log; tail -20 tmp/build.log
  ```

  From `build/` (no environment needed: the tests set the offscreen platform themselves):

  ```sh
  mkdir -p ../tmp/tl
  TONY_TEST_LOG_DIR=../tmp/tl ./test-tony-core > ../tmp/test.log 2>&1; echo "exit:$?"
  TONY_TEST_LOG_DIR=../tmp/tl ./test-tony-app  > ../tmp/test.log 2>&1; echo "exit:$?"   # 4.5 min
  TONY_TEST_LOG_DIR=../tmp/tl ./test-tony-app undo_two_takes_in_order > ../tmp/test.log 2>&1
  grep -a "^FAIL\|^   Loc\|^Totals" ../tmp/tl/*.txt
  ```

  No `.exe` on Linux; the plugin target is `pyin.so`. Tony needs Qt 6.5 or later at run
  time (string connects with `sv::` types, see the A0 log entry).
- Send build output to a log file with the exit status written into it; look at the tail
  or grep it for errors, never read it whole.
- Tests: read results from the per-suite files in `TONY_TEST_LOG_DIR` (see AGENTS.md).
  While working run **only your tests** by name; run **both whole suites once** at the
  end, and again only if something failed. The app suite runs in real time (minutes):
  give it a 10-minute tool timeout.
- Network: GitHub (git and release downloads), the Ubuntu archive, PyPI, conda-forge,
  `download.qt.io` and `dl.google.com` are reachable (but on 2026-09-26 the egress
  gateway refused every CONNECT to `dl.google.com` for A3b's whole session; Gradle needs
  it for the Android Gradle plugin); but `download.qt.io` answers every
  Qt binary archive with a redirect to a mirror, and all mirrors are blocked (A2 builds Qt
  from source). `breakfastquay.com` and
  `ppa.launchpadcontent.net` are blocked by the environment's policy, and `hg.sr.ht`
  answers 502: do not look for mirrors of blocked hosts; report if you need them.
- Every behaviour gets a test that can fail. Show it for the two or three that matter
  most by breaking the code for a moment. Undo the break **by hand**: never
  `git checkout`/`git restore` a file to revert an experiment.
- Do not weaken or delete an existing test to get green. If one is wrong because the
  behaviour was meant to change, change it and say so.

**Docs: almost none.** The docs pages are brought up to date once, by the last phase. You
write only:

- one entry in the log (section 6), **25 lines at most**, appended at the end;
- "Done" on your phase's line in section 5, and a correction of any statement in the spec
  or in section 3 that your work proved wrong.

Edit documents with the Edit/Write tools only: a shell heredoc or one-liner containing
backticks, `$` or non-ASCII text gets mangled and has corrupted documents before.

**Commit: the lead commits.** Leave your work in the working tree, uncommitted. In the
report, list the files to stage and propose a message (`feat:` / `fix:` / `test:` /
`build:` / `docs:`, lower case, a short what-and-why body).

**Report** (your final message; all the lead sees; under 60 lines)

- What was built, by file, briefly.
- The `Totals` lines of the final full test runs, copied, not paraphrased.
- Which tests you saw fail without the change.
- Choices made, deviations, anything fragile or unfinished. Say it plainly: a problem
  reported is cheap, one found later is not.

## 3. Decisions for this port (made by the lead, 2026-09-25)

| Decision | Why |
| --- | --- |
| Android, on branch `feat/tonyandroid`; Sailfish is not being done | The user's request |
| Tony's code is built and tested on **desktop Linux in the container**; Android code is built by the Android toolchain once it is reachable | No Android toolchain here yet (blocked hosts); the desktop suites catch regressions |
| Audio backend: an `OboeAudioIO` class in `main/`, installed by overriding `createAudioIO()` in `MainWindow` on Android, not a bqaudioio fork | `createAudioIO()` is virtual in svapp's `MainWindowBase`; the test suite already installs `FakeAudioIO` that way (`TestRecordWorkflow.h`) |
| Gestures: an event filter class in `main/` on the panes, not a change in the svgui fork | Keeps the forks untouched; testable with synthetic touch events |
| Build route: first try (a), meson with an NDK cross file, a script writing the deployment JSON, then androiddeployqt; fall back to (b), an Android-only `CMakeLists.txt`, only if (a) fails for a reason that cannot be worked around | `meson.build` holds about 1400 lines of source lists that a CMake copy would have to keep in step |
| No Qt Multimedia | Its input path is not low-latency and reports no latency (port-android.md, Audio) |
| Features beyond the spec (latency calibration setting, session bundle) are not built unless a phone test shows they are needed | The spec says so |

## 4. State of the code (kept by the lead; as of 2026-09-25, after phase A1)

- The branch holds `default`, the research docs, the container setup script
  `deploy/linux/container-setup.sh`, and A1's fix.
- Both suites pass on Linux (Qt 6.11.2 from conda-forge). `TestTakesFile`'s Windows path
  assertions run on Windows only.
- A device at another rate than the reference's works: the recording is resampled to the
  reference's rate before the splice (`SingingTakes::spliceRecording(..., rate)`,
  `TakeAudio::resample()`), and `TakeTiming` converts between device frames (latency,
  frames received, the live tracker) and reference frames (`recordRate`,
  `recordedToReference()`, `referenceToRecorded()`). An Oboe backend at 48 kHz needs
  nothing more from Tony for placement.
- Known and not fixed: while recording at a device rate other than 44.1 kHz the play
  cursor runs fast (svgui's `ViewManager::getPlaybackFrame()` adds device frames). A
  `QEXPECT_FAIL` in `takes_placed_from_a_device_at_48000` marks it. It needs changes in
  svcore, svgui and svapp; the lead has not made them.

## 5. Phases

Order: A0, A1, A2, A3a, A3b, then A4 and A5 while the user tries the APK on the phone. A6
needs the result of that phone test. A8 is last. (Since 2026-09-25 `download.qt.io` and
`dl.google.com` are reachable from the container. GitHub workflows are turned off: all
builds happen in the container.)

- A0 — Desktop build and tests in the container. Done.
- A1 — Sample rate: a device that is not at 44.1 kHz. Done.
- A2 — Android toolchain and C libraries. Done.
- A3a — Tony builds for Android. Done.
- A3b — Tony as an APK (no audio): the test port. Built but for the APK itself: Gradle
  was blocked (`dl.google.com` refused); run `deploy/android/build-apk.sh` once it is
  allowed (see the log).
- A4 — Touch gestures on the panes. Done.
- A5 — Compact touch mode. Done.
- A6 — Oboe audio backend.
- A7 — Android files, permission and lifecycle.
- A8 — Documentation pass.

### A0 — Desktop build and tests in the container

Read: [building.md](building.md) "What is particular about this `meson.build`";
[testing.md](testing.md) "Running"; `.github/workflows/linux.yml`; `repoint-project.json`
and `repoint-lock.json`.

- A script, `deploy/linux/container-setup.sh`, that makes a fresh Ubuntu 24.04 cloud
  container able to build Tony and run both test suites: apt packages, meson, and the
  library directories at their pinned revisions. Idempotent (safe to run twice).
- `./repoint install` cannot work here: the Mercurial libraries (`dataquay`, `bqvec`,
  `bqfft`, `bqresample`, `bqaudioio`, `bqthingfactory`) are on `hg.sr.ht`, which is
  blocked. Their Git mirrors are at `github.com/breakfastquay/<name>`. The pins in
  `repoint-lock.json` are Mercurial hashes that the mirrors do not carry. `bqaudioio`'s pin
  `017ab3ed3a33` is the merge of `toggle-record-in-io` into default, which is the mirror's
  commit `7ab6de9` ("Merge from branch toggle-record-in-io"). For the others take the
  mirror commit that matches the pin by date and message if you can tell, else the
  mirror's head, and say which in the script. The Git libraries clone from GitHub at
  their pins.
- Then `meson setup` and a build of `Tony` and the two test executables, and both suites
  run headless (`QT_QPA_PLATFORM=offscreen` is the likely need).
- The suites have only ever run on Windows. A test that fails on Linux only is a finding:
  work out why. If the cause is Tony's code or a test's assumption about the platform, fix
  it in `main/` and say so; if it is in a library, report it. Do not skip or weaken tests.
- Ubuntu 24.04 ships Qt 6.4. If Tony or the forks need a newer Qt, say what fails, and
  try conda-forge's `qt6-main` as the Qt instead (the script then installs that).
- Not in this phase: any Android work, docs pages (A8 writes the building.md section).
- Report: the exact commands for setup, build and the two test runs, for section 2.

### A1 — Sample rate: a device that is not at 44.1 kHz

Read: [mobile-port.md](mobile-port.md) "Sample rate"; [recording.md](recording.md)
"Latency" and "Pre-roll and Record into Selection"; [takes.md](takes.md) "The swap", "Files
on disk", "Known limitations"; [testing.md](testing.md) "What is there to reuse" and
"How tests turned out to be worthless".

- First an app test with `FakeAudioIO` at 48000 Hz (its `Config::sampleRate`) against a
  44.1 kHz reference: record a take from a known position and check where its audio, its
  pitch and its coverage land and how long they are. Also a second take spliced into the
  first. This settles whether the suspected misplacement is real.
- If it is: make take audio match the main model's rate before it is spliced (resample
  the recording, in `tony_core`, with core tests of the resampling itself), and check the
  live tracker, the pre-roll and the latency shift at 48 kHz too.
- If it is not: keep the tests, and correct the spec's "Sample rate" section and the
  "Unverified" weak spot in [open-points.md](open-points.md) (that line only).

### A2 — Android toolchain and C libraries

Read: [port-android.md](port-android.md)
"Qt for Android", "Build and packaging"; [mobile-port.md](mobile-port.md) "Build and
tests".

- A script that fetches or checks the Android toolchain (Qt 6.11 for `android_arm64_v8a`
  plus the matching host Qt, NDK r27c, JDK 21, SDK platform 36 and build tools) and
  cross-compiles the C libraries into one prefix for `arm64-v8a`, API 28: libsndfile
  (without its codec libraries), libsamplerate, fftw3 (double: `meson.build` defines
  `FFTW_DOUBLE_ONLY` everywhere, so no float), Rubber Band 3, libogg,
  opus, opusfile, serd and sord, libmad, libid3tag (with the NDK's zlib). Pin versions.
- Leave out JACK, PulseAudio, ALSA, PortAudio, oggz and fishsound; note any other library
  that turns out to be needed.
- The toolchain is installed into the container (outside the repo, e.g. under `/opt`), so
  that A3 can iterate locally.

### A3a — Tony builds for Android

Read: [port-android.md](port-android.md) "Qt for Android", "Build and packaging";
[mobile-port.md](mobile-port.md) "Build and tests"; [forks.md](forks.md) "Changing a fork";
the A2 log entry.

- Route (a) from section 3: an `android` branch in `meson.build` (today an unknown system
  is an error), the cross file from A2 plus whatever Tony needs on top (Qt's tools), and
  Tony built as the shared library `libTony_arm64-v8a.so` that Qt for Android loads, with
  `main` exported. The test executables are not built for Android.
- A script, `deploy/android/build-tony.sh`, that configures and builds it into its own
  build directory (not `build/`).
- The pYIN and CHP plugins cross-compiled as well; how they are packaged is A3b's.
- Fixes needed for Android in the fork directories (`svcore/`, `svgui/`, `svapp/`,
  `bqaudiostream/`) may be made there this once, smallest possible, guarded for Android
  where they would change anything elsewhere, uncommitted, and listed in the report; the
  lead commits and pushes them. The upstream libraries (`bqaudioio/`, `bqvec/`,
  `dataquay/`, `vamp-plugin-sdk/`, `checker/`, `pyin/`, ...) stay untouched: work around
  in `meson.build`, or report.
- The desktop build and suites unchanged and green.
- Result: the library links; `readelf` shows it exports `main` and needs nothing outside
  the NDK's system libraries and Qt's.

### A3b — Tony as an APK, without audio: the test port

Read: [port-android.md](port-android.md) all of "Platform facts" and "Test port";
[mobile-port.md](mobile-port.md) "The pYIN plugin", "The window".

- A script that writes the deployment JSON (A2 left a model written by Qt's CMake in
  `/opt/android/logs/android-check-deployment-settings.json`) and runs androiddeployqt; a
  custom `AndroidManifest.xml` (with `RECORD_AUDIO` for later).
- Audio: `AUDIO_NONE` under `Q_OS_ANDROID` for now.
- The menu bar: exclude `Q_OS_ANDROID` from the `Q_OS_LINUX` `setNativeMenuBar(false)`
  only if the options menu works better on a phone; otherwise keep the in-window bar and
  say why.
- pYIN found on the phone: `libpyin.so` naming plus legacy packaging, or linking it in.
  The log must show "Setting VAMP_PATH to ...".
- The desktop build and suites unchanged and green.
- No CI workflow: the user has turned the repository's workflows off. The build is a
  script that runs in the container.
- Result: an APK the user can sideload; the lead hands it over.

### A4 — Touch gestures on the panes

Read: [architecture.md](architecture.md) "Selection and tools", "Signals";
[mobile-port.md](mobile-port.md) "The window".

- A class in `main/` that filters the panes' touch events: pinch zooms the time axis
  about the pinch centre, two-finger drag scrolls, long-press opens the pane's right-button
  menu. One-finger touches stay as today (Qt's synthesised mouse events).
- Desktop mouse behaviour unchanged. App tests with synthetic touch events
  (`QTest::touchEvent` and a `QPointingDevice`).

### A5 — Compact touch mode

Read: [mobile-port.md](mobile-port.md) "The window", "Work common to both ports";
[architecture.md](architecture.md) "Where things are", "Who owns what".

- A class in `main/`, wired by `MainWindow`; on by default on Android, and switchable on
  the desktop (a View menu toggle and a `--compact` option) so it can be tested there.
- One toolbar: Play, Record, Record into Selection, the Take box, Undo, Redo, Erase (the
  Ctrl+D action), zoom in and out, and one menu button holding the menus. The Show and
  Play toggles and gains in a panel that slides out. The note-editing tools and the audio
  device menus hidden.
- Switching back restores the desktop layout exactly. Tests for both directions.

### A6 — Oboe audio backend

After the user's phone test of A3b. Detailed when it starts.

### A7 — Android files, permission and lifecycle

Detailed when it starts: microphone permission, stopping audio and saving on suspend,
opening a reference or session through the picker by copying it into app storage, and
exporting.

### A8 — Documentation pass

- Bring the docs pages up to date from the code and the log: building.md (the container
  script, the Android build), testing.md, architecture.md (new classes),
  manual-checklist.md (phone items), mobile-port.md and port-android.md (what was built,
  known limitations, open points), open-points.md. Fold anything still useful from this
  file into those pages, then delete this file and its row in docs/README.md if any. No
  code: suspected bugs go in the report.

## 6. Log (newest last; 25 lines at most per entry)

Template:

    ### Phase <id> — <date>
    Built: ...
    Choices / deviations: ...
    The next phase must know: ...
    Left open: ...

### Phase A0 — 2026-09-25
Built: `deploy/linux/container-setup.sh` (apt packages, conda-forge `qt6-main` 6.11.2 in
`/opt/qt6-conda` through micromamba, the libraries at their pins, `meson setup build`;
`--build` also builds). `main/test/TestTakesFile.h`: its back-slash and case-insensitive
path assertions now run on Windows only, with Linux counterparts.
Choices / deviations:
- Qt 6.11 from conda-forge, not Ubuntu's 6.4. Tony compiles with 6.4, but pYIN results
  never reach `Analyser`: its `SIGNAL()`/`SLOT()` strings say `ModelId` and `sv_frame_t`,
  moc records `sv::ModelId`, and only Qt 6.5 and later match the two through their
  registered metatypes (`methodMatch()` in Qt's `qmetaobject.cpp`). `MainWindow.cpp`'s
  `doubleClickSelectInvoked(sv_frame_t)` connect is the same. Tony needs Qt >= 6.5 at run
  time; nothing in `meson.build` says so.
- Mercurial pins matched to mirror commits by date (reasons in the script): dataquay is
  `2dbf1be`, not the mirror's head; the other four are their heads. sv-dependency-builds
  is not cloned (Linux does not use it). Rubber Band is Ubuntu's 3.3.0.
The next phase must know: only `Qt6*.pc` are on meson's pkg-config path; the RUNPATH is
`/opt/qt6-conda/lib`, so libasound and libstdc++ come from there at run time. No `.exe`;
the plugin target is `pyin.so`. Clean build about 5 min at `-j 4`, 0.8 GB per compile job;
app suite 4.5 min.
Left open:
- `svapp/audio/AudioCallbackRecordTarget.cpp:291` connects to `aboutToBeDeleted()`, which
  no model has: a warning in every recording test, on every platform. Not touched.
- architecture.md "Signals" says such string connects never match: true for pointers
  (`Layer *`), but registered types match from Qt 6.5. For A8.

### Phase A1 — 2026-09-25
Built: the misplacement was real (`FakeAudioIO` at 48 kHz: take audio at 0.919 × its
place, coverage 8.8% too long). `TakeAudio::resample()` (streaming, bqresample's
libsamplerate medium sinc, as svcore uses) and `sampleRate()`; `spliceRecording(..., rate)`
converts a recording at another rate in a `QTemporaryDir` beside the take's files before
the splice, so take WAVs are always at the reference's rate. `TakeTiming::recordRate`:
L and every count off the record target are device frames; `spliceOffset()`,
`liveFrameIntoTake()` and the countdown answer in the reference's, `autoStopFrames()` in
the device's. `MainWindow` fills `recordRate` from the recording model, converts the play
source's output latency and block (`m_recordFramesPerPlayFrame`) to device frames, and
gives the live-dot model the main model's rate.
Choices / deviations:
- Sung test input at 48 kHz is sines at 220.5/294 Hz: pYIN needs whole-sample periods at
  44.1 kHz, and sawtooths whole at both rates went an octave low by alignment.
- `getTargetPlayLatency()` is taken as reference-rate frames: bqaudioio's ResamplerWrapper
  converts it only if the device opened after a model was loaded (else ~1-3 ms off).
- The 48 kHz start gap is ~25 frames short of the first audible sample: the wrapper's
  resampler delays playback and reports nothing. The test allows 64 frames.
The next phase must know: recordings stay at the device's rate; take audio is converted.
Left open:
- svgui fork: while recording, `ViewManager::getPlaybackFrame()` adds device frames, so the
  cursor runs 8.8% fast at 48 kHz (QEXPECT_FAIL in `takes_placed_from_a_device_at_48000`).
- A loaded singing track not at 44.1 kHz: the splice refuses it and erase misplaces.
- For A8: takes.md "Known limitations" (last bullet), open-points.md weak spot,
  recording.md "Latency" (units), testing.md (tones for another device rate).

### Phase A2 — 2026-09-26
Built: `deploy/android/` `setup-toolchain.sh` (apt packages, SDK tools 22.0, android-36,
build-tools 36.0.0, platform-tools, NDK r27c), `build-qt.sh` (host Qt and Qt for Android
6.11.2, qtbase + qtsvg, from source), `build-deps.sh` (static PIC libraries, the cross file,
a link check). Run in that order; fresh container 21 min (Qt 18); idempotent.
Paths: JDK `/usr/lib/jvm/java-21-openjdk-amd64`; SDK `/opt/android/sdk`; NDK
`/opt/android/sdk/ndk/27.2.12479018`; Qt for Android
`/opt/android/qt/6.11.2/android_arm64_v8a`; host Qt `/opt/android/qt/6.11.2/gcc_64` (moc,
rcc, uic in `libexec/`); prefix `/opt/android/deps-arm64-v8a`; cross file
`/opt/android/cross-arm64-v8a.ini`, whose `pkg_config_libdir` is the prefix's
`lib/pkgconfig` and `boost_root` the prefix; logs `/opt/android/logs`.
Choices / deviations:
- Qt from source (GitHub's qt/ mirrors; newest 6.11 tag 6.11.2, the desktop's too):
  download.qt.io redirects every archive to a mirror, all denied. No OpenSSL, SQL, printing.
- fftw3 double (`FFTW_DOUBLE_ONLY`), NEON, Debian's fix for its arm64 NEON probe; no fftw3f.
- Added bzip2 (`BZipFileDevice` includes bzlib.h unconditionally), Boost 1.83 headers
  (pYIN), zix (sord). libmad 0.16.4 / libid3tag 0.16.3 (maintained fork, .pc files). Rubber
  Band 3.3.0, built-in FFT and resampler. SDK tools 22.0: 23.0's sdkmanager wraps a new CLI.
The next phase must know: `dependency(x, static: true)`, or the private libraries (ogg,
opus, zix, serd) are missing; meson then links the NDK's `libz.a`. Model deployment JSON:
`logs/android-check-deployment-settings.json` (qt-cmake's). Gradle's first run got 429 Too
Many Requests from repo.maven.apache.org: expect to retry.
Left open: Oboe (A6) not built; sdkmanager takes the latest android-36/platform-tools revision.

### Phase A3a — 2026-09-26
Built: an `android` branch in `meson.build` (the Linux branch's defines less JACK, PulseAudio,
ALSA, PortAudio, oggz, fishsound; `HAVE_OPUS_READ_ONLY`; every library `static: true`); there
Tony is `shared_library('Tony_arm64-v8a')` with `-Wl,--exclude-libs,ALL`, and the tests are
left out. `deploy/android/qt-arm64-v8a.ini` (a second cross file: Qt for Android's `qmake`),
`deploy/android/build-tony.sh` (configure, build, check with readelf), `build-android` in
`.gitignore`. No change in `main/` or the forks: everything compiled at the first attempt.
Build: `deploy/android/build-tony.sh` (`--wipe` to configure afresh); 4 min from scratch. By
hand: `meson setup build-android --cross-file /opt/android/cross-arm64-v8a.ini --cross-file
deploy/android/qt-arm64-v8a.ini --buildtype=debugoptimized`, `ninja -j 4 -C build-android`.
Logs: `/opt/android/logs/tony-setup.log`, `tony-build.log`.
Choices / deviations:
- meson 1.3.2 (Ubuntu's) finds Qt through that qmake (Qt for Android has no `.pc` files) and
  takes moc, rcc, uic from its `QT_HOST_LIBEXECS`: meson issues 13018 and 6089 did not arise.
- debugoptimized, like the desktop builds: asserts on, debug information (108 MB; 11.5 stripped).
The next phase must know: `build-android/libTony_arm64-v8a.so` needs Qt6 Core, Gui, Widgets,
Xml, Network, Svg, and libc, libm, libdl, libc++_shared (zlib is linked in); it exports `main`,
`qInitResources_tony` and main.cpp's inline functions, nothing from the static libraries.
`pyin.so`, `chp.so` beside it: no `lib` prefix yet, exporting `vampGetPluginDescriptor` only.
androiddeployqt 6.11 has no strip option: check that the APK's copies are stripped (Qt's
Gradle template sets `ndkVersion`, so the Android Gradle plugin should strip them).
Left open: Qt's `Test` module stays in the dependency for Android; `--as-needed` drops it.

### Phase A3b — 2026-09-26
Built: `deploy/android/build-apk.sh` (after build-tony.sh; writes
`build-android/android-Tony-deployment-settings.json`, runs androiddeployqt and Gradle,
checks the APK: `build-android/apk/Tony-debug.apk`, log `/opt/android/logs/tony-apk.log`);
`deploy/android/package/AndroidManifest.xml` (`io.github.jhhr.tony`, RECORD_AUDIO,
sensorLandscape). `main/AndroidFiles` (tony_core; `TestAndroidFiles`). main.cpp on Android:
stdout/stderr to logcat, `AUDIO_NONE`, the plugin links, a box if they fail. `MainWindow::
getOpenFileName()` on Android copies a `content://` pick to `<files>/imported/<name>`.
Choices / deviations:
- Plugins: Android installs only `lib*.so`, svcore names a plugin after its file, and the
  native library folder holds all of Qt: so the APK has `libpyin.so`, `libchp.so` (in
  `libs/` directly: `android-extra-libs` would load them at every start), legacy
  packaging, and VAMP_PATH is `<files>/vamp`, links `pyin.so`, `chp.so` to them, remade
  at each start. `applicationDirPath()` is that folder (argv[0] is the library's path).
- Our three libraries are stripped by the script; the Qt plugins are CMake's list (A2).
- The menu bar stays in the window: a native one needs an action bar, taller than it.
- A `.ton` from the picker is refused with a message (it needs its folder).
Blocked: the egress gateway refused every CONNECT to `dl.google.com` (Google's Maven: the
Android Gradle plugin) all session, so there is no APK yet. All up to Gradle was run and
checked (libraries 16 KB aligned, their NEEDED all packaged or Android's; manifest).
The next phase must know: logcat tag `Tony` has Tony's and svcore's cerr; SVDEBUG goes only
to `files/log/sv-debug.log` (`adb shell run-as io.github.jhhr.tony cat ...`). Phone test:
install; File > Open, pick audio: waveform, then pitch and notes; Play is off (no audio).
Left open: saving and sessions through the picker (A7); `imported/` is never emptied.

### Phase A4 — 2026-09-26
Built: `main/TouchGestures` (tony_app; `MainWindow::paneAdded()` gives each pane one): pinch
zooms the time axis about the fingers, two fingers scroll it, a 500 ms long press sends the
pane a right press (its menu path). `main/PinchZoom` (tony_core): the wheel's zoom grid and
limits for a pinch, with 2% hysteresis; View's x mapping made continuous. Tests:
`TestPinchZoom` (core), `TestTouchGestures` (app; window shown, touch via QTest::touchEvent).
Qt 6.11 (qapplication.cpp, qguiapplication.cpp): only an unaccepted touch event becomes mouse
events, from its first finger; an unaccepted TouchBegin's points go to the first widget above
that subscribes to a gesture: QScrollArea's viewport (PanGesture), which then takes the
second finger too. So the pane subscribes to a gesture that recognises nothing; a lone finger
stays unaccepted (Qt's mouse events, as before); the second finger's event reaches the pane
with both points; from there events are accepted except the one lifting the first finger.
Choices / deviations:
- The first press is held until the finger moves past startDragDistance or lifts (then
  replayed in order) or 500 ms pass: a long press or second finger leaves no drag, selection
  or playhead move. A drag already going is ended by a release where the finger is.
- The long-press menu opens under the finger, its first item Undo; QMenu takes a release
  after 7 moves over an item as a choice. That finger's events are eaten on the menu until
  it lifts, so the menu waits for a tap (a test shows the lift choosing it otherwise).
- Vertical two-finger movement is ignored. The viewport's own pan gesture still scrolls the
  pane stack vertically if it can, as before A4. Pinch starts at 2x startDragDistance.
The next phase must know: touch tests need the window shown and a fresh device per test.
Left open: not tried on a touch screen. Windows desktop touch (OS-made mouse events, its own
press-and-hold right click) untested. For A8: architecture.md, testing.md, manual-checklist.

### Phase A5 — 2026-09-26
Built: `main/CompactLayout` (tony_app): one toolbar in place of the menu bar, every other
toolbar and the overview. View > Compact Layout switches it; main() switches it on before the
window is shown on Android and with `--compact` (`isWantedAtStart()`). Buttons: Menu (a popup
holding the menu bar's own menus), Play, Record, Record into Selection, the take box, Undo,
Redo (`CommandHistory::registerToolbar()`), Erase, Zoom In, Zoom Out, Show and Play (shows
the two bottom toolbars). 40 px icons; the text-only buttons as tall. Hidden while compact:
the Navigate and Edit tools (Navigate is selected first), the two audio device submenus, the
menus' tear-off handles. `MainWindow`: members for what were locals, `setupCompactLayout()`,
`setCompactLayout()`; toolbars named (saveState); Erase's iconText "Erase". `TestCompactLayout`.
Choices / deviations:
- The take box is moved (its QWidgetAction, back before the action that followed it), not
  copied: one widget, nothing to keep in step.
- A shortcut needs a visible widget holding its action or its menu: with only the hidden menu
  bar holding the menus, every menu-only shortcut dies. The popup's button carries them.
- Kept: the status bar (pre-roll countdown, sung note). No property stacks to hide (Tony uses
  `NoPropertyStacks`). The spectrogram stays a panel toggle; the Edit and long-press menus
  keep their pitch and note actions (only the tool modes go). Not saved in the settings.
- `test-tony-app` now links `tony.qrc`: without icons every button is its text, and the bar
  was 1023 px wide (703 with icons); the other app suites pass with the icons too.
The next phase must know: switching off restores what switching on saved; the tool mode stays.
Left open: not seen on a phone: the take box's height (22 px in Fusion), popup menu rows, and
under ~710 dp wide the last buttons go into the toolbar's extension. For A8: architecture.md,
testing.md (icons in the app tests), mobile-port.md, manual-checklist.md.
