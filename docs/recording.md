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
| **L** | recording latency: round trip (measured, or output + input latency as reported) + start gap (`m_recordingLatencyFrames`) |

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
   pitch track and notes are hidden for the duration too (`updateSingingTrackForTake()`,
   called after the base call and again when the take stops): they are drawn over the same
   part of the pane as what is being sung now, the pitch in the same orange as the live
   dots, so with them on show the singer cannot tell what they are singing from what they
   sang before. Hidden with `Layer::showLayer()`, not `Analyser::setVisible()`, which would
   write the state to the shared settings. Only what was on show is hidden, and only what
   was hidden here is shown again.
6. Record mode is switched to **`RecordCreateUnshownModel`** (svapp fork) around the base
   call: the recording becomes a model of the document with no pane, no layer and no
   "Import Recorded Audio" undo entry. `ViewManager::setRecordStartFrame(S)` (svgui fork)
   makes the cursor run with the reference instead of crawling from frame 0; after the
   base call, `setRecordFrameRatio()` gives it the reference's frames per recorded frame
   (`TakeTiming::referenceFramesPerRecordedFrame()`), without which a device at 48 kHz ran
   the cursor 8.8 % ahead of the reference. It is set back to 1 before every base call.
7. `recordStatusChanged(true)` → `recordingStarted()` fires *inside* the base call, before
   the model is in the document. It defers with `QTimer::singleShot(0)`:
   `setupRealtimePitchLayer()`, and, if Play Reference While Recording is on (or the take
   is the audio check's, below), the latency estimate and `m_playSource->play(S)`.
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

`recordingStarted(false)` only calls `updateAlternatePitchForTake()`,
`updateSingingTrackForTake()` and `updateLayerStatuses()`, so the reference pitch track
and the take's own pitch and notes are back the moment the take stops, whatever happens to
the analysis.

`onRealtimePitchDetected()` begins with `if (!m_recordingInProgress) return;` because the
dot model outlives the take.

## Latency

L is the **round trip** plus the **start gap**, both in frames of the recording.

- **The round trip** (GUI thread, in the deferred lambda): `roundTripAt()` gives the figure
  Calibrate Audio measured and the user kept for these devices and the recording's rate,
  unless it is stale, and otherwise the sum of the output and input latency the device
  reports ([calibrate-audio.md](calibrate-audio.md), §5). It is worked out in seconds and
  then turned into frames of the recording, whose rate its model gives, because the two
  reported latencies count frames at different rates: `getTargetPlayLatency()` at the play
  source's `getDeviceSampleRate()` (the session's, when bqaudioio's `ResamplerWrapper`
  converted it; the device's own when the device was opened before any file, when the
  wrapper passes the figure through and tells the play source 0), and
  `getSystemRecordLatency()` at the device's. They differ only when the device is not at
  44.1 kHz. `computeRecordingLatency()` is no longer used here; the tests keep it as the
  reported sum to compare with.
- A run of the audio check may bring a round trip of its own for its takes (the dev checks,
  with the one the calibration before them measured). Nothing is stored, and the Playback
  menu goes on describing the window's own figure.
- **Start gap estimate** (same place): `m_recordTarget->getFramesReceived()` just before
  `play()`, the part of the recording made before the reference began to play.
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
- L is per take, not per device: each recording measures its start gap afresh. The round
  trip is per device and rate, but every take restarts the stream, and on MME the offset
  between input and output moves by about 13 ms from one start to the next, which the start
  gap does not see ([calibrate-audio.md](calibrate-audio.md), §10).
- `m_takeLatency` keeps what the last take was placed with: the round trip, whether it was
  measured, the reported pair in seconds, the recording's rate, and the start gap and
  whether it was measured. The audio check reads it for each of its takes.
- **The latency asked for** is not part of L: it is what bqaudioio asks the driver for on
  each side (PortAudio's `suggestedLatency`), which sets the driver's buffers, and the
  round trip with them. It is chosen per driver under Playback > Audio Latency (10 to
  200 ms, `Preferences/audio-latency-<driver>`; 200 ms where none is chosen, what every
  stream asked for before there was a choice), and `MainWindow::createAudioIO()` hands it
  to bqaudioio before each device is opened; choosing one opens the device again. A
  measured round trip is kept per driver, not per latency: a figure measured at another
  latency is told apart only by the staleness fingerprint
  ([calibrate-audio.md](calibrate-audio.md), §5), when the reported pair moves with the
  buffers.
- **The driver** (Playback > Audio Driver: MME, DirectSound, WASAPI, shown where more than
  one is built in, which is on Windows) is `Preferences/audio-target`. Where none is named,
  `MainWindow::createAudioIO()` names WASAPI (MME where there is no WASAPI) before the
  first device is opened, and the Playback menu before it shows the device menus, carrying
  the devices chosen before over to its keys: bqaudioio lists no devices for no driver when
  it has several. Choosing a driver or a latency is shut during a take and while a check runs
  ([audio-drivers.md](audio-drivers.md)).

## Pre-roll and Record into Selection

- **Pre-roll**: R = `MainWindow/prerollseconds` (3 s, no UI on purpose) clipped to the
  start of the song. The device records from the press of Record as always; the splice
  simply starts R frames later. With Play Reference off it is just a pause.
- **Constrain Playback to Selection is lifted for a take** (`liftPlaySelectionForTake()`,
  just before `play(S)`) and put back in `recordingFinishedFull()` and `closeSession()`.
  Constrained, the play source starts in the selection rather than at S and stops or loops
  at its end, while the take counts the reference as playing on from S without a break:
  with a pre-roll, what was sung landed a whole pre-roll early. Done through `ViewManager`,
  which writes no settings; the button follows, and is greyed out during the take.
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

## The audio check's takes

Calibrate Audio and the dev checks ([calibrate-audio.md](calibrate-audio.md)) record
through this same path, so that what they measure is what a take does. Their takes record
into the selection the runner makes, play the reference and have a lead-in of the run's
own, whatever the toolbar says: the three toggles write QSettings when they are toggled, so
the runner never touches them. It sets an override instead (`m_audioCheckTakes`, with the
run's pre-roll and round trip), which `record()`, the deferred lambda and
`wantedPreRollFrames()` read, and clears it when the take stops.

Record's action goes to `recordPressed()`, which ignores a press while a check runs (the
button is greyed then as well). The guard is not in `record()`: the runner and
`pollTakeProgress()` start and stop the check's takes through `record()`, and it cannot tell
their calls from a press.

## The live tracker

`RealtimePitchTracker::run()`: read a 2048-frame window of the **mixdown**
(`getData(-1, ...)`, so a mic on input 2 works), YIN, keep the estimate, advance 256;
sleep 5 ms when there is not a full window yet. `kHopSize` is also the resolution of the dot model, whose
unit must be `"Hz"` for the layer to align to the pane's log-frequency scale.

**Getting the dots to the pane.** The tracker finds about 170 estimates a second. Handed
to the GUI thread one at a time (a queued call each), a GUI thread that needs longer for one
than the tracker takes to find the next (5.8 ms) falls behind for good, and the dots trail
the singing more and more for as long as the take lasts; a phone is that slow. So the
tracker keeps its estimates, and `m_liveDotsFeed` (`LiveDotsFeed`, `tony_core`) takes all
of them every 40 ms and hands them to `onRealtimePitchDetected()` as one batch: a slow GUI
thread gets bigger batches, never a queue. The dot model is made with `notifyOnAdd` false,
so it tells nobody of a dot (a notice has the pane draw all of itself; with no notice at
all the dots were drawn only when one widened the model's pitch range); instead each batch
has the pane draw only the strip where its dots go (`updateViewFrames()`, `PaneUtils`).
Once a second the feed's costs go to the log ("live dots: ... s recorded, tracker at ...
s, dots to ... s; ..."), which is how a phone's log says whether the dots keep up.

**The dots are kept out of the pane's cache** (`Layer::setCachedInView(false)`, svgui
fork). Told of a change to the model of a layer in its cache, a pane draws every layer in
the cache again: the reference's waveform, pitch track and notes, 25 times a second. Kept
out, each notice costs a copy of the cache and the dots. During a take in a 1920 px window
on the cloud machine the GUI thread used about 22 % of a core, against 35 % with the dots
in the cache. Every layer in front of the dots is drawn at every paint as well: only cheap
ones, such as the coverage strip, may be raised above them.

Correct as they are, though they look wrong:

- The `1/frameSize` scale in the FFT difference function: bqfft's inverse is unscaled.
  It matches pyin's `fastDifference`, which `test-tony-core` links as the reference.
- The mixdown is a sum, not an average: YIN's normalised difference is scale-free.
