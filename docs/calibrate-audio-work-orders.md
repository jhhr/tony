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
  - app suite: see the log's lead entry after the first full run.

## 4. Phases

Done: none.

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
- **Tests:**
  - one event missing, the rest found;
  - fading;
  - clipped;
  - resampled by 48000/44100 → PositionDependent;
  - two punch-ins 20 ms apart → Scattered or Unsteady by threshold;
  - monitoring echo detected;
  - the arithmetic with both signs. **Show that the sign test fails** when flipped.

### B1 — The alignment check runner (spec §2, §5 "App, every build", §6 app suite)

To be refined by the lead after A2.

### B2 — Measured round trip in use (spec §5 `LatencyCalibration`, "MainWindow")

To be refined by the lead after B1.

### B3 — Calibrate Audio dialog and menu (spec §2)

To be refined by the lead after B2.

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
