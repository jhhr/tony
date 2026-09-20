# Testing

QtTest suites in `main/test/`, in two executables that mirror the two libraries
(see [architecture.md](architecture.md)). The commands are in [AGENTS.md](../AGENTS.md).

| Executable | Links | Suites | Time |
| --- | --- | --- | --- |
| `test-tony-core` | `tony_core`, svcore, pyin's `YinUtil.cpp` as the YIN reference. `QCoreApplication`, no GUI. | `TestRealtimeYin`, `TestRealtimePitchTracker`, `TestLatencyShift`, `TestCoverage`, `TestTakeAudio`, `TestTakeEvents`, `TestSingingTakes`, `TestTakesFile`, `TestTakeTiming` | seconds |
| `test-tony-app` | `tony_app` + `tony_core`, a real `MainWindow` on the offscreen platform, the real pYIN plugin, `FakeAudioIO`. | `TestSingingDocument`, `TestSingingAnalysis`, `TestRecordWorkflow` | about 4.5 minutes (measured 2026-09-20), nearly all of it `TestRecordWorkflow`: takes are recorded in real time |

`meson test` / `build.bat test` runs both plus four svcore suites.

- **The `tony-app` meson test has `timeout: 300` and the suite takes about 277 s unloaded.**
  A few more real-time tests, or a busy machine, and `meson test` reports a timeout although
  every test passes. Raise the timeout in `meson.build` when adding workflow tests. Running
  the executable by hand has no timeout.
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

## Running

- `RunSuite.h` writes each suite's results to `$TONY_TEST_LOG_DIR/<SuiteClassName>.txt`.
  Read those with `grep -a` (the files can contain odd bytes); stdout is unreliable when
  redirected.
- Test function names on the command line are passed to **every** suite in the
  executable. The ones that do not have the function report it as unknown and fail, so
  the exit status of a run with names is always 1. Only a run with no names has a
  meaningful exit status.
- `QT_QPA_PLATFORM=offscreen` is set by `main()` when not given.

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

## What is there to reuse (`TestRecordWorkflow.h`)

- `FakeAudioIO` (`FakeAudioIO.h`): a duplex device with a worker thread that runs the
  callback in real time, input first and then output, as PortAudio and JACK do. `Config`
  sets rate, block size, reported latencies, a programmed mono input, its delay, and
  whether the input clock starts at the first audible output sample ("a singer exactly on
  time"). It captures the output, so tests can assert what reached the speakers.
- `TestMainWindow`: subclass of `MainWindow` that exposes protected operations as
  `doRecord()`, `doSwitchToTake()`, `seekTo()`, `selectRange()` and so on, installs the
  fake device through `createAudioIO()`, and **answers dialogs through virtual seams**:
  `confirmRecordingOverTake()`, `confirmDeleteTake()`, `askForTakeName()`, each with a
  `set...Answer()` and a counter of questions asked. A test cannot answer a real dialog:
  anything new that asks the user needs such a virtual.
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

## What stays manual

Anything about a real device, real timing by ear, or how something looks:
[manual-checklist.md](manual-checklist.md).
