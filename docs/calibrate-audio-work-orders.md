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

1. This file, all of it. Not `docs/calibrate-audio-log.md` (the finished phases), unless
   you are phase D.
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
      ninja -j 4 -C build_linux tony test-tony-core test-tony-app test-tony-dev test-tony-device pyin.so > tmp/build.log 2>&1; echo "exit:$?" >> tmp/build.log; tail -5 tmp/build.log

  Search the log for `error:`; never read it whole. meson reconfigures by itself after
  `meson.build` changes. Targets have no `.exe`.
- **`pyin.so` must be built too.** The app suite sets `VAMP_PATH` to its own directory.
  Without the plugin, every test that waits for pitch analysis hangs until QtTest's
  5-minute watchdog aborts the run.
- Test, from `build_linux/`:

      mkdir -p ../tmp/tl && rm -f ../tmp/tl/*.txt
      TONY_TEST_LOG_DIR=../tmp/tl ./test-tony-core > ../tmp/test.log 2>&1; echo "exit:$?"
      TONY_TEST_LOG_DIR=../tmp/tl ./test-tony-app  > ../tmp/test-app.log 2>&1; echo "exit:$?"
      TONY_TEST_LOG_DIR=../tmp/tl ./test-tony-dev  > ../tmp/test-dev.log 2>&1; echo "exit:$?"
      TONY_TEST_LOG_DIR=../tmp/tl ./test-tony-dev some_test_name > ../tmp/test-dev.log 2>&1
      grep -a "^FAIL\|^   Loc\|^Totals" ../tmp/tl/*.txt

  - Read results from the per-suite files, not stdout.
  - A test name on the command line goes to every suite in the executable, so the exit
    status of a run with names is meaningless.
  - New test classes are registered in `main/test/tony-core-test.cpp`,
    `tony-app-test.cpp` or `tony-dev-test.cpp` and added to `meson.build`.
    `TestDevChecks` is the only suite of `test-tony-dev`.
- While working, run **only your tests**. Run all three whole suites **once**, at the
  end, and again only if something failed. `test-tony-app` takes about 8 minutes: run it
  with the Bash tool's `run_in_background: true` and wait for the completion notice, as
  a foreground call can hit the 10-minute tool limit. Never run two suites at once, nor a
  suite while building: the app tests record in real time.
- App tests run in real time against `FakeAudioIO`: keep them short (seconds, not tens
  of seconds).
- Every behaviour gets a test that can fail. Show it for the two or three that matter
  most by breaking the code for a moment. Undo the break **by hand**: never
  `git checkout` or `git restore` a file to revert an experiment.
- Do not weaken or delete an existing test to get green. If one is wrong because the
  behaviour was meant to change, change it and say so.
- **Linux traps:**
  - Section 3 gives the suite results to expect.
  - Qt 6.4 does not match a `SIGNAL()`/`SLOT()` string naming `ModelId` or `sv_frame_t`
    with moc's `sv::` names: the connection fails at run time with "No such slot". The
    user's Qt happens to match them, so a string connect can pass there and fail here.
    Use member-pointer `connect` only, and grep test output for "No such slot".
- **Windows traps** (the user builds with MinGW; Linux will not catch these):
  - Windows headers define `near` and `far` as empty macros, so a variable named `near`
    compiles here and breaks the user's build. Avoid those two names, and other
    Windows macro names such as `min`, `max`, `ERROR`, `IN`, `OUT`.

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
- The `Totals` lines of the final full runs of all three suites, copied, not paraphrased.
- Which tests you saw fail without the change.
- Choices made, deviations, anything fragile or unfinished. Say it plainly: a problem
  reported is cheap, one found later is not.

## 3. State of the code

As of 2026-09-26, after C1a and the merge of `default`. The work orders and log of the
finished phases are in `docs/calibrate-audio-log.md`: **do not read it**; what you need of
it is here.

**What exists**

- `tony_core`: `LatencyCheck` (the three layouts, the sweep finder, `judgeTake()` with its
  verdicts and second-arrival `echo`, `punchInsFor()`), `LatencyCalibration` (the stored
  round trip per device key and rate), `TakeDiff` (audio bit-identical outside a range,
  events unchanged outside a range ± 0.25 s, pitch and notes across a join, sample step at
  a join). Core tests: `TestLatencyCheck`, `TestLatencyCalibration`, `TestTakeDiff`.
- `AudioCheckRunner` (every build): a `Plan` (layout; `punchIns` × `eventsEach` or explicit
  `ranges`; `keepSession`; a `roundTrip` for the run only), steps driven by a 50 ms timer,
  never a nested event loop. Its takes go through `MainWindow::record()` with the
  override `m_audioCheckTakes` (Record into Selection, Play Reference While Recording,
  a 1 s pre-roll, whatever the toolbar says) and are judged from the take's own file.
  `progress()` reports the step (`Recording`, `AnalysingTake`, …) and punch-in.
  App tests: `TestAudioCheck` in `test-tony-app` (23 tests, about 2 minutes).
- `CalibrateAudioDialog` (every build): instructions, progress, result; in dev builds a
  checkbox that carries on into the dev checks with the calibrated round trip.
- `MainWindow`: `m_takeLatency` (`TakeLatency`: round trip, reported pair, rate, start gap
  and whether it was measured) for the last take; `roundTripAt()`;
  `m_audioCheckRoundTrip`, used only in `recordingStarted()`'s deferred lambda; Record's
  action goes to `recordPressed()`, which ignores presses while `audioCheckRunning()`.
- `main/dev/DevChecks` (dev builds, `TONY_DEV_CHECKS`): stages driven by a timer and the
  runner's `finished()`. Stage 1 "Fresh punch-ins": the dev reference (`devLayout()`) opened
  as a session of its own, punch-ins [6.3, 10.2] and [16.8, 21.2] s. Stage 2 "Save and
  reopen" into a numbered scratch folder. Items 1 (`latency_on_this_machine`) and 2
  (`several_phrases_in_one_take`), ±2 ms per sweep. `CheckResult`, `DevReport`, the report
  `DevChecks.txt`. Free for new punch-ins: before 4.3 s, 10.2–16.8 s, 21.2–25 s, and the
  held tones from 26.9 s (C2's joins).
  Tests: `TestDevChecks`, in its own executable `test-tony-dev` (9 tests, about 66 s).
- **From `default`:** `TestUiChecks` (in `test-tony-app`) automates the checklist's screen,
  keyboard and dialog items on the fake device. `test-tony-device` (`TestRealDevice.h`,
  run by hand) records a 4-minute click track through the air and checks latency, two
  recordings in one take, echo, Stop time, input channel levels and live dots. **The user
  decided that the dev run takes these over, and `test-tony-device` is retired (C3).**
  `TestMainWindow` lives in `main/test/TestMainWindow.h`, with this work's accessors
  (`audioCheck()`, `devChecks()`, `takeLatency()`, the menus and actions).

**Facts found along the way**

- A model's `getLocalFilename()` is the decoded copy in the temporary directory ("normalise
  audio" is on); the file opened is `AudioCheckRunner::mainModelFile()`.
- On the loopback fake with its true round trip every sweep lands at 0 frames; its
  reported pair is 2.8 ms short of the true round trip.
- After a reopen, pitch and notes are restored from the session, not analysed.
- `getOutputLevels()` / `getInputLevels()` give the peak since the previous call, and reset
  it: one reader only, or each takes the other's peaks.
- `TestAudioCheck`'s fixture deletes each window's `DevChecks`, so that its dialog tests
  do not carry on into them. Test references go to Qt's test location
  (`QStandardPaths::setTestModeEnabled`); reports and scratch folders to the test's own
  directory.
- Every take logs "No such signal sv::WritableWaveFileModel::aboutToBeDeleted()" (from
  svapp, old, harmless here); ignore it.

**The user's first run on Windows** (MME, wired mic and headphones, one earcup to the mic):
round trip 301 and 295 ms, verdict Unsteady both times (the spread across or within
punch-ins between 5 and 15 ms); with the mic between both cups, Scattered (over 15 ms).
The detailed figures are awaited. So the ±2 ms of items 1 and 2 will fail on the user's
machine as things stand: thresholds get tuned from real report files, not now. Whether to
keep the stream running between takes (an svapp change) waits on those figures.

**Linux baseline** (Qt 6.4.2): core all green but 4 `TestTakesFile` tests
(`takes_folder`, `relative_audio_path`, `resolve_audio_path`, `in_folder`: Windows paths);
your final runs must show exactly these 4. `test-tony-app` green in about 8 minutes
(`TestRecordWorkflow` 98, `TestUiChecks` 19, `TestAudioCheck` 23, …); `test-tony-dev`
green. `tony-app-test.cpp` draws text without sub-pixel anti-aliasing, or Qt 6.4's
coloured fringes on the scale's labels read as live dots in `TestUiChecks`.

## 4. Phases

Done: A1 (`944df7c`), A2 (`a03b7ec`), B1 (`58de074`), B2 (`47944f2`), B3 (`8524d5f`), B4 (`9b1fb6c`), C0 (`1ef2494`).

Also done: C1a (`4370131`), the merge of `default` (`c8b9585`), `test-tony-dev` (lead).

**Order from here:** C1b, C1c, C2, C3, then the lead's release build, then D. The spec's
old C2 (observer group) and C4 (smoke group) are gone: `TestUiChecks` covers the smoke
items and the screen, and what the dev run measures on the device is now in C1b–C2.

**Budget.** C1a took over 500k tokens against a target of 200k. Read what you need, but
build only your phase, keep tests few and meaningful, and stop to report rather than
redesign.

### C1b — Take observer; items 3, 4, 5 (spec §4 rows 3, 4, 5, 8)

Read also: `main/dev/DevChecks.{h,cpp}` whole; `main/test/TestDevChecks.h` for the
fixture; in `main/test/TestRealDevice.h` the tests `record_the_reference_through_the_air`,
`nothing_of_the_take_comes_back_out` and `live_dots_were_drawn` (search `Checklist:`),
which these checks take over; `docs/recording.md` on the live tracker and the cursor
during a take; `main/test/FakeAudioIO.h`.

- **`main/dev/TakeObserver.{h,cpp}`** (dev builds). DevChecks starts one when the
  runner's `progress()` reports `Recording` for a punch-in, and stops it when that
  punch-in's analysis is done. Every 20 ms it records, with the time: the playback frame
  (the `ViewManager`'s), the output levels, the input levels, the frames received, the
  status text, whether a modal widget is up; and each live dot as it first appears (the
  realtime model, `m_realtimePitchModelId`, looked up each poll), with its frame, its
  value and the playback frame at that moment; and when the take stopped recording.
- **Levels, a checked fact.** `getOutputLevels()` / `getInputLevels()` return the peak
  since the previous call and reset it. `ViewManager::checkPlayStatus()` (svgui) already
  reads them every 20 ms: **while recording it reads the input levels only**, and emits
  `monitoringLevelsChanged(left, right)` when they change; while only playing, the
  output levels. So during a take the observer takes the input levels from that signal,
  and is the only reader of the output levels. Find out whether the lead-in counts as
  recording there, and say.
- **Items**, from the punch-ins of stage 1 (they then hold for every later punch-in too):
  - **3 Live dots** (and row 8's number). Pass when each punch-in drew more than 10 dots
    (as `test-tony-device`), and the dots lie on the reference's tones: frames within the
    tone's span ± 1 hop, value within 50 cents of the tone. Numbers: dots per punch-in,
    and the dot-to-cursor offset (dot frame against the playback frame when it
    appeared), median and spread in ms: the "Cursor versus dots" risk of spec §8.
  - **4 Nothing of the take in the speakers.** Fail on a second arrival
    (`TakeSummary::echo`, as the calibration already detects it), or on any output level
    above zero in a poll that lies wholly in one of the reference's silent gaps, with a
    margin you work out from the output latency and the poll interval; also Play Singing
    Audio's state the same before and after the take. Numbers: echo delay and level,
    the largest output level in the gaps.
  - **5 Mic channel.** Per-channel level of each punch-in's raw recording (the newest
    `recorded-*.wav`, as `newestRecording()` finds it in `TestRealDevice.h`, or better the
    path the window itself knows, if it does). Measured: which input carries the mic, and
    its level. When it is input 2, item 3's dots are the check (Pass/Fail); otherwise
    "not applicable here".
- **The report header** gains what `test-tony-device`'s `device()` test logs: the audio
  drivers built in, the reported playback and record latencies.
- **`FakeAudioIO`**: a second loopback tap (delay and gain), new fields that change no
  existing meaning, for item 4's failing test. The input on one channel exists already
  (`inputChannel`).
- **Tests** (`TestDevChecks`): passing on the loopback fake; item 4 failing with the echo
  tap; item 5 with the input on channel 2 (dots still drawn, "input 2"). Show failure for
  the dots-on-tones check and the silent-gap check by breaking them.

### C1c — Re-record and pre-roll stages; items 7, 12, 13, 14 (spec §4 rows 7, 12, 13, 14)

To be refined by the lead after C1b. Outline:

- Before and after each punch-in: the take's samples from its file and its pitch and
  notes, compared with `TakeDiff`.
- New stages before "Save and reopen": re-record over one of stage 1's punch-ins, starting
  inside it, so its lead-in plays over earlier material (items 7, 12, 14), with no
  overwrite question for the check's takes; then a punch-in at P = 1 s with a 3 s
  pre-roll for that plan (item 13: playback from 0, a shorter countdown, placement right).
- Item 14 from the observer: the take stopped within 0.25 s plus one poll of the
  selection's end; the coverage added is exactly the selection; no modal widget.
- Items 1 and 2 then cover every punch-in of the run.

### C2 — Long song and joins; items 9, 10 (spec §4 rows 9, 10)

To be refined by the lead after C1c. Outline:

- **9** After "Save and reopen": the long reference (`longLayout()`, 4 minutes) as a new
  session; the whole-song analysis time; two punch-ins far apart (as `test-tony-device`,
  about 60 s and 150 s), each timed from Stop to its pitch merged. Pass when each is under
  half the whole-song time and pitch outside the range is unchanged (the ranged path ran).
  These punch-ins also count for items 1 and 2: placement far into a song is where a
  rate mismatch shows.
- **10** Two punch-ins meeting in the middle of a held tone of the dev layout: `TakeDiff`'s
  step, pitch and note checks at the join, and nothing moved outside ± 0.25 s. C0 found
  (from the code) that the join is a 10 ms dip, and that the notes merge by onset may drop
  or split the note: measure, report the result as it is, and if it fails on today's code
  the app test is an expected failure with the reason, as `default` does for its defects.

### C3 — Retire `test-tony-device`

To be refined by the lead after C2. Outline: remove `TestRealDevice.h`,
`tony-device-check.cpp` and its target once everything it checks is in the dev run (its
"no input does no harm" case included, as an app test if not already covered); update the
docs that name it in the same commit (`AGENTS.md`, `building.md`, `testing.md`, and
`manual-checklist.md` section 1, which becomes "Calibrate Audio with the dev checks").

### Lead — release build

A build directory of type `release`: it must compile and link with no `main/dev/` file
(spec §8). The lead does this, not an agent.

### D — Documentation pass

- Read the log in `docs/calibrate-audio-log.md` as well as this one: you are the one
  phase that does.
- Bring `docs/` up to date from the code, the logs and the spec:
  - `recording.md`: the latency section;
  - `testing.md`: a Dev checks section, `TestAudioCheck`, the loopback fake, and who uses
    `TestMainWindow`;
  - `manual-checklist.md`: what the dev run settles, and what is left by hand;
  - `architecture.md`, if new classes change who owns what;
  - `building.md`: whether "Qt 6.11, not Ubuntu's 6.4" still holds now that the connects
    are member-pointer ones;
  - `README.md` and `open-points.md`: agree on what of the checklist has been tried; remove
    the Calibrate Audio "Not built" item and add what is left open, the svapp
    `aboutToBeDeleted()` warning included.
- Make `docs/calibrate-audio.md` describe what was built, with a "Known limitations and
  open points" section.
- Delete this work-orders file and the log file, and remove the spec's links to them.
- No code. Suspected bugs go in the report.

## 5. Log (newest last; 25 lines at most per entry)

Template:

    ### Phase <id> — <date>
    Built: ...
    Choices / deviations: ...
    The next phase must know: ...
    Left open: ...

### Phase C1b — 2026-09-26
Built: `main/dev/TakeObserver` (a sample every 20 ms: cursor frame, frames received just
before and after the output-level read, output levels while recording only, input levels
from `monitoringLevelsChanged`, status text, modal; each dot on first sight with the cursor
then; the raw recording's path; when it stopped; Play Singing Audio before and after).
`DevChecks` starts one on the runner's `Recording` progress, keeps it (`Watched`) when the
next punch-in records or the runner finishes; items 3 `live_dots`, 4
`nothing_of_the_take_in_the_speakers`, 5 `mic_on_input_2` from stage 1; report header
with drivers and reported latencies. `FakeAudioIO`: `echoDelay`/`echoGain`, opt-in
`reportLevels`. `TestDevChecks`: 5 checks; `dev_checks_echo_and_the_mic_on_input_2`.
Choices / deviations: output is placed from frames received and the measured start gap
(input, then output, per callback), not from time or the output latency, which the levels
precede. Margin one block either side; the block bounded by the most frames received
between two looks not held up (35 ms on the fake). Item 3: dots from a sound's start
−1 hop to its end + half the tracker window +1 hop (±1 hop failed the clean fake: dots run
440 frames past a tone); sweep ends make 2–3 dots at 860–980 Hz, counted apart, pitch not
judged. Cursor = the ViewManager's frame (S + recorded), read only while recording: dots
trail it by the round trip + about 40 ms (+322 ms at 281 ms on the fake). Item 5 "not
applicable" is Measured; on input 2 alone it passes on more than 10 dots per punch-in.
Echo and input 2 share one run.
The next phase must know: an observer runs for every punch-in of a runner run DevChecks
starts (`m_watched`, cleared per stage); stage 1's kept in `m_freshWatched`.
Left open: at −20 ms item 3 passes or fails with the hop phase (the test allows both).
The gap check was seen failing on a non-silent reference, not a take played back out:
stage 1 has no take audio where it plays (C1c's re-record has). Dot spread 40–230 ms.
