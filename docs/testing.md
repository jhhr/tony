# Testing

QtTest suites in `main/test/`, in two executables that mirror the two libraries
(see [architecture.md](architecture.md)), plus one for the development checks and one for
the real device. The commands are in [AGENTS.md](../AGENTS.md).

| Executable | Links | Suites | Time |
| --- | --- | --- | --- |
| `test-tony-core` | `tony_core`, svcore, pyin's `YinUtil.cpp` as the YIN reference. `QCoreApplication`, no GUI. | `TestRealtimeYin`, `TestRealtimePitchTracker`, `TestLatencyShift`, `TestCoverage`, `TestTakeAudio`, `TestTakeEvents`, `TestSingingTakes`, `TestTakesFile`, `TestTakeTiming`, `TestModelChangeThrottle`, `TestLyrics`, `TestLyricsTtml`, `TestLyricsEdit` | seconds |
| `test-tony-app` | `tony_app` + `tony_core`, a real `MainWindow` on the offscreen platform, the real pYIN plugin, `FakeAudioIO`. | `TestSingingDocument`, `TestViewCache`, `TestSingingAnalysis`, `TestLyricsLayer`, `TestRecordWorkflow`, `TestUiChecks`, `TestAudioCheck` | about 8 minutes (measured 2026-09-26 on Linux), nearly all of it `TestRecordWorkflow`, `TestAudioCheck` and `TestUiChecks`: takes are recorded in real time |
| `test-tony-dev` | as `test-tony-app`; built only where the development checks are (any build type but `release`, `TONY_DEV_CHECKS`) | `TestDevChecks` | about a minute and growing: each test records several takes in real time |
| `test-tony-device` | as `test-tony-app`, but with the **real** audio device | `TestRealDevice` | about a minute; run by hand only, see the [manual checklist](manual-checklist.md) |

`meson test` / `build.bat test` runs the first three plus four svcore suites. `test-tony-device`
is built with them and never run by `meson test`: it needs a microphone that hears the
speakers. `test-tony-dev` is apart from `test-tony-app` so that the everyday runs stay
shorter: run it when a change touches what the development checks drive (see
[AGENTS.md](../AGENTS.md)).

- The `tony-app` meson test has `timeout: 900`; the suite took about 277 s unloaded when
  that was set. Every workflow test adds real time, so if the suite comes near it, raise it
  in `meson.build`: `meson test` reports a timeout even when every test passes. Running the
  executable by hand has no timeout.
- `main()` of the app suite replaces `VAMP_PATH` with the executable's directory, so an
  installed pYIN is never the one tested; the meson test `depends:` on `pyin_plugin`
  because nothing else builds `pyin.dll`. Build `pyin.dll` too when running by hand after
  a clean.
- Both mains set the organisation/application names to `tony-tests` / `test-tony-*` and
  every suite works in a `QTemporaryDir`, so the user's QSettings and record directory
  are never touched.
- `Tony.exe` links both libraries with `link_whole:`. A new source file that is in neither
  `tony_core_files` nor `tony_app_files` is invisible to the tests.
- `build_mingw/meson-logs/testlog.txt` contains a dump of the whole inherited environment.
  Do not print it or grep it loosely.

Suites are header-only classes (`TestX.h`). A new suite needs: the header, an `#include`
and a `runSuite()` block in `tony-core-test.cpp` or `tony-app-test.cpp`, and the header in
the matching `*_test_moc_files` list in `meson.build`. A new test function in an existing
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
  meaningful exit status.
- `QT_QPA_PLATFORM=offscreen` is set by `main()` when not given.
- **Built on Linux with Qt 6.4**, some tests are expected to fail, whatever the change.
  Core: `TestTakesFile`'s `takes_folder`, `relative_audio_path`, `resolve_audio_path` and
  `in_folder`, which test Windows paths (`C:\...`, case-insensitive). App:
  `TestRecordWorkflow`'s `take_analysis_covers_the_range_it_lost`,
  `range_analysis_torn_down_while_running`, `save_during_ranged_analysis`,
  `undo_during_analysis_then_redo` and `analyse_now_reanalyses_the_take`, and
  `TestUiChecks`' `menus_follow_the_take_by_themselves` and
  `stop_then_close_the_window_at_once` (the latter nearly every time), where the analysis
  finishes before the race they need can be set up. Which of those seven fail changes from
  run to run, and so can where: `undo_during_analysis_then_redo` fails
  either before the undo, with no ranged analysis left running, or after the redo, with
  `analysedRangeStart()` already 0 — the same race.

## Design principles

- **Split a number from what is done with it.** Latency is tested as arithmetic
  (`LatencyUtils.h`, `TakeTiming` — core suite) and, separately, as "the application applies
  the number it was given" (app suite, with `FakeAudioIO` reporting latencies chosen by the
  test and delaying its input by exactly that much). The real figure of a real device is
  for the [manual checklist](manual-checklist.md).
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
  time"), `loopback` (the output fed back into the input, as speakers into a microphone),
  and `inputChannel` (the input on one channel only, as a microphone on input 2). It
  captures the output, so tests can assert what reached the speakers.
- `TestMainWindow` (`TestMainWindow.h`, shared by the three suites that drive a window):
  subclass of `MainWindow` that exposes protected operations as `doRecord()`,
  `doSwitchToTake()`, `seekTo()`, `selectRange()` and so on, installs the fake device
  through `createAudioIO()` (or the real one, `setUseRealDevice()`), and **answers dialogs
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

## What stays manual

Anything about a real device, real timing by ear, or how something looks:
[manual-checklist.md](manual-checklist.md).
