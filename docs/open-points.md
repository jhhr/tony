# Open points

Decisions waiting for the user, ideas not built, and known weak spots. Limitations that
belong to the takes design are in [takes.md](takes.md#known-limitations); defects in the
library forks are in [forks.md](forks.md). Remove an item when it is dealt with.

## For the user to decide

- **Pre-roll length** is fixed at 3 s (QSettings `MainWindow/prerollseconds`) with no UI.
  Is 3 s right, and should there be a control?
- **No overwrite question when recording into a selection**: the selection is taken as the
  consent. Right in use?
- **Take operations clear the undo history with no prompt** (all but Rename).
- **The alternate pitch track at ±3 octaves** of a 220 Hz reference (28 Hz, 1.8 kHz) is
  outside the range the pane shows, and nothing scrolls to it; ±2 is in view.
- Of the [manual checklist](manual-checklist.md), the device check (Calibrate Audio with
  the dev checks) has been run on real hardware only in part: Calibrate Audio, and a dev
  run of an early build with items 1 and 2 only (the user's PC, MME, 2026-09-26). The whole
  dev run not yet. Of section 2, only the looks, from cloud screenshots (2026-09-25).
- **The dev checks' "not judged" reads Pass.** Items 4 and 12 pass when no look at the
  output lay in a silent gap, with a message that says so, and the report's Totals then
  overstate. Should it count otherwise, as Measured say?
  ([calibrate-audio.md](calibrate-audio.md), §10.)
- **The thresholds** of the sweep finder, the verdicts and the dev checks are starting
  values, to be tuned from the report files of real runs (what to send back:
  [calibrate-audio.md](calibrate-audio.md), §8).

## Not built

- **A lower-latency driver**, the next project (the user's decision, 2026-09-26): first
  the device-rate fix (a take recorded at 48 kHz is placed frame for frame into the
  44.1 kHz session; convert when it is spliced), then a `bqaudioio` fork for choosing the
  host API, WASAPI's rate conversion and a settable `suggestedLatency` (`jhhr/bqaudioio`
  exists, the remote `jhhr` in `bqaudioio/`, not pinned yet: [forks.md](forks.md)), then a
  driver type in Tony with the stored round trip per type, then Calibrate Audio and a dev
  run on each type. MME stays the default until a run shows another better
  ([calibrate-audio.md](calibrate-audio.md), §10).
- Showing two takes at once, or any comparison of takes other than switching.
- Singing track gain and pan are not saved in the session.
- Background music is not saved in the session; it is reloaded by hand.
- An old session (before takes) loses its singing track without telling the user why.
- Recording that starts before frame 0 of the reference.

## Weak spots

- **Loop Playback is left on during a take**, unlike Constrain Playback to Selection: a
  take that runs past the end of the reference would hear it start again while the take
  places what is sung after the end. Not tried.
- After playback the pane's own cache of what it drew holds the translucent note boxes
  painted twice over themselves, darker, until the next zoom or scroll. Seen with the
  offscreen platform, through the window's backing store; whether it shows on a real screen
  is not known. `TestUiChecks::grabPaneRedrawn()` works around it.
- With no audio device at all, "Couldn't open audio device" is shown again for every file
  opened (`MainWindowBase::createAudioIO()` tries each time).

- **If pYIN fails part-way, the live dots wait for ever**: they are removed on
  `initialAnalysisCompleted`, which then never comes.
- **`Analyser::newFileLoaded()` error path for the singing track** (pYIN plugin missing):
  not verified that no layers or models are leaked before the error return.
- **Play Singing Audio turned off during a take is not remembered for the next take**: it
  lives in `m_singingAudioAfterTake`, not in the settings.
- `Analyser::cancelAnalyses()` cannot see transforms whose layers are held only by the undo
  history.
- The dead pane-pruning fallback in `record()` (`m_pendingExtraPanes`,
  `drainPendingExtraPanes()` from `teardownRecordingLayer()`) is kept as a safety net since
  `RecordCreateUnshownModel`; it can go once that has proved itself.
- `Coverage::regionLabel()` gives every region a blank label, for a stock `RegionLayer`
  that printed the value otherwise. With `PlotStrip` it is no longer needed.
- Untested by any suite: removal of dots placed before the latency was measured; the
  deferred and error paths of the dot teardown; `ContinuousSynth` deletion in the svapp
  fork; the 30 s give-up of `waitForRangedAnalysis()`; `commitData()` relocating takes on
  Windows (the test runs elsewhere only); the two other ways `MainWindowBase::record()` can
  fail.
- **Closing the session during an ordinary take, then pressing Stop, hung** (seen once,
  2026-09-26, while testing the audio check; not looked into). `closeSession()` stops a
  check's take through the Stop path, but not the user's own.
- **The toolbar's level controls write the settings on their own.** In a window's first
  file, `updateLayerStatuses()` shows the pitch and notes gain of 0.5, which lies between
  two notches; the control moves to the nearest, 0.562, emits it, and the window makes
  both audible and writes that to the shared settings.
- **Play Audio's setting is overridden on load by the spectrogram's**: the spectrogram
  layer is on the reference's model and shares its play parameters, and `Analyser` loads
  `audible-3` after `audible-0`.

### Calibrate Audio and the dev checks

The reasons are in [calibrate-audio.md](calibrate-audio.md), §10.

- **Restart jitter on MME.** Every take restarts the stream, and on the user's PC the
  offset between input and output moved by about 13 ms from one start to the next. No one
  round trip then places every take: the dev checks' items 1 and 2, and 7 and 13 whenever
  their punch-in lands more than 2 ms off, fail on MME today. The remedy is the driver
  project above.
- The runner allows the reference's analysis 60 s (`kReferenceTimeoutMs`); the 4-minute
  song's took 10.4 s on the cloud machine, so a PC six times slower ends the dev run at its
  first stage.
- The countdown at a punch-in 1 s into the song with a 3 s pre-roll reads 1, 2, 1: it is
  shown before the round trip is known.
- Live dots trail the cursor by about the round trip (and 40 ms more on the fake): the
  cursor runs with what has been recorded.
- Items 3 and 5 judge the fresh punch-ins only. Item 14 cannot see an overwrite question,
  which `record()` would ask before the observer starts.
- Not in the dev run, of what the retired `test-tony-device` did: a take with no lead-in
  and not into a selection, stopped by hand; "no dialog" over every take (item 14 watches
  two stages).
- Every take logs "No such signal sv::WritableWaveFileModel::aboutToBeDeleted()", from the
  svapp fork ([forks.md](forks.md), known defects).
- The calibration's result page does not give the microphone's channel or the noise
  floor, which nothing in it measures.
