# Partial recordings and takes: design

How a singing take is stored, changed, analysed, undone and saved, and why. The recording
itself (what happens between Record and Stop) is in [recording.md](recording.md); the rules
of the SV libraries this leans on are in [architecture.md](architecture.md).

## What it is for

1. Skip intros and intermissions: record only where there is singing.
2. Practise one part repeatedly: re-record a portion and keep the rest.
3. Keep several takes of a song.

## Terms

- **Recording**: one press of Record to one Stop. A raw `recorded-*.wav` in the record
  directory. Tony never deletes these and no session refers to them.
- **Take**: what the singing track shows and plays — one **combined audio file**, a pitch
  layer, a notes layer and a coverage list. Built from one or more recordings.
- **Combined file**: a WAV that starts at **frame 0 of the reference's timeline**, silent
  wherever nothing was recorded. Named `take-<timestamp>[-n].wav`.
- **Coverage**: the frame ranges of the combined file that hold singing (`main/Coverage.*`).

## Decisions, and what was rejected

| Question | Decision |
| --- | --- |
| A take made of several recordings | One combined file per take, so there is one singing model and one analyser as before. A new file is written for every change; nothing is edited in place. |
| Analysis after a partial recording | Only the recorded range is analysed and merged in. |
| Inactive takes | Their pitch, notes and coverage stay in pane 0 as hidden, dormant layers. **Only the active take has an audio model and an analyser** (an audio model costs a file handle, a peak cache and a place in the play source). Rejected: an analysis file per take. |
| Showing two takes at once | Not supported. |
| Where audio lives | `<session>.takes/` beside the `.ton`, relative paths in the file. |
| Sessions from before takes | No migration: they open without their singing track, silently. |
| Editing | One operation, Erase Singing in Selection, covers remove, trim and split. No hand-editing of singing pitch or notes exists, so replacing a range loses nothing the user made. |
| Undo | Recordings and erases are undoable to any depth. Take operations (new, duplicate, delete, switch, Load Singing Track) are not, and **clear the undo history** without a prompt; Rename does not. |

## The pieces

`tony_core` (no window, unit-tested): `Coverage`, `TakeAudio` (`splice()` / `erase()`,
streaming, ~5 ms edge fades, refuse to overwrite a file, refuse mismatched sample rates),
`TakeEvents` (what an erase does to pitch and note events), `SingingTakes` (the list:
name, audio path, coverage, active index; and the bookkeeping of files), `TakesFile` (the
`<takes>` element and the folder), `TakeTiming`.

App side: `TakeLayers` (layers by name, `raise()`), `CoverageStrip`, `TakeCommands`
(`SingingTakeCommand`), and the wiring in `MainWindow`.

`SingingTakes` knows nothing of layers or models on purpose. Keep it that way.

## A take is linked to its layers by name only

`"Take 2 Pitch"`, `"Take 2 Notes"`, `"Take 2 Coverage"` (`TakeLayers::nameFor()` /
`parse()` / `find()`). No pointer to a take's layer is kept anywhere except in the analyser
of the active take. Consequences:

- The names are stored in the session file and are **not translated**.
- A take name is never used twice in a session, even after its take is deleted
  (`reserveTakeName()`): that take's layers may still be in the document.
- After a session load, names found on layers in the pane are reserved as well.

## An inactive take is owned by no analyser

An `Analyser` claims any pitch or notes layer whose model's source model is its audio
model. So `MainWindow::putOtherTakeLayersAway()` — **the one enforcement point**, called
from a switch and from `setupSingingTrackAnalyser()` — clears the source model of every
inactive take's layers, makes them dormant and inaudible, and takes their models out of the
play source (a longer inactive take would otherwise hold playback open past the end).

The active take's layers are raised (`raiseActiveTakeLayers()`), because tools act on the
topmost layer of a kind.

## The swap: new audio under existing pitch and notes

`MainWindow::swapSingingAudio(path)`, used after every splice and erase, by undo/redo and
by a take switch:

1. Open the new file **first** with `openTakeAudioFile()` — a file that cannot be read then
   disturbs nothing.
2. `m_analyser2->releaseLayers()`: cancels analyses, deletes only the waveform layer (which
   releases the old audio model), forgets pitch and notes without deleting them. Delete
   the analyser.
3. `setSourceModel(newAudio)` on the pitch and notes models. `Document::releaseModel()`
   clears the derivation of dependent models, so after step 2 they belong to nobody.
   **Nothing may read a layer's source model between 2 and 3**, and anything that held the
   old audio model's id is stale.
4. `setupSingingTrackAnalyser(newAudio, deferAnalysis=true)`: its scan
   (`claimExistingAnalyses()`) claims the two layers and `connectAnalysisLayers()` wires
   them; no pYIN runs. **`m_rebuildingTakeAudio` must be set around this**, or the coverage
   becomes "the whole of the new file" (the rule for a file the user loaded).
5. Put back what settings do not know: visibility, audibility, stacking.

The first recording of a take has no layers to keep: `loadTakeAudio()` opens the file and
`Analyser::addEmptyAnalyses()` makes an empty pitch and notes pair indistinguishable from an
analysed one. Both layers must exist before a ranged run.

`adoptTakeLayers(audio)` does step 3 for a restored or switched-to take, by name, just
before the analyser is made. Without it the take is analysed from scratch beside its own
layers.

## Ranged analysis and merge (`Analyser::analyseRange`)

The same two pYIN transforms as a whole-file analysis (`buildAnalysisTransforms()` serves
both), over the recorded range **widened by 0.5 s each side**, clipped to the coverage
range it sits in, aligned to the 256-frame grid. The temporary layers are in the document
but in **no view**, so nothing shows, selects or claims them.

When both are complete the result is merged into the claimed models directly (no command,
as a transform's output never was) and `rangedAnalysisMerged()` then
`initialAnalysisCompleted()` are emitted.

- **Only the middle of the run is merged.** W = the range asked for ± 0.25 s. The ends of
  a run are where pYIN has least context (it cannot stamp its first two hops at all), so
  what the models already hold there is better. Merging the whole run left a two-hop hole
  0.5 s before every recording and split notes in unchanged audio. Where the run's far
  edge is the coverage's edge (`m_rangedClippedEnd`), W reaches it and takes in the two
  hops the run stamped past it.
- **Time stamps.** The smoothed pitch output is fixed-sample-rate: the host rounds it onto
  the whole file's grid, no correction. The **notes** output is variable-rate and pYIN
  times a note by frame number *within the run*: `m_rangedStart` is added back. That is
  what the grid alignment is for.
- **Pitch**: old events in W go, new events in W are added.
- **Notes, by onset**: old notes with onset in W go; new notes with onset in W are added.
  An old note from before W is cut back only if a new note overlaps it. A new note that
  would overlap an old note starting at or after the end of W is cut back to that onset. A
  new note cut off by the *end of the run* (ends within four hops of it) takes the end of
  the old note that ran past, if there is one.
- **pYIN stamps one frame of every run twice** in fixed-lag mode: the last frame of
  `process()` comes again first from `getRemainingFeatures()`, 100 hops before the end.
  Whole-file tracks have it too, harmlessly. In a ranged run it falls inside W, so the
  merge drops the second copy. `pyin` is upstream and untouched.
- Every remove and add is recorded (`getRangedPitchChange()` / `getRangedNotesChange()`)
  for undo.
- One ranged run at a time. A second `analyseRange()` abandons the first; so do
  `cancelAnalyses()`, `releaseLayers()`, `fileClosed()` and `~Analyser()`. If a run is
  still going when the next take stops, the new run is widened to take in
  `m_takeAnalysisRange`, since the swap kills the old run's result.
- Do not clip a range to a model's frame count that is still growing: it is 0 just after a
  splice.
- **`getInitialAnalysisCompletion()` knows nothing of a ranged run.** Anything that means
  "is analysis finished?" must also ask `isAnalysingRange()`.

Analyse Now on a take re-analyses all coverage as **one** run from the first range to the
last (silence in between is cheaper than a queue). It is not undoable and closes the open
command first.

## Undo and redo (`SingingTakeCommand`)

The command holds **values only**: take name, audio path and `Coverage` before and after,
the `TakeEvents::Change` of pitch and of notes, and a range that still has to be analysed.
`execute()` / `unexecute()` hand a `TakeState` to `MainWindow::applyTakeState()`, which
finds the analyser and layers *then*.

- A recording's command is pushed at once, work already done, and stays **open**
  (`m_openTakeCommand`) until its analysis merges: `takeAnalysisMerged()` adds the two
  event changes and closes it. One Undo therefore covers the splice and its analysis.
- Undo before the merge abandons the run; the command remembers the range
  (`setPendingAnalysis()`), and a redo analyses it again.
- `applyTakeState()` uses `SingingTakes::restoreTake()`, not `setTake()`: neither file
  supersedes the other however often they are swapped.
- Event changes are applied removals-of-both-models first, then additions: where audio did
  not change, an analysis can put back the very event it removed, and it must be there
  once.
- Undo of a take's first recording has an empty path: the singing analyser is torn down,
  leaving no singing track at all.
- Erase is **refused while a ranged analysis runs** (the swap would lose its result); the
  action re-enables on `initialAnalysisCompleted()`.
- The history is never cleared by a recording or an erase.

## Erase rules (`TakeEvents`)

A pitch event in an erased range goes. A note wholly inside goes; a note running *into*
the range keeps its onset and is cut back; a note whose **onset** was erased begins again
at the end of the range, shorter; a range through the middle of a note leaves two notes. No
analysis runs. Erasing all coverage leaves the take, with an all-silent file.

## The coverage strip is the stored coverage

One `RegionLayer` (svgui fork's `PlotStrip` style: a 6 px band along the bottom,
display-only, `EqualSpaced` scale so the pane's scale is untouched) per take, in pane 0.
`Coverage::toEvents()` / `fromEvents()` are the entire file format: after a session load a
take's coverage is read back out of its strip.

`MainWindow::syncCoverageStrip()` is the one place it is kept in step. Call it **after** a
swap, never before: the swap restores the pane's state as it found it, and makes the take's
waveform layer again, on top, where it covers the band; `syncCoverageStrip()` raises the
strip above it every time. It also removes the strip's model from the play source.

## Files on disk

`SingingTakes` tracks three sets: **superseded** paths (kept until the session closes, for
undo), paths **this run wrote**, and paths a save has **protected**. `closeSession()`
deletes only files this run wrote that no take refers to and no save protected. The
superseded list can contain a file the user loaded; that is never ours to delete.

- `takeAudioDirectory()` is the record directory until the session has a file, and
  `<session>.takes/` from then on.
- **Every** save — not only the first and Save As — copies into the folder whatever takes
  refer to outside it (`copyTakeAudioForSave()` → `TakesFile::copyTakeAudioInto()`),
  because an undo after a save can point a take back at the record directory. Copied, not
  moved: Windows will not move an open file. Never overwrites (`-2`, `-3`). If a copy fails
  the copies made are removed and **the save is refused**.
- Paths are **absolute in memory** (takes, undo commands, protect list) and relative only
  in the file.
- `saveSessionFile()` first calls `waitForRangedAnalysis()` (polls; after 30 s it abandons
  the run rather than write two temporary models into the file).
- A takes folder this run made and left empty is removed on close.
- `QFileInfo` equality is true when neither file exists; do not use it to compare an output
  path that has not been written yet.

## The session file

```xml
<takes active="Take 2">
  <take name="Take 1" audio="My Song.takes/take-20260920-101500.wav"/>
  <take name="Take 2" audio=""/>
</takes>
```

`MainWindowBase::toXml()` writes the whole document in one call with no hook, so
`MainWindow::toXml()` buffers it and inserts the element before `</sv>`. A take's waveform
layer has `setSavedInSession(false)`, so the `.ton` names the audio once and holds no wave
model but the reference's. Without that, a missing take file produced the session
reader's repeating "locate it?" question plus an "incomplete session" warning before
Tony's own.

**Restore** (`openSession()` → `restoreTakes()`, with `m_restoringSession` set around the
base call so the queued `analyseRestoredSingingModel()` stands aside):

1. `dropRestoredSingingTrack()` drops every audio model but the reference. Current
   sessions carry none, older ones do — keep it.
2. No `<takes>` element: an old session. Derived layers and take-named layers go too.
3. Reserve names, add each take, read coverage from its strip, resolve its audio path
   against the `.ton`'s directory.
4. `activateTake()` — the same path a switch uses. **Nothing is analysed.**
5. Missing audio is reported in **one** warning naming the folder. The take still shows
   pitch, notes and coverage. Recording into it is refused: the splice needs the file.
6. Opening is not a change: `documentRestored()` unless already modified.

A file the user loads with Load Singing Track becomes a take covering the whole file
(`setWholeFileTake()`) and is analysed in full.

## Known limitations

Things to know, none of which stops the feature being used. See also
[open-points.md](open-points.md).

- A save during a ranged analysis longer than 30 s loses that range's analysis (Analyse
  Now brings it back).
- An undo after a save followed by another save leaves the same audio in the takes folder
  twice. Harmless.
- `commitData()` (crash/logout save to `~/.sv1`) goes through `saveSessionFile()`, so it
  copies the takes there **and points the running session's takes at the copies**. The
  next ordinary save brings them back.
- About 11 ms of pitch at the very start of a coverage range cannot be produced (pYIN's
  first two hops).
- One sung note can still become two when a note runs into W from before it *and* its
  audio changed. Deliberate trade for not splitting notes in unchanged audio.
- A splice or erase that succeeded on disk but could not be shown is not rolled back; the
  user gets a dialog naming the file.
- Playing a wave model with a **positive** start frame plays up to a block early and
  without an edge fade. This design avoids it: a take's file always starts at frame 0.
- A recording device whose sample rate differs from the reference's is unexercised;
  `TakeAudio` refuses the splice.
