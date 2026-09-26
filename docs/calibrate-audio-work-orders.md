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

Done: A1 (`944df7c`), A2 (`a03b7ec`), B1 (`58de074`), B2 (`47944f2`), B3 (`8524d5f`), B4 (`9b1fb6c`), C0 (`1ef2494`), C1b (`276036e`), C1c (`b1b8f08`), C2 (`fbdce6c`), C2b (`4fb73ac`), C2c (`5f7b2b8`).

Also done: C1a (`4370131`), the merge of `default` (`c8b9585`), `test-tony-dev` (lead).

**Order from here:** C1b, C1c, C2, C2b, C2c, C3, then the lead's release build, then D. The spec's
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

Read also: `main/dev/DevChecks.{h,cpp}` and `main/dev/TakeObserver.h`; `main/TakeDiff.h`;
`main/TakeTiming.h` (`shouldStopAt()`, `countdownText`); in `main/MainWindow.cpp`
`wantedPreRollFrames()`, `pollTakeProgress()` and the start of `record()`;
`docs/recording.md` on pre-roll and Record into Selection; spec §4 rows 7, 12, 13, 14.

- **Snapshots.** Before and after each punch-in of the new stages: the take's samples from
  its file (`AudioCheckRunner::readTakeFile()`), its pitch and notes (by value, looked up
  afresh), and its coverage. Compared with `TakeDiff`.
- **Stage "Re-record"**, after "Fresh punch-ins": one runner run keeping the session, one
  range starting inside stage 1's second punch-in ([16.8, 21.2] s) between 17.9 and 19.2 s
  and ending by 21.2 s. Its 1 s lead-in then plays over the earlier punch-in's recorded
  sweep at 17.7 s, and it still judges the sweep at 20.1 s (C1a's note). Items:
  - **7** Placement as in items 1 and 2; outside the placed range the take's audio is
    bit-identical (`audioOutside()`), and its pitch and notes are unchanged beyond
    ± 0.25 s (`eventsOutside()`).
  - **12** The lead-in: nothing before P changed (the same comparisons, their part before
    P, reported apart), and C1b's output-in-the-gaps check over the lead-in's looks, which
    now lie over take audio. **This is the case C1b could not show failing**: show it here.
  - **14** The take stopped by itself: the frames recorded past what `shouldStopAt()`
    needed, in seconds, within 0.25 s plus one poll of the take timer (100 ms); the
    coverage added is exactly the selection; no modal widget seen during the take. The
    check's takes always record into a selection, and `record()` asks the overwrite
    question only when not (`end < 0`), so "no question" is watched, not arranged.
- **Stage "Pre-roll near the start"**: one range from P = 1 s judging the sweep at 3.1 s,
  with a pre-roll of 3 s for this plan only: a new `Plan` field, read where
  `wantedPreRollFrames()` reads `kPreRollSeconds` today. Item **13**: playback ran from
  frame 0 and never before it (the observer's cursor), the countdown began at 1 and not
  3 (the observer's status text), placement right.
- Items 1 and 2 then cover every punch-in of the run; say how their numbers read now.
- **Tests** (`TestDevChecks`): the passing run gains the two stages (keep the whole run
  under about 25 s on the fake). Failing cases, sharing runs where they can:
  - the take audible during the re-record's lead-in (set its play parameters audible when
    the runner reports `Recording` for that punch-in, from the test): items 4 and 12 fail;
  - show failure by breaking the code for item 7 (e.g. the splice's fade beyond its range)
    and item 13 (e.g. the pre-roll not clamped at 0), and undo by hand.

### C2 — Long song and joins; items 9, 10 (spec §4 rows 9, 10)

Read also: `main/dev/DevChecks.{h,cpp}` (its stages, `runs()`, `Snapshot`); `main/TakeDiff.h`
(`stepAt()`, `pitchAcross()`, `notesAcross()`, `eventsOutside()`); `main/LatencyCheck.{h,cpp}`
for `longLayout()` and the dev layout's held tones; `docs/takes.md` on ranged analysis and
the merge window W; in `main/test/TestRealDevice.h`, `stop_is_quicker_than_a_whole_song`
and how it times the whole-song analysis; spec §4 rows 9 and 10.

- **Stage "Long song", the first of the run**, before "Fresh punch-ins": the long
  reference as a session of its own (a new reference, not `keepSession`), timed from the
  session opening to the reference analysed; then two punch-ins far apart in one runner
  run (as `test-tony-device`, near 60 s and 150 s), each timed from its Stop to its pitch
  merged (the runner's `AnalysingTake` step). First, so that the dev reference then
  replaces it as a check's own unsaved session, and the run still ends on the saved dev
  session; no saved session is ever replaced.
  - **9** Pass when each punch-in's Stop-to-merged time is under half the whole-song
    analysis time, and the take's pitch outside the range ± 0.25 s is unchanged
    (`eventsOutside()`), which shows the ranged path ran. Numbers: both times.
  - These punch-ins join `runs()` for items 1, 2 and 4: placement far into a song is where
    a rate mismatch shows.
  - **Test time**: a 4-minute reference in every passing test is too slow. Give
    `longLayout()` a length parameter (default 240 s, core test for it) and `Options` the
    long reference's length; tests use about 60 s with punch-ins to match. Report what
    that makes the whole-song and ranged times on the fake.
- **Stage "Joins"**, after "Pre-roll near the start", keeping the session: two punch-ins
  in one runner run that meet at J in the middle of one of the dev layout's held tones
  (3 s tones after the sweeps at 26.9, 30.9 and 35.2 s). The first holds that tone's
  sweep; the second runs on past the next event's sweep so that both are judged, and its
  lead-in plays over the first's recording. Snapshots before and after.
  - **10** At J: `stepAt()` on the take's file, `pitchAcross()` and `notesAcross()` on the
    take's pitch and notes; and `eventsOutside()` over the two ranges together. Numbers:
    the step in dB, the largest pitch gap, the notes at J and the nearest note edge.
  - C0 found (from the code) that the join is a 10 ms dip, and that the notes merge by
    onset may drop or split the note. **Measure and report the result as it is.** If it
    fails on today's code, do not fix `Analyser` or the splice: the app test expects that
    failure with `QEXPECT_FAIL` and the reason, as `default` does for its defects, and the
    report says so.
  - On the user's MME, two punch-ins carry different restart offsets (spec §8), so the
    join may fail there for that reason too: the message should say which part failed.
- **Tests**: the passing run gains both stages; keep `test-tony-dev` under about 3 minutes
  in all (135 s now). Show failure for item 9 (e.g. Stop analysing the whole song) and one
  part of item 10 by breaking the code, and undo by hand.

### C2b — Item 12 robust against a stalled event loop (a de-race, its own commit)

Read also: `main/dev/DevChecks.cpp` (`gapLooks()`, the "Re-record" stage, item 12) and
`main/dev/TakeObserver.{h,cpp}`; C1b's and C1c's log entries.

- **The flake (found in C2):** item 12 judges the output levels in the silent gaps of the
  re-record's lead-in. Its only gap there, 18.8–19.2 s, holds 15–16 looks; a stall of the
  GUI thread of about 340 ms (seen on this VM, nothing in the log) left 9, once 0, and
  item 12 failed in a clean run. A real device can stall too, and MME's larger blocks
  widen the margin and shrink the gap further.
- **The fix, in the design, not the tolerance:**
  - Give the re-record stage's plan a longer pre-roll (`Plan::preRoll`), so that its
    lead-in also spans the 0.9 s gap at 16.8–17.7 s inside the earlier punch-in
    [16.8, 21.2]: a pre-roll of 2.4 s starts it at 16.8 s. Check that items 7, 12 and 14
    still read as before, and say what the lead-in now covers.
  - Look at what a stall does to a look (the frames received jump; the look's window then
    spans a sweep and is left out). Where a gap check ends with no look in any gap, the
    part is "not judged" with the reason, not a Fail: it has not seen the take played
    out. Item 4 the same.
- **Tests:** a test that stalls the GUI thread for about 0.4 s during the re-record's
  lead-in (a single-shot timer that busy-waits, started when the runner reports that
  punch-in recording): item 12 still judges and passes; and the take-heard fault still
  fails with such a stall. Show the stall test failing on the code before the fix.

### C2c — The notes merge keeps one note across a join (`Analyser`, its own commit)

Read also: `docs/takes.md` "Ranged analysis and merge" and its known limits (search "by
onset", "deliberate trade"); `Analyser::analyseRange()`'s merge in `main/Analyser.cpp`
(search "Notes go by their onset"); the existing merge tests (search `TestRecordWorkflow.h`
and `TestSingingAnalysis.h` for "note"); C2's log entry.

- **The defect (found by item 10):** two punch-ins meeting at J inside a held tone. The
  first's note ends at J (the audio after J was silence when it was analysed). The second
  punch-in's run starts 0.5 s before J, inside the tone, so its note begins before W (the
  range ± 0.25 s): the merge adds only notes with their onset in W, and the tone after J
  has no note. Analysing the whole take gives one note, 27.21–30.20 s.
- **Wanted:** one note across the join. When an old note runs into W from before it and
  a new note that begins before W overlaps it there, the old note keeps its onset (the
  audio before W has not changed) and takes the new note's end, cut back as today at an
  old onset after W. Decide, and justify, what happens when a new note begins before W
  with no old note there. Keep everything else the merge promises: notes in unchanged
  audio not split, the change record (`m_rangedNotesChange`) that undo reverses, and the
  "deliberate trade" of `docs/takes.md` unless the fix removes it (then say so).
- **Tests:**
  - an app test on the fake: two punch-ins meeting inside a held tone leave one note
    across J; seen failing before the fix;
  - undo of the second punch-in gives back the first's note exactly;
  - every existing merge test green and unchanged;
  - `TestDevChecks`: item 10's `QEXPECT_FAIL` removed; the check passes.
- **Docs:** `docs/takes.md`'s "Notes, by onset" and its known limit, in the same commit.

### C3 — Retire `test-tony-device`

Read also: `main/test/TestRealDevice.h` whole (607 lines: what is being retired);
`docs/manual-checklist.md` whole; `docs/testing.md`'s table and the passages naming
`test-tony-device`; `main/dev/DevChecks.h` (the items and the report); the runner's
`Recording` step in `AudioCheckRunner.cpp`.

- **Check coverage first**, test by test of `TestRealDevice.h`, and write the mapping into
  your log entry: `device()` → the report header; `takes_line_up_with_the_reference` →
  items 1 and 2; `nothing_of_the_take_comes_back_out` → item 4;
  `stop_is_quicker_than_a_whole_song` → item 9; `live_dots_were_drawn` → items 3 and 5;
  `no_input_does_no_harm` → see below. Anything it checks that the dev run does not:
  report it, and port it if small.
- **A device that opens but delivers nothing** (its `no_input_does_no_harm` and the
  QFAIL "the device opened, but … delivered no input at all"): the runner today waits
  out its `Recording` step and ends with "A take did not stop at the end of its range",
  which misleads. Make the runner end a take that has received no frames at all by the
  time the take should be over, with a message that says the device delivered no input,
  through the Stop path, leaving no take and no harm; and an app test in
  `TestAudioCheck` with a fake device that opens and never calls back (see how
  `FakeAudioIO` and `TestMainWindow` make one; add a field if needed). A device that
  cannot be opened at all is covered already (`check_fails_without_a_device`).
- **Remove** `main/test/TestRealDevice.h`, `main/test/tony-device-check.cpp` and the
  `test-tony-device` target in `meson.build`, and `TestMainWindow`'s
  `setUseRealDevice()` and what serves only it, if nothing else uses them.
- **Docs, in the same change** (AGENTS.md's "Keeping the docs true"):
  - `AGENTS.md` and `docs/building.md`: the build command without `test-tony-device.exe`;
  - `docs/testing.md`: its table row and paragraph;
  - `docs/manual-checklist.md` section 1 becomes "The device check: Calibrate Audio with
    the dev checks": in a development build, Playback ▸ Calibrate Audio… with the
    checkbox on; the earcup against the microphone; the report file `DevChecks.txt` in
    Tony's application data folder; what each item's numbers mean in a line each; what
    fails on MME today (items 1, 2 and 10's offsets, spec §8) and why. Keep the list of
    what it covers and the dated notes that still hold; drop what was only about the
    executable (`TONY_DEVICE_CHECK_FAKE`: `TestDevChecks` checks the check now).
  - `main/dev/DevChecks.h`'s comments that name `test-tony-device`.
  - Leave `docs/calibrate-audio*.md` beyond your log entry and spec §7's "Done": phase D
    brings them up to date.
- **Tests:** the new `TestAudioCheck` test, seen failing before the runner change. The
  three suites green; `test-tony-device` no longer builds or exists.

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

### Phase C1c — 2026-09-26
Built: `AudioCheckRunner::Plan::preRoll` (default `kPreRollSeconds`, negative refused),
handed to `MainWindow::m_audioCheckPreRoll`, which `wantedPreRollFrames()` reads.
`DevChecks` stages 2 "Re-record" [19.2, 21.2] and 3 "Pre-roll near the start" [1.0, 4.2]
with 3 s (both `keepSession`), a `Snapshot` (take file, pitch, notes, coverage) before and
after each; items 7 `record_from_a_position`, 12 `nothing_heard_or_changed_in_the_lead_in`,
13 `pre_roll_near_the_start`, 14 `record_into_selection_stops_by_itself`. Items 1, 2, 4 now
cover every punch-in (`runs()`, numbered 1–4 along the run); item 1's reopen compares with
the file judged just before the save over all ranges. C1b's gap logic is `gapLooks(until)`.
Choices: P = 19.2 s: shortest range judging 20.1 s, a gap (18.8–19.2) in the lead-in
(16 looks), a whole note before P − 0.25. Item 7 excuses only the selection; item 12 is the
same before P, plus the looks that end by P. Item 14: raw recording frames minus (round
trip + start gap + R + E − P), within [0, 0.25 s + take timer interval + one block], not via
`TakeTiming`, whose margin it checks. Item 13: highest countdown shown ≤ ceil(min(3 s, P)
+ round trip + start gap + 50 ms), ending at 1. The fault test cancels at stage 3.
Found: on a noiseless loopback the take equals the reference, so one played out shows in
no gap: `loopbackInARoom()` adds −60 dBFS noise (pass and fault runs). The countdown reads
1, 2, 1: `record()` shows it before the round trip is known (harmless, reported).
The next phase must know: a new recording stage joins `runs()` for items 1, 2 and 4; a
cancelled run still works out the checks of the stages it finished; the runner repeats
`progress(Recording)` as the seconds left tick down.
Left open: an overwrite question would come inside `record()`, before the observer starts,
so item 14 cannot see it. Items 3 and 5 judge stage 1 only. `test-tony-dev` about 135 s.

### Phase C2 — 2026-09-26
Built: `LatencyCheck::longLayout(rate, seconds)`, default `kLongSeconds` (240), core test
`long_layout_of_another_length`. `DevChecks` stage 1 "Long song" (`Options::longSeconds`,
0 leaves it out and item 9 Skipped; `longPunchIns()`: the shortest range judging the first
sweep from 1/4 and from 5/8 of the song, 61.04–62.96 and 150.84–152.76 s at 240 s) and
stage 5 "Joins" (`joinPunchIns()`: [26.0, 28.7] and [28.7, 32.0], J = 28.7 s, the middle of
the tone 27.2–30.2 s; they judge 26.9 and 30.9 s). Items 9 `stop_on_a_long_song`, 10
`the_joins`. `Run` carries its layout (`gapLooks()` takes it); items 1, 2, 4 count the long
song's punch-ins first (1–8 now); the reopen's `punchInsSoFar()` keeps to the dev take.
Choices: timed by `longSongStep()` from the runner's reports: whole song = its
AnalysingReference step (writing and opening before it, 0.14 s at 60 s, not counted); a
punch-in = its AnalysingTake step, ending at the next Recording report (after `record()`)
or at `finished()` (after the judging). Item 9's pitch before a punch-in is read at its first
Recording report. Item 10's messages name the part: "step:", "pitch:", "note:", "outside:".
On the fake: 60 s song 2.9–3.0 s, punch-ins 0.59–0.70 s (20–24 %); 240 s: 10.4 s, 0.63 and
0.75 s (6–7 %). At J the step reads −8.3 dB (the two 5 ms fades, a dip), pitch gap 1 hop.
Found: item 10 fails, the note only: the first punch-in's note ends 5.7 ms past J, the tone
after J has none (the ranged run starts 0.5 s before J, so its note begins before W and is
dropped). Analysing the whole take gives one note, 27.21–30.20 s. `QEXPECT_FAIL` in the test.
Seen failing: item 9 (Stop analysing the whole take: 40 % and 72 %, pitch changed), item
10's step (no fade-in: 29.4 dB). Tests: the passing run and the cancel, close and dialog tests
have a 60 s long song; the fault runs and the deletion test leave it out.
Left open: item 12 (C1c) once found 0 looks in its 0.4 s lead-in gap (15–16 usual, 9 once):
a silent GUI stall of 340 ms or more there, likely this VM's disk; a real run could show it.
The runner's 60 s limit on the reference's analysis is 6× 240 s's 10 s. `test-tony-dev` 180 s.

### Phase C2b — 2026-09-26
Built: `DevChecks::kReRecordPreRollSeconds` (2.4 s) for stage 3: its lead-in runs from 16.8 s,
where stage 2's second punch-in begins, over two silent gaps (16.8–17.7 and 18.8–19.2 s), the
sweep at 17.7 s and the tone from 18 s; item 12's looks there went from 15–16 to 57.
`GapLooks::longestWait` (the longest wait between two looks begun before `until`), a number
of item 12. Items 4 and 12: no look in any gap makes that part "not judged", with the reason
(longest wait, margin) in the message, not a Fail. `TestDevChecks`:
`dev_checks_lead_in_through_a_stall`, two rows: 0.45 s over the gap before P (judged, Pass)
and over the whole lead-in (not judged, Pass); `dev_checks_take_heard_during_the_lead_in`
gains the 0.45 s stall and still fails items 4 and 12 on the take heard; `describe(item)`.
Choices: the stall is a busy-wait in a 5 ms timer's slot, due by frames received since the
record start (the record duration is counted on the GUI thread and stands still in a stall).
A part not judged leaves the verdict to the other parts: Pass if they pass (Measured and
Skipped mean other things), the message saying what was not judged.
Items 7 and 14 read as before (14: 0.299 s past, earlier runs 0.31–0.35 s); item 13 unchanged.
Seen failing: before the fix, both tests (0 looks, item 12 Fail: the flake); the whole-lead-in
row with "not judged" put back among the problems.
The next phase must know: the margin is the most frames received across one look's two reads;
an OS stall between those reads (not the event loop) widens it for the whole take and can
leave no look in any gap: now "not judged", not a Fail.
Left open: `test-tony-dev` 13 tests, about 228 s (two runs of about 22 s added).

### Phase C2c — 2026-09-26
Built: `Analyser::mergeRangedAnalysis()`: an old note sounding at W's start and ending inside
W takes the end of the run's note sounding there (onset before W), that end found as for an
added note (`newNoteEnd()`: `endBeyondRun` if the run's end cut it off, cut back at the next
old onset after W), then cut back to the first added onset as before. Recorded in
`m_rangedNotesChange` like the old cut, so undo restores the old note. Tests:
`TestRecordWorkflow::join_inside_a_held_note_keeps_one_note` (Record into Selection
[0.5, 1.5] then [1.5, 2.5] s on one held tone: one note; undo gives the first's note back
exactly, redo the one note); `TestSingingAnalysis::ranged_join_inside_a_note`, rows "the same
note going on" (one note) and "a new note from the join" (two, the first ending at J).
`TestDevChecks`: item 10's `QEXPECT_FAIL` gone; `dev_checks_fail_with_the_round_trip_off`
now requires item 10 to pass (it allowed either).
Choices: "the same note" = both sounding at W's start, where the audio has not changed. No
pitch tolerance (the values are medians over different stretches; a pitch the user corrected
must not stop the note). Only an old note ending inside W: one running past W keeps its end
(`ranged_keeps_a_note_across_the_window_edge` compares it exactly). A run's note beginning
before W with no old note sounding there is still not added: before W the models' notes stand.
Seen failing: the same-note cases of both new tests before the fix (the first's note alone,
0.517-1.509 s); the "new note" row with a naive join (an old note carried over a new note
beginning within 4 hops of its end, and the cut at the first added onset off).
Left open (`docs/takes.md`, known limits): a note running on past W keeps its old end where
the new audio stopped it inside W; a note whose onset in the run and in the models lie a hop
or two either side of W's start is lost (pre-existing, found by reading).
