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
   trip Calibrate Audio can measure), pre-roll, record into selection, an octave-shifted
   "alternate" pitch track to follow, timed lyrics along the bottom of the pane with the
   word being sung highlighted and every word editable in place, and a background music
   track that is played but never analysed.
6. **An Android build** ([port-android.md](port-android.md)): a compact layout of large
   buttons, touch gestures on the panes, pitch and lyrics drawn at a size for a small
   screen, sessions opened and saved where they are, and an audio backend of its own. The
   compact layout and the gestures work on the desktop too, where the suites test them.

The user-facing description is in the [README](../README.md).

## Where things are

Everything of Tony's own is in `main/`. The sibling directories (`svcore/`, `svgui/`,
`svapp/`, `pyin/`, `bq*/` ...) are separate repositories checked out by repoint and
**gitignored here**; see [forks.md](forks.md).

`meson.build` splits `main/` into two static libraries so that the test executables link
only what they need: `test-tony-core` links `tony_core` alone, `test-tony-app` and
`test-tony-dev` both. `tony_app` also compiles svgui and svapp in. The application links
both **whole** (`link_whole`), which keeps objects nothing refers to, such as
`AndroidMediaReadStream`'s registration with bqaudiostream. On Android the application is
a shared library (`libTony_arm64-v8a.so`) whose `main()` Qt's Java launcher calls, and no
test is built.

| Library | Rule | Contents |
| --- | --- | --- |
| `tony_core` | No GUI, no document, no layers. Unit-tested without a window. | `RealtimePitchTracker`, `LiveDotsFeed`, `OctaveSlips`, `VoiceThreshold`, `VoiceGate`, `InputChannel`, `InputDevice`, `InputLevel`, `Coverage`, `TakeAudio`, `RecordingAlignment`, `TakeEvents`, `SingingTakes`, `TakesFile`, `TakeTiming`, `TakeDiff`, `Lyrics`, `LyricsTtml`, `LyricsEdit`, `LatencyUtils.h`, `LatencyCheck`, `LatencyCalibration`, `AudioDriverSettings`, `PlaybackSettings`, `AudioRoute`, `StreamLatency`, `PinchZoom`, `VerticalZoom`, `SongScroll`, `PopupArea`, `AndroidFiles`, `DecodedPcm`, `LogFile`; on Android only, `AndroidMediaReadStream` |
| `tony_app` | Anything that touches a `Document`, a `Layer` or a window. | `MainWindow`, `Analyser`, `AlternatePitchTrack`, `CoverageStrip`, `LyricsTrack`, `LyricsEditor`, `LyricsSize`, `PlotSize`, `TakeCommands`, `TakeLayers`, `PaneUtils`, `AudioCheckRunner`, `CalibrateAudioDialog`, `AudioCheckIndicator`, `AudioDriverMenus`, `VoiceThresholdMenu`, `InputChannelMenu`, `InputLevelFeed`, `InputLevelMeter`, `CheckInputLevelDialog`, `TakeRecordingSearch`, `CompactLayout`, `SongScrollBar`, `TouchGestures`, `TouchMenuStyle`; on Android only, `OboeAudioIO`, `AndroidStorage`, `AndroidScreen`; in development builds only, `main/dev/` (`DevChecks`, `TakeObserver`) |

Android-only files are in the `if system == 'android'` additions to those lists, and
Android-only code elsewhere is under `#ifdef Q_OS_ANDROID`; neither may change what the
desktop does. What only the Android build calls but needs no Android API (`AndroidFiles`,
`StreamLatency`, `DecodedPcm`, `PopupArea`, `LogFile`, `TouchMenuStyle`) is built
everywhere, so that the desktop suites test it. Qt defines `Q_OS_LINUX` on Android too:
code for the Linux desktop alone needs `!defined(Q_OS_ANDROID)` as well.

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
  `AlternatePitchTrack`, `CoverageStrip`, `LyricsTrack`. All three watch
  `Document::layerAboutToBeDeleted` in case someone else deletes their layer, and all
  three must be deleted in `~MainWindow` **before** the base class deletes the document
  (as must `m_analyser2`). `LyricsEditor` owns no layer: it finds the lyrics through
  `LyricsTrack` at every event, and is deleted before it.
- `RealtimePitchTracker` is a `QThread` that only **reads** the recording's
  `WritableWaveFileModel` and keeps its estimates for `takeEstimates()`. It never touches
  the pitch model; `LiveDotsFeed` (a member of `MainWindow`) takes the estimates on the GUI
  thread every 40 ms and `MainWindow::onRealtimePitchDetected()` writes them there. Every
  hop passes, on the tracker's thread, through an `OctaveSlips` of the tracker's own. Stop
  the feed, then the tracker, **before** releasing the model it reads
  (`stopRealtimePitchTracker()`).
- **Calibrate Audio** ([calibrate-audio.md](calibrate-audio.md), §9): `MainWindow` owns
  the `AudioCheckRunner` and, in development builds, the `DevChecks` (both made with the
  window), and the `CalibrateAudioDialog` (made the first time it is asked for).
  `DevChecks` drives the runner and owns a `TakeObserver`. The runner, the dev checks and
  the observer are `friend`s of `MainWindow`, as they drive the take path and read the
  take's state; the development ones only under `#ifdef TONY_DEV_CHECKS`. `~MainWindow`
  deletes the dialog, then the dev checks, then the runner, before anything they read;
  `closeSession()` tells the runner, then the dev checks. They are driven by timers and
  signals, never a nested event loop: the window can be closed during a run. The dialog
  owns the `AudioCheckIndicator` it shrinks to while a check runs, which `calibrateAudio()`
  puts at the right end of the status bar.

For a phone, all of which the desktop builds too:

- **`CompactLayout`**: `MainWindow` makes it before the menus and hands it the parts in
  `setupCompactLayout()`: the actions for its one toolbar, what to hide and what to show
  instead. `main()` switches it on before the window is shown, on Android always and on
  the desktop with `--compact`; View > Compact Layout switches it. Switching off restores
  what switching on saved. Anything new that a phone should not show goes into those
  parts. A shortcut works only while a visible widget holds its action or its menu, so
  the toolbar's menu button holds every menu of the hidden menu bar.
- **`SongScrollBar`** stands in for svgui's `Overview`, in its grid cell, in the compact
  layout, which shows it. `syncSongScrollBar()` hands it the reference's audio and pitch
  model ids whenever they may have changed (the analyser's layers changed, its analysis
  completed or merged, the main model changed, the session closed); it holds ids, not
  models. It moves the panes through the `ViewManager`, as the overview does, and leaves
  the playhead alone. `SongScroll` is its arithmetic.
- **`TouchGestures`**: `paneAdded()` gives every pane one, which the pane owns, with a
  `VerticalRange` of callbacks into the window. `PinchZoom` and `VerticalZoom` are its
  arithmetic (see [Touch](#smaller-features) below).
- **`PlotSize`** and **`LyricsSize`**, made before the menus: a View submenu each,
  remembered in QSettings, with defaults of their own on Android (150 % and 50 %, against
  100 %). `PlotSize` sets the view manager's plot scale (svgui fork); `LyricsSize` goes to
  `LyricsTrack::setTextScale()`.
- **`TouchMenuStyle`** is the application's style on Android only, set by `main()` before
  any widget is made, with the main window for its safe area margins. `PopupArea` is its
  arithmetic, which `CalibrateAudioDialog` places itself by too.

Android only:

- **`OboeAudioIO`** is the audio device ([port-android.md](port-android.md)): Android's
  `MainWindow::createAudioIO()` makes it, with input once recording has been asked for and
  the microphone allowed, else for output alone; a 250 ms timer (`checkAudioDevice()`)
  stops whatever is going on and replaces one that has failed, up to three times in ten
  seconds. `StreamLatency` is its latency arithmetic, and `AudioRoute` the route it
  reports (as an `AudioRouteReporter`, which the tests' `FakeAudioIO` is too), by which a
  measured round trip is kept ([calibrate-audio.md](calibrate-audio.md), §5).
- **`AndroidStorage`** (All files access; documents read and written through a picker's
  grant) is owned by `MainWindow`. `AndroidFiles` is the path work behind it and behind
  `main()`'s links to the Vamp plugins. `AndroidScreen` keeps the screen on while
  `CalibrateAudioDialog` runs a check. `LogFile` is the copy of the system log that
  `main()` writes and Help > Save Log... saves.
- **`AndroidMediaReadStream`** reads what the Android build's libraries cannot (M4A, AAC,
  FLAC, Ogg and the like) through Android's decoders. It registers itself with
  bqaudiostream's reader factory, so svcore's reader finds it; nothing refers to it, which
  is why the libraries are linked whole. `DecodedPcm` makes the decoder's buffers into
  float frames.

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
- **A take plays centred**: `Analyser::addWaveform()` pans the reference's waveform hard
  left (its pitch and notes, right) as upstream does, and the singing track's to the
  centre: it has no pan control, its pitch and notes are silent, and it is listened to for
  how the voice sounds ([recording.md](recording.md#input-channels)).
- Mute with `getPlayParameters()->setPlayAudible(false)` directly.
  `Analyser::setAudible()`, `setVisible()`, `setGain()` and `setPan()` **write the user's
  settings** (below); use them only for the user's own toggles and controls, never for
  temporary states such as "during a take". For temporary hiding use
  `showLayer(pane, false)`.
- The toolbar's level controls (`LevelPanToolButton`) answer a level between their notches
  by moving to the nearest notch and emitting it, as if the user had moved them, and the
  window then sets that level with `Analyser::setGain()` and switches the track on with
  `setAudible()`, both written as the user's choice. New pitch and notes are made at 0.5,
  between two notches. So a control is shown a level only under a `QSignalBlocker`:
  `updateLayerStatuses()` shows every one so (`showLevelAndPan()`), and so must anything
  else that moves one (the audio check's runner does). The toggles answer `triggered`,
  which `setChecked()` does not emit.

### The bottom bar's settings

The mixer of the Show and Play and the Playback Controls toolbars is kept between
launches: the tracks' Show and Play toggles, levels and pans, the master volume, and the
background music's mix, level and pan. It is the user's, not part of the song.
`PlaybackSettings` (`tony_core`) names the groups and keys and reads and writes them,
numbers as text as `LatencyCalibration` keeps its figures, so that every settings format
(the Windows registry too) keeps them exactly. The reference's tracks are in group
`Analyser` (`visible-N`, `audible-N`, `gain-N`, `pan-N`, N being the
`Analyser::Component`), the singing track's toggles in `SingingAnalyser`, the master
volume and the background music in `MainWindow`.

- **Written only for the user's own action on a control**: the `Analyser` setters above,
  the fader, the background music's toggle and level control. Never for a temporary state:
  a take's mutes and hides, the lifted constrain mode, the audio check's playback, a level
  only being shown, what the command line leaves out.
- **Each setter writes its own key, and a load writes nothing.** `Analyser::loadState()`
  reads all of a track's keys, then applies them with the `apply*()` functions, the setters
  without the write. A key never set stays unset: a default is not the user's choice.
- **The settings win over a session** for the reference's tracks. Every load (a file, a
  session, Analyse Now) applies what they hold. A level or pan never set is the layer's own:
  the fixed one a new layer is given (`addWaveform()`, `configureAnalysisLayers()`), or a
  session's for its own layers. The background music is the exception (below).
- **Each analyser has its own group** (`getSettingsGroup()`, from the colour scheme), so
  that the singing toggles never change what the reference shows or plays, nor the other
  way round. The analysis options stay in `Analyser`, for both. The singing analyser keeps
  no level or pan (there is no control for them), and its pitch and notes are silenced by
  `silenceSecondaryAnalysisLayers()`, never through the settings. Play Singing Audio
  pressed while a take has the singing muted is written at once, and applied when the
  take stops.
- **The spectrogram keeps `visible-3` only.** It is on the reference's model and plays
  through the audio's play parameters, so an audible setting of its own, loaded after the
  audio's, would be what Play Audio came back as. `audible-3` is neither read nor written.
- **`--no-spectrogram` and `--no-sonification` hold a track off**
  (`Analyser::keepHidden()`, `keepSilent()`): applied at once and at every later load,
  since Analyse Now makes the layers anew, and written nowhere, so that a launch without
  the switch has the track as the user left it.
- **Every device opened gets the master volume.** A device opens at unity gain, whatever
  the fader shows, and every one the window opens goes through
  `MainWindow::createAudioIO()`, which gives it the fader's value: at the first file;
  again with input for the first take (svapp's `record()`); for a device, driver or
  latency chosen; for a device menu opened. The fader is set from the settings in the
  constructor, where `setValue()` says nothing, so a device need not exist yet.
- **The background music** loaded from the File menu is given the kept mix, level and
  pan; while none is loaded, its toggle and level control show them. Music a session
  brings back (`adoptBackgroundMusic()`, below) keeps the mute, gain and pan the session
  saved with it: a song's backing track, and its balance with the reference, belong to
  the song, as the alternate track's octaves do. The kept ones are what the user last
  chose, for music loaded next. A level taken to nothing switches the mix off and keeps
  the level, as the reference's controls do.
- **Not remembered, on purpose** (the user's decision): the playback speed, Loop Playback
  and Constrain Playback to Selection, which belong to a song or a moment. The alternate
  pitch track's octaves are the song's: a session saved with the track on keeps them, in
  its layer's name.

### Selection and tools

- Tony's pane stack is built with `NoPropertyStacks`, so `Pane::getSelectedLayer()` is
  always null and **`Analyser::stackLayers()` / `PaneStack::setCurrentLayer()` do nothing**.
  A tool acts on the topmost layer of its kind (`Pane::getTopFlexiNoteLayer()`, which
  skips dormant layers in the svgui fork). Layer order is changed with `TakeLayers::raise()`.
- **The hover readout is not the top layer's.** The top of pane 0 is seldom the layer
  pointed at: the coverage strip after a take, which describes nothing; the alternate
  pitch track; and, after any selection, the reference's pitch candidates, hidden, which
  stay there until the next re-analysis. The box at the top right and the note lit up
  are therefore of the note under the pointer, in whichever note layer on show it is,
  the reference's or the take's; off the notes, of the topmost layer on show that
  describes anything there; and with the Edit tool, of the notes it edits
  (`Pane::getIdentifyLayer()`, svgui fork). While the pointer is on a note low in the
  pane, every layer above that one is drawn at every paint, as the lit layer is kept out
  of the view's cache: about 13 ms a paint on the cloud machine with a 15 s take in view,
  against 2 ms on a note of the take.
- Making any selection starts `Analyser::reAnalyseSelection()` on the reference, so a
  transformer is usually running afterwards. Tests cannot assert
  `!haveRunningTransformers()` after selecting.

### Drawing

- A pane draws its layers into an image at the **whole** pixel ratio above the screen's
  (3 on a phone at 2.75), through a `ViewProxy` whose coordinates are those physical
  pixels, while mouse events come in the pane's logical pixels. A size a layer gives in
  pixels is therefore physical: size a plot element with `scalePlotSize()` (it follows
  View > Plot Size), anything else with `scalePixelSize()` (it follows the font), and a
  hit area as what is drawn. The coverage strip and the lyrics use the latter.
- A change to the model of a layer in the pane's cache has the pane draw every cached
  layer again. A layer whose model changes many times a second is kept out of the cache
  (`Layer::setCachedInView(false)`, as the live dots are), and whatever lies in front of
  it is then drawn at every paint too ([recording.md](recording.md#the-live-tracker)).

### Signals

- Connect with **member pointers**, not `SIGNAL()`/`SLOT()` strings, for anything whose
  signature has `sv::` types when the receiving class is outside namespace `sv`: the
  string form need not match, and then fails silently at run time (this kept
  `Analyser::layerAboutToBeDeleted` from ever being called). Whether it matches depends
  on the type and the Qt version: a pointer (`Layer *` against `sv::Layer *`) never
  matches; a registered value type (`ModelId`, `sv_frame_t`) matches from Qt 6.5 but not
  in 6.4. Member pointers are checked when compiling and match under any version.
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
  lyrics, `"Background Music"` the background music. They are not translated.

### Miscellaneous

- `floatvec_t` (bqvec allocator) is not `std::vector<float>`; copy before handing to code
  that wants the latter.
- `ModelById` is in `data/model/Model.h` in the pinned svcore.
- `WritableWaveFileModel::addSamples()` does not update the read view; the record target
  calls `updateModel()` on a timer (10 ms in the svapp fork, ~200 ms upstream). Readers
  of a recording in progress poll and wait.
- The MinGW build force-includes `main/mingw_byte_fix.h` to resolve the C++17
  `std::byte` / `byte` clash; do not remove it.
- The window's place is kept at close as Qt's own record of it (`saveGeometry()`,
  `MainWindow/geometry`), which says whether it was maximised: its size and position
  alone brought a maximised window back as large as the screen but not maximised, and on
  Windows a little off it. `main()` puts it back (`restoreWindowGeometry()`) before it
  shows the window, never the constructor: every test window would then come back where
  the one closed before it was. On macOS the native window is made with the window
  (`setUnifiedTitleAndToolBarOnMac()`), and a hidden window's geometry reaches it only
  when it is shown, maximised by then: it kept Qt's default 640 x 480 as the size to
  un-maximise to, so the restore gives it the normal geometry first.
- A path, a URI or a name the user typed goes into a message in one `arg()` call with
  all the arguments (`arg(a, b)`), or the last of a chain: a `%1` or `%3A` in it (a take
  called "a %1", a `content://` URI) is taken for a placeholder by the `arg()` after it.

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
given a hidden waveform layer of Tony's own **before** the extra pane is pruned. The
session saves that layer and its model, and with the model its mute, gain and pan; the
file is looked for as the reference is, and one that is not found leaves the session
incomplete. On opening, `adoptBackgroundMusic()` finds the layer by its object name
`"Background Music"`, **before** `dropRestoredSingingTrack()`, which would otherwise take
its model for a singing track's, and drops the layer of music that could not be read (a
waveform layer may be without a model). Sessions saved before the layer had its name
open without their music.

**Load Singing Track** follows the same order: `analyseNewSingingModel()` synchronously
after `openPath()`, and only then prune the extra pane — the imported waveform in that pane
is the only reference to the model until `m_analyser2` has a layer of its own. It ends with
`clearTakeHistory()`, which also disposes of the "Import" command for the pruned pane.

**Voice threshold** (`VoiceThreshold`, `VoiceGate`, `VoiceThresholdMenu`): for singing
with the music on speakers, which the microphone hears as well. A level in dBFS under
which what the microphone hears is not taken for singing: no live dots are drawn for it,
and the take's ranged analysis keeps no pitch or notes there. What is found in the audio
is gated, never the audio, which is recorded, spliced and played as it is. A noise gate in
the splice was rejected: it would cut quiet singing out of playback for good, where a
threshold that proves too high is lowered and the take analysed again. One measure
throughout, the level of the half window whose frames YIN compares to find a pitch (the
window's first half, and in pYIN's Unbiased Timing its middle half), the channels'
average, as the live tracker's floor has it ([recording.md](recording.md#the-live-tracker);
the gate in [takes.md](takes.md#the-voice-threshold)). pYIN's own `lowampsuppression` is
not used for it: it only lowers the voicing probability, and `pyin` is upstream. A take
keeps the threshold it started with; the audio check's takes have none.

Playback > Voice Threshold sits straight after Record, which it is for, and not with the
Audio Driver and Audio Latency menus after it: the compact layout hides those, while a
phone on its own speaker needs this as much, so it is not among the compact layout's
hidden actions and the menu button reaches it. It is greyed out during a take and while an
audio check runs. The setting is QSettings `MainWindow/voicethreshold` in dBFS (Off is no
key at all, and anything at or under the tracker's −60 dBFS floor reads as Off): one value
for every driver and device, and not in the session. A choice only writes it: `record()`
reads it at every Start, and Analyse Now and a redo that analyses again read it when they
run.

**Input channel** (`InputChannel`, `InputChannelMenu`): Playback > Input Channel, for a
microphone on one input of an interface with two. Both Inputs, the default, makes a stereo
take of a two-input device, whose levels are the channels' average; Input 1 or Input 2
makes the take mono, from that input alone, which the live tracker reads, the splice puts
in, and pYIN and the voice threshold then hear at its own level. The design, and what a
phone's Android does with a stereo device, are in
[recording.md](recording.md#input-channels). Next to Voice Threshold, for the same
reasons: with Record, reached on a phone from the menu button, greyed out during a take
and a check. Kept per input device, as `LatencyCalibration` keys its figures (group
`InputChannel`), and the menu's first line names that device. `record()` reads it after
the base call, as a phone's input is known only then; the audio check's takes are made of
both inputs. A choice on a phone opens the device again, as its input is opened otherwise
for one input than for both (`OboeAudioIO`).

**A phone's input device** (`InputDevice`;
[port-android.md](port-android.md#choosing-the-input)): Playback > Audio Input Device,
which on the desktop lists the driver's devices, lists on a phone the inputs Android's
`AudioManager` lists (`MainWindow::listsInputDevices()`, `listedInputDevices()`, which
the tests give a phone's), through `InputDeviceMenu`, made the first time the menu is
listed. The choice is kept by type and product name (group `InputDevice`), and
`OboeAudioIO` asks for the id to open it by each time it opens its input, of the inputs it
lists once for that open. Before the first take, the measured round trip and the input
channel are looked up for the input chosen if it is plugged in; with none chosen and none
recorded from, Input Channel cannot say which input the take will open, and its entries
are shut until one has (`InputChannel::Key::known`). One chosen but not plugged in, or
not opened, is said in the status bar for 8 s, over the take's time and notes.

**The status bar** (`StatusLine`): over what the views write there (the visible range,
playback's position, a take's time, the note sung), first the countdown of a lead-in,
then a notice about the audio device for 8 s, then the last take's level, held until the
next take, playback or a selection. Everything that writes the status bar asks
`MainWindow::showStatusLine()` first; when a notice runs out, what was under it is
written again at once, as nothing else may write for a while.

**The input level** (`InputLevel`, `InputLevelFeed`, `InputLevelMeter`,
`CheckInputLevelDialog`; [recording.md](recording.md#the-input-level)): a meter beside
Record, in the compact layout's toolbar too (a `QWidgetAction`, so that each toolbar has a
meter of its own, all drawing one feed's state: the clip light lit in one is lit in all);
the take's peak and clipped places in the status bar at Stop; Playback > Check Input
Level, next to Input Channel. `MainWindow` owns the feed, made with the toolbar, and the
dialog, made when first asked for, and deletes both in `~MainWindow` before the base class
deletes the record target the feed reads. The feed reads only while the device runs with
its input (`InputLevelFeed::setRunning()`, from `MainWindow::updateInputLevelReading()`):
whatever is busy has resumed it, and it runs on until it is suspended as idle or opened
again (`deleteAudioIO()`).

**Touch** (`TouchGestures`, one per pane: an event filter in `main/`, not a change to
svgui, so that synthetic touch events test it on the desktop). One finger is left to Qt,
which makes mouse events of a touch nothing accepts, so taps, drags and selections go
through the pane's mouse handling as before; the first press is held back until the
finger moves, lifts or has been down 500 ms, so that a long press or a second finger
leaves no drag, selection or playhead move behind. Why the pane subscribes to a gesture
that never happens is in `TouchGestures.cpp`: it is how the second finger reaches the pane
rather than the scroll area. Two fingers zoom and scroll time; a long press opens the
pane's right-button menu, which then waits for a tap. Spread up the pane and moved up and
down, the fingers zoom and scroll the **frequency range**, which is the reference
analyser's spectrogram's, dormant or not: every pitch and note layer in the pane defers
its scale to the topmost Hz layer with a scale of its own, which is that one. So the range
is zoomed only in the reference's pane and only while it draws Hz on it, between A0 and C8
and never narrower than a major third (`VerticalZoom::pitchLimits()`). It is not undoable
and marks nothing modified; the session saves it with the spectrogram, and a new
reference resets it. A zoom is anchored at the middle of the pitch on show (both
analysers' pitch tracks and notes, those not hidden, in the pane's time range), pulled
towards the pane's middle as it zooms in, so that a low voice stays in view; with none on
show, about the fingers. The median was rejected as the anchor: it centres a lopsided
phrase badly while it still fits. Not built: the range following the pitch in playback,
and a "fit the pitch" action.

**Song scroll bar** (`SongScrollBar`, compact layout only): the whole song, a faint
contour of the reference's pitch (per pixel column its lowest to highest, on a log scale;
unvoiced frames leave gaps), a thumb for what the panes show and the playhead. The contour
is drawn into an image at the pixel ratio and made again only when the pitch model has
settled after a change, on a resize or a new ratio; playback's paging moves the panes
without a signal, so the thumb is checked each time the playhead moves and the strip
repainted only when something moved a pixel. The song's extent is the reference's audio
(a take past its end is not shown), and nothing of the singing or the takes is drawn: the
simpler option.

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
the pane takes its vertical scale from the top layer and this one has none (the hover
readout passes over it: see [Selection and tools](#selection-and-tools)). `show()`
raises the layer that was on top before; `adopt()` raises the one under the lyrics if
the session was saved with them on top; and when any other layer is deleted
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
