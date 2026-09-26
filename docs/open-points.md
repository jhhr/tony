# Open points

Decisions waiting for the user, ideas not built, and known weak spots. Limitations that
belong to the takes design are in [takes.md](takes.md#known-limitations); defects in the
library forks are in [forks.md](forks.md). Remove an item when it is dealt with.

## For the user to decide

- **Pre-roll length** is fixed at 3 s (QSettings `MainWindow/prerollseconds`) with no UI.
  Is 3 s right, and should there be a control?
- **No overwrite question when recording into a selection**: the selection is taken as the
  consent. Right in use?
- **Constrain Playback to Selection + pre-roll**: worse than a short lead-in. The play
  source starts playback at the selection, so none of the lead-in is played, and the splice,
  which takes playback to have started a pre-roll earlier, places what was sung a whole
  pre-roll too early (`preroll_with_playback_constrained_to_the_selection`, expected to
  fail). Keep the two apart, or start playback at the lead-in regardless?
- **Take operations clear the undo history with no prompt** (all but Rename).
- **A selection is an entry of the undo history**: making one re-analyses the reference in
  it (upstream Tony's pitch candidates) and pushes "Re-Analyse Selection". Selecting for
  Record into Selection or for Erase therefore puts such entries between "Record Singing"
  and "Erase Singing", and if the re-analysis finishes after an erase, Ctrl+Z takes it
  back instead of the erase.
- **The Edit tool edits the take's note at the time it is used, wherever in the pane**,
  the band of the coverage strip included. The strip itself takes no edits. Should the band
  keep the tools off the notes?
- **The alternate pitch track at ±3 octaves** of a 220 Hz reference (28 Hz, 1.8 kHz) is
  outside the range the pane shows, and nothing scrolls to it; ±2 is in view.
- Of the [manual checklist](manual-checklist.md), the device check has been run only in
  the cloud (no sound card, and the fake device); nothing yet on real hardware.

## Not built

- Showing two takes at once, or any comparison of takes other than switching.
- Singing track gain and pan are not saved in the session.
- Background music is not saved in the session; it is reloaded by hand.
- An old session (before takes) loses its singing track without telling the user why.
- Recording that starts before frame 0 of the reference.

## Weak spots

- **The live dots cost a redraw of the whole pane 25 times a second** during a take
  (`ModelChangeThrottle`): on the cloud machine, a 1920 px window, the GUI thread went from
  29 % to about 54 % of a core. A HiDPI screen makes each redraw dearer. A way in svgui to
  keep a layer out of a view's cache would make it nearly free ([forks.md](forks.md)).

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
