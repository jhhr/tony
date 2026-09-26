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

