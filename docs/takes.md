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
| Where audio lives | `<session>.takes/` beside the `.ton`, relative paths in the file. So a session is opened and saved only by a path, never through a `content://` URI on Android, where it needs All files access ([port-android.md](port-android.md)). |
| A device at another rate than the reference's (a phone at 48 kHz) | The recording is converted to the reference's rate before the splice (`SingingTakes::spliceRecording()`, `TakeAudio::resample()`, at the quality svcore loads files with), in a temporary folder beside the take's files: a take's file is always at the reference's rate, and the splice, the coverage and the events count the reference's frames. The raw recordings stay at the device's rate. Rejected: opening the device at 44.1 kHz ([recording.md](recording.md#a-device-at-another-rate)). |
| The channels of a take's file | Those of its first recording, or one with one input chosen (Playback > Input Channel): then the take is made of that input alone. A recording into a stereo take with one input chosen goes into both of its channels, as a mono recording does ([recording.md](recording.md#input-channels)). A take plays centred, whatever its channels. |
| Sessions from before takes | No migration: they open without their singing track, silently. |
| Editing | One operation, Erase Singing in Selection, covers remove, trim and split. No hand-editing of singing pitch or notes exists, so replacing a range loses nothing the user made. |
| Undo | Recordings, erases and Replace Take Audio from Recording are undoable to any depth. Take operations (new, duplicate, delete, switch, Load Singing Track) are not, and **clear the undo history** without a prompt; Rename does not. |

## The pieces

`tony_core` (no window, unit-tested): `Coverage`, `TakeAudio` (`splice()` / `erase()`,
streaming, ~5 ms edge fades, refuse to overwrite a file, refuse mismatched sample rates;
`resample()`, `extract()`), `TakeEvents` (what an erase does to pitch and note events),
`SingingTakes` (the list: name, audio path, coverage, active index; and the bookkeeping of
files), `TakesFile` (the `<takes>` element and the folder), `TakeTiming`,
`RecordingAlignment` (where a take's audio is in a longer recording).

App side: `TakeLayers` (layers by name, `raise()`), `CoverageStrip`, `TakeCommands`
(`SingingTakeCommand`), `TakeRecordingSearch` (the search, on a thread of its own), and
the wiring in `MainWindow`.

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
`initialAnalysisCompleted()` are emitted. Completion is looked at from the event loop,
**never within `analyseRange()`**, even when a short run is done before the call returns
(it can be, on a quiet machine): the caller always finds the range being analysed when the
call returns, and waits for the merge the same way whatever pYIN took.

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
- **The voice threshold gates the run's events** ([below](#the-voice-threshold)) after
  W's far end is worked out and before anything is removed or added. After, because the
  far end is where the run stamped, loud or not: a run whose last hops are quiet must
  still replace what the models held out to there. Before, because the notes' edges below
  are then worked out from what is kept, and the undo record is what was merged. With the
  threshold Off the merge is as it was without one.
- **Pitch**: old events in W go, new events in W are added.
- **Notes, by onset**: old notes with onset in W go; new notes with onset in W are added.
  A new note that would overlap an old note starting at or after the end of W is cut back
  to that onset. A new note cut off by the *end of the run* (ends within four hops of it)
  takes the end of the old note that ran past, if there is one.
- **A note running into W from before it** keeps its onset. If it also *ends inside W*, it
  takes the end of the run's note that is sounding at W's start (that end found as for a
  new note, above). The audio at W's start has not changed, so two notes sounding there
  are one note; this is what keeps one note through a join inside a held note, where the
  old note stops where the old recording did and the run's note began before W. No pitch
  tolerance: the two values are medians over different stretches, and a pitch the user
  corrected by hand must not stop the note. One that runs on past W keeps its end, in
  unchanged audio. Either way it is cut back to the onset of a new note that starts inside
  it, so it never runs over a note the run found in W.
- A run's note that begins before W is otherwise **not added**, even where no old note
  sounds at W's start: before W the notes the models hold stand (the old analysis had more
  context, or the user deleted that note).
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

### The voice threshold

For a singer with the music on speakers: what pYIN found where the take is quieter than
the threshold, where the microphone heard only the music, is not merged. The measure is
the live tracker's for its dots ([recording.md](recording.md#the-live-tracker)): the level
of the half window whose frames YIN compared to find a result, the channels' average (of
a take made of one input, that input's own level), a level at the threshold counting as
voice. For a stamp f that is **[f − 512, f + 512) in
either timing**. With the default timing pYIN stamps a block 512 frames in and compares
its first half, as the live tracker does; with the Analysis menu's Unbiased Timing
(`precisetime`) it stamps the block 1024 frames in but compares its middle half
(`YinUtil::slowDifference()`, a little wider at longer lags), which begins 512 frames
before the stamp too. Measuring from where the block begins in that timing, [f − 1024, f),
would take the first hops of every phrase and leave the hops of music after it. A pitch
event under the threshold
goes. A note is trimmed to begin at its first stamp at or over it and to end a hop after
its last (quiet stamps between them stay), and goes if it has none; its value is left as
pYIN gave it. The rules are `VoiceGate`'s, in `tony_core`.

- **The take's file is read again, raw; never the audio model.** Tony has every audio file
  read normalised to its peak (`MainWindow` sets the "normalise audio" preference), so the
  model's levels are relative to the take's loudest sample, and a take of quiet music
  alone reads as loud as any. A take's file holds what was recorded, at the level the
  microphone gave it. A WAV at the model's rate is read through a `WavFileReader`, a block
  at a time and only around the stamps, so a long take is never in memory whole; anything
  else goes through `AudioFileReaderFactory`, unnormalised, which decodes all of it first.
  Audio that cannot be read so is merged ungated, and the log says so.
- **Which threshold.** At Stop, the one the take started with
  ([recording.md](recording.md#start-click), step 3). Analyse Now and a redo that analyses
  again use the setting as it is then: the command holds no threshold, and Analyse Now is
  how a threshold chosen later is had on singing already recorded. Nothing is taken out of
  the audio, so a lower threshold and Analyse Now give back what a higher one left out. A
  run widened at Stop over an earlier take's range gates all of it with the new take's
  threshold.
- It runs on the GUI thread, in the merge: about 140 ms per 90 s of take where it was
  measured ([open-points.md](open-points.md)).

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

The strip takes no mouse input of its own. The Edit tool acts on the take's note at the
time under the pointer, at any height in the pane, and that includes the band: decided so,
rather than keeping the tools off the notes there (`strip_ignores_the_mouse`).

## Replace Take Audio from Recording

**Takes > Replace Take Audio from Recording...** puts a take's singing back from a longer
recording of it made elsewhere. The case it is for: a wireless microphone's radio dropped
out during the take, leaving gaps of digital silence, while the transmitter recorded the
singing whole on its own (a RØDE Wireless PRO or GO records 32-bit float WAV at 48 kHz,
from whenever its recording was started, for as long as it was left running).

1. A WAV is picked: `getOpenFileName()`, so on a phone Tony's own picker, the file
   copied into `imported/` where it has no path Tony may open
   ([port-android.md](port-android.md)).
2. **The search** (`TakeRecordingSearch`) runs on a thread of its own, with a progress
   dialog that can cancel it. The dialog is shown, never `exec()`'d, and is modal to the
   window, so the take cannot change meanwhile; a window closed during a search cancels it
   and waits for its thread. All of the recording is read once, for its level every
   10 ms, and each range of the take's coverage is looked for in that
   (`RecordingAlignment::findSegments()`):
   - **Coarse**: the take's level against the recording's, a correlation at every
     offset, through the FFT; the 16 best offsets at least 0.1 s apart are the
     candidates. Several, as the recording may hold other singings of the same song,
     whose levels rise and fall as the take's do. Every offset that leaves half a second
     of the range or more on the recording is a placement: the recording may have been
     started after the range began, or stopped before it ended. Off its ends it reads as
     silence, which the take's quiet before and after its singing is alike.
   - **Fine**: the take's loudest half second against the recording's samples within
     25 ms of each candidate. The best is the anchor. Off the recording it is matched
     against silence, alike to nothing, so the anchor lies on the recording.
   - **The walk**: quarter seconds end to end over what of the range the recording holds
     at the anchor's offset, each against the recording within 0.5 ms of the offset of
     the last that matched, out from the anchor both ways. So a drift between two clocks
     is followed. After pieces that did not match the reach grows by what two clocks
     100 ppm apart drift meanwhile, to at most 2 ms. The **confidence** is the median of
     how alike the pieces are (a correlation coefficient, 1 for the same waveform at any
     gain); the anchor's only when no piece was walked. Below **0.5** the range is not
     found. A short range is judged by its one or two pieces, not by the anchor: a range
     of 0.74 s whose two pieces were a punch-in read the anchor's 0.99, mostly its last
     quarter second, and was found.
   - The samples are **pre-emphasised** (a first difference): a held note of another
     singing finds a match within half a period anywhere, and the waveform's detail is
     what only the same singing has.
   - **Dropout gaps** (runs of 5 ms or more under −100 dBFS) are left out of every
     comparison: the recording has singing there, which the take does not, and counting
     it would read a take with many gaps as unlike its own recording.
   - **What the recording does not hold** of a range found, before its start or after
     its end, is left as the take has it.
   - **More than one recording session in a range**: a punch-in over an earlier take
     merges into one coverage range, but the transmitter recorded it at another time.
     The range found is scanned in 20 ms windows every 10 ms, each at the offset of the
     nearest of the walk's pieces alike. Where 0.1 s of loud windows are unlike the
     recording, with no 0.1 s alike among them, that stretch is another session: its ends
     are halfway between the last window alike and the first unlike. One of half a second
     or more is looked for on its own, the same way; a shorter one cannot be, and is left
     as the take has it. Another singing of the same notes reads alike now and then for a
     few windows (three in a row in the tests' punch-in), which ended a stretch there when
     two alike in a row did, and a punch-in came in pieces. The stretches are disjoint and
     in order by how they are found, and never the range itself again.
   - **Four deep at most**: a session inside one four deep is not looked into, and is not
     found. A range not found as a whole may be mostly punch-ins: its longest run of four
     or more of the walk's pieces alike in a row is a session of its own, scanned as
     above, and the rest is looked for.
   - **Before these**, another session was two or more of the walk's quarter seconds in a
     row unlike. A punch-in of 0.3 s or less left the pieces it straddled alike as a whole
     (0.55 to 0.62 against 0.5), and the old singing went in over it. The switches were
     found from either side of the piece between two such runs, and where that piece's
     first and last 20 ms were unlike, the two stretches overlapped by the piece, and the
     first was found to hold itself as another session and looked into for ever. A short
     range found by its anchor did the same. All three are tests now.
3. **Refused** unless every stretch looked for is found. The message names the first that
   was not, and how alike its best match was. Left as it was, and the report says so: a
   range under 0.5 s, another session's stretch under 0.5 s, and what the recording does
   not hold (`Match::left`).
4. **The replacement** (`TakeReplacement`, `tony_core`): for each stretch, its span of
   the recording, from the frame before to the frame after, cut at the recording's ends
   (`RecordingAlignment::recordingSpan()`; `TakeAudio::extract()` would fill what lies
   past them with silence), is written to a file of its own in a temporary folder beside
   the take's files. It is **scaled to the take's level** by the RMS ratio over the
   pieces that matched. Then it is spliced in as a recording is
   (`SingingTakes::spliceRecording()`, converted to the take's rate, 5 ms fades at each
   end). The fraction of a frame by which the span begins early is left off its front.
   Each take file written on the way goes as soon as the next is written
   (`SingingTakes::discardWritten()`): undo knows the take before and after, nothing in
   between. The coverage does not change. A splice that fails puts the take back as it
   was, and every file written goes.
5. **One undoable step**, as a recording is: a `SingingTakeCommand` named "Replace Take
   Audio", open until the analysis merges. The analysis is one ranged run from the first
   stretch to the last, as Analyse Now's, gated by the voice threshold as it is set now.
   Undo puts back the take's file and its pitch and notes; redo analyses again.
6. **The report** says, for each stretch, where it was found in the recording and how
   alike it was. It gives how far apart its two ends lie (the offsets near the first
   three and the last three pieces that matched, if a second or more apart), and by how
   much the level was brought to the take's.

**Two clocks.** The transmitter's clock and the receiver's are not the same clock: their
two ends may lie apart. More than **2 ms** apart is reported as such, and the audio is
**not stretched**. The offset used is halfway between the two ends', so each end is at
most half of that from where the take had it.

**Decided, by the lead:** the level is matched to the take's, which the request did not
ask for. The voice threshold, the input meter's figures and the take's scan read the
take's file as recorded. A transmitter's recording at another level (a 32-bit float file
has no gain of its own) would otherwise move every one of them, and a quieter one would
lose its pitch to the threshold.

Measured on the tests' synthetic singing: the same singing reads 0.997 to 0.9999 alike;
the same singing 22 dB down under −66 dBFS of noise, 0.93; another singing of the same
song, the same notes 30 ms or so apart, 0.20; anything else, 0.02 to 0.08. A take with a
fifth of it in dropouts reads 0.997 (0.887 with the gaps counted). A 100 ppm drift over
30 s reads 2.9 ms, the end pieces a little inside the ends. Punch-ins of 0.15 to 0.4 s
are told apart, their ends within 30 ms. The scan costs about 0.1 s a minute of take in
memory (a 60 s take with a punch-in: 447 ms against 342 without it, which also did not
look for the punch-in); reading a long recording costs more. Nothing has been measured on
a real transmitter's recording yet ([manual-checklist.md](manual-checklist.md)).

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

1. `dropRestoredSingingTrack()` drops every audio model but the reference and the
   background music, which `adoptBackgroundMusic()` has claimed by then. Current sessions
   carry no take's audio, older ones do — keep it.
2. No `<takes>` element: an old session. Derived layers and take-named layers go too.
3. Reserve names, add each take, read coverage from its strip, resolve its audio path
   against the `.ton`'s directory.
4. `activateTake()` — the same path a switch uses. **Nothing is analysed.**
5. Missing audio is reported in **one** warning naming the folder. The take still shows
   pitch, notes and coverage. Recording into it is refused: the splice needs the file.
6. Opening is not a change: `documentRestored()` unless already modified.

A file the user loads with Load Singing Track becomes a take covering the whole file
(`setWholeFileTake()`) and is analysed in full.

**A session that loaded incomplete is never saved unasked.** When audio a session names
cannot be read (svapp's "Incomplete session loaded"), its file would lose the reference to
that audio. svapp then gives the session no file, so Save is Save As; Save, Save As (before
the picker, which on Android makes the file) and Save Session to Audio File Path all ask
first (`confirmSaveOfIncompleteSession()`). The flag is the document's (`isIncomplete()`,
set by `SVFileReader`); a save clears it, as the file is then what the session is.

**Saved when Android sends Tony to the background** (`applicationStateChanged()`, on
`Qt::ApplicationSuspended`): a take being recorded is stopped as Stop does, playback is
stopped, the device suspended, and the session saved if it may be without asking
(`maySaveUnasked()`: it has a file, is modified and did not load incomplete). A session
never saved stays unsaved: there is no one to ask where it should go. Qt posts that state
and then holds the GUI thread's event loop until Tony is back, so nothing there may wait:
a save that has to wait for a take's ranged analysis to merge is tried again every 250 ms,
which in effect is once Tony is back. Nothing is saved while a dialog or the picker is open
(a nested event loop): the picker and the settings page send Tony to the background too,
and what is open may be about to save, or to decide not to. The desktop saves only when
asked.

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
- One sung note becomes two only where the run finds an onset inside it (a re-attack,
  say). A note that runs on past W keeps its old end even where the new audio stopped it
  inside W, up to the next onset the run found.
- A note whose old onset lies just inside W and whose onset in the run lies just before
  it (a hop or two either side of W's start) is lost: the old one goes with W, and the
  run's is not added. Found by reading the merge, not seen.
- A splice or erase that succeeded on disk but could not be shown is not rolled back; the
  user gets a dialog naming the file.
- Playing a wave model with a **positive** start frame plays up to a block early and
  without an edge fade. This design avoids it: a take's file always starts at frame 0.
- Two recordings that meet at a frame J each fade over 5 ms against what the take held
  there, not into each other: where that was silence, the join is a 10 ms dip. The dev
  checks' join check reads it as no step, and a pitch gap of about one hop.
- A file loaded with Load Singing Track at another rate than the reference's becomes a
  take whose file is at its own rate (only the model in memory is at 44.1 kHz): the next
  recording's splice refuses it, the rates differing, and an erase silences the wrong
  frames, since coverage counts the reference's.
- A take made with Both Inputs of a microphone on one input of two is stereo, and stays
  so after Input 1 or Input 2 is chosen: what was recorded before plays in one ear and
  reads 6 dB down for the voice threshold and pYIN; only what is recorded into it after
  the choice is the chosen input in both channels. A new take is mono from the start.
- A session that loaded without its reference cannot be saved as it is: Save As waits for
  the reference's analysis (`waitForInitialAnalysis()`), which never comes, until Cancel.
- **The voice threshold gates ranged runs only.** A file loaded with Load Singing Track is
  analysed in full, ungated (Analyse Now of it is gated), and so is a recording made with
  no reference, which becomes the session and is analysed as a reference is, though its
  live dots were gated.
- A run widened at Stop over an earlier take's range gates that range with the new take's
  threshold, which is not the one the earlier take started with if the setting changed
  between the two.
- A note the voice threshold trims keeps the value pYIN gave it, over its whole length,
  the quiet ends included.
- **Replace Take Audio from Recording** takes a dropout to be digital silence, as the
  tests make it. A receiver that hides a dropout some other way (repeating, or fading)
  leaves a piece that is less alike, not left out; one of 0.1 s or more reads as another
  session, and is left as the take has it.
- Its search reads all of the recording, and holds the take's range in memory: a long
  recording on a phone takes a while, with the progress dialog up.
- Two stretches it puts in that meet each fade over 5 ms against what the take held there,
  as two recordings that meet do (above): where that is a dropout, a 10 ms dip.
- A punch-in shorter than half a second is told apart (from 0.1 s), but cannot be looked
  for on its own: it is left as the take has it. One that holds a range's loudest half
  second is where the search starts, and the range is refused unless the punch-in is a
  second or more ([open-points.md](open-points.md)).
