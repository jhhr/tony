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
5. Around that: play the reference while recording, latency compensation (with a round
   trip Calibrate Audio can measure), pre-roll,
   record into selection, an octave-shifted "alternate" pitch track to follow, and a
   background music track that is played but never analysed.

The user-facing description is in the [README](../README.md).

## Where things are

Everything of Tony's own is in `main/`. The sibling directories (`svcore/`, `svgui/`,
`svapp/`, `pyin/`, `bq*/` ...) are separate repositories checked out by repoint and
**gitignored here**; see [forks.md](forks.md).

`meson.build` splits `main/` into two static libraries so the two test executables link
only what they need:

| Library | Rule | Contents |
| --- | --- | --- |
| `tony_core` | No GUI, no document, no layers. Unit-tested without a window. | `RealtimePitchTracker`, `ModelChangeThrottle`, `Coverage`, `TakeAudio`, `TakeEvents`, `SingingTakes`, `TakesFile`, `TakeTiming`, `LatencyUtils.h`, `LatencyCheck`, `LatencyCalibration`, `TakeDiff` |
| `tony_app` | Anything that touches a `Document`, a `Layer` or a window. | `MainWindow`, `Analyser`, `AlternatePitchTrack`, `CoverageStrip`, `TakeCommands`, `TakeLayers`, `PaneUtils`, `AudioCheckRunner`, `CalibrateAudioDialog`; in development builds only, `main/dev/` (`DevChecks`, `TakeObserver`) |

Development builds are every build type but `release`: they define `TONY_DEV_CHECKS` and
compile `main/dev/`, and everything that uses it elsewhere is inside `#ifdef
TONY_DEV_CHECKS`, so that a release build compiles with no `main/dev/` file
([calibrate-audio.md](calibrate-audio.md), §6).

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
  `AlternatePitchTrack`, `CoverageStrip`. Both watch `Document::layerAboutToBeDeleted`
  in case someone else deletes their layer, and both must be deleted in `~MainWindow`
  **before** the base class deletes the document (as must `m_analyser2`).
- `RealtimePitchTracker` is a `QThread` that only **reads** the recording's
  `WritableWaveFileModel` and emits `pitchDetected(frame, hz)`. It never touches the pitch
  model; `MainWindow::onRealtimePitchDetected()` writes it on the GUI thread (queued
  connection). Stop the tracker **before** releasing the model it reads.
- **Calibrate Audio** ([calibrate-audio.md](calibrate-audio.md), §9): `MainWindow` owns
  the `AudioCheckRunner` and, in development builds, the `DevChecks` (both made with the
  window), and the `CalibrateAudioDialog` (made the first time it is asked for).
  `DevChecks` drives the runner and owns a `TakeObserver`. The runner, the dev checks and
  the observer are `friend`s of `MainWindow`, as they drive the take path and read the
  take's state; the development ones only under `#ifdef TONY_DEV_CHECKS`. `~MainWindow`
  deletes the dialog, then the dev checks, then the runner, before anything they read;
  `closeSession()` tells the runner, then the dev checks. They are driven by timers and
  signals, never a nested event loop: the window can be closed during a run.

The reference is the pane's **work model** (`Pane::setWorkModel()`, svgui fork, set in
`analyseNewMainModel()`). Without that the pane greys itself out from the end of the
take's audio file, or of the recording being written.

Colours: reference pitch black / notes bright blue; singing and live dots orange / notes
bright purple; alternate pitch faded brown, dark brown while a take is recorded.

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
recording's hidden waveform, the alternate pitch track, the coverage strip, background
music — is added with **`Document::attachLayerToView()`** (svapp fork): in the view and in
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
  (coverage strip, layers of inactive takes) are taken out with `m_playSource->removeModel()`.
- The svapp fork emits `Document::modelAboutToBeReleased(ModelId)` and `MainWindowBase`
  removes the model from the play source on it. Upstream only did so from
  `RemoveLayerCommand`, which forced deletes never run.
- Mute with `getPlayParameters()->setPlayAudible(false)` directly. `Analyser::setAudible()`
  and `Analyser::setVisible()` **write QSettings keys that both analysers share**; use them
  only for the user's own toggles, never for temporary states such as "during a take".
  For temporary hiding use `showLayer(pane, false)`.
- The toolbar's level controls (`LevelPanToolButton`) answer a gain between their notches
  by moving to the nearest notch and emitting it, and the window then sets that gain
  through `Analyser::setGain()` and `setAudible()`, writing the shared settings. A play gain
  set directly on the reference (as the audio check's is) needs the control moved first,
  under a `QSignalBlocker`, before `updateLayerStatuses()` shows it.

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
  string form never matches and fails silently at run time (this kept
  `Analyser::layerAboutToBeDeleted` from ever being called).
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
  the octave count, `"Take 2 Pitch"` links a layer to its take. They are not translated.

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
