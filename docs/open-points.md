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
- **A shortcut for Show Lyrics or Edit Lyrics?** There is none; one would have to be
  checked against `KeyReference` for clashes first.
- **The editing constants** were defaults taken without the user: the 20 ms shortest
  word, the 0.5 s new word, the 6 px grab on each side of an edge, the menu's wording,
  and Edit Lyrics living in the Edit menu.
- **The alternate pitch track at ±3 octaves** of a 220 Hz reference (28 Hz, 1.8 kHz) is
  outside the range the pane shows, and nothing scrolls to it; ±2 is in view.
- Of the [manual checklist](manual-checklist.md), the device check (Calibrate Audio with
  the dev checks) has been run on real hardware only in part: Calibrate Audio, and a dev
  run of an early build with items 1 and 2 only (the user's PC, MME, 2026-09-26); then
  Calibrate Audio and a whole dev run on MME at 200 ms and on WASAPI at 20 and 10 ms
  (2026-09-26, [audio-drivers.md](audio-drivers.md), §7). Not DirectSound, nor the driver
  menus themselves (section 2). Of section 3, only the looks, from cloud screenshots (2026-09-25). None of the lyrics
  items.
- **The dev checks' "not judged" reads Pass.** Items 4 and 12 pass when no look at the
  output lay in a silent gap, with a message that says so, and the report's Totals then
  overstate. Should it count otherwise, as Measured say?
  ([calibrate-audio.md](calibrate-audio.md), §10.)
- **The thresholds** of the sweep finder, the verdicts and the dev checks are starting
  values, to be tuned from the report files of real runs (what to send back:
  [calibrate-audio.md](calibrate-audio.md), §8).

## Not built

- **Beyond the three drivers** ([audio-drivers.md](audio-drivers.md), §8): WASAPI's
  exclusive mode and WDM-KS, lower still but taking the device from every other program.
- Showing two takes at once, or any comparison of takes other than switching.
- Singing track gain and pan are not saved in the session.
- Background music is not saved in the session; it is reloaded by hand.
- An old session (before takes) loses its singing track without telling the user why.
- Recording that starts before frame 0 of the reference.
- A phone version. Android and Sailfish OS were researched and nothing was built; see
  [mobile-port.md](mobile-port.md).
- **Lyrics are edited a word at a time, or all together**: the whole song can be shifted
  (Shift-drag, Edit > Shift Lyrics...), but not one line or the words from one on; no
  splitting or merging of words, no editing of line breaks, no syllables (a file's
  syllables are joined into their word). Not wanted for now. Import and Remove are not
  undoable, and a new import replaces the lyrics, edits made in Tony included, without
  asking.
- **Shift-drag in the lyrics' box row is the editor's while edit mode is on**: in
  Navigate mode it is also the pane's "Re-Analyse Area" gesture, which then works only
  above the row. A shift is clamped at the first word reaching 0 and says so only by
  the amount in the status bar (the dialog's) or by the words stopping (the drag's).
- **TTML and LRC only**: no SRT or Moises JSON. Another format is another parser that
  `parseLyrics()` chooses.

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
- **A word in the first ~30 px of the view is hidden** under the pane's vertical scale,
  which the pane draws over the left edge for its top layer: at the bottom left now, as it
  was at the top. Seen by rendering the window with the view scrolled so that a word sat
  at the left edge: only its last letters showed. With the view at the start, that zoom
  (about 86 px/s) showed 1.6 s before 0 s, so a word at 0 s was clear of the scale, but
  the half of its box before 0 s was under the pale wash the pane draws before the start
  of the reference.
- **The lyrics' layout is a guess at what reads well**: the gap between labels (a sixth of
  the font size), how far a label may move from its box (until its middle would leave it),
  the second row, the bold line starts, the font size and its growth, and the 2 s / 5 s
  caps on inferred ends are all constants to be judged by eye
  ([manual checklist](manual-checklist.md)).
- **Zoomed out, words still go to the second row or are left out**: with the font twice
  the view's at the least, a fast word's label is wider than its box. Rendered with
  made-up but realistic timing (about three words a second) and a 26 px font: one row at
  200 px/s and above, a few words in the second row at 150 px/s, many in it and some left
  out at 100 px/s. Only the word being sung is always drawn.
- **An undo from the keyboard in the middle of a lyrics drag can leave the word twice**:
  once the word has moved, undoing an earlier step of the same word (Ctrl+Z with the
  mouse button still held) removes the word as that step left it, which is not in the
  model, and adds the old one back beside the moved one. The word the drag made is still
  there, so the drag's own check does not see the change. There is no hook before an
  undo to end the drag first. A Shift-drag sees any change and is dropped, but an undo
  in the middle of one acts on words the drag has moved and the history does not know
  about, so it can leave a word twice in the same way.
- **The first click of a double-click on a word moves the playback cursor** there, as
  any click does; only the second is the editor's. Accepted for now.
- **A double-click the pane handles itself** (edit mode off, or between words in edit
  mode) can open the edit dialog of the pitch point under it: upstream Tony's Navigate
  mode, not new, but easier to meet now that double-clicks are in use there.
- **Lyrics steps left on the history after Remove Lyrics or an import do nothing** when
  undone or redone: their model is gone (the command logs a warning). The menu still
  offers them.
- **Pane 0 of a reference has no context-help connection**: only `newSession()` connects
  its pane to the status bar, so the pane's own help never shows there; the lyrics editor
  sends its help itself and clears it on leaving the row.
- **Right after `closeSession()`, Show Lyrics and the alternate pitch actions keep their
  enabled and checked states** until the next reference or session opens: nothing there
  calls `updateLayerStatuses()`. Show Lyrics then does nothing when chosen.
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

- **Restart jitter.** Each start of the stream moved the offset between input and output
  by up to about 8 ms either way on the user's PC, on MME and WASAPI alike, and items 1,
  2, 7 and 13 failed on it. The stream is now kept running between takes on desktop
  ([recording.md](recording.md#latency)); a dev run on WASAPI at 20 ms is to show that
  they pass. Opening the device again (a driver, latency or device chosen, a device menu
  opened, Tony started again) still moves the alignment, so a figure kept from an earlier
  session is up to about 8 ms off: calibrate at the start of a session. And the
  microphone shows as in use from the first take until Tony quits.
- **A round trip is kept per driver, not per latency**: after a latency change the kept
  figure is used unless the latencies the device reports moved by more than 1 ms. And
  before a device's first take, the menu line, Forget Measured Latency and the dialog
  look the figure up at the session's rate, so on a 48 kHz device they show the driver's
  figure although one is kept ([audio-drivers.md](audio-drivers.md), §8).
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
