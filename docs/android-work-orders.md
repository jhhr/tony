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
  `download.qt.io` and `dl.google.com` are reachable. `breakfastquay.com` and
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

## 4. State of the code (kept by the lead; as of 2026-09-25, after phase A0)

- Nothing of the port exists yet. The branch holds `default`, the research docs, and the
  container setup script `deploy/linux/container-setup.sh`.
- Both suites pass on Linux (Qt 6.11.2 from conda-forge). `TestTakesFile`'s Windows path
  assertions run on Windows only.

## 5. Phases

Order: A0, A1, A2, A3, then A4 and A5 while the user tries the APK on the phone. A6
needs the result of that phone test. A8 is last. (Since 2026-09-25 `download.qt.io` and
`dl.google.com` are reachable from the container, and GitHub Actions is enabled on
`jhhr/tony`.)

- A0 — Desktop build and tests in the container. Done.
- A1 — Sample rate: a device that is not at 44.1 kHz.
- A2 — Android toolchain and C libraries.
- A3 — Tony as an APK (no audio): the test port.
- A4 — Touch gestures on the panes.
- A5 — Compact touch mode.
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
  (without its codec libraries), libsamplerate, fftw3 (float), Rubber Band 3, libogg,
  opus, opusfile, serd and sord, libmad, libid3tag (with the NDK's zlib). Pin versions.
- Leave out JACK, PulseAudio, ALSA, PortAudio, oggz and fishsound; note any other library
  that turns out to be needed.
- The toolchain is installed into the container (outside the repo, e.g. under `/opt`), so
  that A3 can iterate locally. The CI workflow comes in A3, once there is an APK to build.

### A3 — Tony as an APK, without audio: the test port

Read: [port-android.md](port-android.md) all of "Platform facts" and "Test port";
[mobile-port.md](mobile-port.md) "The pYIN plugin", "The window", "Build and tests".

- Route (a) from section 3: an `android` branch in `meson.build`, an NDK cross file, Tony
  built as `libTony_arm64-v8a.so`, a script that writes the deployment JSON and runs
  androiddeployqt, a custom `AndroidManifest.xml` (with `RECORD_AUDIO` for later).
- Audio: `AUDIO_NONE` under `Q_OS_ANDROID` for now.
- The menu bar: exclude `Q_OS_ANDROID` from the `Q_OS_LINUX` `setNativeMenuBar(false)`
  only if the ⋮ options menu works better on a phone; otherwise keep the in-window bar and
  say why.
- pYIN found on the phone: `libpyin.so` naming plus legacy packaging, or linking it in.
  The log must show "Setting VAMP_PATH to ...".
- The desktop build and suites unchanged and green.
- A CI workflow `.github/workflows/android.yml` (Linux runner, triggered by pushes to
  `feat/tonyandroid` and by hand) that builds the APK the same way and uploads it as an
  artifact. The lead pushes and reads the run; write it so it can only be judged there.
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

After the user's phone test of A3. Detailed when it starts.

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
