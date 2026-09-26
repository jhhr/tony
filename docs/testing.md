# Testing

QtTest suites in `main/test/`, in two executables that mirror the two libraries
(see [architecture.md](architecture.md)), plus one for the development checks. The
commands are in [AGENTS.md](../AGENTS.md).

| Executable | Links | Suites | Time |
| --- | --- | --- | --- |
| `test-tony-core` | `tony_core`, svcore, pyin's `YinUtil.cpp` as the YIN reference. `QCoreApplication`, no GUI. | `TestRealtimeYin`, `TestRealtimePitchTracker`, `TestLatencyShift`, `TestCoverage`, `TestTakeAudio`, `TestTakeEvents`, `TestSingingTakes`, `TestTakesFile`, `TestTakeTiming`, `TestLyrics`, `TestLyricsTtml`, `TestLyricsEdit`, `TestLatencyCheck`, `TestLatencyCalibration`, `TestTakeDiff`, `TestModelChangeThrottle`, `TestRunSuite` | seconds |
| `test-tony-app` | `tony_app` + `tony_core`, a real `MainWindow` on the offscreen platform, the real pYIN plugin, `FakeAudioIO`. | `TestSingingDocument`, `TestViewCache`, `TestSingingAnalysis`, `TestLyricsLayer`, `TestRecordWorkflow`, `TestUiChecks`, `TestAudioCheck` | about 9 minutes in one process, a minute and a half in eight (measured 2026-09-26 on Linux), nearly all of it `TestRecordWorkflow`, `TestAudioCheck` and `TestUiChecks`: takes are recorded in real time |
| `test-tony-dev` | as `test-tony-app`; built only where the development checks are (any build type but `release`, `TONY_DEV_CHECKS`) | `TestDevChecks` | about 4 minutes in one process, a little over one in eight (2026-09-26, Linux): each test records a dev run's takes, or part of them, in real time |

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
- The mains set the organisation/application names to `tony-tests` / `test-tony-*` (a
  shard's name with a suffix of its own, see "Running") and every suite works in a
  `QTemporaryDir`, so the user's QSettings and record directory are never touched.
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

Suites find the files in `testdata/` through `TONY_TEST_DATA_DIR`, which `meson.build`
defines for both test executables as a path with forward slashes: the backslash of a
Windows path would start an escape in the C string.

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
- **Shards.** With `TONY_TEST_SHARD=i/n` each suite runs only every n-th of its test
  functions, from the i-th, in declaration order, and a suite with none in the shard does
  not run. The app suite nearly only waits on `FakeAudioIO`'s real-time clock, so n
  processes at once take about 1/n of the time: on four cores the load stayed under 2 with
  eight, and reached 3.5 with twelve. `deploy/linux/run-tests.sh` starts them and adds up
  their results. Processes running at once must not share settings: the suites clear and
  rewrite them, and would do it under each other. So each shard runs under an application
  name of its own, `<base>-shard<i>of<n>` (`RunSuite.h`), and its settings, data location,
  svcore temp directory and log are all keyed by that name, in `QStandardPaths`' test mode
  too. That holds on Linux and Windows alike (on Windows they are the registry and known
  folders, which no environment variable moves), so every process shares the user's
  `HOME`; each shard leaves a settings file and a data folder of its own, one per `i` and
  `n`. The script is written to run from Git Bash on Windows too, with the environment
  AGENTS.md gives and executable names with `.exe`. It has not run there yet, and how many
  processes suit that machine is not measured
  ([windows-shards.md](windows-shards.md#on-the-windows-machine-after-the-merge)). Do not
  combine shards with test names on the command line.
- A sharded run is a whole run of the suites, but the tests that share a process are other
  ones. After a change to object lifetimes, threads or teardown (see "Timing and races"),
  run the one-process run as well. It also loads the machine more: built against Ubuntu's
  Qt 6.4, `TestUiChecks`' `live_dots_under_the_cursor` failed in both of two runs in eight
  processes (the tracker itself 313 and 325 ms behind the cursor, over the test's 300 ms)
  and passed with `-j 4`. Judge a failure of it there by running it alone.
- **On Linux some tests fail whatever the change.** With the Qt of the cloud setup,
  conda-forge's 6.11 ([building.md](building.md#building-on-linux)), only
  `TestTakesFile`'s `takes_folder`, `relative_audio_path`, `resolve_audio_path` and
  `in_folder`, which test Windows paths (`C:\...`, case-insensitive). Built against
  Ubuntu's Qt 6.4 instead, `TestRecordWorkflow`'s `undo_during_analysis_then_redo` and
  `analyse_now_reanalyses_the_take` can fail too, where the analysis finishes before the
  race they need can be set up; which, and where, changes from run to run:
  `undo_during_analysis_then_redo` fails either before the undo, with no ranged analysis
  left running, or after the redo, with `analysedRangeStart()` already 0 — the same race.
  Five other tests failed so as well until they held the take's merge ("Timing and
  races"), which these two do not yet.
- **Qt 6.4's watchdog times the whole suite**, not one test function: with
  `QTEST_FUNCTION_TIMEOUT=20000` it ended `TestRecordWorkflow` 20 s after the suite began,
  2.5 s into a test. That suite runs for longer than the five-minute default, so
  `runSuite()` raises the limit to 30 minutes when built against a Qt older than 6.5.
  After such a fatal error the executable does not exit: it spins, or waits for the gdb
  that Qt starts for a backtrace. A run that has written nothing for minutes has
  stopped; kill it.

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
- **A layer painted in strips must equal the layer painted whole**
  (`painting_in_strips_matches_painting_whole`, `TestLyricsLayer`). A view that scrolls
  repaints only the strip that comes into sight, so anything a layer lays out from its
  neighbours must not depend on the rect being painted. The test paints into an image once
  whole and once strip by strip, and compares the pixels; a layout worked out from the
  painted rect fails it.

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
  `askForTakeName()`, `askForLyricsFile()`, `askForLyricsExportFile()` (which also keeps
  the path it was offered), each with a `set...Answer()` and a counter of questions asked.
  `askForLyricsWordText()` takes a queue of answers (`answerWordText()`,
  `cancelWordText()`; none left is Cancel) and can run something while the question is
  open (`whileAskingWordText()`), as a real dialog's event loop lets anything happen.
  `askForLyricsShift()` is answered the same way (`answerLyricsShift()`,
  `cancelLyricsShift()`, `whileAskingLyricsShift()`).
  Anything new that asks the user needs such a virtual. `setRecordOverAskedInDialog()`
  lets the real dialog through instead, for a test that presses its buttons.
- Fixture helpers: `makeWindow(config)`, `writeWav()`, `openReference()`, `startTake()` /
  `stopTake()` / `take(ms)`, `verifyPlaySourceClean()`, `layersOnModel()`,
  `paneHasLayer()`, `documentHasLayer()`, `reopenAsSession()` / `reopenSession()`,
  `verifyEventsSurvived()`; for the lyrics `lyricsFixture()`, `writeLrc()`,
  `verifyLyricsUntouched()`; for editing them `lyricsEditFixture()` and the mouse helpers
  below.
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
- `testdata/lyrics/`: LRC and TTML files with invented text. Two LRC files are in the
  exact format of the Moises lyrics exporter (word timing and line timing: no end times, a
  `♪` gap line, a word with punctuation glued to the one before, a line its clamp stamped
  0); the third is a generic LRC that does give ends. `moises-exporter-words.ttml` and
  `moises-exporter-lines.ttml` are the exporter's TTML, word by word and line by line,
  offset 0, **made by the exporter's own code**: a small node script copied its input
  handling and TTML branch verbatim and ran them on an invented Moises-style JSON (segment
  format, one word in syllables, punctuation as a word of its own). The script is not in
  the repository, because it is the exporter's code; to make the files again, do the same
  from the exporter's reviewed commit. `amll-style.ttml` is written by hand in the style
  of AMLL TTML Tool: times `mm:ss.mmm`, two agents, a background-vocal span and
  translation spans.

Prefer signals that describe themselves: `TestTakeAudio` uses constants and ramps so that
every sample says where it came from. Assert **identity** as well as equality where the
point is that something survived: the same layer and model objects before and after.

### The mouse in pane 0 (`lyrics_edit_*`)

The lyrics editor is an event filter on pane 0, so its tests send it real mouse events.
The rules of the edits themselves are tested without a window, in `TestLyricsEdit`.

- **`QApplication::sendEvent()` to the pane** (`sendMouse()`, and `hoverAt()`,
  `pressAt()`, `moveHeldTo()`, `releaseAt()`, `dragFromTo()`, `doubleClickAt()`,
  `rightPressAt()` on top of it, and `shiftPressAt()` ... `shiftDragFromTo()` with Shift
  held): an event sent so goes to the pane's event filters first
  and then to the pane, as real input does. Not `QTest::mouseMove`, which does not carry
  the buttons held. A double-click is sent as Qt makes one: a press and a release, then
  `MouseButtonDblClick` in place of the second press, and its release.
- **Positions from what was painted**: y from `getLyricsBoxRow()`, x from the pane's
  `getXForFrame()` (`inRow()`, `columnOf()`). The layer knows where the row is only once
  it has painted it, so the helpers paint the pane first (`grab()`).
- **The pane gets a size and a zoom of its own** (`showEditableLyrics()`: 1000 x 120,
  128 frames a pixel). The test window is never shown, and its layout leaves pane 0 a few
  pixels high, or never lays a new pane out at all. At that zoom every word is in view and
  about a hundred pixels wide, so an edge's grab never reaches across a word; the fixture
  checks all of that before the test relies on it.
- The words' menu: `menuEntriesAt()` / `choose()` are the seam for what it offers and
  does (`menuAt()`, `chooseAt()`); `lyrics_edit_right_press` also finds the menu really
  popped up (`wordsMenu()`) and triggers its entries. A popped-up menu has no event loop
  to end and the dialog watchdog leaves it alone: close it with `closeMenus()`.
- A double-click that the pane handles itself (edit mode off, or between words) can open
  the edit dialog of the pitch point there, as upstream Tony does in Navigate mode. The
  watchdog closes it, and a test that sends one takes it with `takeDialogs()`, or
  `cleanup()` fails it.
- `lyricsModel()` changes the words straight in the model, as setup: no command, nothing
  marked modified.
- A spy on `CommandHistory::commandExecuted()` counts undos and redos as well as pushes:
  count pushes before any undo, or from a count taken after the last one.

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
- pYIN may analyse a short recorded range before Stop returns, so nothing is being analysed
  after it. A test that acts during that analysis calls `holdRangedMerges(true)` on its
  `TestMainWindow` before Stop (`Analyser::setRangedMergeHeld()`), and `false` before it
  waits for `analysed()`, or from a timer where a save's own wait has to let the merge go.
- A take stopped as soon as it started can have nothing in it under load: the fake device
  has delivered nothing yet, the take is dropped, and no analysis comes for `stopTake()`
  to wait for. A test that only looks at something during a take calls
  `waitForSomethingRecorded()` before `stopTake()`.
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
