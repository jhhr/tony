# Calibrate Audio: work orders for phase agents

You are one of a line of agents, each building **one small phase** of the Calibrate
Audio work. A lead reviews your work when you report back. You have no memory of earlier
phases; what you need is here. This file is working memory for the feature branch, and
the documentation phase removes it.

**Your context is the budget.** Aim to finish well under 200k tokens. The rules below
say how. They are about not reading huge files whole and not maintaining big documents;
they are **not** a licence to skip what you need to understand. Careful, correct work
comes first.

## 1. What to read, and what not to

1. This file, all of it.
2. `AGENTS.md` at the repository root. Its rules apply, except that the **build and test
   commands in section 2 below replace its Windows ones**.
3. `docs/calibrate-audio.md` (the spec, about 400 lines): always §1, §5, §10 and §11,
   plus the sections your work order names. Search for the heading and read that range.
4. `docs/testing.md`, "What there is to reuse" and "Design principles", if you write app
   tests. `docs/recording.md`, "Latency", if you touch the take path.
5. Code:
   - `main/MainWindow.cpp` is over 6000 lines and `main/test/TestRecordWorkflow.h` over
     2000. **Never read them whole**: search for the function, then read that range.
   - Before writing a test, read an existing test next to where yours will go and copy
     its shape.

## 2. Rules

**Scope**

- Build your phase only. If something from a later phase is needed, build the smallest
  part of it and say so.
- The spec is agreed with the user. Where it is silent, choose the simpler option and say
  so. Where it is **wrong or impossible**, do not improvise another design: finish what
  can be finished, leave the tree building and green, and report.
- **Do not edit** `svcore/`, `svgui/`, `svapp/`, `bqaudiostream/` (forks) or any other
  top-level library directory (`bqaudioio/`, `pyin/`, `vamp-plugin-sdk/`, …). These are
  separate repositories, gitignored here. If one needs a change, report exactly which
  change; the lead makes it.
- Match the surrounding code: naming, comment density, idiom. Comments say why, in plain
  words. Every new source file starts with the project's GPL header (copy it from
  `main/TakeTiming.h`).
- Put pure logic in `tony_core` (listed in `meson.build` as `tony_core_files`), as plain
  structs and functions like `TakeTiming`. State gets a class and files of its own;
  `MainWindow` only wires it. The layer, model and command rules in `AGENTS.md` apply.
- **Qt here is 6.4.2; the user builds with Qt 6.11 on Windows.** Use no Qt API newer than
  6.4, and nothing specific to Linux.

**Build and test.** This session builds on Linux in `build_linux/`, not the user's
MinGW setup.

- Build:

      cd /home/user/tony
      ninja -j 4 -C build_linux tony test-tony-core test-tony-app pyin.so > tmp/build.log 2>&1; echo "exit:$?" >> tmp/build.log; tail -5 tmp/build.log

  Search the log for `error:`; never read it whole. meson reconfigures by itself after
  `meson.build` changes. Targets have no `.exe`.
- **`pyin.so` must be built too.** The app suite sets `VAMP_PATH` to its own directory.
  Without the plugin, every test that waits for pitch analysis hangs until QtTest's
  5-minute watchdog aborts the run.
- Test, from `build_linux/`:

      mkdir -p ../tmp/tl && rm -f ../tmp/tl/*.txt
      TONY_TEST_LOG_DIR=../tmp/tl ./test-tony-core > ../tmp/test.log 2>&1; echo "exit:$?"
      TONY_TEST_LOG_DIR=../tmp/tl ./test-tony-app  > ../tmp/test.log 2>&1; echo "exit:$?"
      TONY_TEST_LOG_DIR=../tmp/tl ./test-tony-app some_test_name > ../tmp/test.log 2>&1
      grep -a "^FAIL\|^   Loc\|^Totals" ../tmp/tl/*.txt

  - Read results from the per-suite files, not stdout.
  - A test name on the command line goes to every suite in the executable, so the exit
    status of a run with names is meaningless.
  - New test classes are registered in `main/test/tony-core-test.cpp` or
    `tony-app-test.cpp` and added to `meson.build`.
- While working, run **only your tests**. The app suite takes several minutes of real
  time: run both whole suites **once**, at the end, and again only if something failed.
  Give the app suite a tool timeout of 10 minutes.
- App tests run in real time against `FakeAudioIO`: keep them short (seconds, not tens
  of seconds).
- Every behaviour gets a test that can fail. Show it for the two or three that matter
  most by breaking the code for a moment. Undo the break **by hand**: never
  `git checkout` or `git restore` a file to revert an experiment.
- Do not weaken or delete an existing test to get green. If one is wrong because the
  behaviour was meant to change, change it and say so.
- **Linux traps:**
  - Section 3 lists suite results known before this work started.
  - Qt 6.4 does not match a `SIGNAL()`/`SLOT()` string naming `ModelId` or `sv_frame_t`
    with moc's `sv::` names: the connection fails at run time with "No such slot". The
    user's Qt happens to match them, so a string connect can pass there and fail here.
    Use member-pointer `connect` only, and grep test output for "No such slot".

**Docs: almost none.** The documentation phase (D) brings `docs/` up to date. You
write only:

- one entry in the log (section 5), **25 lines at most**, appended at the end;
- "Done" on your phase's line in spec §7, and a correction of any spec statement your
  work proved wrong.

Edit documents with the Edit/Write tools only. A shell heredoc or one-liner containing
backticks, `$` or non-ASCII text gets mangled, and has corrupted documents before.

**Git.** The lead commits. Leave your work uncommitted, building and green. Never commit,
push, amend, stash, or `git add -A`.

**Report** (your final message, and all the lead sees; under 60 lines)

- What was built, by file, briefly.
- The `Totals` lines of the final full runs of both suites, copied, not paraphrased.
- Which tests you saw fail without the change.
- Choices made, deviations, anything fragile or unfinished. Say it plainly: a problem
  reported is cheap, one found later is not.

## 3. State of the code (kept by the lead; as of 2026-09-25, before phase A1)

- Nothing of the feature exists yet. The spec's §11 lists the facts about today's code
  that the phases build on.
- **Linux baseline** (after the lead cherry-picked the member-pointer connect fix from
  the lyrics branch, `e2cf7c0`):
  - core suite: all green except 4 tests in `TestTakesFile` (`takes_folder`,
    `relative_audio_path`, `resolve_audio_path`, `in_folder`). They use Windows paths
    (`C:\Songs\...`, case-insensitive) and fail on Linux only. Not yours to fix; your
    final runs must show exactly these 4 and nothing else.
  - app suite: all green, `TestRecordWorkflow` 92 tests in about 4.5 minutes. The lead
    also made `stale_pitch_event_ignored` queue its event as a functor (`e13fb9a`),
    because invoking a slot by name with an `sv::` argument type fails under Qt 6.4.

## 4. Phases

Done: A1 (`944df7c`), A2 (`a03b7ec`), B1 (`58de074`), B2 (`47944f2`), B3 (`8524d5f`).

### A1 — Test reference and sweep finder (spec §5 "tony_core", §6 core suite)

- New `main/LatencyCheck.{h,cpp}` in `tony_core`, pure.
- **Generator.**
  - A layout gives the length and the events. Make three: *calibration* (about 26 s),
    *dev* (adds 3 s held tones) and *long* (4 minutes).
  - Each event: a sweep of 1 → 8 kHz, 200 ms, 10 ms raised-cosine edges, −12 dBFS
    peak; then a tone at a pitch with a whole number of samples per period at 44.1 kHz
    (196, 220.5, 245, 294 Hz; see `docs/testing.md` on pYIN subharmonics).
  - Gaps between events are irregular, 1.6–2.6 s, all different by at least 0.1 s.
  - Deterministic. It returns the samples and each sweep's exact frame.
  - Exponential or linear sweep: choose one and say why. The spec leans neither way.
- **Finder.** Given take audio (samples and rate) and an expected event time, search
  ±0.8 s:
  - FFT matched filter against the sweep (bqfft; see how `RealtimePitchTracker` uses
    it), weighted to the sweep's band;
  - the envelope of the result;
  - the **earliest** peak within 6 dB of the largest;
  - two confidences: the peak over the window's median in dB, and the peak over the
    second-best peak outside ±10 ms in dB;
  - the error in frames and in ms.

  The thresholds are named constants (15 dB and 6 dB to start).
- **Not in this phase:** aggregation over events and punch-ins, verdicts, second arrivals.
- **Tests,** in a new core test class `TestLatencyCheck`:
  - generator: deterministic, events where it says, gaps all different, peak level;
  - finder on synthetic takes, made by shifting the reference:
    - shifts across ±0.75 s, plus one fractional shift (resample or interpolate);
    - white noise at 0 and −10 dB SNR;
    - high-pass at 1 kHz and low-pass at 4 kHz;
    - polarity inverted;
    - a reflection 6 dB **stronger** 7 ms after the direct sound: the direct sound
      must be found;
    - silence: no confident peak.
  - **Show that the reflection test fails** when the finder takes the largest peak.

### A2 — Verdicts and calibration arithmetic (spec §5 "tony_core")

- In `LatencyCheck`: given the events inside a take's coverage ranges (one range per
  punch-in), and the finder's results, compute:
  - median and spread per punch-in and across punch-ins;
  - the slope of offset over position;
  - the input peak, for clipping;
  - whether confidences fall steadily over time, for fading;
  - a **second arrival**: a second confident peak at a consistent extra delay across
    events, which means monitoring echo.
- `Verdict`: Ok, NoSignal, Fading, Clipped, Scattered, Unsteady, PositionDependent, with
  the thresholds of spec §5 as named constants.
- The calibration arithmetic as a pure function: new round trip = used round trip +
  median offset, in seconds. A take that lands late was spliced from too early a frame.
- **Refined by the lead after A1:**
  - **Entry point for B1.** One function takes a layout, the take's samples and rate,
    and the punch-ins. Each punch-in is its timeline range in seconds, in the order
    recorded. The function returns a summary with the verdict, all flags that applied,
    per-punch-in figures, and per-event results.
  - **Which events are judged.** An event is *judged* in a punch-in when its sweep and
    the finder's window sit inside the range with a margin, a named constant. The splice
    cuts content at the range ends, and a sweep cut in half is not a failure of the
    path. Count judged and found separately; NoSignal is about found out of judged.
  - **Second arrivals.** `findSweep()` computes the second peak but does not return
    its position; add it, and its level against the chosen one, to `Arrival`. Monitoring
    echo is a second arrival at a consistent extra delay (a few ms of spread) across
    most found events, not more than some named dB below the direct sound.
  - **Verdict order.** When several apply, pick one and document it, with all flags kept.
    Suggested: NoSignal, Clipped, Fading, PositionDependent, Scattered, Unsteady, Ok.
  - **PositionDependent against Scattered.** Fit offset over punch-in position.
    PositionDependent is a slope above 0.5 % whose fit leaves little residual; large
    residuals are Scattered.
- **Tests:**
  - one event missing, the rest found;
  - fading;
  - clipped;
  - misplacement that grows with position (the 48000/44100 case) → PositionDependent;
  - two punch-ins 20 ms apart → Scattered or Unsteady by threshold;
  - monitoring echo detected;
  - an event cut by a range end is not judged;
  - the arithmetic with both signs. **Show that the sign test fails** when flipped.
  - A1 found that a take of 48 kHz frames read as 44.1 kHz finds nothing, because the
    sweeps are stretched. Build the rate case as punch-ins displaced by
    P·(1 − 44100/48000), not as a stretch.

### B1 — The alignment check runner (spec §2, §5 "App, every build", §6 app suite)

Read also: `docs/recording.md` whole (154 lines), `docs/architecture.md` sections on
layers, models and commands (search the headings), `docs/testing.md` "What there is to
reuse". In `MainWindow.cpp`, read by range: `record()`, the deferred lambda in
`recordingStarted()`, `pollTakeProgress()`, `finishSingingTake()` and
`wantedPreRollFrames()`.

- **Pure helper first**, in `LatencyCheck`, with a core test:
  - `punchInsFor(layout, count, eventsEach)` returns `count` consecutive punch-in ranges
    in seconds. Each holds `eventsEach` events that `judgeTake()` will judge, using its
    margins.
  - The calibration uses 4 × 3 on the calibration layout. Tests use a short layout of
    2 × 2 (≈ 8 s) to keep real time down.
- **New `main/AudioCheckRunner.{h,cpp}`** in `tony_app_files`. A QObject owned and wired
  by `MainWindow`. It is driven by signals and a polling timer, like
  `pollTakeProgress()`, **not by nested event loops**: it runs in every build, and the
  user can close the window at any moment.
- **Steps:**
  1. Write the reference WAV to the app data directory, overwriting the old one. Mono,
     44.1 kHz.
  2. `checkSaveModified()`, then `openPath(path, ReplaceSession)`. Wait for the
     reference's analysis, as `openReference()` in the tests does.
  3. For each punch-in: select its range and call `record()`; the take stops itself.
     Wait for the take's analysis before the next punch-in. It is not strictly needed,
     but it keeps pYIN's CPU load out of the next take's timing.
  4. Read the take's audio: the model `analyser2()->getMainModelId()`, mixed to mono,
     at its own rate. Call `judgeTake()`.
- **Record into Selection, Play Reference While Recording and a 1 s pre-roll** apply to
  the check's own takes through an override in `MainWindow`. `record()`,
  `recordingStarted()` and `wantedPreRollFrames()` consult it. **Never through
  `setChecked()`**: those actions write QSettings.
- **What each take used.** Keep, per take, the round trip it used: today
  `computeRecordingLatency(out, in)`, which B2 will change. Also keep the reported
  output and input latency, and the **recording's sample rate**.
- **The result** carries:
  - the `TakeSummary`;
  - the round trip used, and the reported pair;
  - the recording's rate and the reference's;
  - a **rate-mismatch flag**, set when the two rates differ, whatever the sweeps say
    (A2's finding: a 48 kHz take runs off the finder's reach);
  - the calibrated round trip from `calibratedRoundTrip()`, meaningful only on Ok or
    Unsteady.

  Emitted as a signal when done. Failures (no device, recording refused, the session
  closed mid-run) end the run with a reason.
- **Cancel** stops a take in progress through the normal Stop path and clears the
  override. `closeSession()` and `~MainWindow()` cancel a running check.
- **Not in this phase:** storing or using the result (B2), any dialog or menu entry (B3).
- **App tests.** Choose between a new class and adding to `TestRecordWorkflow`; say
  which. `TestMainWindow` and the fixtures live in `TestRecordWorkflow.h`. Use
  `FakeAudioIO` `loopback = true` and the short layout.
  - Wrong reported latencies (e.g. 2·4096 and 4096), `inputDelay` = 3·4096 + 123. The
    median offset equals the difference, in seconds at 44.1 kHz, to within a few
    frames. The verdict is Ok. The calibrated round trip equals `inputDelay`.
  - Fake at 48 kHz: the rate mismatch is flagged and both rates are given. Assert only
    what the runner reports, and that nothing crashes. What Tony does with 48 kHz takes
    is a separate, known bug.
  - After a check, the three toggles and their QSettings values are as before.
  - Cancel during a take leaves no recording in progress and the override cleared.
  - **Show failure** for the first test with the override's Play Reference half
    removed: nothing is heard, so NoSignal.

### B2 — Measured round trip in use (spec §5 `LatencyCalibration`, "MainWindow")

Read also: `docs/recording.md` "Latency"; `main/LatencyUtils.h` whole; in
`MainWindow.cpp`, the deferred lambda in `recordingStarted()` (search
`m_takeLatency.roundTrip`) and `refineRecordingLatency()`; `AudioCheckRunner.h`
(`AudioCheckResult`, `calibrationUsable()`).

- **New `main/LatencyCalibration.{h,cpp}`** in `tony_core`:
  - **Key.** The three Preferences values `createAudioIO()` reads (`audio-target`,
    then `audio-playback-device` and `audio-record-device`, each suffixed with the
    implementation when one is pinned; see `audioDeviceSettingKey()` in
    `MainWindow.cpp`) and the recording's rate. Device names can hold `/` and non-ASCII
    characters: encode them, so that QSettings does not make subgroups.
  - **Stored:** round trip and spread in seconds, the date, and the reported output
    and input latency, each **in seconds**. The reported pair is the staleness
    fingerprint: a stored figure is stale when either differs by more than a named
    tolerance.
  - `store`, `load`, `forget`, and a staleness test. QSettings group
    `LatencyCalibration`.
- **Use in `recordingStarted()`.** The round trip is the stored one when there is one
  for this key and it is not stale; otherwise the reported sum. Either way it is
  **converted to frames at the recording's rate**, from seconds.
  - Fix the reported sum's units while you are there. `getTargetPlayLatency()` counts
    frames at the session's rate (`ResamplerWrapper` converts it), and
    `getSystemRecordLatency()` at the device's. Today the two are added as they come;
    B1 measured 242 ms instead of 256 at 48 kHz.
  - The recording's model (`m_currentRecordingModelId`) exists by the time the lambda
    runs, and gives the rate.
  - Keep in `m_takeLatency` which source was used (reported or measured), and log it.
  - The start gap and everything downstream stay as they are.
- **MainWindow API for B4.** Store a check's result, forget the stored figure, and
  describe the figure in use (source, milliseconds, date).
- **Not in this phase:** any dialog, menu entry or playback change.
- **Tests:**
  - **Core.** Store, load and forget round trip, with a device name holding `/` and
    `ä`; the staleness tolerance on both sides; different keys stay apart. Use a
    QSettings scope the tests own and clear.
  - **App.**
    - `latency_end_to_end`'s recipe with wrong reported latencies and a stored figure
      equal to `inputDelay`: the sung step lands on the reference's. **The same test
      without the stored figure must fail**; show it.
    - A stale stored figure (fingerprint differs): the reported sum is used.
    - 48 kHz fake: the reported sum, in recording frames, equals
      `playbackLatency + recordLatency`, both device frames, to within a frame or two.
    - Check, store, check again (short plan): the second check's median offset is
      within a few frames of 0. This is the strongest test in the feature, and it
      takes about 25 s.

### B3 — The check's playback, and progress (spec §2, §8 "Loudness")

Read also: `docs/architecture.md` on play parameters and on `Analyser::setAudible()`
(search "audible"); in `Analyser.cpp`, where the reference's pan and the
sonification's audibility are set (search `setPlayPan`, `PlayParameters`).

- For the check's session only, the reference plays **centred** and at **−12 dBFS
  peak** after normalisation: the model is normalised to full scale when it is read, so
  the gain has to come off at playback. The pitch-track sonification is **silent**.
  - Do it through the play parameters of the check session's models and layers, never
    `Analyser::setAudible()`, which writes the shared settings (see `AGENTS.md`).
  - Make sure nothing puts them back later in the run, for example the analysis
    finishing, or the next take.
  - Nothing of the user's own sessions changes.
- **A progress signal** on the runner: step, punch-in *k* of *n*, and the seconds left
  where known, for B4's dialog.
- **Two runner fixes from B2's report:**
  - **Reference file name.** The reference WAV gets a new name each run (a counter or a
    timestamp), and old ones are removed when no session holds them. **Check Again**
    otherwise rewrites the file that the check session it replaces still has open; on
    Windows that write can fail. Linux cannot show it.
  - **Reported output latency.** `AudioCheckRunner` divides the reported output latency
    by the reference's rate. Have `TakeLatency` carry both reported figures **in
    seconds**, as `MainWindow::roundTripAt()` works them out, and have the result use
    those. Then the take path and the check can never disagree.
- **Tests:**
  - During a check the reference's play parameters are centred at the planned gain,
    and the sonification is not audible.
  - Afterwards, a newly opened ordinary file plays as before: reference pan and
    sonification as the settings say.
  - The sweeps reach `FakeAudioIO`'s captured output at about −12 dBFS on the mixed
    channel.
  - Progress reports each punch-in in order.

### B4 — Calibrate Audio dialog and menu (spec §2)

Read also: how an existing Tony dialog is built and tested (search
`confirmRecordingOverTake` and `askForTakeName` in `MainWindow.cpp` and
`TestRecordWorkflow.h`).

- **`main/CalibrateAudioDialog.{h,cpp}`**, non-modal and thin. Its pages:
  1. **Instructions:** the output and input device names, the latency in use with its
     source, "hold one earcup against the mic, off your ears", moderate volume.
  2. **Progress,** from B3's signal, with Cancel.
  3. **Result:**
     - the verdict in plain words, with the fix for each failure (spec §2 and §8);
     - the measured round trip against the driver's figure;
     - the spread;
     - both rates, with a plain sentence when they differ;
     - the input peak;
     - the echo, if one was heard.

     **Use this latency** (only when `calibrationUsable()`), **Check Again** and
     **Close**.
- **Menu, Playback:**
  - **Calibrate Audio…**, disabled while recording;
  - a disabled line saying the latency in use ("Latency: measured 187 ms, 25 Sep" or
    "Latency: driver's figure, 400 ms");
  - **Forget Measured Latency**.
- The calibration plan is 4 punch-ins × 3 events on the calibration layout; it fits
  since the lead's spacing change.
- **The device key is taken when the check starts.** Carry it in `AudioCheckResult`,
  and have `storeMeasuredLatency()` use it, not the Preferences at the moment the
  button is pressed. The dialog is non-modal, so the user could change device in
  between (B2's report). Also disable both Audio Device menus while a check runs.
- Anything that asks the user goes through a virtual seam, as
  `confirmRecordingOverTake()` does. The app tests drive the dialog's slots directly;
  the dialog watchdog fails a test on any unexpected modal dialog.
- **Tests:**
  - the menu starts a check;
  - Use this latency stores (B2's API), and the menu line changes;
  - Forget clears it;
  - the dialog's result words for NoSignal and for a rate mismatch;
  - Calibrate Audio is disabled during an ordinary take.
- **After B4 the user runs it on Windows** (the checkpoint in spec §7).

### C0 — TakeDiff (spec §5 "tony_core")

To be refined by the lead.

### C1 — Dev-check framework and first group (spec §3, §4, §5 "development builds only")

To be refined by the lead.

### C2 — Observer group (spec §4 items 3, 4, 5, 8, 15, 16)

To be refined by the lead.

### C3 — Joins and long song (spec §4 items 9, 10)

To be refined by the lead.

### C4 — Smoke group (spec §4 items 19–25, 27, 28)

To be refined by the lead.

### D — Documentation pass

- Bring `docs/` up to date from the code, this log and the spec:
  - `recording.md`: the latency section;
  - `testing.md`: a Dev checks section, and the loopback fake;
  - `manual-checklist.md`: automated items marked with their check's name;
  - `architecture.md`, if new classes change who owns what;
  - `open-points.md`: remove the item, and add what is left open.
- Make `docs/calibrate-audio.md` describe what was built, with a "Known limitations and
  open points" section.
- Delete this work-orders file and remove the spec's link to it.
- No code. Suspected bugs go in the report.

## 5. Log (newest last; 25 lines at most per entry)

Template:

    ### Phase <id> — <date>
    Built: ...
    Choices / deviations: ...
    The next phase must know: ...
    Left open: ...

### Phase A1 — 2026-09-25
Built: `main/LatencyCheck.{h,cpp}` in `tony_core`: `calibrationLayout()`, `devLayout()`, `longLayout()` (rate defaults to 44100), `sweep(rate)`, `generate(layout)`, `findSweep(samples, count, rate, expectedSeconds)` → `Arrival {found, errorFrames, errorSeconds, peakOverMedianDb, peakOverSecondDb, levelDb, inputPeak}`. Thresholds are `k…` constants in the header. `main/test/TestLatencyCheck.h`: 16 tests, 1.4 s.
Choices / deviations:
- Linear sweep: flat spectrum, narrowest peak; an exponential sweep's harmonics match it 67/106 ms *early*, where the earliest-peak rule looks.
- Envelope = magnitude of the analytic signal (a second inverse FFT gives the Hilbert part).
- Earlier peak counts if it is a local maximum, within 6 dB, and ≥ 1 ms before the largest. No dip rule: one arrival with a hole in its band beats, with deep dips, so only time tells arrivals apart (`finder_takes_one_arrival_as_one`).
- No band mask beyond the matched filter itself: a 0 dBFS 100 Hz hum already comes through 100 dB down; a mask changed nothing measurable.
- Confidences are the chosen (earliest) peak's; "second" is the envelope's maximum more than 10 ms from it.
- A gap is sweep to sweep. Calibration sweeps at 1.0, 3.1, 4.7, 7.2, 9.1, 11.4, 13.1, 15.7, 17.7, 19.5, 21.9, 24.1 s; each tone starts 0.3 s after its sweep, 0.8 s long. Dev adds 3 s held tones after sweeps at 26.9, 30.9, 35.2 s (40 s). Long: 113 events, 240 s.
- Reflection test at 5.5 dB (direct found) and 7 dB (reflection taken): exactly 6 dB passes here only by rounding (6.1 dB flips).
The next phase must know:
- Pass the take's samples at their own rate, and the expected time in seconds (layout frame / layout rate).
- A take of 48 kHz samples read as 44.1 kHz (sweeps stretched 8.8%) finds nothing: level −22 dB, 0.1 dB over the second. `findSweep` at 48000 finds them. A2's "resampled by 48000/44100" case must be built as misplaced frames, or try both rates.
- Measured: noise alone 6–12 dB over the median (threshold 15); SNR 0 / −10 dB: 38 / 28 dB over the median, 26 / 16 dB over the second. In digital silence the median is ~0, so over-the-median reads up to the 200 dB clamp.
- About 20 ms per call. The second peak's position is computed but not returned; second arrivals (A2) need it.
Left open: every threshold untuned; nothing reads `inputPeak` yet.

### Phase A2 — 2026-09-25
Built: in `main/LatencyCheck.{h,cpp}`: `Arrival::secondDelaySeconds`, `secondLevelDb`; `judgeTake(layout, take, count, rate, punchIns)` → `TakeSummary {verdict, flags, punchIns[], events[], judged, found, medianOffset, spread, slope, slopeResidual, inputPeak, fadingDb, echo}`; `PunchIn {start, end}` in timeline seconds; `Verdict`, declared in precedence order (NoSignal, Clipped, Fading, PositionDependent, Scattered, Unsteady, Ok); `verdictName()`; `calibratedRoundTrip(used, offset)` = used + offset. 9 tests in `TestLatencyCheck` itself (reusing its helpers); the class now takes 2.9 s.
Choices / deviations:
- Judged: the finder's window, plus a sweep's length past it, inside the range with 50 ms to spare (the splice crossfades 5 ms). An event under a later, overlapping punch-in is judged in that one only. Ranges stop at the take's end.
- Across = median of the punch-ins' medians (each stream start counts once); spread = their max − min. Unsteady/Scattered take the larger of that and any spread within one punch-in. PositionDependent: 3 punch-ins at least (a line through two always fits), |slope| > 0.5 %, and what the least-squares line leaves ≤ 5 ms. NoSignal also when nothing is found, or nothing judged.
- Fading reads `levelDb`, not a confidence: over the median of near silence a confidence runs up to the 200 dB clamp. Median of the first half of the judged events (as recorded) minus that of the second ≥ 10 dB; needs 6 events.
- Echo is judged over events *heard* (≥ 15 dB over the median), not found: an echo within 6 dB leaves nothing found. Added `kEchoMinDelaySeconds` = 20 ms: a reflection 9 ms after the direct sound and 5.5 dB stronger leaves the tail of its peak at 10.1 ms, 9–23 dB down, after every sweep, and was reported as an echo. Also ≤ 30 dB down, within 3 ms of the median delay, in more than half of the heard events and 3 at least.
- Clipped: the largest sample inside the ranges ≥ −0.2 dBFS.
The next phase must know:
- **At 48 kHz, §2's punch-ins at 14 and 20 s land 1.14 and 1.63 s early, beyond the finder's 0.8 s reach.** Measured: the finder takes the neighbouring sweeps, fully confident (+662, +575 ms), and the verdict is Scattered. PositionDependent needs punch-ins that start before about 9.8 s, so §6's "a 48 kHz fake reports PositionDependent" fails with §2's punch-ins. The rates themselves can name the rate.
- §2's punch-ins as [2,7], [8,13], [14,19], [20,25] s judge 7 events (2, 2, 2, 1). A punch-in shorter than 1.95 s judges none.
- An echo under 20 ms (an interface's direct monitor) is not seen.
Left open: every threshold untuned. The verdict thresholds came from the lead's brief; spec §5 has none of them.

### Phase B1 — 2026-09-26
Built: `LatencyCheck::punchInsFor()` (+ `kPunchInSlackSeconds`, 10 ms) and core test `punch_ins_hold_the_events_asked_for`. `TakeLatency` in `LatencyUtils.h`. `main/AudioCheckRunner.{h,cpp}` (`tony_app`): `Plan`, `start()`, `cancel()`, `sessionClosing()`, `finished(AudioCheckResult)`. `MainWindow`: `friend class AudioCheckRunner`, the override `m_audioCheckTakes` (read by `record()`, the `recordingStarted()` lambda, `wantedPreRollFrames()`), `m_takeLatency`, the runner made in the constructor, deleted first in `~MainWindow`, told by `closeSession()`. New app class `main/test/TestAudioCheck.h`: 6 tests, 32 s.
Choices / deviations:
- **4 × 3 does not fit the calibration layout** (spec §2 now says why). `punchInsFor()` returns nothing then; 4 × 2 and 3 × 3 fit. B3 needs the lead's choice.
- The take is read from its **file**, not its model: the model is peak-normalised as read (measured: every take Clipped) and resampled to 44.1 kHz.
- `friend` over accessors: the runner needs about nine internals. A new test class, not `TestRecordWorkflow` (5475 lines); its watchdog and init/cleanup are copied.
- Selection: `clearSelections()` + `addSelectionQuietly()`, so the reference is not re-analysed during a take; each punch-in adds one or two "Select" undo steps, as a user's selection does.
- Waits: poll every 50 ms for "nothing being analysed", not "analysed": with auto-analysis off the reference never gets layers (test `check_runs_without_automatic_analysis`). Limits 60 s reference, 30 s a take's analysis, take length + 10 s to stop (then the Stop path); each ends the run with a reason.
- `~MainWindow` deletes the runner: the run ends silently and a take in progress is left to the destructor (the Stop path would splice and start pYIN mid-teardown). `closeSession()` → Stop path + `finished()`.
- Reference: `AppDataLocation/calibrate-audio-reference.wav` unless the plan names a path (tests: their temp dir). Save question first, then write, then open.
- `calibrationUsable()`: Ok or Unsteady, and no rate mismatch.
- No loopback gain was needed (see below).
The next phase must know:
- `Analyser` pans the reference hard left, sonification hard right: only the **left earcup** carries sweeps. Normalised, the reference plays at 0 dBFS, not −12. The fake averages channels: sweeps loop back at half level (peak 0.905 with the synth).
- `getTargetPlayLatency()` counts session frames, `getSystemRecordLatency()` device frames; the result converts each at its own rate. At 48 kHz the round trip used was 242 ms, not 256.
- 48 kHz fake: Scattered, 3 of 3 found, offsets −200 ms median, as A2 foresaw.
- No progress signal yet; B3's dialog may want one (`m_punchIn`).
Left open: no test deletes the window mid-check. Seen while proving the session-close hook: closing a session during an **ordinary** take, then pressing Stop, hangs (pre-existing).

### Lead — 2026-09-26, after B1
- Reordered the calibration spacings to `{21,16,25,19,17,23,26,20,24,18,22}` (B1's suggestion) so that 4 × 3 punch-ins fit; `punch_ins_hold_the_events_asked_for` now asks for 4 × 3 and failed on the old order. `judge_only_events_inside_a_punch_in` names its events from the layout instead of 9.1 and 11.4 s.
- Split B3 into B3 (the check's playback and progress) and B4 (dialog and menu), after B1 needed 370k tokens.
- Calibration sweeps now at 1.0, 3.1, 4.7, 7.2, 9.1, 10.8, 13.1, 15.7, 17.7, 20.1, 21.9, 24.1 s.

### Phase B2 — 2026-09-26
Built: `main/LatencyCalibration.{h,cpp}` (`tony_core`, namespace): `Key`, `currentKey(settings, rate)`, `Figure`, `store`/`load`/`forget` (all take a `QSettings &`), `isStale`, `kStaleToleranceSeconds` = 1 ms, `Source`, `InUse {source, roundTrip, date, stale}`, `roundTripInUse()`, `reportedSeconds()`, `toFrames()`. `TakeLatency::measured`. `MainWindow`: `roundTripAt(rate)`, used by the `recordingStarted()` lambda; B4's API `storeMeasuredLatency(result)`, `forgetMeasuredLatency()`, `latencyInUse()`. Core class `TestLatencyCalibration` (6 tests); app tests `latency_measured_round_trip_used`, `latency_stale_round_trip_ignored`, `latency_reported_at_the_device_rate`, `latency_reported_with_device_opened_first` (TestRecordWorkflow), `check_stored_round_trip_is_used` (TestAudioCheck, 25 s).
Choices / deviations:
- Settings: `LatencyCalibration/<driver>|<playback>|<record>/<rate>/`. In names only `%`, `/`, `\` and `|` are percent-encoded. Registry key names stop at 255 characters, and full encoding of long non-ASCII names could pass that. Values are stored as text (`'g'`, 17 digits) and the date as ISO UTC.
- The key follows `createAudioIO()`, not `audioDeviceSettingKey()`: they differ for `audio-target` = "auto", which Tony never writes.
- **The output latency's unit** (spec §11, corrected): frames at `m_playSource->getDeviceSampleRate()`, or at the recording's rate when that is 0. This is not always the session's rate. If a device is chosen before any file is opened (or a standalone take is the first action), `ResamplerWrapper` has no source rate yet. It passes the device's figure through unconverted and tells the play source 0. Using the session's rate there gave 13012 frames instead of 12288 at 48 kHz (`latency_reported_with_device_opened_first`).
- `storeMeasuredLatency()` stores only when `calibrationUsable()`. The key uses the current Preferences, the rate is the result's, and the fingerprint is the result's reported pair.
- `latencyInUse()`/`forget` use the rate of the last take placed with a round trip. Before any take they use the session's rate, the only rate at which a usable check stores. That rate is reset when a device is chosen from the menu. The device's rate cannot be known before a take: `AudioCallbackRecordTarget` has no getter for it.
- A stale figure is not deleted; it becomes valid again if the driver goes back to reporting the old pair.
The next phase must know:
- With no figure stored at 44.1 kHz, the round trip is exactly the old sum; this is tested in core across a grid of values. At 48 kHz the check now uses 256 ms, not 242.
- The runner's `reportedOutputLatency` still divides by the reference's rate. It matches the take path at 44.1 kHz, the only rate that is stored.
- `storeMeasuredLatency()` reads the device from the Preferences when "Use this latency" is pressed. If B4's non-modal dialog lets the device change in between, the figure is stored under the new device.
Left open: `computeRecordingLatency()` is unused outside `TestLatencyShift`. The svapp fork could add `AudioCallbackRecordTarget::getRecordSampleRate()` so that `latencyInUse()` knows the rate before the first take.

### Phase B3 — 2026-09-26
Built: `AudioCheckRunner::setPlayback()`: on the play parameters of the check session's own models, the reference audible, centred, gain 10^(kPeakDbfs/20) (1 if the normalise preference is off); its pitch and notes muted. Applied once the reference is open and again before every punch-in. Public `Step`, `Progress {step, punchIn, punchIns, secondsLeft}`, signal `progress()`. `referenceDirectory()`, `nextReferencePath(dir, inUse)`. `LatencyCalibration::InUse::reportedOutput/Input` (seconds, whichever source won); `TakeLatency::reportedOutput/Input` are now those seconds, and `end()` copies them. App tests `check_plays_the_reference_centred_and_quiet` (13 s), `check_leaves_the_next_session_alone` (4 s), `check_reference_gets_a_file_of_its_own`; core `round_trip_in_use` extended. Four existing tests now compare the reported pair in seconds.
Choices / deviations:
- The toolbar's reference level control (`m_audioLPW`) answers a gain between its notches (−12 dB lies between −11.25 and −20) by emitting the nearest; `audioGainChanged()` then sets it through `Analyser::setGain()`/`setAudible()`, writing `Analyser/audible-0`. `setPlayback()` moves the control first under a `QSignalBlocker`. Without it both new session tests fail on the settings.
- Reference files `calibrate-audio-reference-N.wav`: every such file in the directory but the open session's main model file is removed, then the lowest free N is taken, so names alternate 1, 2. A timestamp per run would add a dead Recent Files entry per run (`RecentFiles` has no remove, and keeps 20).
- The check session keeps its playback after the run. The reference is made audible even where the user's settings mute it.
- `progress()` comes from `poll()` only, never from inside `start()`/`cancel()`: on each step change, and each whole second less while recording. `secondsLeft` counts recording to come (min(1 s, start) + range per take), not analyses. No metatype: direct connections only.
- "Afterwards" is tested after a cancelled run, against the same file opened before the check: the settings alone do not say how a session plays (below).
The next phase must know:
- Pre-existing, not fixed: (a) in the first file of a window the same feedback moves the pitch and notes gain from 0.5 to 0.562 and forces both audible, writing the settings; (b) `audible-0` is overridden at load by `audible-3`: the spectrogram is a layer on the reference's model, so it plays whenever `audible-3` is true.
- B4: add a few seconds per analysis to `secondsLeft` for a rough total.
Left open: the default reference path is exercised only through `nextReferencePath()`; the tests name their own file.

### Lead — 2026-09-26, after B3
- De-raced `TestSingingAnalysis::waitForRange()` (`2a306fb`): `initialAnalysisCompleted()` also fires from `layerCompletionChanged()` before a ranged merge; it failed once in a full run.
- For phase D's `open-points.md`, older bugs B3 found: (a) in a window's first file the toolbar level control's notches move the pitch/notes gain 0.5 → 0.562, force both audible and write that to the settings; (b) `audible-0` (Play Audio) is overridden on load by `audible-3` (the spectrogram layer on the same model, loaded last). Also B1's: closing a session during an ordinary take, then Stop, hangs.

### Phase B4 — 2026-09-26
Built: `main/CalibrateAudioDialog.{h,cpp}` (`tony_app`), a `QDialog`, not modal, with three pages (instructions, progress, result): `present()`, `startCheck()` (Start and Check Again), `cancelCheck()`, `useLatency()`, `showResult()`, `reject()`; for tests `page()`, `pageText()`, `canUseLatency()`, `setPlan()`; static `describeLatency(InUse)`. `AudioCheckRunner::calibrationPlan()` (4 × 3). `AudioCheckResult::key`: the Preferences' devices taken in `start()`, the rate set in `end()`; `storeMeasuredLatency()` stores under it. `MainWindow`: Playback ▸ Calibrate Audio..., a disabled line "Latency: ...", Forget Measured Latency, after the device submenus; `calibrateAudio()`, `updateLatencyMenuLine()`. `updateMenuStates()` shuts Calibrate Audio during any take or check, and both device menus during a check; the runner's `progress` and `finished` call it. `TestAudioCheck`: 5 tests `calibrate_audio_*`, one full run (13 s); `TestMainWindow` accessors.
Choices / deviations:
- The window owns the dialog, makes it on first use, and deletes it in `~MainWindow` before the runner. The dialog calls only `latencyInUse()` and `storeMeasuredLatency()`, and shows only runs it started itself.
- Closing it (title bar, Esc, Close) while its check runs cancels the check. Opened again, it shows the instructions.
- Menu line: "Latency: measured 281 ms, 26 Sep" (the year only when not this one), "Latency: driver's figure, 279 ms", plus " (the measured one is out of date)" when stale, or "not known yet" while the device reports 0. Forget is enabled while a figure is kept, stale or not. The line is refreshed on the menu's `aboutToShow`, in `updateMenuStates()`, and by store and forget.
- Result page: one sentence for the verdict, then its fix. A rate mismatch replaces the verdict's words, and an echo adds a paragraph. Then a table: the round trip measured (not for NoSignal or a rate mismatch) against the driver's (out + in), what the takes were placed with, each punch-in's offset, the spread, found of judged, both rates, the input peak, the echo, and the devices. The text is selectable, to copy.
- Progress: the runner's seconds left plus `kSecondsPerAnalysis` = 3 s for each analysis to come. The bar never goes back.
- Nothing new asks the user, so there is no new seam: Forget asks nothing.
The next phase must know:
- A run replacing a check session asks "Session modified: save?" (its takes mark it modified). Check Again always meets it; answer No. The runner could skip the question for its own reference's session.
- Not on the result page: §2's mic channel and noise floor. The runner measures neither.
Left open: Record stays enabled during a check. Pressing it there goes through the Stop path and ends the check's take early; what the run then makes of it was not tried.
