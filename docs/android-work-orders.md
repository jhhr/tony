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

  For the whole suites, `default`'s sharded runner is quicker (the app suite in about a
  minute and a half) and prints a per-suite summary with every failure; from the repo
  root: `deploy/linux/run-tests.sh test-tony-core > tmp/core-run.log 2>&1` and the same
  for `test-tony-app` and `test-tony-dev` (the dev checks' suite, since the merge of
  `default`; build it with the others). Under its load `stale_pitch_event_ignored` failed
  once at `QVERIFY(model)` and passed alone and on the next run.

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

- 2026-09-26, after A7c: `default` merged in (`fb5fa6f`): lyrics, Calibrate Audio (a
  measured round trip, `roundTripAt()`, which converts through seconds per rate), the dev
  checks (`test-tony-dev`; compiled into non-release builds, the Android one included), the
  sharded test runner, the live dot throttle and cache exclusion. svgui is pinned at
  `c685b97`, checked out as its branch `feat/tonyandroid`. The test window class is in
  `main/test/TestMainWindow.h`. All three suites green; the Android build links.

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
- While recording at a device rate other than 44.1 kHz the play cursor ran fast (svgui's
  `ViewManager::getPlaybackFrame()` added device frames). Fixed in A11 with svgui alone
  (`ViewManager::setRecordFrameRatio()`, which `record()` sets once the recording has
  started), not svcore and svapp as was thought; the `QEXPECT_FAIL` is gone.

## 5. Phases

Order: A0, A1, A2, A3a, A3b, then A4 and A5 while the user tries the APK on the phone. A6
needs the result of that phone test. A8 is last. (Since 2026-09-25 `download.qt.io` and
`dl.google.com` are reachable from the container. GitHub workflows are turned off: all
builds happen in the container.)

- A0 — Desktop build and tests in the container. Done.
- A1 — Sample rate: a device that is not at 44.1 kHz. Done.
- A2 — Android toolchain and C libraries. Done.
- A3a — Tony builds for Android. Done.
- A3b — Tony as an APK (no audio): the test port. Done; the lead built the APK once
  `dl.google.com` was allowed again, and the user's first phone test passed (findings in
  A7).
- A4 — Touch gestures on the panes. Done.
- A5 — Compact touch mode. Done.
- A6 — Oboe audio backend. Done.
- A7 — Sessions in place on the phone, and fixes from the first phone test. Done.
- A7b — Fixes from the second phone test: menus, the picker, Downloads. Done.
- A4b — Vertical zoom and scroll by touch. Done.
- A7c — M4A/AAC and other formats through Android's decoders; no autosave of an incomplete session. Done.
- A9 — Live dots in real time on the phone. Done.
- A10 — Plot elements sized for the screen. Done.
- A11 — Fixes from the fifth phone test: the cursor at a 48 kHz device, Save Log. Done.
- A4c — Vertical zoom keeps the pitch in view. Done.
- (Lead, 2026-09-26: lyrics at 65 % on Android, svgui `setLyricsTextScale()`; `feat/wasapi`
  merged in for Calibrate Audio at 48 kHz.)
- A12 — Calibrate Audio on the phone. Done (the dialog's size and layout left to A12c, by the
  lead's change of scope).
- (Lead, 2026-09-26: View > Lyrics Size, 35-100 %, 50 % by default on Android; the
  size drawn is logged.)
- A12c — The Calibrate Audio dialog: small, and out of the way while a check runs. Done.
- A12b — The dev run on the phone. Done.
- A13 — Fixes from the dev runs on the phone: idle input latency, a stream disconnected
  while idle, tones a phone can play, pitch that cannot be judged. Done.
- (Lead, 2026-09-26, from the fourth dev run, which ran the build before A13: the input
  latency read 23219 frames, more than the input buffer holds, so what inflates it is input
  lost to overruns, not input waiting. `OboeAudioIO` refuses a reading after an input
  overrun (`getXRunCount()`), which A13's guard on waiting input did not see; the log's
  first line gives the installed version name, with its commit.)
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

Read: [port-android.md](port-android.md) "Audio", "Permissions and lifecycle";
[mobile-port.md](mobile-port.md) "Audio I/O", "Sample rate"; [recording.md](recording.md)
all of it; the A1 log entry.

- Oboe (github.com/google/oboe, a pinned 1.x release) built as a static library into the
  A2 prefix by `deploy/android/build-deps.sh`, with whatever meson needs to find it.
- `OboeAudioIO` in `main/`, compiled for Android only: a `breakfastquay::SystemAudioIO`
  as `PortAudioIO` (bqaudioio) and the tests' `FakeAudioIO` are. One full-duplex callback
  (`oboe::FullDuplexStream`), input handed over before output is asked for, low-latency
  performance mode, the device's native rate (A1 made Tony handle any rate), latencies
  reported through `setSystemRecordLatency()` / `setSystemPlaybackLatency()` from Oboe's
  estimates, `suppressRecordSide()` honoured, playback-only when there is no input.
- `MainWindow::createAudioIO()` overridden on Android to install it, as the tests install
  `FakeAudioIO`; `AUDIO_NONE` on Android removed.
- The microphone permission (`QMicrophonePermission`) asked for before input is opened;
  without it, playback only, and the user told why.
- A device change (headphones in or out) reopens the streams rather than going silent.
- Pure arithmetic (latency from timestamps and the like) in `tony_core` with core tests;
  the rest can only be judged on the phone.

### A7 — Sessions in place on the phone, and fixes from the first phone test

Decided with the user (2026-09-26): sessions live in ordinary folders on the phone, which
a sync app (Syncthing, FolderSync, Dropsync, OneSync) mirrors with the desktop, and Tony
works on them in place with Android's "All files access" (`MANAGE_EXTERNAL_STORAGE`, fine
for a sideloaded app). No session bundle.

Read: [port-android.md](port-android.md) "Files, storage and cloud apps", "Permissions and
lifecycle"; [mobile-port.md](mobile-port.md) "Files and sessions"; [takes.md](takes.md)
"Files on disk", "The session file"; the A3b log entry (`AndroidFiles`, the
`getOpenFileName()` override).

- All files access: declared in the manifest; asked for (a short explanation, then the
  system's settings page for it) the first time a session is opened or saved, or a picked
  file lies in the phone's own storage; checked with `Environment.isExternalStorageManager()`.
- A picked `content://` URI from the phone's own storage (the external storage provider,
  and the downloads provider's `raw:` ids) is turned back into a real path (pure function
  in `tony_core`, core tests). With access, that path is what Tony opens and saves:
  sessions open and save in place with their takes folder, audio opens in place.
- Without a path (Drive, Dropbox, OneDrive, or no access): audio is copied in as now; a
  session is refused with a clear message, and a save does not leave an empty file
  behind (the picker creates the document before Tony writes).
- First phone test (user, A3b/A5 APK): no crash; loading, analysis, zoom and scroll work.
  Fix: menus taller than the screen cannot be scrolled (items off-screen) — make menus
  scrollable on Android; Save Session As suggests no file name and accepts an empty one;
  the save error message came out garbled because the `content://` URI's `%3A`/`%2F`
  were taken for `QString::arg()` placeholders by chained `.arg()` calls.
- On `Qt::ApplicationSuspended`: stop playback, and finish a take being recorded as Stop
  does; save the session if it has a path and is modified.
- The 48 kHz play cursor (A1) is not this phase's: the user expects a fix on `default`.

### A7b — Fixes from the second phone test: menus, the picker, Downloads

The user's second phone test (APK at 3062d25), 2026-09-26. Worked: menu scrolling; the
All files access prompt; Save Session As with a suggested name, saving once access was
granted; playback stopping in the background. Did not:

- The topmost menu item (Open, in the File menu) is hard to tap: a tap at the very top
  edge of the screen hardly registers. Menus must keep clear of the screen's edges and of
  the system bars (`QScreen::availableGeometry()`, the window's safe area margins in Qt
  6.9+), with a margin, wherever Qt places them.
- Files in Google Drive cannot be picked: tapping one in the picker does nothing. Suspect
  the picker's MIME type filter (Qt turns name filters into `EXTRA_MIME_TYPES`; a provider
  disables files whose type is not listed): check `qandroidplatformfiledialoghelper.cpp`
  and what Tony's filters become; on Android offer all files and check the type after.
- A file downloaded from Drive into the phone's storage then failed to open, audio as
  well as `.ton`, "with the same error as before" (the user suspected the access grant
  had been lost). Likely: picks from Downloads (`msf:` ids) and from the picker's Recent
  and Audio roots (the media provider's `audio:` ids) have no path in A7's mapping. With
  All files access, MediaStore gives their path (the `_data` column, through the
  `ContentResolver`); map those too. The refusal message names the provider, so that a
  case still unmapped can be reported; and log the URI.

Later evidence (the user): "File or URL ... could not be opened" came from Open Recent
after the file had been moved; "File does not exist ... content://...primary%3AMusic%2F(vocals)
Avi Kaplan - Peace Somehow.ton" came from svgui's `InteractiveFileFinder`, whose
`QFileInfo::exists()` on the URI Qt's content file engine answers wrongly for a name with
parentheses.

### A4b — Vertical zoom and scroll by touch

The same phone test: the pitch track is tiny, because the pane's frequency range is far
wider than the singing, and pinch zooms only the time axis.

- A pinch zooms each axis by the spread of the fingers along it: horizontal spread the
  time axis as now, vertical spread the frequency range, about the frequency under the
  pinch centre; a diagonal spread both. A two-finger drag scrolls vertically too.
- The frequency range is the analyser's: `Analyser::getDisplayFrequencyExtents()` /
  `setDisplayFrequencyExtents()`, which View > Edit Display Extents
  (`MainWindow::editDisplayExtents()`) uses. Check the scale (log for pitch), the limits,
  and what the other layers in the pane do when it changes.

### A7c — M4A/AAC through Android's decoders; no autosave of an incomplete session

The user's third finding (2026-09-26): a desktop session whose reference is
"(vocals) Avi Kaplan - Peace Somehow.m4a" opens on the phone (the reference is found by name
next to the .ton, the stored path being `c:/Users/.../OneDrive/Singing/...`), but reports
"Incomplete session loaded": the Android build has no AAC decoder. On Windows, M4A is read
by bqaudiostream's `MediaFoundationReadStream`; on Android only WAV (sndfile), MP3 (mad) and
Opus (opusfile) are read.

- An `AudioReadStream` over the NDK's `AMediaExtractor` and `AMediaCodec` (libmediandk,
  API 21+), in `main/`, compiled for Android only, registered with bqaudiostream's factory
  the way its own readers are (`static AudioReadStreamBuilder<...>` with a URI and the
  extensions; see `bqaudiostream/src/MediaFoundationReadStream.cpp` ~84): m4a, aac, mp4,
  and whatever else the phone's decoders take that Tony cannot read already (flac, ogg,
  opus are candidates; say which, and what wins when two readers claim an extension).
  svcore's `BQAFileReader` asks that factory, so no fork changes. The registration must
  survive linking (`link_whole` into the application library keeps it; check).
- Decoded to float at the file's own rate and channels, as the other readers give; Tony
  resamples on load (A1's notes). Seeking is not needed if the other readers do not seek.
- Pure parts (format bookkeeping, sample conversion) testable on the desktop; the decoder
  itself only on the phone.
- A session that loaded incomplete (audio it names could not be read) is never saved
  without the user asking: not by the save on suspend (A7), and Save warns first. Find
  where svapp reports "Incomplete session loaded" and how Tony can know it happened.

### A9 — Live dots in real time on the phone

The fourth phone test (2026-09-26): the Avi Kaplan session and a direct `.m4a` open, but
while recording the orange live dots lag **several seconds** behind the singing on the
phone. On the desktop they keep up. Nothing else about recording was reported yet.

That APK was built before `default` was merged in (2026-09-26, `fb5fa6f`). `default` had
fixed the same symptom on the desktop in two steps: `ModelChangeThrottle` tells the pane of
new dots at most every 40 ms instead of once per dot, and the dots layer is kept out of the
pane's cache (`Layer::setCachedInView`, svgui fork), so a notice no longer has the pane draw
the reference's waveform, pitch track and notes again (see `docs/recording.md` and the
messages of `2a20de5` and `7fb4174`: GUI thread from ~80 % to ~22 % of a core in a
1920 px window on this container). Nobody has measured that on a phone. So:

What is known from the code (not measured):

- `RealtimePitchTracker` (its own thread) still emits `pitchDetected()` **once per hop**,
  256 frames, about 172 signals a second. Each is a queued call into
  `MainWindow::onRealtimePitchDetected()`, which adds one point to the live model, tells
  the throttle, and sets the status bar text. A GUI thread that needs more than about
  5.8 ms per estimate falls behind for good, and the lag grows for as long as the take
  lasts.
- The recorded audio reaches the model on the GUI thread too: svapp's
  `AudioCallbackRecordTarget::updateModel()` every 10 ms (the fork's timeout; upstream about
  200 ms) writes to the file and calls `WritableWaveFileModel::updateModel()`, which closes
  and reopens the file (`WavFileReader::updateFrameCount()`). The tracker can only see
  what that has written.
- svgui's `View` paints layers into an image at `ceil(devicePixelRatio)`: 3 on the phone
  (2.75), nine times the pixels of the desktop. The pane follows the playback cursor (20 ms
  timer, svgui fork) while recording.
- `main.cpp` copies every `cerr` line to logcat and to the log file.

Wanted:

- **Measure first, in the container**: the desktop build, at `QT_SCALE_FACTOR=3` with a
  window of a phone's logical size (about 400 x 850), a reference of a few minutes with its
  pitch track and notes, recording through `FakeAudioIO`. Find what the GUI thread spends
  per estimate, per record update, and per paint of the pane, and whether the tracker's
  thread itself keeps up. A phone core is several times slower than this container's:
  say which costs would scale to a lag.
- **Fix so the lag is bounded by design**, whatever the phone's speed: the dots come to the
  GUI thread in batches (everything the tracker found since the last one) at a paced rate,
  go into the model together, and the status bar is set once per batch. If the GUI thread
  is slow, the dots arrive later in bigger batches, but never a growing queue behind. Keep
  `default`'s throttle and cache exclusion; fold the throttle into the batching if that is
  simpler, saying so. Fix any other per-estimate or per-update cost the measurement shows,
  the simpler way.
- **A log line once a second while recording**, so the phone's log (Help > Save Log...)
  says whether it holds: seconds recorded, seconds the tracker has reached, seconds of
  dots drawn, and the GUI-side costs measured above (e.g. slot time, paint time of the
  pane, largest and average). Keep it to one line a second.
- A test that fails with the per-estimate design: e.g. a GUI thread made slow on purpose
  in the test, and the newest dot must stay within a bound of the recording.
- If the measurement points into svgui (the pane repaints its whole image for each point,
  say), you may change the fork `svgui/` for it: on its branch `feat/tonyandroid`
  (checked out; fork work for a Tony branch goes on a fork branch of the same name),
  committed there with its own style (`view: what`), not pushed; the lead pushes it and
  pins it. `svcore/` and `svapp/`: report the change, do not make it.

### A10 — Plot elements sized for the screen

The same phone test: the pitch tracks are thin lines and the notes thin bars, hard to make
out, while the rest of the GUI is sized for the phone. A4b's vertical zoom spreads the
pitch but does not thicken what is drawn. The user: "the GUI elements have been adequately
resized to fit the higher DPI resolution of the mobile screen but the graph plot elements
have not."

What is known from the code: svgui's `View` renders the layers into an image at
`ceil(devicePixelRatio)` (3 on the phone) through a `ViewProxy` whose coordinates are in
those physical pixels. Sizes that the layers give in pixels are then physical pixels:
`TimeValueLayer`'s `PlotPoints` (Tony's pitch tracks and the live dots) draws each point as
`drawRect(x, y - 1, w, 2)`; `FlexiNoteLayer` draws notes `NOTE_HEIGHT` (16) high; and
`ViewProxy::scalePenWidth()` scales pens by only the square root of the ratio. So on the
phone a pitch point is about a third, and a note a third, of its desktop height in logical
pixels.

Wanted:

- What Tony draws in its panes (the reference and singing pitch tracks, the live dots, the
  notes, the coverage strip, anything else of Tony's that looks too thin) keeps its size
  in **logical pixels** at any pixel ratio: at ratio 3 it is three times as many physical
  pixels as at ratio 1. Note editing (hit areas that use `NOTE_HEIGHT`) must match what is
  drawn.
- A **plot size** setting for making them bigger still: View menu, a few steps (say 100%,
  150%, 200%), remembered in `QSettings`, applied at once. Default 100% on the desktop,
  150% on Android. The same on both platforms otherwise.
- The desktop at ratio 1 and 100% draws **exactly as before**: prove it with a test that
  renders the layers into images before/after, or equivalent.
- The change is in the fork `svgui/`, which you may edit for this phase: on its branch
  `feat/tonyandroid` (checked out; fork work for a Tony branch goes on a fork branch of
  the same name), as small and general as it can be (e.g. a plot scale in
  `ViewManager` that `View`/`ViewProxy` apply, and the layers using `scalePixelSize()` for
  their hard-coded sizes), committed there with its own style (`layer: what`,
  `view: what`), not pushed; the lead pushes it and pins it. Tony's side (the setting, the
  menu) in `main/`. Say which svgui layers change on a hi-DPI desktop (Windows at 150% or
  200%), since they do too.
- Tests: the sizes at ratio 1 and 3 and the setting's steps, in images rendered offscreen
  (`QT_SCALE_FACTOR` or a `QImage` with a device pixel ratio).

### A11 — Fixes from the fifth phone test: the cursor at a 48 kHz device, Save Log

The fifth phone test (2026-09-26, APK at `1979ccc`): the live dots now keep up and sit
where they should, and the reference plays in time during a take; pitch and notes are
easier to see. Two faults:

- **While recording, the play cursor races ahead** of the dots and of what is heard, further
  and further as the take goes on. This is the known fault of section 4 (A1): svgui's
  `ViewManager::getPlaybackFrame()` gives, while recording, `m_recordStartFrame +
  m_recordTarget->getRecordDuration()`, and the duration counts the device's frames
  (48 kHz on the phone) against a pane at the reference's rate (44.1 kHz): 8.8 % fast.
  `QEXPECT_FAIL` in `takes_placed_from_a_device_at_48000` marks it.
- **Help > Save Log... saved an empty file.** The log was not empty (else "There is no log
  to save"), and no error was shown. `MainWindow::saveLog()` writes to the document the
  picker made through `AndroidStorage::openDocument(uri, "w")`, which takes the file
  descriptor with `ParcelFileDescriptor.detachFd()` and later `::close()`s it. A provider
  that opened the document with a close listener (the media store behind Downloads, a cloud
  app) is then told the client detached, not that it finished, and may drop what was
  written. Reading (`"r"`, the audio copied in by A7b) uses the same call.

Wanted:

- The cursor at the reference's frames while recording at any device rate: the smallest
  change in the svgui fork, which you may edit (branch `feat/tonyandroid`, checked out;
  `view: what`; committed there, not pushed; the lead pushes and pins). svcore's
  `AudioRecordTarget` has no rate; Tony knows it once the take has started
  (`TakeTiming::recordRate`), and already tells `ViewManager` where the take starts
  (`setRecordStartFrame()` in `record()`), so it can tell it the ratio as well. The
  `QEXPECT_FAIL` goes and the check passes.
- Documents written and read through the picker's grant are closed the way Android
  expects: keep the `ParcelFileDescriptor` and `close()` it (or write through
  `ContentResolver.openOutputStream()`), with `"wt"` for a write. After Save Log, check
  what the document holds (its size, through the resolver) and say in the log, and in the
  message if it differs, how many bytes went in and how many are there. Android-only code;
  what is pure goes in `tony_core` with a test.
- Say in the report what the phone test should look for in the log.

### A4c — Vertical zoom keeps the pitch in view

The fifth phone test: pinching to zoom the frequency range zooms about the frequency under
the fingers (A4b). The Avi Kaplan song is low, and zooming in pushes the pitch below the
bottom edge; the user: "The smart thing to do would be to keep the plot centered somehow
when zooming in."

- A vertical zoom anchors at the middle, on the pane's log scale, of the pitch that is on
  show in the pane's time range: the reference's pitch track and notes, and the singing's,
  whichever are shown. With none on show, it anchors under the fingers as now. The
  horizontal part of a pinch and the two-finger drag (vertical scroll) stay as they are.
- The anchor's choice is pure (`VerticalZoom` in `tony_core`: given the values on show and
  the current range, the anchor) and tested there; `MainWindow` gathers the values
  (`TouchGestures::VerticalRange` is where the pane's range comes from now, see
  `MainWindow::paneAdded`), and a test through synthetic touch events shows a low pitch
  staying in view as the range narrows.
- Not asked, so not built: following the pitch vertically during playback, a "fit the
  pitch" action. Say in the report if either looks needed.

### A12 — Calibrate Audio on the phone

The user (2026-09-26): the calibration and dev-check framework that came from `default`
"should be developed for Android too, to set the latency variables and perform the
hardware dev-test for my phone too". It is described in `docs/calibrate-audio.md` (read
sections 1, 2, 5, 9 and 10; the rest as needed). This phase: Playback > Calibrate Audio on
the phone, up to a stored, usable figure that places takes. A12b: the dev run.

Known before starting:

- **48 kHz**: `feat/wasapi`'s fix is merged: a device at another rate than the reference
  is measured like any other. Check the phone's path through it (Oboe records at 48 kHz
  only).
- **The key** (`LatencyCalibration::currentKey()`) is the driver and the playback and
  record devices as the Preferences name them. On Android `OboeAudioIO` opens whatever
  route the phone has (speaker, wired or USB headset, Bluetooth; A6), and none of that is
  in the settings: one figure would serve every route, and a Bluetooth route is 100-200 ms
  longer than the speaker's. The key must name the route Oboe opened (the output and input
  device: AAudio's device id, and its type and product name from `AudioManager`), so that
  each route is calibrated and used on its own. A6 reopens the device when the route
  changes: the menu line follows.
- **Staleness**: a stored figure is stale when a reported latency differs by more than
  1 ms from what was reported when it was measured. Oboe's latencies come from timestamps
  and move between starts: the fifth phone test's log has output 252 then 401 frames
  (5.2 then 8.4 ms), input 134, 154 and 222 frames. With 1 ms every figure would be stale
  at the next take. On Android the fingerprint should be what the stream was opened with
  (MMAP or not, sharing and performance mode, burst, buffer size and capacity, the
  devices), or a tolerance the measurements justify: choose, and test it in `tony_core`.
- **The dialog on a phone**: reachable in the compact layout; fits a landscape phone
  (about 923 x 411 logical px, the log's "popups within ... of 923x411"), touch-sized
  buttons, text that scrolls; the result's text copyable, and on Android a **Save
  Report...** through the picker as Help > Save Log... writes (share that code rather than
  copy it; A11's `AndroidStorage::Document`).
- **Instructions for a phone**: the loopback is an earcup of wired headphones held to the
  phone's microphone, or the phone's own speaker and microphone in a quiet room. A headset
  with a microphone of its own moves the input to it; say which input and output are in use
  (the route) on the instructions page. Android's input processing: Tony opens the input
  with the VoicePerformance preset (A6); say if the check sees anything that suggests echo
  cancellation or noise suppression.
- Timeouts that assume a desktop's speed (`AudioCheckRunner`'s 60 s for the reference's
  analysis, 30 s for a take's): the calibration reference is short; say whether they hold
  on a phone several times slower, and scale them if not.

Tests on the desktop as far as they go (the fake device at 48 kHz, the key and staleness
rules in core); the JNI for the route compiles only for Android. Say what the phone test
should do and send back.

### A12c — The Calibrate Audio dialog: small, and out of the way while a check runs

The user's phone test of Calibrate Audio (2026-09-26, the APK before A12): the text could
be selected but not copied (A12 added Copy); "the calibrate modal is too large and the
buttons on the bottom are off screen. The modals ought to be resized much smaller. Using
a smaller font-size would be acceptable too. I noticed this on desktop too ... the modal
blocks the view of the test happening. The modal could be made very small, just a small
progress bar and small status text could be shown in the corner. Tapping that could expand
to allow canceling an ongoing test so that possibility doesn't go away. Once the test
finishes the modal would expand again. This kind of change would be done for both desktop
and Android but it's mainly for Android."

- **Smaller**: every page of `CalibrateAudioDialog` fits a landscape phone (the log's
  923 x 411 logical px, less the safe area margins 58,24,48,0) with all its buttons on
  screen; text that does not fit scrolls. A smaller font on Android is acceptable. The
  desktop dialog gets smaller too; keep it readable.
- **Out of the way during a check**: when a check starts, the dialog collapses to a small
  indicator in a corner of the window (a progress bar and one line of status: the step,
  the punch-in, the time left) that does not cover the pane where the takes are drawn.
  Tapping or clicking it expands the dialog to the progress page, with Cancel, and a way
  back to small. When the check ends (done, failed or cancelled), the dialog expands by
  itself to the result page. The dev run (A12b) will use the same progress page and
  indicator for its stages.
- **Behaviour kept**: not modal; closing it while a check runs cancels the check; Copy,
  Save Report..., Use this latency, Check Again; the instructions and result texts A12
  wrote. A12 changed the dialog only by adding Copy and Save Report... on the result page,
  the phone instructions, NoSignal and Fading advice for a phone, a "Streams:" row, and
  asking for the microphone before Start.
- **The other dialogs**: open each of Tony's other dialogs in a window of the phone's size
  with the compact layout (message boxes, the take name question, Edit Display Extents,
  the lyrics dialogs, Preferences) and list in the report which do not fit. Fix only the
  generic cause, if there is one (a font or margin that the compact layout could set for
  all dialogs); the rest is for the user to choose.
- Tests: the pages' sizes against a phone-sized window; collapse at the start, expand on
  a tap and at the end, Cancel reachable while collapsed by expanding; closing still
  cancels.

### A12b — The dev run on the phone

After A12: the dev checks (`main/dev/`, compiled in the Android build, which is
`debugoptimized`) run after a usable calibration on the phone and their report reaches the
user. Read `docs/calibrate-audio.md` sections 6 to 8.

- The report, `DevChecks.txt` in the application data directory, cannot be reached on a
  phone: the result page's Save Report... (A12) saves it as well, or with the calibration's
  text, and the log names where it is.
- Timeouts that assume a desktop (the long song's 240 s reference analysed within 60 s,
  4 minutes a stage): scale them for a phone from what a phone takes (the log's pYIN
  times, if any, or a margin stated in the report).
- Go through the stages and checks for what differs on a phone: one input channel (item
  5), the output and input levels through `OboeAudioIO`, the save and reopen of stage 6 in
  the application data directory, anything that opens a dialog or a picker, the scratch
  folders.
- Tests on the desktop where the change is not Android-only; `test-tony-dev` whole.

### A13 — Fixes from the dev runs on the phone

Three dev runs on the user's Pixel 9a (2026-09-26), Bluetooth earphones (WF-1000XM6, A2DP)
for output and the phone's microphone for input, an earbud held to the microphone. The
calibration was steady (276 ms measured, 12 of 12 sweeps, punch-ins within 2.4 ms, spread
1.9 ms); items 4, 13 and 14 passed; 5 Measured. Faults, from the reports and the log:

- **Idle input latency.** After a take stops, `OboeAudioIO` reports input latencies of
  11691-11752 frames (244 ms), against 100-190 frames during a take: nothing reads the input
  while not recording, and its buffer (11424 of 11520 frames) fills. That figure went into
  the dev report's header ("Record latency reported") and into the next take's round trip
  ("round trip 12098 frames at 48000 Hz (252.044 ms), reported"): a take on a route with
  no measured figure, started after an idle spell, would be placed 244 ms off. Measure the
  latency only while the input is being read, or keep reading and discarding it while not
  recording (say which, and why).
- **A stream disconnected while idle.** Three times the first take after an idle spell
  logged `OboeAudioIO: failed to start: ErrorDisconnected`; the take recorded 0 frames
  ("nothing to use"), and only then did `checkAudioDevice` reopen the device. `resume()`
  should reopen the streams and start again at once when a start fails because they were
  disconnected, logged, so that the take (or playback) goes ahead.
- **Tones a phone can play.** No run found pitch on the reference's tones: 0-4 live dots a
  punch-in, all on sweeps; `mergeRangedAnalysis: 0 pitch event(s)` for every punch-in; even
  with the input peak at -12.7 dBFS. The tones are pure sines at 196-262 Hz
  (`LatencyCheck`), and an in-ear earbud without the seal of an ear canal, like a phone's
  speaker, gives out almost no bass: the sweeps, which reach higher, come through; the
  tones do not. Give the tones harmonics (a voice-like spectrum), so that their period is
  there even when the fundamental is lost; keep their pitches, timing and level; the sweeps
  and the calibration's measurement unchanged. pYIN and the live tracker must still find the
  fundamental (tests with the fundamental filtered out).
- **Pitch that cannot be judged.** Items 7, 9, 10 and 12 failed with "the take had no pitch
  outside the range to compare" while their audio parts passed. A part that has nothing to
  judge says so ("not judged: ..."), and the verdict comes from the rest, as items 4 and 12
  already do with their gaps ([calibrate-audio.md](calibrate-audio.md) section 10). Item 3
  keeps failing when no dots appear: that is what it checks.
- **Small**: the report's "Audio drivers built in:" is empty on Android; it should name Oboe.

Not changed without the user's word: the ±2 ms of items 1 and 2 (the runs had +3.2, +2.3 and
+2.4 ms with Bluetooth).

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

### Phase A6 — 2026-09-26
Built: `build-deps.sh` builds Oboe 1.11.0 (`liboboe.a`, `oboe.pc` with `-llog`; Oboe dlopens
AAudio and OpenSL ES). `main/OboeAudioIO` (Android only): stereo float output at the device's
rate, mono float input at that rate (`VoicePerformance`), low latency, exclusive (AAudio falls
back to shared), `FullDuplexStream`; each callback hands over its input, then asks for output.
`main/StreamLatency` (tony_core, `TestStreamLatency`). `MainWindow` on Android: `createAudioIO()`
(duplex once recording is asked for and the microphone allowed, else output only, as the base);
`record()` asks for `QMicrophonePermission` first, starts the take on Granted, else says where
to allow it; a 250 ms timer finds a failed device, stops the take (Stop path) or playback, and
reopens it (3 times in 10 s at most). main.cpp: the `AUDIO_NONE` forcing is gone.
Choices / deviations:
- Latency in device frames at the device's rate, as PortAudioIO. Read from the timestamps on
  the GUI thread (Oboe: not in the callback), 9 readings, those a callback ran through dropped,
  median round trip. Each part is off by the time to the next callback, the sum is exact
  (tested). Measured when opened (the constructor runs the streams up to 1 s and leaves them
  suspended) and at every suspend(): a take uses the figures of the device's last run.
- `setSystemPlaybackChannelCount()` gets the play source's count, not the stream's: svapp's
  wrappers refuse (or throw on) any other count in `getSourceSamples()`. Mixed to 2 here.
- FullDuplexStream drains and discards ~50 callbacks after each start: sound starts ~0.1-0.25 s
  after Play or Record. A callback may read fewer input frames than it writes; the start gap
  subtracts the output block, so it can be up to one burst (2-5 ms) short.
- No error callback for the input (it has no callback): FullDuplexStream's Stop is flagged.
The next phase must know: once a take is recorded the device stays duplex (svapp), so Play
opens the microphone too; nothing calls `suppressRecordSide()`. Logcat tag Tony: "OboeAudioIO:".
Left open: not run on a phone; the first Record press blocks for the open (up to ~1 s).

### Phase A7 — 2026-09-26
Built: `AndroidFiles` (core): `pathFromContentUri()` (externalstorage `primary:`/volume-UUID ids,
alone or under a tree; downloads `raw:`; split before decoding; `..` refused), `suggestedSessionName()`,
`sessionFileName()`, `removeIfEmpty()`. `AndroidStorage` (Android only): `isExternalStorageManager()`,
the root, `ask()` (a box, Settings' page for Tony or else the list, then a Qt box that closes when Tony
is back with access). `TouchMenuStyle` (app; main() installs it on Android only). `MainWindow` on
Android: Open maps a pick to its path (asks with a session, once with audio), else copies audio or
refuses a session; Save As runs its own QFileDialog; `applicationStateChanged()`. Manifest:
MANAGE_EXTERNAL_STORAGE (`tools:ignore="ScopedStorage"`; debug builds run no lint). Chained `.arg()`
with paths or names made single calls in main/: TakesFile wrote a take named "a %1" or "%3" wrong.
Choices / deviations:
- Qt 6.11 (androidjnimain.cpp): on Suspended Qt posts the event, then stops the GUI dispatcher until
  Active (no `android.app.background_running`): a nested loop in the slot blocks until Tony is back.
  So nothing there waits; a save that needs a take's ranged analysis merged is made after return.
  A never-saved session is not saved. No save while a dialog or the picker is up (`loopLevel() > 1`).
- QMenu wraps a tall menu into columns (off the side on a phone); scrollable, it has hover arrows
  (a tap scrolls to the end), so a finger's drag scrolls it too, through wheel events.
- Save As: `selectFile()` gives EXTRA_TITLE; no default suffix (Qt appends it to the URI's decoded
  path); MIME octet-stream. Exports still go through svgui's dialog and the URI. `build-apk.sh`
  now deletes Gradle's last APK (incremental packaging left 7 MB holes).
The next phase must know: Android's native QMessageBox cannot be closed from code (its helper's
hide() does not end exec()): DontUseNativeDialog. `activeModalWidget()` misses native dialogs.
Left open: not run on a phone. Below Android 11 no in-place sessions. Downloads (`msf:` ids)
refused for sessions. Save to Audio Path with copied audio saves into app storage. For A8:
port-android.md "Files, storage..." (bundle superseded) and "Permissions and lifecycle".

### Phase A7b — 2026-09-26 (the agent stopped twice; the lead finished MainWindow.cpp)
Built: `PopupArea` (menus kept inside the safe area, a finger's width from the screen's top
and bottom; TouchMenuStyle applies it), `LogFile` (the logcat lines kept in
`<AppData>/log/tony.log`, 512 KB and one older part; Help > Save Log... writes a copy through
the save picker), `AndroidFiles::grantedUri()`, `providerOf()`, `pathLookupFor()`,
`mediaStoreUriFor()`, `chooseDownload()`, `hasExtensionIn()`, `usableRecentFiles()`;
`AndroidStorage::pathFor(uri, why)` (static; MediaStore `_data` for `msf:`/`audio:` ids,
name and size for numbered downloads), `displayName()`, `openDocument()` (a descriptor
from the ContentResolver), `removeIfEmpty(uri)`. MainWindow: Tony's own Open picker for
sessions and audio (no type filter; the extension checked after), mapped path used only if
it is a file, otherwise audio copied in through `openDocument()`; every refusal carries
`pickDetails()` (provider, path looked for, why); Open Recent says a moved file is gone,
and the menu lists only files that are there.
Cause found: Qt's content file engine re-encodes '(' and ')' and then has no grant for the
URI, so `QFileInfo::exists()` and `QFile` fail for such names; hence descriptors from the
ContentResolver with the URI exactly as Android wrote it.
Tests seen failing: `the_uri_is_opened_as_android_wrote_it` with `PrettyDecoded` (lead).
Left open: none of it run on a phone. Layer import still goes through svgui's dialog.

### Phase A4b — 2026-09-26
Built: `main/VerticalZoom` (tony_core; `TestVerticalZoom`): value at y and back on a log or
linear scale (svgui's CoordinateScale mapping), zoom about a value held at y, `limited()`
(`pitchLimits()`: A0 to C8, a major third at the narrowest). `PinchZoom::AxisMovement`: a
movement along an axis counts past a dead zone (2x slop) if at least half that across, then
less the dead zone. `TouchGestures`: each axis zooms by the fingers' spread along it (never
under 4x slop); the time axis as A4 had it, with the spread across in place of the distance;
the range about the value between the fingers, following them up and down past the dead zone;
held at a limit, re-anchored. `TouchGestures::VerticalRange` (get/set/limits); `paneAdded()`
gives it the reference analyser's `get/setDisplayFrequencyExtents()`, in its pane only, and
only while the pane draws Hz on that range. 8 app tests in `TestTouchGestures`.
Found: the range is the primary analyser's `SpectrogramLayer` (MelodicRange: log, 40-1500 Hz,
dormant). Pitch, notes, the take's layers, live dots, the alternate track are AutoAlignScale:
they defer to the topmost "Hz" layer with a scale of its own, dormant or not (View::
getEffectiveVerticalExtents), which is it. Not undoable, no modified flag; saved in the session
(the spectrogram's minFrequency/maxFrequency) and restored with it; a new reference resets it.
Choices / deviations:
- The spectrogram keeps whole Hz (int, lrint): steps of up to ~12 px at a major third about
  110 Hz on a 300 px pane. svgui fork change to fix: `m_minFrequency`/`m_maxFrequency` double,
  no lrint in `setDisplayExtents()`, `toDouble()` in `setProperties()`. Not made.
- Time scroll keeps A4's (no dead zone); the range is sticky: a drag within ~27 degrees of
  across never moves it.
Tests seen failing: across rule removed; zoom about the middle; no limits; spectrogram unsaved.
Left open: not on a phone. A drag up may also scroll the pane stack, if it can scroll.

### Phase A7c — 2026-09-26
Built: `main/AndroidMediaReadStream` (tony_core, Android only, `-lmediandk`): a bqaudiostream reader
over AMediaExtractor/AMediaCodec for m4a mp4 aac 3gp amr flac ogg oga webm mka mkv. Nothing else
claims those on Android (libsndfile.a has no FLAC or OGG format; oggz/fishsound are out); wav, mp3
and opus stay with sndfile, mad, opusfile (two readers of one tag: the first registered wins, in
link order). First audio track, in order, no seek; channels and rate of the decoder's output at
its first audio; `pcm-encoding` (16-bit if absent). Logged: per file MIME, codec, rate, channels,
encoding, duration, delay/padding; at the end frames decoded and compressed frames; each refusal
("this phone has no decoder for AAC (audio/mp4a-latm)"). `main/DecodedPcm` (core,
`TestDecodedPcm`): five PCM encodings to float, channels folded, reads of any size. `MainWindow`:
`sessionIsIncomplete()` (the document's flag, from SVFileReader), `maySaveUnasked()` (the suspend
save's test), `askToSaveIncompleteSession()` before Save with a file, Save As (before the picker)
and Save in Audio Path; a save clears the flag. build-tony.sh allows libmediandk.so.
Choices / deviations:
- Encoder delay/padding kept: the decoder gets 0 for both (AOSP trims them otherwise, from
  memory; its sources were unreachable). The user's .ton has Media Foundation's decode as 9352192
  = 9133 x 1024 frames, whole AAC frames: trimming would move the reference 2112 frames (48 ms)
  against the saved pitch. The phone's log gives its count to compare. 16-bit output, as MF's.
- A file that fails is tried twice (by extension, then by every reader): logged twice.
Found: svapp gives an incomplete session no file (`m_sessionFile` ""), so A7's suspend save
already passed it by. With the reference missing, Save As waits for an analysis that never comes
("Waiting for analysis" until Cancel): not fixed; `waitForInitialAnalysis()` could pass then.
Tests seen failing: DecodedPcm compaction; `maySaveUnasked()`'s guard; Save As's question.
Left open: the decoder not run on a phone. SDK sources 36 installed in /opt/android/sdk.

### Phase A9 — 2026-09-26
Measured (container; `QT_SCALE_FACTOR=3`, 400x850 window, compact layout, 3-minute reference
with pitch and notes, 4.4 s page, take with the reference playing): GUI thread 29-30% of a core.
Per estimate 16 us (0.3%), record update 61 us per 10 ms (0.6%), tracker thread 1.3%: all keep
up. Pane paints ~26%: each of the dots' 25 notices a second drew the whole pane (7 ms in a take;
3.2 ms idle, 4-4.9 at ratio 2.75, the smooth downscale); the pointer's ~50 strips/s 2.7-3 ms each.
Built: `RealtimePitchTracker` keeps its estimates (`takeEstimates()`, `getFramesAnalysed()`), no
signal per hop. `LiveDotsFeed` (core, `TestLiveDotsFeed`): a 40 ms timer on the GUI thread hands
all found since the last look to `onRealtimePitchDetected(estimates)`: dots added together, status
bar set once, the pane drawn again only over the batch's frames (`PaneUtils::updateViewFrames()`).
It times batches and the pane's paints (an event filter that delivers them itself) and logs once
a second ("MainWindow: live dots: 12.03 s recorded, tracker at 12.01 s, dots to 12.01 s; 25
batches of 6.8 dots, ..."). `ModelChangeThrottle` is gone (the interval is the throttle). After:
18-20% of a core. Found: the phone's APK predates the merge: dots cached, told nothing, drawn at
page turns only (~3.5 s on a 4.4 s page), which is "several seconds behind".
On a core 4-5x slower the pointer's strip paints would dominate (50-75% of a core; they coalesce:
fewer frames, no growing lag): `TimeValueLayer::paint` costs ~10 us a dot (`getModelsEndFrame()`,
two `QFontMetrics`, a pen, a brush per point), paints 100 physical px past the area; svgui's
pointer strip is 65 px. svgui candidates, not made.
Tests seen failing: `live_dots_keep_up_with_a_slow_gui` on HEAD (1.9 s behind at 4.3 s);
`live_dots_draw_only_where_they_are` with a whole-pane update; the feed per estimate.
Flaky: tests needing the ranged analysis running just after Stop lose when pYIN ends inside
`createDerivedLayers()` (~16 ms) and merges at once (seen in a log): one in 7 of 10 runner runs
here, 0 of 3 at HEAD, which also lost one in a parallel repeat. Lead: `analyseRange()` now looks
at a finished run from the event loop, never within the call; 5 of 5 runner runs green after.
The lead also brought the docs naming `ModelChangeThrottle` up to date. Not on a phone.

### Phase A10 — 2026-09-26
Built: svgui `a082647` (view): `ViewManager::setPlotScale()` and `plotScaleChanged()` (views drop
their cache and repaint); `LayerGeometryProvider::scalePlotSize()` (logical px x ratio x plot
scale, no font factor: the identity at 1 and 1) and `scalePlotPixelSize()`;
`ViewProxy::scalePenWidth()` by ratio x plot scale, not sqrt(ratio). `8286def` (layer):
TimeValueLayer's point height and least width, FlexiNoteLayer's note height, outline and hit
area (`getRelativeMousePosition()`, `getFeatureDescription()`) through it. Neither pushed nor
pinned. `main/PlotSize` (app): View > Plot Size 100/150/200 %, `MainWindow/plotsize`, default 150
on Android and 100 elsewhere; nothing stored until a step is chosen, anything else stored is the
default. Tests: `TestPlotSize`, one each in `TestViewCache` and `TestCompactLayout`.
Measured: a whole pane of Tony's at ratio 1 (waveform, pitch, notes) was byte-identical before
and after (a one-off shot, since removed). Ratio 3, 400x850 compact: pitch layer 6.2 ms a paint
before and after at 100 %, 6.7-7.5 ms at 150/200 %; notes 0.05 ms; whole pane 17-19 ms either
way. At ratio 1, 150/200 % doubles the pitch layer (2.5 to 6 ms): a pen wider than a pixel leaves
Qt's fast path, which at ratio 3 the old 1.7 px pen had left already.
Hi-DPI desktop (ratio 2 at Windows 150/200 %) changes too: TimeValueLayer (points 2 logical px
high, not 1; pens 2 px, not 1.4), FlexiNoteLayer (notes 16 logical px, not 8, now as high as
their hit area), the pens of RegionLayer's bar styles and of SliceLayer/SpectrumLayer. Not the
coverage strip or lyrics (already `scalePixelSize()`), waveform, spectrogram or time ruler.
Left: the ruler's ticks and the waveform's lines stay 1 physical px (looked fine at ratio 3); the
strip and lyrics do not follow the plot size; forks.md not updated (A8). Not on a phone.
Tests seen failing: ViewProxy without the ratio (the old sizes at 3): sizes, hit area; plot
scale x1.1 at 100 %: desktop_draws_as_before; no connection: the cache test; PlotSize not
applying: its step test and the menu test.

### Phase A11 — 2026-09-26
Built: svgui `049c6d9` (view, not pushed or pinned): `ViewManager::setRecordFrameRatio()`
(default 1); while recording the playback frame is the record start frame plus the duration
times it (`getRecordingFrame()`, both places). `TakeTiming::referenceFramesPerRecordedFrame()`
(tested). `record()` sets the ratio to 1 before the base call (the device's rate is unknown
then; a recording that becomes the session is at its own rate) and to the take's after it,
when the recording model is known. `takes_placed_from_a_device_at_48000`: `QEXPECT_FAIL`
gone; `preroll_and_punch_out_with_a_device_at_48000` checks the cursor after the lead-in.
The lyrics highlight follows the same frame, so it too was 8.8% fast in a 48 kHz take.
Save Log: `AndroidStorage::Document` replaces `openDocument()`: it keeps the
ParcelFileDescriptor, hands out `getFd()`, and `close()` asks `checkError()` (if it
`canDetectErrors()`: a reliable pipe) and closes through Java. The likely cause, from the
source: `detachFd()` sends the peer DETACHED at once, before anything is written, so a
provider that takes the file on its close listener took it empty. Writes "wt" (then "w" if
refused); `flush()` checked; the size behind the descriptor (`getStatSize()`) logged;
`_size` through the resolver (`sizeOf()`), asked up to 10 times 100 ms apart while it
differs (the provider's listener runs on its own thread). `AndroidFiles::savedSize()`
(core, tested). A warning box when the provider gives another size; the log line always.
The audio copy-in reads the same way and drops a copy whose close reports an error.
Tests seen failing: both cursor checks with the ratio left at 1; `savedSize()` taking "" as 0.
Left open: nothing run on a phone. forks.md (svgui) and recording.md "Start click" step 2 and
6 name the start frame plus the duration: for A8.

### Phase A4c — 2026-09-26
Built: `VerticalZoom` (core): `middleShown(values, range)`, the middle on the range's scale of
the values it shows, half way from lowest to highest less a twentieth at each end (an octave
jump, a breath); `towardsMiddle(y, height, factor)`, zooming in the held value's distance from
the pane's middle divided by the factor, zooming out where it was. `VerticalRange::drawn`;
`TouchGestures::beginPinch()` asks it once a pinch: with values on the start range the zoom is
about their middle, pulled towards the pane's middle; with none, about the fingers as before.
Scroll (travel) and re-anchoring at a limit as A4b. `Analyser::getPitchOnShow()`: pitch track
and notes, each if not dormant (a temporary hide counts), notes spanning the frames.
`paneAdded()` gathers both analysers in their pane over its start to end frame.
Choices / deviations:
- On show = in the pane's time range and on the range: pitch all off the range falls back to
  the fingers. Wider than the zoomed range: still the middle of its extent, both ends cut
  alike; a gap there (voices an octave apart) shows empty far in, two fingers scroll to
  either. The median was not taken: it centres a skewed phrase badly while it still fits.
- The pull: the middle held where it was keeps a low voice's middle in view but loses its
  lower half once it fills a third of the pane (B1-G2 from 40-1500: at 2.8x); pulled, all of
  it stays until it fills about 90%. Not asked for in words; the user's "centered".
- Once a pinch (every two-finger touch): an event copy per point in view, ~700 per analyser
  on a 4 s page. Not counted: the alternate pitch track, other takes' layers, live dots.
Tests: TestVerticalZoom +5; TestTouchGestures +3 (the low voice, checked at each step; the
fingers when nothing is on show; the singing counted); narrows/widens/diagonal now check the
pitch. Pitch checks allow for the whole-Hz range (~3 px at a bottom near 40 Hz).
Tests seen failing: anchor off (5); no pull (3 app, 2 core); singing not gathered; dormancy
ignored (2). Left open: not on a phone; no follow in playback, no "fit the pitch" action.

### Phase A12 — 2026-09-26
Built: `AudioRoute` (core): a route's devices (AudioDeviceInfo id, type, product name), its
rate and how each stream opened; `AudioRouteReporter`, which `OboeAudioIO` (JNI:
`AudioManager.getDevices()`, matched by `getDeviceId()`) and the tests' fake implement.
`LatencyCalibration`: `routeKey()` ("oboe", type and product name, never the id: a headset
gets a new one at each plug-in), `onlyRecordDevice()` (a phone is output-only until its first
take), a figure's `outputStreams`/`inputStreams`. `MainWindow::latencyKey()`,
`audioRoute()`; the runner keys a result by its first take's route (`TakeLatency::route`).
Staleness on a route: stale only if a stream it describes opened otherwise (API, MMAP,
sharing, mode, burst, buffer and capacity, preset); reported latencies ignored (they moved
4 ms take to take). Desktop: key and 1 ms rule as they were. `PunchIn::placedWith`:
`judgeTake()` counts each punch-in as if placed with the first's round trip; Oboe's reported
pair moves per start, which made the spread and the calibrated figure wrong. Dialog: Copy
(all platforms), Save Report... (Android, `saveTextThroughPicker()`, Save Log's code), the
route and the phone's loopback on the instructions, a phone's advice for NoSignal/Fading, a
Streams row; Start asks for the microphone first (the runner refuses a take without it: the
permission's answer would otherwise start a take of the user's after the check had ended).
Not changed: the dialog's size and layout (A12c), the timeouts (26 s reference ~1.3 s here,
a punch-in ~0.6 s: 60 s and 30 s leave 5-10x for a slower phone).
Tests seen failing: placement not counted (core Unsteady, app Unsteady 10 ms); streams not
passed (a route's figure unused once reported latency moved).
For A8: calibrate-audio.md §5 (route key, streams rule), recording.md "Latency", §3 judging.
Left open: none of it on a phone; which input an output-only device will open is a guess.

### Phase A12c — 2026-09-26
Built: `CalibrateAudioDialog` sized by `fitToWindow()` at each page and show: 64 average
characters wide (wider if the buttons need it), as tall as the page's text, never more than
`windowArea()` (the window less its safe area margins, within the screen), centred there when
shown (`PopupArea::place()`, core, tested), kept where it is when on show. The texts in scroll
areas that report the text's height for a width; the stack is not asked (it gives the tallest
page's). Android: font at 85 %, buttons 3/4 of a finger high, one-finger scroll (QScroller),
the result not selectable (Copy). `AudioCheckIndicator` (app): a bar and one elided line
("Recording punch-in 2 of 4, 25 s left", "Dev checks, stage 1 of 6: ..."); `MainWindow::
calibrateAudio()` puts it at the right end of the status bar. A started check collapses the
dialog to it; a tap expands to the progress page (Make Small, Cancel); the run's end expands.
Choices: the corner is the status bar's right end: the pane fills all between toolbar and status
bar, the status line (countdown, sung note) is at the left; it grows the bar (a phone: 2/3 of a
finger), covers nothing. Closing still cancels, but a small dialog is hidden: expand first.
Dialogs at 817x387, compact, fonts 12/15/17 px: message boxes, take name, Open Location, Edit
Display Extents, lyrics word and shift fit. What's New does not (minimum 520x450 scaled by font,
~624x540 at a phone's); About at 17 px (537x393); Key Reference is sized from the screen
(600x274 here) but has no parent (placed by Qt). No Preferences dialog. No generic cause: none
fixed.
Tests seen failing: fit off (fits_a_phone); no collapse (3); no expand at the end (from_the_menu).
For A8: calibrate-audio.md §2 (small, Make Small, not selectable on Android), §9, §11.
Left open: not on a phone (the font, the finger scroll, the dialog's place under the bars).

### Phase A12b — 2026-09-26
Built: `CalibrateAudioDialog::reportText()` (Copy, and Save Report... on Android) ends with
DevChecks.txt whole when a dev run wrote one; Save Report... then suggests
`tony-dev-checks-<time>.txt` and logs the report's path; on Android the result page says Copy
and Save Report... take it. `AudioCheckRunner::kAnalysisTimeFactor` (1; 4 on Android) scales
the reference's and a take's analysis limits (60/30 s; 240/120 s); `DevChecks`' stage limit is
the runner's reference limit, two take limits and 2 min (240 s; 600 s), the reopen 60 s x the
factor. The runner logs every analysis's time ("the reference, 240 s, was analysed in ...").
`AndroidScreen::keepOn()` (Android; FLAG_KEEP_SCREEN_ON on Android's main thread) from the
dialog's Start to the run's end. Item 5: one input channel is Measured, "the device records one
input channel". Item 14: past the selection's end worked out in seconds, device frames by the
device's rate, lead-in and range by the session's; `TakeObserver` counts an event loop deeper
than its start as a dialog. DevChecks.txt names the route's driver and streams.
`FakeAudioIO::Config::inputChannels`. Tests: `dev_checks_on_a_phone` (48 kHz, 1 in 2 out, a
route; through the dialog: calibration, then the dev run with a 60 s long song),
`dev_checks_see_a_dialog_qt_does_not_draw`.
Found: at 48 kHz item 14 failed every take by ~0.35 s (reference frames taken as the device's).
A screen going off (no touch for minutes) suspends Tony: the take is stopped and the loop held.
Android's own dialogs (message boxes, picker) are no active modal widget (A7): item 14 missed them.
Margin: the 240 s long song took ~10 s here; a phone 3-6x slower, 30-60 s; 240 s is 4x that.
Tests seen failing: phone run at HEAD (item 14); loop level ignored; report text without the file.
For A8: calibrate-audio.md §7 (limits on a phone, items 5 and 14, the header), §8, §10.
Left open: not on a phone. Leaving Tony during a run (power key, a call) still ends it wrongly;
a user's own long take has no screen kept on.

### Phase A13 — 2026-09-26
Built: `OboeAudioIO`: each duplex callback reads all the input there is (`Engine::readInput()`
over FullDuplexStream's, which reads only what the output asks, so a backlog from a held-up
callback stayed for the whole run; own buffer, input capacity); a latency reading with more
input waiting than the output buffer and two input bursts is refused, the figures kept, and
logged ("the input was N frames ... behind as the device stopped"); suspend() logs a callback
that read more than that at once. resume(): ErrorDisconnected reopens both streams (fresh
Engine and ErrorFlag, `openStreams()`/`measureOnceOpen()` split from the constructor, the
route found again), measures, starts; any other failure as before. `StreamLatency::
inputFramesToRead()`, `inputKeptUp()` (core, tested). `LatencyCheck`: tones with harmonics to
4 kHz at 1/n, Newman's phases, vibrato ±10 cents at 5.5 Hz, peak -12 dBFS from one period's
waveform (rate-independent). DevChecks: items 1, 7, 9, 12's pitch parts and item 10's pitch,
note and outside parts say "not judged" with no pitch there, verdict from the rest; "oboe" in
the header's drivers on Android. calibrate-audio.md §3, §7, §10.
Choices / deviations:
- Mechanism of the 244 ms not found in the code (callbacks held up after Stop is the guess);
  read-all fixes it if so, the guard keeps any such reading out whatever the cause.
- Vibrato not asked for: exact periodic tones tie pYIN's P, 2P, 3P; subharmonics seen for
  the old sines too (294 as 73.5 Hz). Item 1's reopen comparison done like 7, 9, 10, 12.
Tests seen failing: old sines (harmonics test, YIN and pYIN rows); no vibrato (pYIN rows);
FullDuplexStream's read rule; guard always true; item 7's old verdict (`dev_checks_without_pitch`).
For A8: testing.md (exactly periodic tones tie in pYIN even at whole periods), recording.md
"Latency" (Oboe: readings refused with a backlog; the reopen on ErrorDisconnected).
Left open: not on a phone; the calibrated figure may move by up to a burst (2 ms) with read-all.
