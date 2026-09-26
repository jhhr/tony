# Testing

QtTest suites in `main/test/`, in two executables that mirror the two libraries
(see [architecture.md](architecture.md)), plus one for the development checks. The
commands are in [AGENTS.md](../AGENTS.md).

| Executable | Links | Suites | Time |
| --- | --- | --- | --- |
| `test-tony-core` | `tony_core`, svcore, pyin's `YinUtil.cpp` as the YIN reference. `QCoreApplication`, no GUI. | `TestRealtimeYin`, `TestRealtimePitchTracker`, `TestLatencyShift`, `TestCoverage`, `TestTakeAudio`, `TestTakeEvents`, `TestSingingTakes`, `TestTakesFile`, `TestTakeTiming`, `TestLatencyCheck`, `TestLatencyCalibration`, `TestTakeDiff`, `TestModelChangeThrottle` | seconds |
| `test-tony-app` | `tony_app` + `tony_core`, a real `MainWindow` on the offscreen platform, the real pYIN plugin, `FakeAudioIO`. | `TestSingingDocument`, `TestViewCache`, `TestSingingAnalysis`, `TestRecordWorkflow`, `TestUiChecks`, `TestAudioCheck` | 8 to 9 minutes (measured 2026-09-26 on Linux), nearly all of it `TestRecordWorkflow`, `TestUiChecks` and `TestAudioCheck` (about 2 minutes): takes are recorded in real time |
| `test-tony-dev` | as `test-tony-app`; built only where the development checks are (any build type but `release`, `TONY_DEV_CHECKS`) | `TestDevChecks` | about 4 minutes (2026-09-26, Linux): each test records a dev run's takes, or part of them, in real time |

`meson test` / `build.bat test` runs these three (`test-tony-dev` where it is built) plus
four svcore suites. No suite uses the
real audio device: that is checked in the app itself, by Calibrate Audio with the
development checks (section 1 of the [manual checklist](manual-checklist.md)).
`test-tony-dev` is apart from `test-tony-app` so that the everyday runs stay shorter: run
it when a change touches what the development checks drive (see
[AGENTS.md](../AGENTS.md)).

- The `tony-app` and `tony-dev` meson tests have `timeout: 900`; the app suite took about
  277 s unloaded when that was set, and about 550 s on Linux on 2026-09-26. Every workflow
  test adds real time, so if a suite comes near it, raise it in `meson.build`: `meson test`
  reports a timeout even when every test passes. Running the executable by hand has no
  timeout.
- `main()` of the app and dev suites replaces `VAMP_PATH` with the executable's directory,
  so an installed pYIN is never the one tested; their meson tests `depends:` on
  `pyin_plugin` because nothing else builds `pyin.dll`. Build `pyin.dll` (`pyin.so` on
  Linux) too when running by hand after a clean: without it every test that waits for an
  analysis hangs until QtTest's five-minute watchdog aborts the run.
- The mains set the organisation/application names to `tony-tests` / `test-tony-*` and
  every suite works in a `QTemporaryDir`, so the user's QSettings and record directory
  are never touched.
- `Tony.exe` links both libraries with `link_whole:`. A new source file that is in neither
  `tony_core_files` nor `tony_app_files` is invisible to the tests.
- `build_mingw/meson-logs/testlog.txt` contains a dump of the whole inherited environment.
  Do not print it or grep it loosely.

Suites are header-only classes (`TestX.h`). A new suite needs: the header, an `#include`
and a `runSuite()` block in `tony-core-test.cpp` or `tony-app-test.cpp`, and the header in
the matching `*_test_moc_files` list in `meson.build`. `tony-dev-test.cpp` runs
`TestDevChecks` alone, and its moc list is inside meson's `if dev_checks` with the
`TONY_DEV_CHECKS` define for moc. A new test function in an existing
suite needs nothing but itself (a private slot). **Every private slot runs as a test**, so
helpers must not be slots; connect to lambdas instead. For access to private statics use
`friend class TestX;`, as `RealtimePitchTracker.h` does.

## Running

- `RunSuite.h` writes each suite's results to `$TONY_TEST_LOG_DIR/<SuiteClassName>.txt`.
  Read those with `grep -a` (the files can contain odd bytes); stdout is unreliable when
  redirected.
- Test function names on the command line are passed to **every** suite in the
  executable. The ones that do not have the function report it as unknown and fail, so
  the exit status of a run with names is always 1. Only a run with no names has a
  meaningful exit status; `test-tony-dev` has one suite, so there a run with names has one
  too.
- `QT_QPA_PLATFORM=offscreen` is set by `main()` when not given.

## Design principles

- **Split a number from what is done with it.** Latency is tested as arithmetic
  (`LatencyUtils.h`, `TakeTiming` — core suite) and, separately, as "the application applies
  the number it was given" (app suite, with `FakeAudioIO` reporting latencies chosen by the
  test and delaying its input by exactly that much). The real figure of a real device is
  measured in the app, by Calibrate Audio ([manual checklist](manual-checklist.md),
  section 1).
- **Pure logic goes in `tony_core`** so that it can have many cheap tests. The app suite is
  for order-of-events and ownership: what is in the document, the pane, the play source
  and the undo history after a workflow.
- **Ranged analysis is judged against a whole-file analysis of the same audio**, over the
  whole file, not just around the range (`ranged_leaves_the_rest_alone`): that is what
  caught the merge damaging unchanged audio half a second away.

## What is there to reuse (`TestRecordWorkflow.h`, `TestMainWindow.h`)

- `FakeAudioIO` (`FakeAudioIO.h`): a duplex device with a worker thread that runs the
  callback in real time, input first and then output, as PortAudio and JACK do. `Config`
  sets rate, block size, reported latencies, a programmed mono input, its delay, and
  whether the input clock starts at the first audible output sample ("a singer exactly on
  time"), `loopback` (the output, the mean of its channels, fed back into the input
  `inputDelay` frames late, as speakers into a microphone), `echoDelay` / `echoGain` (a
  second arrival of the loopback, as an input played back out and heard again),
  `inputChannel` (the input on one channel only, as a microphone on input 2),
  `reportLevels` (the peaks of each block, as `PortAudioIO` reports them for the meters)
  and `neverCallsBack` (a device that opens and then delivers nothing). It captures the
  output, so tests can assert what reached the speakers.
- `TestMainWindow` (`TestMainWindow.h`, shared by the four suites that drive a window:
  `TestRecordWorkflow`, `TestUiChecks` and `TestAudioCheck` in `test-tony-app`,
  `TestDevChecks` in `test-tony-dev`): subclass of `MainWindow` that exposes protected
  operations as `doRecord()`, `doSwitchToTake()`, `seekTo()`, `selectRange()` and so on,
  and the audio check's parts (`audioCheck()`, `devChecks()`, `takeLatency()`, the
  Playback menu's actions). It installs the fake device through `createAudioIO()`, or no
  device at all when made with `installDevice` false, and **answers dialogs
  through virtual seams**: `confirmRecordingOverTake()`, `confirmDeleteTake()`,
  `askForTakeName()`, each with a `set...Answer()` and a counter of questions asked.
  Anything new that asks the user needs such a virtual. `setRecordOverAskedInDialog()`
  lets the real dialog through instead, for a test that presses its buttons.
- Fixture helpers: `makeWindow(config)`, `writeWav()`, `openReference()`, `startTake()` /
  `stopTake()` / `take(ms)`, `verifyPlaySourceClean()`, `layersOnModel()`,
  `paneHasLayer()`, `documentHasLayer()`, `reopenAsSession()` / `reopenSession()`,
  `verifyEventsSurvived()`.
- A **dialog watchdog**: a 50 ms timer closes any modal dialog and records it, and
  `cleanup()` fails the test for one that was not expected. `dialogsMatching()` is for the
  dialogs a test does expect.
- `analysed()` waits for analysis completion, no running transformers **and** no ranged
  run. `snapshotTake()`, `verifyStripMatchesTake()`, `takeLayers()`.
- `TestSignals.h`: sine, sawtooth, seeded noise, comparison in cents.
  `TestSingingAnalysis.h` works the expected ranges out for itself (`widenRange()`,
  `mergeWindow()`, `comparePitchAcrossFile()`, `verifyNothingLeftOver()`).
- `PyinReference.*`: pYIN's YIN as the reference for the live tracker. A translation unit
  of its own because the Vamp *plugin* SDK headers must not meet the *host* SDK headers
  svcore uses.
- `testdata/happy_birthday_gp_masked.wav`: a real sung recording.

Prefer signals that describe themselves: `TestTakeAudio` uses constants and ramps so that
every sample says where it came from. Assert **identity** as well as equality where the
point is that something survived: the same layer and model objects before and after.

### Setup that fails confusingly when it is missing

- An `Analyser` without a `MainWindow` needs `qRegisterMetaType` for `"ModelId"`,
  `"sv_frame_t"` and `"sv_samplerate_t"` (else "No such slot" and completion never
  fires), the QSettings value `Transformer/use-flexi-note-model=true`, and the named
  colours in `ColourDatabase`. Delete the `Document` before the `PaneStack`.
- A `MainWindow` needs the network-permission setting (else a modal dialog),
  `setApplicationSessionExtension("ton")` and the record directory.
- `QSignalSpy` connects directly; for a signal from another thread use a receiver object
  on the test thread.
- In `TestMainWindow` override only `createAudioIO()`: `~MainWindowBase` calls
  `deleteAudioIO()` non-virtually.
- The app and dev mains draw text without sub-pixel anti-aliasing. Ubuntu's fontconfig asks
  for it and Qt 6.4 follows it: the scale's labels then have orange fringes, which
  `TestUiChecks` takes for live dots. A new main that shows a window needs the same.
- With `FakeAudioIO`'s `inputFollowsPlayback`, `inputDelay` must be at least one block.
  The device delivers about two blocks before the application sets its recording flag;
  `inputIsKept` counts only input that was kept, which exact start-gap tests need.

Copy the shape of a neighbouring test. Use `QVERIFY2` with a message that says what went
wrong, and `if (QTest::currentTestFailed()) return;` after a helper that asserts.

## How tests turned out to be worthless, and how to avoid it

A review found five tests that could not fail. So: **see each important test fail** — write
it first, or break the code for a moment (mark the line `MUTATION`, and check
`git diff | grep -c MUTATION` is 0 afterwards; rebuild after putting it back).

- An assertion made vacuous by an earlier guard or early return in the test.
- Setup that resets the state under test: `openReference()` goes through `ReplaceSession`
  → `closeSession()`, which clears the very flags a test may have just set.
- Passing by accident: the play source reads about **3 s ahead**, so a take that is
  wrongly audible is still not heard in a short test. Only a test that re-seeks
  (`take_silent_in_output_after_reseek`) proves a mute at the device output.
- A comparison that is true for the wrong reason: `QFileInfo` equality compares canonical
  paths, which are both empty when neither file exists.
- **Test tones need a whole number of samples per period** (220.5 Hz = 200 samples,
  294 Hz = 150, at 44.1 kHz). Otherwise pYIN reports a subharmonic: 220, 330 and 440 Hz all
  came out as 110 Hz.
- `MainWindowBase::m_timeRulerLayer` is set only by a `.ton` load. Bugs about the ruler or
  pane pruning show only in the `_after_session` variants; the plain-wav tests passed
  against the broken code.
- Re-analysis does not change an event count. Detect it by the layer or model object
  having been replaced, or with a spy on `layersChanged()`.
- Sparse models are inaudible by default (`getDefaultPlayAudible()`), so asserting that
  one is muted proves nothing.
- `RealtimePitchTracker`'s FFT difference shares pYIN's off-by-one in the power term on
  purpose. "Fixing" one side makes the live dots and the pYIN track disagree.

A bug that is known and not yet fixed is committed as a test with `QEXPECT_FAIL` naming
it; the marker goes in the commit that fixes it. There are none at present.

## Timing and races

- Wait for conditions (`QTRY_VERIFY`, the helpers' waits), never for fixed times. Wait for
  `ModelTransformerFactory::haveRunningTransformers()` to go false before closing anything
  — but not after making a selection: a selection starts `reAnalyseSelection()` on the
  reference, so a transformer is then usually running and nothing of the take depends on it.
- A take's analysis lands in two steps, `rangedAnalysisMerged()` then
  `initialAnalysisCompleted()`. Read results after the merge (`analysingRange()` false),
  not after some other signal that happens to come at about the same time.
- The status bar is written by three base-class timers; a test that reads it must go
  through what `showTakeCountdown()` controls.
- Deleting a derived layer does not stop its transform; only
  `Analyser::cancelAnalyses()` does, and the transformer still counts as running for one
  more turn of the event loop. The live dots likewise go one turn after the models report
  completion.
- The evidence for a fixed race is N clean runs **under CPU load** (run `test-tony-core`
  alongside, say): a regression shows as an intermittent crash of the executable, often
  several tests later, not as a failed assertion. The tests this matters for:
  `swap_during_analysis`, `rerecord_during_analysis`,
  `range_analysis_torn_down_while_running`, `close_session_during_analysis`.
- If a worker thread drops the last reference to a `WritableWaveFileModel`, it dies off the
  GUI thread with its `QTimer` ("Timers cannot be stopped from another thread", then an
  access violation). That message in a test log means an analysis was not cancelled before
  its model was released.
- After a `.ton` round trip compare frames exactly and values with a tolerance (six
  significant figures in the file).

## The window as it is seen (`TestUiChecks`)

The window is shown (still offscreen), made active so that its shortcuts work, and driven
with `QTest` key presses, mouse gestures on pane 0 and the dialogs MainWindow shows. What
it draws is judged by pixels:

- **Read the screen**: `grabPane()` has pane 0 paint itself, as its next update would, and
  copies it out of the window's backing store. Without the paint, a pane that has just
  turned a page is still the old page in the backing store while every position asked of
  it is on the new one: a play pointer 500 px from where it was looked for.
- After any playback the pane's cache holds the translucent note boxes painted twice.
  Compare images only after `grabPaneRedrawn()`, which forces a full redraw (a zoom one
  step away snaps back to the same level and redraws nothing; it doubles the level).
- The pane has its vertical scale at the left, about 30 px, over everything; the play
  pointer is two dark lines around a light one (`pointerX()`), drawn over the band.
- The take's pitch track is under its notes' translucent purple, so its orange reads as
  about (246, 126, 114) there: `isSinging()` takes both. Bright orange is the reference's
  pitch candidates, which a selection makes.
- Record puts the view back on the take's position: work out x positions again after it.
- What the Edit tool does is decided by where the pointer last hovered over a note
  (`FlexiNoteLayer::mouseMoveEvent()`): near its top a drag moves the note, near its bottom
  a click splits it, and before any hover a drag moves and a click does nothing. `hover()`
  sends that move to the pane; `QTest::mouseMove()` with no button held moves the
  platform's cursor instead.
- A drag of a note re-analyses the pitch under it. The candidates arrive when that
  transform finishes, and one of them may go into the pitch track, so judge only once
  `haveRunningTransformers()` is false. The drag may leave more than one entry in the undo
  history, and undoing them leaves the candidates in the pane: they are the analyser's own
  layers, gone at the next re-analysis.
- Timing checks in real time go through the pane's own timers (the pointer moves every
  20 ms): allow a tick.

With `TONY_TEST_SHOT_DIR` set, the suite saves the images it judged, and some of the whole
window, as `<test>-<what>.png`, for the [manual checklist](manual-checklist.md)'s look.

## The audio check and the dev checks (`TestAudioCheck`, `TestDevChecks`)

What they cover is in [calibrate-audio.md](calibrate-audio.md), section 11. Both fixtures
follow `TestRecordWorkflow`'s (a `TestMainWindow`, the dialog watchdog, the user's toggles
reset in `init()`), and `TestDevChecks`' is a copy of `TestAudioCheck`'s, not shared: each
class keeps its own. `cleanup()` also removes any round trip a test stored, which would
place the next test's takes. How they are built, and what to keep in mind when adding to
them:

- **The loopback fake.** Both record through `FakeAudioIO` with `loopback` on (their
  `loopback()`). The device reports 2 × 4096 frames out and 4096 in, and the true round
  trip (`inputDelay`) is 123 frames longer: a check that works measures the true one, and
  with it every sweep lands at 0 frames, while a run placed with the reported pair lands
  2.8 ms off. `TestDevChecks`' loopback also has `reportLevels` on, for the observer's
  output levels.
- **Keep them short.** Every run records in real time.
  - `TestAudioCheck`'s `shortPlan()` is two punch-ins of two sweeps on the calibration
    reference cut short after its fifth event (10.8 s), about 13 s a run.
  - A dev run takes `DevChecks::Options`, which `options()` fills in: the round trip for
    the run, the report and scratch directories of the test's own, and the long song's
    length (`longSeconds`). The passing run's long song is 60 s, not 240: long enough that
    its whole analysis takes well over twice a punch-in's (item 9), and about 50 s for the
    whole run. Runs that look at other things leave the long song out (0), and a fault run
    may cancel the run once the stage it needs is done (at "Pre-roll near the start", for
    the re-record's faults): the checks of the stages it got through are worked out all the
    same. `runDevChecks()` waits up to 120 s.
- **A dev check is not a QtTest function.** Each returns a `CheckResult`; a test runs a
  dev run, or part of one, and asserts on its report: `check(item)` gives a check,
  `number(check, label)` one of its numbers by its label, `describe()` every check's
  verdict, message and numbers for a failure message (QtTest cuts a long one: pass the
  item). Each check has a run where it passes and one where it fails, from a fault given to
  the fake or the window, or was seen failing with the code broken for a moment. The
  faults: the round trip given 20 ms off; `echoDelay` / `echoGain` with the input on
  channel 2 (`inputChannel`); the take made audible as the re-record's punch-in starts
  recording (a lambda on the runner's `progress()` that sets its play parameters); a stall.
- **A noise floor for the output checks.** On a noiseless loopback the take holds the
  reference and nothing else, silent wherever the reference is, so a take played back out
  shows in no silent gap, and items 4 and 12 cannot fail. `loopbackInARoom()` adds a
  programmed input of white noise at −60 dBFS, as a room gives a microphone: too quiet for
  the sweep finder, and no pitch for the live tracker or pYIN. The passing run and the
  lead-in's fault and stall runs use it.
- **Holding the GUI thread up on purpose** (`stallTheReRecording()`,
  `dev_checks_lead_in_through_a_stall`): a busy-wait in the slot of a 5 ms timer, due by
  where the reference is being handed out (playback start plus the frames received), not by
  the take's recorded duration, which is counted on the GUI thread and stands still while
  it is held up.
- **Where files go.** `TestAudioCheck`'s plans name a reference file in the test's own
  directory; its tests of the check's own directory, and all of `TestDevChecks`, write
  references where the check writes them, the application data directory, with
  `QStandardPaths::setTestModeEnabled()` on so that it is Qt's test location. Reports and
  scratch folders go to the test's own directory, never `TONY_TEST_LOG_DIR`: a failing
  run's `DevChecks.txt` would land among the suites' result files, which are grepped for
  `^FAIL` and `Totals`. The passing test prints the report line by line after `report:`, so
  that its Totals never begins a line of the suite's log.
- **`TestAudioCheck`'s windows have no dev checks.** Its fixture deletes each window's
  `DevChecks` (`doDeleteDevChecks()`): in a development build its dialog tests would
  otherwise carry on into them, the checkbox being on. So it runs the same in a `release`
  build, where it is the only test of the button.
- **No device, and a dead one.** `makeWindow(config, false)` makes a window with no audio
  device at all; `neverCallsBack` a device that opens and delivers nothing.

## What stays manual

Anything about a real device, real timing by ear, or how something looks:
[manual-checklist.md](manual-checklist.md).
