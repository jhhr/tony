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
- Of the [manual checklist](manual-checklist.md), the device check has been run only in
  the cloud (no sound card, and the fake device); nothing yet on real hardware, and none
  of the lyrics items.

## Not built

- **Calibrate Audio**: a measured round trip in place of PortAudio's reported latency,
  and dev checks for the manual checklist. Planned in [calibrate-audio.md](calibrate-audio.md).
- Showing two takes at once, or any comparison of takes other than switching.
- Singing track gain and pan are not saved in the session.
- Background music is not saved in the session; it is reloaded by hand.
- An old session (before takes) loses its singing track without telling the user why.
- Recording that starts before frame 0 of the reference.
- **Lyrics are edited a word at a time**: no shifting of a line or of the whole song, no
  splitting or merging of words, no editing of line breaks, no syllables (a file's
  syllables are joined into their word). Not wanted for now. Lyrics that are all off by
  the same amount, because the reference is not the recording they were timed to, can
  only be moved by an LRC file's `[offset:]` tag. Import and Remove are not undoable, and
  a new import replaces the lyrics, edits made in Tony included, without asking.
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
  undo to end the drag first.
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
