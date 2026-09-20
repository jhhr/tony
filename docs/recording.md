# Recording a singing take: order of events, latency, timing

The Stop and Start paths of `MainWindow::record()` are long and their **order** is the
hard part. This page explains why each step is where it is. Read it before changing
`record()`, `recordingStarted()`, `finishSingingTake()` or `recordingFinishedFull()`.
What happens to the recording afterwards (splice, swap, ranged analysis, undo) is in
[takes.md](takes.md).

Symbols used throughout, all in frames on the reference's timeline
(`main/TakeTiming.h` holds every formula, unit-tested in `TestTakeTiming`):

| | |
| --- | --- |
| **P** | where the take starts (`m_takePosition`): the playhead, or the start of the selection |
| **E** | where it stops itself (`m_takeEnd`), -1 when the user presses Stop |
| **R** | pre-roll lead-in actually available before P (`m_takePreRoll`), 0 without one |
| **S** | where playback starts: P − R |
| **L** | recording latency: output + input latency + start gap (`m_recordingLatencyFrames`) |

The recording's own file always begins at the press of Record. What was sung in answer to
the reference at P is therefore at file frame **L + R**, and the splice reads from there.

## Start click

1. **A Stop click returns early** into `MainWindowBase::record()` at the top of `record()`.
   Everything below is for a Start. The latency fields are zeroed only on Start: the
   splice on Stop still needs them.
2. **P is read before the base call.** `MainWindowBase::record()` calls
   `setGlobalCentreFrame(0)`, and from the moment recording starts
   `ViewManager::getPlaybackFrame()` reports *record start frame + recorded duration*,
   not the position.
3. With Record into Selection on and a selection present, P/E come from
   `TakeTiming::chooseRange()` (the range the playhead is in, else the first). No
   overwrite question is asked in this mode: the selection is the consent. Otherwise, if
   P is inside the take's coverage, `confirmRecordingOverTake()` asks (virtual, so tests
   answer it; QSettings `MainWindow/confirmrecordover`).
4. Leftovers of an unfinished take are cleared (`teardownRealtimePitchLayer()`,
   `teardownRecordingLayer()`). The existing singing track is **not** torn down: a
   recording adds to the take.
5. The take's existing audio is muted for the duration (`muteSingingAudioForTake()`,
   directly on the play parameters). What the Play Singing Audio button asks for meanwhile
   is kept in `m_singingAudioAfterTake` and applied when the take is over. The take's
   pitch and notes stay on show.
6. Record mode is switched to **`RecordCreateUnshownModel`** (svapp fork) around the base
   call: the recording becomes a model of the document with no pane, no layer and no
   "Import Recorded Audio" undo entry. `ViewManager::setRecordStartFrame(S)` (svgui fork)
   makes the cursor run with the reference instead of crawling from frame 0.
7. `recordStatusChanged(true)` → `recordingStarted()` fires *inside* the base call, before
   the model is in the document. It defers with `QTimer::singleShot(0)`:
   `setupRealtimePitchLayer()`, and, if Play Reference While Recording is on, the latency
   estimate and `m_playSource->play(S)`.
8. `modelAdded()` sees `m_recordingAsSingingTrack`, stores `m_currentRecordingModelId` and
   returns. The recording is raw material, not the singing track; no analyser is made.
9. After the base call: if `isRecording()` is false (no device, device busy) the take
   flags are cleared and the singing unmuted. Otherwise the playback and centre frames are
   restored to S, `setupRecordingLayer()` gives the recording a hidden, muted waveform
   layer (`attachLayerToView`, `setSavedInSession(false)`) — the document needs *some*
   layer to hold the model — and `startTakePolling()` starts the 100 ms timer if there is
   an E to reach.

The recording's waveform is never shown: it starts at frame 0 of its own file, not at P.
The live dots are the feedback.

The pane-pruning loop and the `m_pendingExtraPanes` fallback after the base call find
nothing to do since `RecordCreateUnshownModel`. They are kept as a safety net.

## Stop

`recordCompleted()` → `analyseNow()` → `finishSingingTake()`. (Analyse Now is ignored while
`isRecording()`; `stopRecording()` clears that flag before emitting `recordCompleted`.)

1. `refineRecordingLatency()`; read the recording's path and length from the model.
2. **Stop the tracker, then** `teardownRecordingLayer()`. The tracker goes first so the
   model is never released under it. `stopRecording()` has already called
   `writeComplete()`; releasing the model closes the reader too, so the WAV is whole and
   closed before the splice reads it.
3. Playhead back to P, so Play hears what was sung and Record records the same part again.
4. `m_takes->spliceRecording(...)` — see [takes.md](takes.md). A recording no longer than
   L + R is dropped quietly; a real failure is a dialog and leaves the track as it was.
5. The undo command is made, the audio swapped, the ranged analysis started, the command
   pushed, and `syncCoverageStrip()` called **after** the swap.
6. `recordingFinishedFull(analysing ? m_analyser2 : nullptr)` clears flags, restores
   audibility, stops reference playback. With an analyser, the live dots stay until it
   emits `initialAnalysisCompleted` (`m_realtimeLayerTeardownConnection`); without one
   they go at once.

`recordingStarted(false)` only calls `updateAlternatePitchForTake()` and
`updateLayerStatuses()`, so the reference pitch track is back the moment the take stops,
whatever happens to the analysis.

`onRealtimePitchDetected()` begins with `if (!m_recordingInProgress) return;` because the
dot model outlives the take.

## Latency

- **Estimate** (GUI thread, in the deferred lambda):
  `computeRecordingLatency(getTargetPlayLatency(), getSystemRecordLatency())` plus
  `m_recordTarget->getFramesReceived()` just before `play()` — the *start gap*, the part
  of the recording made before the reference began to play.
- **Measurement** (audio callback): the lambda given to
  `m_playSource->setPlayStartCallback()` runs with the first block after `play()` and
  stores `getFramesReceived() − blockFrames` in an atomic. Drivers deliver a block's input
  before asking for its output; `FakeAudioIO` does the same. No Qt calls in there.
- `refineRecordingLatency()` swaps the estimate for the measurement. When the figure
  changes, the dots placed so far are cleared: they belong to sound from before the
  reference started.
- `currentRecordingLatency()` reads the measurement without consuming it; only
  `refineRecordingLatency()` consumes it. `currentTakeTiming()` is therefore built afresh
  wherever an answer is wanted — never cache a `TakeTiming`.
- Live dots are drawn at `P + TakeTiming::liveFrameIntoTake(frame)`; a negative result
  (lead-in, or sound from before the reference started) means the dot is dropped.
- The compensation ends up **in the audio**: a take's file always starts at frame 0.
  `setStartFrame(-L)` on the model is the old route; `TestLatencyShift` and
  `shift_aligns_onset` still cover it, and the svapp fork still restores the `start`
  attribute so that older `.ton` files open right.
- L is per take, not per device: each recording measures its start gap afresh.

## Pre-roll and Record into Selection

- **Pre-roll**: R = `MainWindow/prerollseconds` (3 s, no UI on purpose) clipped to the
  start of the song. The device records from the press of Record as always; the splice
  simply starts R frames later. With Play Reference off it is just a pause.
- **Record into Selection**: the take stops itself when `getFramesReceived()` reaches
  `L + R + (E − P) + 0.25 s` (`autoStopFrames()`). `pollTakeProgress()` calls `record()` —
  the same path as the Stop button, so everything that ends a take is in one place. The
  splice keeps at most E − P frames.
- `stopTakePolling()` is called from every place a take can end (`record()`'s Stop branch,
  `finishSingingTake()`, `recordingFinishedFull()`, `closeSession()`, `~MainWindow()`), and
  the poll stops itself when it finds no take.
- **The countdown**: three things in `MainWindowBase` write the status bar during a take
  (recorded duration every 10 ms, playback position every 20 ms, visible range on
  scroll). All three are routed through `MainWindow::showTakeCountdown()` first. Anything
  written to the status bar from a timer of your own will be overwritten before it can be
  read.

## The live tracker

`RealtimePitchTracker::run()`: read a 2048-frame window of the **mixdown**
(`getData(-1, ...)`, so a mic on input 2 works), YIN, emit, advance 256; sleep 5 ms when
there is not a full window yet. `kHopSize` is also the resolution of the dot model, whose
unit must be `"Hz"` for the layer to align to the pane's log-frequency scale.

Correct as they are, though they look wrong:

- The `1/frameSize` scale in the FFT difference function: bqfft's inverse is unscaled.
  It matches pyin's `fastDifference`, which `test-tony-core` links as the reference.
- The mixdown is a sum, not an average: YIN's normalised difference is scale-free.
