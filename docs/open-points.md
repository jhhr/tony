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
- **A shortcut for Show Lyrics?** There is none; one would have to be checked against
  `KeyReference` for clashes first.
- None of the [manual checklist](manual-checklist.md) has been run.

## Not built

- Showing two takes at once, or any comparison of takes other than switching.
- Singing track gain and pan are not saved in the session.
- Background music is not saved in the session; it is reloaded by hand.
- An old session (before takes) loses its singing track without telling the user why.
- Recording that starts before frame 0 of the reference.
- **Lyrics cannot be shifted or edited in Tony.** The remedy is to edit the LRC file and
  import it again; its `[offset:]` tag is the only shift. That includes lyrics that are
  all off by the same amount because the reference is not the recording they were timed
  to. Import and Remove are not undoable, and a new import replaces the lyrics without
  asking.
- **No highlight of the word being sung**: the playback cursor crosses the words. A
  highlight would tie the lyrics layer's painting to the play position, which defeats the
  view's paint cache.
- **LRC only**: no SRT, TTML or Moises JSON. The exporter's TTML carries real word (and
  syllable) end times where its LRC has none, so it is the natural second format if the
  inferred ends turn out misleading; another format is another function beside
  `parseLrc()`.

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
- **A word in the first ~30 px of the view is hidden** under the pane's vertical scale,
  which the pane draws over the left edge for its top layer. Seen with a word at 0 s and
  the view at the start.
- **The lyrics' layout is a guess at what reads well**: two rows (a word with no room is
  left out), the bold line starts, and the 2 s / 5 s caps on inferred ends are all
  constants to be judged by eye ([manual checklist](manual-checklist.md)).
- **Right after `closeSession()`, Show Lyrics and the alternate pitch actions keep their
  enabled and checked states** until the next reference or session opens: nothing there
  calls `updateLayerStatuses()`. Show Lyrics then does nothing when chosen.
- Untested by any suite: removal of dots placed before the latency was measured; the
  deferred and error paths of the dot teardown; `ContinuousSynth` deletion in the svapp
  fork; the 30 s give-up of `waitForRangedAnalysis()`; `commitData()` relocating takes;
  the two other ways `MainWindowBase::record()` can fail.
