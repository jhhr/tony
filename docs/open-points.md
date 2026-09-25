# Open points

Decisions waiting for the user, ideas not built, and known weak spots. Limitations that
belong to the takes design are in [takes.md](takes.md#known-limitations); defects in the
library forks are in [forks.md](forks.md). Remove an item when it is dealt with.

## For the user to decide

- **Pre-roll length** is fixed at 3 s (QSettings `MainWindow/prerollseconds`) with no UI.
  Is 3 s right, and should there be a control?
- **No overwrite question when recording into a selection**: the selection is taken as the
  consent. Right in use?
- **Constrain Playback to Selection + pre-roll**: the play source constrains playback to
  the selection, the lead-in is outside it, so it is cut short. Nothing keeps the two apart.
- **Take operations clear the undo history with no prompt** (all but Rename).
- None of the [manual checklist](manual-checklist.md) has been run.

## Not built

- **Calibrate Audio**: a measured round trip in place of PortAudio's reported latency,
  and dev checks for the manual checklist. Planned in [calibrate-audio.md](calibrate-audio.md).
- Showing two takes at once, or any comparison of takes other than switching.
- Singing track gain and pan are not saved in the session.
- Background music is not saved in the session; it is reloaded by hand.
- An old session (before takes) loses its singing track without telling the user why.
- Recording that starts before frame 0 of the reference.

## Weak spots

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
  fork; the 30 s give-up of `waitForRangedAnalysis()`; `commitData()` relocating takes;
  the two other ways `MainWindowBase::record()` can fail.
