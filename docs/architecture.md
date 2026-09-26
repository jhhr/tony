# Architecture of the singing-practice fork

What this fork adds to upstream Tony, who owns what, and the rules of the Sonic Visualiser
(SV) libraries that the code depends on and that are not visible from Tony's own sources.
Member lists, slots and method names are in the headers; they are not repeated here.

## What the fork is for

Upstream Tony analyses the pitch of one recording. This fork makes it a singing practice aid:

1. **Two pitch tracks in one pane**: the reference (black) and the singing (orange).
2. **Live pitch dots** while recording, from a fast YIN tracker on its own thread.
3. **pYIN analysis of what was just recorded** replaces the dots when the take stops.
4. **Partial recordings and takes**: record from the playhead into part of the song, keep
   the rest, erase, undo, and keep several takes. See [takes.md](takes.md).
5. Around that: play the reference while recording, latency compensation, pre-roll,
   record into selection, an octave-shifted "alternate" pitch track to follow, timed
   lyrics along the bottom of the pane with the word being sung highlighted and every
   word editable in place, and a background music track that is played but never
   analysed.

The user-facing description is in the [README](../README.md).

## Where things are

Everything of Tony's own is in `main/`. The sibling directories (`svcore/`, `svgui/`,
`svapp/`, `pyin/`, `bq*/` ...) are separate repositories checked out by repoint and
**gitignored here**; see [forks.md](forks.md).

`meson.build` splits `main/` into two static libraries so the two test executables link
only what they need:

| Library | Rule | Contents |
| --- | --- | --- |
| `tony_core` | No GUI, no document, no layers. Unit-tested without a window. | `RealtimePitchTracker`, `ModelChangeThrottle`, `Coverage`, `TakeAudio`, `TakeEvents`, `SingingTakes`, `TakesFile`, `TakeTiming`, `Lyrics`, `LyricsTtml`, `LyricsEdit`, `LatencyUtils.h` |
| `tony_app` | Anything that touches a `Document`, a `Layer` or a window. | `MainWindow`, `Analyser`, `AlternatePitchTrack`, `CoverageStrip`, `LyricsTrack`, `LyricsEditor`, `TakeCommands`, `TakeLayers`, `PaneUtils` |

When adding a file: put it in the right `*_files` list, and in the matching `*_moc_files`
list **only if** it has `Q_OBJECT`. Logic that can be written as pure functions or a plain
struct goes in `tony_core` so that it can be tested cheaply — `TakeTiming` (all the frame
arithmetic of a take) and `TakeEvents` (what an erase does to events) are the models to
follow. `MainWindow` then only fills the struct in and puts the answer on screen.

## Who owns what

- `MainWindow` has two `Analyser`s: `m_analyser` for the reference (the document's main
  model) and `m_analyser2` for the **active** singing take. `Analyser` takes a
  `ColorScheme`; the secondary one makes no spectrogram and mutes its pitch and notes.
- **Both analysers share pane 0.** Pane 1 is the time ruler. Never put the singing track
  in a pane of its own.
- An `Analyser` recognises a pitch or notes layer as its own when the layer's model has
  the analyser's audio model as its **source model**. This one rule is what session
  restore, the audio swap and take switching all rest on, and it is why layers of
  inactive takes have their source model cleared (see [takes.md](takes.md)).
- `Analyser::fileClosed()` clears the layers but **not** `m_fileModel`.
- Helper objects that own one layer each and are only wired by `MainWindow`:
  `AlternatePitchTrack`, `CoverageStrip`, `LyricsTrack`. All three watch
  `Document::layerAboutToBeDeleted` in case someone else deletes their layer, and all
  three must be deleted in `~MainWindow` **before** the base class deletes the document
  (as must `m_analyser2`). `LyricsEditor` owns no layer: it finds the lyrics through
  `LyricsTrack` at every event, and is deleted before it.
- `RealtimePitchTracker` is a `QThread` that only **reads** the recording's
  `WritableWaveFileModel` and emits `pitchDetected(frame, hz)`. It never touches the pitch
  model; `MainWindow::onRealtimePitchDetected()` writes it on the GUI thread (queued
  connection). Stop the tracker **before** releasing the model it reads.

The reference is the pane's **work model** (`Pane::setWorkModel()`, svgui fork, set in
`analyseNewMainModel()`). Without that the pane greys itself out from the end of the
take's audio file, or of the recording being written.

Colours: reference pitch black / notes bright blue; singing and live dots orange / notes
bright purple; alternate pitch faded brown, dark brown while a take is recorded; both
waveforms grey, pale grey while lyrics are on show over them.

## Rules of the SV libraries

These were all learned from crashes or wrong behaviour. They hold for any new code.

### Models and layers

- `Document` owns all models and layers. Register a model with `addNonDerivedModel(id)`.
  **Never call `ModelById::release()` on a model the document knows** — double free.
- `Document::releaseModel()` silently does nothing while any layer still references the
  model. So *some layer must hold a model* for as long as it is wanted: that is the only
  reason the recording and the background music have hidden waveform layers.
- Silent teardown is **`deleteLayer(layer, true)` and nothing else**. It removes the layer
  from every view, releases the model if unreferenced and deletes the layer.
- **Never `removeLayerFromView()` followed by `deleteLayer()`** on the same layer:
  the first pushes a `RemoveLayerCommand` holding a raw pointer onto the undo stack.
- `Pane::removeLayer()` / `View::removeLayer()` do **not** update
  `Document::m_layerViewMap`. Deleting a pane whose layers are still in that map leaves
  a dangling `View*` that the next `deleteLayer()` dereferences. Remove a pane only with
  `pruneExtraPane()` (`main/PaneUtils.cpp`), which force-deletes the layer that owns the
  given model and `detachLayerFromView()`s the shared ones (time ruler) first.
  Precondition: another layer already references that model.
- `createLayer()` + `setModel()`, not `createEmptyLayer()`: the latter makes a throwaway
  model that enters the play source.
- `View::removeLayer()` fails to disconnect `layerMeasurementRectsChanged`;
  `TakeLayers::raise()` does it by hand.

### Tony's own layers make no undo commands

Every layer Tony makes for itself — analysers' layers, pitch candidates, live dots, the
recording's hidden waveform, the alternate pitch track, the coverage strip, the lyrics,
background music — is added with **`Document::attachLayerToView()`** (svapp fork): in the view and in
the layer-view map, so the session keeps it, but no command and no modified flag.
`addLayerToView()` (the undoable Add Layer) must not be used for these: Undo after a take
has to find the take. Whoever attaches the layer calls `documentModified()` if the change
should count. Such a layer is shown and hidden with `showLayer()` and removed with
`deleteLayer(layer, true)`, never by command: an undo that takes a layer out of the pane
leaves whoever keeps a pointer to it holding a layer that the redo stack owns and deletes.

### Commands

- `CommandHistory::addCommand()` clears **and deletes** the redo stack. A command pushed
  while an undo or redo is running therefore destroys the command that is running.
  Nothing reachable from `execute()` / `unexecute()` may push a command.
- `openPath()` pushes an `AddPaneCommand`. That is why a take's audio is opened with
  `MainWindow::openTakeAudioFile()` (a `ReadOnlyWaveFileModel` + `addNonDerivedModel()`,
  no pane, no layer, no command, no Recent Files entry) and never `openPath()`.
- A command whose work is already done is pushed with `addCommand(command, false)`.
- Commands hold **values only** (paths, `Coverage`, event lists), never layer or model
  pointers: by the time an undo runs the models have been made again several times.

### Playback

- The play source takes in the model of **every layer in a view**, playable or not, and
  what it holds decides where playback ends. Models that must not extend playback
  (coverage strip, lyrics, layers of inactive takes) are taken out with
  `m_playSource->removeModel()`. A session load adds each layer to its view and so puts
  their models back in: take them out after a load too.
- The svapp fork emits `Document::modelAboutToBeReleased(ModelId)` and `MainWindowBase`
  removes the model from the play source on it. Upstream only did so from
  `RemoveLayerCommand`, which forced deletes never run.
- Mute with `getPlayParameters()->setPlayAudible(false)` directly. `Analyser::setAudible()`
  and `Analyser::setVisible()` **write QSettings keys that both analysers share**; use them
  only for the user's own toggles, never for temporary states such as "during a take".
  For temporary hiding use `showLayer(pane, false)`.

### Selection and tools

- Tony's pane stack is built with `NoPropertyStacks`, so `Pane::getSelectedLayer()` is
  always null and **`Analyser::stackLayers()` / `PaneStack::setCurrentLayer()` do nothing**.
  A tool acts on the topmost layer of its kind (`Pane::getTopFlexiNoteLayer()`, which
  skips dormant layers in the svgui fork). Layer order is changed with `TakeLayers::raise()`.
- Making any selection starts `Analyser::reAnalyseSelection()` on the reference, so a
  transformer is usually running afterwards. Tests cannot assert
  `!haveRunningTransformers()` after selecting.

### Signals

- Connect with **member pointers**, not `SIGNAL()`/`SLOT()` strings, for anything whose
  signature has `sv::` types when the receiving class is outside namespace `sv`: the
  string form need not match, and then fails silently at run time (this kept
  `Analyser::layerAboutToBeDeleted` from ever being called). Whether it matches can
  depend on the Qt version: Qt 6.4 does not match `ModelId` in a string against a slot
  moc recorded as taking `sv::ModelId`, where the Qt used for development does.
- `audioFileLoaded()` is emitted for `CreateAdditionalModel` too (singing track, background
  music). `analyseNewMainModel()` returns early if the main model is the one it already
  analysed (`m_analysedMainModelId`); handing the reference to `m_analyser` twice forgets
  its pitch candidates and double-connects `regionOutlined()`.
- `recordStatusChanged(true)` fires inside `startRecording()`, **before** the base class
  has added the recording's model to the document. Work that needs the model is deferred
  with `QTimer::singleShot(0)`.

### Session files

- `Document::toXml()` writes a model only if a layer **in a view** uses it. Layers that
  store data (inactive takes) therefore stay in pane 0, hidden.
- A layer with `setSavedInSession(false)` (svgui/svapp forks) is left out, as is a model
  that only such layers show. Used for layers Tony makes again by itself on load.
- `SVFileReader` only warns about unknown elements, which is what lets the `.ton` carry
  Tony's `<takes>` element.
- An event's value is written with six significant figures. After a round trip compare
  frames exactly and values with a tolerance.
- Layer **object names carry identity** across a save: `"Alternate Pitch Track -1"` holds
  the octave count, `"Take 2 Pitch"` links a layer to its take, `"Lyrics"` marks the
  lyrics. They are not translated.

### Miscellaneous

- `floatvec_t` (bqvec allocator) is not `std::vector<float>`; copy before handing to code
  that wants the latter.
- `ModelById` is in `data/model/Model.h` in the pinned svcore.
- `WritableWaveFileModel::addSamples()` does not update the read view; the record target
  calls `updateModel()` on a timer (10 ms in the svapp fork, ~200 ms upstream). Readers
  of a recording in progress poll and wait.
- The build force-includes `main/mingw_byte_fix.h` to resolve the MinGW C++17
  `std::byte` / `byte` clash; do not remove it.

## Smaller features

**Alternate pitch track** (`AlternatePitchTrack`): a copy of the reference pitch model
shifted by -3..+3 octaves (never 0), in a model of its own with **no source model** so no
analyser claims it. It rebuilds 50 ms after a burst of changes to the source (queued:
pYIN writes from its own thread), and `MainWindow::syncAlternatePitchTrack()` re-points
it on `m_analyser::layersChanged()`, because Analyse Now replaces the reference's pitch
layer and model. Found again after a session load by its object name (`adopt()`). During
a take the reference pitch layer is hidden with `showLayer()` and comes back the moment
recording stops.

**Background music**: loaded with `openPath(CreateAdditionalModel)` under the
`m_loadingBackgroundMusic` flag so `modelAdded()` does not take it for a singing track;
given a hidden waveform layer of Tony's own **before** the extra pane is pruned; not
saved in the session.

**Load Singing Track** follows the same order: `analyseNewSingingModel()` synchronously
after `openPath()`, and only then prune the extra pane — the imported waveform in that pane
is the only reference to the model until `m_analyser2` has a layer of its own. It ends with
`clearTakeHistory()`, which also disposes of the "Import" command for the pruned pane.

**Lyrics** (`Lyrics` and `LyricsTtml` read the file, `LyricsTrack` owns the layer,
`LyricsEditor` edits the words): one `RegionModel` in pane 0 on the reference's timeline,
a region per word (or per line, for a line with no word times): frame = start,
duration = end - start (at least one frame), label = the word, value = the line's index,
which is what the bold line starts go by. It is drawn by the
svgui fork's `PlotLyrics` style ([forks.md](forks.md)), in boxes along the bottom of the
pane just above the coverage strip, because a session restores only layers
`LayerFactory` can make. Found again after a session load by its untranslated object
name `"Lyrics"`, in `analyseNewMainModel()` after the alternate pitch track. Its model is
taken out of the play source after an import and again after a load: a word past the end
of the reference would hold playback open. It is **never the pane's top layer**, because
the pane takes its hover readout and vertical scale from the top layer and this one has
neither. `show()` raises the layer that was on top before; `adopt()` raises the one under
the lyrics if the session was saved with them on top; and when any other layer is deleted
(the alternate pitch track turned off, a take deleted) a zero-time timer does the same,
because that layer is still in the pane when `layerAboutToBeDeleted` arrives. Not tied to
takes: the take code finds layers by take name, source model or extra pane, so it never
finds this one, and it stays on show during a take, when the singer needs the words most.
Import and Remove push no command and leave the undo history alone, like Load Background
Music: the simpler option, and the file is still there to import again (edits made in
Tony since are not: Export Lyrics keeps them). Show Lyrics is the layer's own
visibility, which the session saves, not a QSettings key. Both parsers strip control
characters (and U+FFFE, U+FFFF, which the UTF-8 decoder lets through) from every label,
and the editor cleans a typed text the same way: XML 1.0 cannot hold them, and one in a
label would make the `.ton` unreadable.

**Lyrics files.** `parseLyrics()` reads the file as TTML if its first character that is
not blank, after a byte order mark, is `<`, and as LRC otherwise: an LRC file never
starts with `<`, so the content decides whatever the file is called. TTML is read as
Apple Music, the Moises-Lyric-Exporter and AMLL TTML Tool write it, a `<p>` per line and
a timed `<span>` per word, with elements matched by local name in any namespace (files
without the TTML namespace exist). Its times are read as **absolute**: strict TTML makes
a child's time relative to its parent's, but no lyrics tool writes them so, and read that
way every word would move by its line's start. Timed spans with no white space between
them are syllables of one word and are joined: Tony edits words, and the exporter writes
punctuation as a span of its own straight after the word. Background vocals,
translations and romanisations (`ttm:role` `x-bg`, `x-translation`, `x-roman`,
`x-romanization`) are skipped with a warning: a translation is not what is sung, and
background vocals overlap the lead's words in a row that has room for one word at a time.
A `<p>` with no timed spans is one word, the whole line. Ends the file does not give are
inferred as for LRC; the exporter's TTML gives them all, which is why the README says to
set the exporter to TTML. A file with a `<!DOCTYPE` is refused: TTML has none, and it is
how entity tricks get in.

**Export Lyrics** writes the words as the model has them now, edits included
(`lyricsFromEvents()`, then `writeTtml()` in the exporter's Apple style), through a
`QSaveFile`, so that a file already there is replaced only by a complete one. It is **not
a command and does not mark the session modified**: nothing in the session changes. The
title is not in the model; it comes from the layer's name, which an import sets from the
file's title. Read back, an export gives the same words, texts and lines, the times to
half a millisecond.

The word being sung is highlighted. `MainWindow::playbackFrameChanged()` passes every
frame the view manager reports to `LyricsTrack::setPlaybackFrame()`, which passes it to
the layer's `setHighlightFrame()`: while playing, while recording (when the frame runs with
the reference from where the take starts), and on a seek with playback stopped, since
`ViewManager::setPlaybackFrame()` emits whenever the frame changes. It does so **before**
`showTakeCountdown()` can return, or the highlight would stand still through a pre-roll's
lead-in while the reference plays. An import and an adopt pass the current frame at once.
The layer repaints only when the word changes; the highlight is not saved and makes no
command.

While the lyrics are shown and visible, both analysers' waveforms are drawn "Pale Grey"
instead of "Grey" so that the words can be read over them: `Analyser::setWaveformFaded()`,
applied to both by `MainWindow::updateWaveformFade()`. The colour is set straight on the
layer, **never through `setVisible()` or anything else that writes QSettings**, and marks
nothing modified. The analyser remembers the fade for a waveform it makes or takes over,
but a new analyser starts without it, and a session saves the colour with the reference's
waveform layer. So `updateWaveformFade()` runs after an import, a remove and Show Lyrics;
after `setupSingingTrackAnalyser()`, which every recording, take switch, Load Singing
Track and session load goes through with a new waveform; in `analyseNewMainModel()`
whether or not lyrics were adopted, because the analyser took the saved layer over before
the lyrics were looked for; and in `closeSession()`, because `m_analyser` lives on for the
next file.

**Editing the lyrics** (`LyricsEdit` in `tony_core` says what an edit may do,
`LyricsEditor` does it). The lyrics layer is never the pane's top layer, and the pane's
tools act only on the top layer (`PlotLyrics` also calls itself not editable), so no tool
mode can reach the words. The editor is therefore an **event filter on the lyrics' pane**,
installed only while Edit > Edit Lyrics is on. In the box row it keeps from the pane only
what it acts on: a left press on an edge and the drag it starts, up to the release; a
left press with Shift held anywhere in the row and the drag it starts; a
double-click inside a word; a right press, for the words' menu; and moves with no button
held, where the cursor and the context help are its own. Everything else reaches the pane
as it would without edit mode, so a click still moves the playback cursor. Editing needs
a mode because words touch: the row is edges nearly everywhere, and an editor always on
would take the clicks meant for the cursor.

- **Edit mode goes off in one place**, `updateMenuStates()`, whenever
  `lyricsEditAllowed()` is false: the lyrics removed or hidden, recording started, the
  session closed (through `documentRestored()`). Each of those ends in
  `updateMenuStates()`. An import switches it off itself, before the words there go.
  Going off finishes a drag in progress, as a release would, and so pushes its command;
  when the lyrics have gone first (closing the session deletes the document before
  `updateMenuStates()` runs), the drag is dropped unpushed instead, as below.
  `updateMenuStates()` runs during undo and redo too, where a push would delete the
  command running, so nothing an undo or redo does may make `lyricsEditAllowed()` false:
  the lyrics' presence and visibility stay out of commands.
- **One `ChangeEventsCommand` per edit** ("Move Word Start", "Move Word End", "Change Word
  Text", "Add Word", "Delete Word", "Shift Lyrics"), holding the model's id and `Event` values, pushed
  done with `addCommand(command, false)`; `CommandHistory` marks the session modified, and
  the session saves the model as it is. A drag changes the model at every move, so that
  the word follows the pointer and the layer lays the words out again (svgui fork), and
  its command is pushed **only at the release**; a drag that ends where it began pushes
  nothing. The editor pushes only from a mouse event or a menu choice, never from
  anything an undo or redo reaches. Take operations clear the history, lyrics steps with
  it; Import and Remove do not, and a lyrics step left from before them does nothing, its
  model being gone.
- **The drag rules are fed the words as they were at the press**, at every move: the
  limits (the neighbour, the 20 ms minimum) come from where the word was then, so a drag
  back puts the word back. Fed the moved words, the limits would travel with the word.
  The edge moves as far as the pointer has, in frames, since the press, so a press a few
  pixels off the edge makes no jump, and a view that scrolls in the middle of a drag
  changes nothing.
- **The model can change under a drag** (a keyboard undo that takes the word away, the
  lyrics removed or replaced). The editor finds the layer and model through `LyricsTrack` at every event
  and checks that the word is still what the drag made it; if not, the drag's command is
  deleted unpushed and the rest of the drag, to the release, is swallowed, as the pane
  never saw its press.
- **The box row is the layer's** (`getLyricsBoxRow()`), never worked out again from the
  pane: the layer knows where it painted, in the pane's logical coordinates, the ones a
  mouse event has, which on a high-DPI screen are half those of the proxy it paints
  through. The row is empty before the first paint, and hidden lyrics keep the row they
  were last painted in, so the editor checks visibility as well. The boxes' x come from
  the pane's `getXForFrame()`, as the layer paints them.
- **After a question, look again.** The text dialog runs an event loop of its own, in
  which anything can happen: an import, a session closed, an undo. So after it the edit
  looks the model up again (the same id, edit mode still on) and the word in it (still
  there, unchanged), and does nothing if either has gone; Add Word works its span and line
  out again from the words as they are then. The words' menu is `popup()`ed, as Tony's
  own menu is, so that the question is not asked from inside the pane's event handling;
  its entries hold values (the model's id, the word, a frame) and are looked up again
  when chosen.
- Add Word goes at the **last** frame of the column clicked: the first can be inside the
  word before, whose end lies in the column after its box.
- **Shifting all the words** (a Shift-drag in edit mode, or Edit > Shift Lyrics..., which
  is to be had whenever `lyricsEditAllowed()` is, edit mode on or not): every start and
  end moves by one amount, clamped by `LyricsEdit::clampShift()` so that the first word
  does not go before frame 0; a shift that comes to 0 pushes nothing. What a press is,
  an edge's drag or all the words', is decided at the press, whatever Shift does after.
  The command takes **all the words out, then puts all the shifted ones in**: one out
  and one in at a time, a shifted word could meet an original still in the model.
  A Shift-drag must **not fill its command as it goes**, as an edge drag does:
  `ChangeEventsCommand` folds an add and the remove of the same event only when the two
  are next to each other, which they never are when all the words go out and come back
  at each move, so the command would keep every word's every step and its undo would
  replay them all. The drag moves the model itself, with no command, checking at each
  move that the model holds exactly the words it last put there (anything else, and the
  drag is dropped, as an edge drag is); at the release it puts the words back as they
  were and pushes one command from the words at the press to the words at the release.
  The dialog's shift, like the text question, looks the lyrics up again after the
  dialog (the same model, editing still allowed) and finishes a drag in progress first.
- Pane 0 of a reference has no context-help connection (only `newSession()` makes one),
  so the editor's help goes straight to the status bar, and the editor clears it itself
  when the pointer leaves the row.
