# Dependency forks

Tony's libraries are separate repositories checked out into the top-level directories by
[repoint](../repoint-project.json) and **gitignored in this repository**. Six of them are
forks under `github.com/jhhr` that exist only for this Tony fork:

| Directory | Fork branch | Why it is forked |
| --- | --- | --- |
| `svcore/` | `jhhr/svcore` `tony-customizations` | Qt 6.11 build fixes, and one of its own tests made to pass on macOS. No behaviour change. |
| `svgui/` | `jhhr/svgui` `tony-customizations` | See below. |
| `svapp/` | `jhhr/svapp` `tony-customizations` | See below. |
| `bqaudiostream/` | `jhhr/bqaudiostream` `master` | `<shobjidl.h>` instead of `<shobjidl_core.h>` under MinGW, needed for `-DHAVE_MEDIAFOUNDATION`. |
| `bqaudioio/` | `jhhr/bqaudioio` `tony-customizations` | See below. Upstream is Mercurial on sourcehut; the fork started from its GitHub mirror. |
| `pyin/` | `jhhr/pyin` `tony-customizations` | See below: its own tests run with Tony's, and it is where the pitch analysis changes. Upstream is `c4dm/pyin`. |

`chp/` and the rest are upstream and must stay untouched.

Every fork's branch for Tony is `tony-customizations` (bqaudiostream, which has no such
branch, stays on `master`), and `repoint-lock.json` pins their heads. The fork branches
made for Tony's feature branches are merged there: svgui's `feat/tonyandroid` (the plot
scale, the record frame ratio, the lyrics' text scale) and svapp's and bqaudioio's
`feat/wasapi` (the stream kept running, the drivers). A checkout left on one of those is
not what the lock file names: move it to `tony-customizations`.

## Changing a fork

The forks are free to change when Tony needs it; prefer a small, general addition to the
library over a workaround in `main/`.

1. Edit and commit inside the library's directory (it is its own git repository). The
   fork's branch follows Tony's: for work committed straight to Tony's `default`, the fork
   branch of the table; for work on a Tony feature branch, a fork branch of the **same
   name**, on top of what it already holds (or started from the fork branch of the table),
   which reaches the fork branch when the Tony branch is merged. Commit messages there
   follow that repository's style: `area: what`.
2. Push to the remote named **`jhhr`**. In `svcore`, `svgui` and `svapp`, `origin` is
   upstream sonic-visualiser, in a `bqaudioio` cloned from its mirror it is
   breakfastquay's, and in a `pyin` cloned before the fork it is `c4dm`'s — do not push
   there. In a cloud session the checkouts are
   `container-setup.sh`'s, whose `origin` is the fork, and two checks stand in the way:
   - The session's git proxy refuses a push to a repository not attached to the session,
     a new branch included (HTTP 403). The session's add-repository tool attaches it, with
     push access.
   - Auto mode trusts only the repository the session started in and its remotes, and so
     blocks committing in a fork's checkout, attaching the fork and pushing to it. The
     environment's setup script names five of the forks, all but `bqaudioio`, as trusted
     as well, which the user chose ([building.md](building.md#building-on-linux));
     `claude auto-mode config`
     shows whether a session has that entry. Without it, the user's own message has to
     ask for the action, naming the fork and the branch. After a denial, stop and tell the
     user what is blocked: trying again another way counts as getting round the check, and
     is blocked too. The user can instead push the change.

   Do not start the session with the forks selected instead: a session with several
   repositories runs no repository's SessionStart hook, so the background build does not
   start.
3. Put the new commit hash in `repoint-lock.json` as that library's `pin`, and commit that
   in Tony together with the code that needs it.
4. A sub-agent that was told to work only in `main/` does not edit a fork: it reports
   exactly which change it needs, and the lead session makes it.

When switching Tony branches, check out the fork branches that go with it: a fork left on
another branch builds something the lock file does not say.

**repoint does not run on the development machine** (it needs an SML compiler and none is
installed). The checkouts are managed with plain git, and `repoint-project.json` /
`repoint-lock.json` are edited by hand. Keep the lock file's pins equal to what is checked
out: CI and anyone else's checkout get exactly what the lock file says. Keep its final
newline too: `repoint install` writes the file afresh, with one, and without it CI's tree
counts as changed (the APK's version then says `+`).

**Searching**: ripgrep-based search tools skip these directories because they are
gitignored. Pass the directory as the search path explicitly, or use `grep -rn` in Bash.

## What the forks add (and why Tony needs it)

### svapp

- `Document::attachLayerToView(View*, Layer*)`: layer into a view and the layer-view map,
  no undo command, document not marked modified. For every layer that is Tony's own
  furniture. Without it Undo after a take found Add Layer commands.
- `Document::detachLayerFromView(View*, Layer*)`: the reverse, used by `pruneExtraPane()`
  to keep shared layers (the time ruler) alive when an extra pane is removed.
- `Document::modelAboutToBeReleased(ModelId)`, and `MainWindowBase` removing the model
  from the play source on it. Upstream reached `removeModel()` only from
  `RemoveLayerCommand`, which forced deletes never run.
- `Document::toXml()` leaves out layers with `setSavedInSession(false)` and any model only
  such layers show; what was derived from it is written as not derived.
- `MainWindowBase::RecordCreateUnshownModel`: record into a model with no pane, layer or
  "Import Recorded Audio" command. Before it, that command referred to a pane Tony had
  deleted, so the history had to be cleared before every take (undo depth one).
- `AudioCallbackRecordTarget`: `recordUpdateTimeout` 10 ms (upstream ~200 ms) so the live
  tracker sees audio promptly; `setSystemRecordLatency()` actually stores its value
  (upstream: a no-op); `getSystemRecordLatency()`; `getFramesReceived()`;
  `getApplicationChannelCount()` is always two, where upstream it was the channels the
  device opened last gave, so that after a one-input device every device was opened with
  one, and an interface's second input could not be had (Input Channel,
  [recording.md](recording.md#input-channels)).
- `AudioCallbackPlaySource`: `setPlayStartCallback(std::function<void(int)>)`, run from
  the audio callback with the first block after `play()` (start-gap measurement);
  `getModels()` for tests; `removeModel()` tolerates a model already gone.
  `AudioGenerator::removeModel()`/`clearModels()` also delete the continuous synth.
- `SVFileReader` restores the `start` attribute of wave file models (upstream wrote it and
  never read it). Only older `.ton` files need it now.
- `MainWindowBase::suspendAudioOnStop()`, a virtual that `stop()` asks before it suspends
  the device, true by default as upstream behaves: Tony's is false on every platform, so
  the stream runs on between takes, and Tony suspends it itself once it has idled (on
  Android) or when Android sends it to the background
  ([recording.md](recording.md#latency)).

### svgui

- `ViewManager`: `checkPlayStatus` timer 500 ms → 20 ms, so the cursor keeps up with the
  live dots.
- `ViewManager::setRecordStartFrame()` / `getRecordStartFrame()`: while recording, the
  playback frame is this plus the recorded duration, not the duration alone. Without it a
  take recorded at P > 0 showed the cursor crawling from frame 0 and the pane scrolling
  away from the dots. `setRecordFrameRatio()` scales the duration, which the record
  target counts in the device's frames, to the timeline's: a phone at 48 kHz against a
  reference at 44.1 kHz. svcore's record target has no rate, so the application gives the
  ratio once the recording has started; the default of 1 changes nothing.
- `RegionLayer::PlotStrip` plot style: the coverage strip. Saved through the existing
  `plotStyle` attribute.
- `RegionLayer::PlotLyrics` plot style, after `PlotStrip` so saved numbers keep their
  meaning: the lyrics. Each region is a light box in one row along the bottom of the view
  just above `PlotStrip`'s 8 px, **exactly as long as the region** and never widened for
  its label: the box edges are the word times, and two words next to each other share the
  line between them. The label, dark text with a halo in the box's colour whatever the
  view's colours, is centred on the box and runs over its edges where it is longer. The
  static, pure `placeLyricsLabels()` places the labels (Tony's app suite tests it): one
  that would come closer than a small gap to the label before it is moved right, and the
  labels before it left, as little as will do but never so far that a label's middle
  leaves its box; one that cannot be goes in a second row above the boxes, and one with no
  room there either is left out (its box is still drawn). There is no gap between boxes
  to keep, so contiguous words whose labels fit share the first row. The first word of a
  line (where the value changes) is bold. The font (`getLyricsFontPixelSize()`) is twice
  the view's at the least, up to four times, and never more than an eighth of the view's
  height; it grows with the **square root** of the zoom, so that zooming in gives the
  words room (their boxes grow with the zoom itself). `setLyricsTextScale()` draws the
  words at a share of that: View > Lyrics Size (`LyricsSize`, 50 % by default on Android,
  where the desktop's size left room for only a few words); each new size is written to
  the log. No vertical scale, no feature description, and not editable by the pane's
  tools: Tony's `LyricsEditor` edits the model itself.
  `setHighlightFrame()` draws the region at that frame in amber (the latest to start, where
  regions overlap) and emits `layerParametersChanged()` only when that region changes: the
  highlight is painted into the view's cache, so each new word repaints the view, a few
  times a second at most, and only views listen to that signal, so nothing is marked
  modified. `getHighlightedEvent()` says which region it is. A highlighted word that was
  left out is drawn in the boxes' row, centred on its box, over the others for as long as
  it is highlighted, with an amber halo: the one being sung is the one the singer must be
  able to read. Where a label goes depends on the labels before it, so the
  layout is made for the **whole model** at once and cached per zoom level and font; a
  strip newly scrolled into sight then agrees with what is already on show. That is what
  lets the layer stay **scrollable**: `View::getNonScrollableFrontLayers()` treats every
  layer in front of a non-scrollable one as non-scrollable too, so the pitch tracks above
  the lyrics would repaint on every cursor update.
  The layout is made again, the highlighted region found again and the whole view
  repainted on **any** change to the model (member-pointer connections to `modelChanged`
  and `modelChangedWithin`): an edited word keeps the count of regions and often the
  extent of the whole, which with the zoom and the font is all the cache otherwise checks;
  a label that changes or moves can move the labels before it and the rows of those after,
  anywhere in the view; and the word being sung may be the one edited, or another one now.
  `getLyricsBoxRow(view)` says where the boxes' row was last painted in that view, empty
  before the first paint, for Tony's editor to tell whether the pointer is over a box. It
  is in the view's own **logical** coordinates, the ones a mouse event has: on a high-DPI
  screen the layer paints through a proxy at twice the size, so the row cannot be worked
  out again from the pane.
- `Pane::getTopFlexiNoteLayer()` skips dormant layers, so note tools cannot edit the
  notes of a take that is put away.
- `Pane::getIdentifyLayer()`: the layer whose feature under the mouse the pane describes
  (the box at top right) and illuminates, which upstream is always the top layer, dormant
  or not, and describing something or not. Now: the selected layer, or the one the tool
  in hand acts on; else the topmost layer on show with a note under the mouse
  (`FlexiNoteLayer::getNoteAt()`); else the topmost layer on show whose description is not
  empty. Without it, Tony's readout was the hidden pitch candidates' after any selection
  ("No local points" everywhere outside it) and the coverage strip's after a take
  (nothing at all). `FlexiNoteLayer`'s readout names a note over the box drawn for it;
  it used to from 4 px below its top to 8 px below its bottom.
- `FlexiNoteLayer::getAssociatedPitchModel()`, which the note tools set a note's pitch
  from, takes the pitch track with the same source model as the notes, and the first in
  the view only when there is none. With the reference first in the pane, an edited
  take's note otherwise took the reference's pitch.
- `Pane::setWorkModel()` / `getWorkModel()`: which model's extents are blocked off at the
  ends of the pane (a pale wash and a line), and whose duration, title and alignment are
  reported. The scan that chooses one now skips layers dormant in that pane. Tony's pane
  holds three audio models, and the pane used to block itself off at the end of whichever
  was topmost -- the take's file, which stops where the singing did, or the recording in
  progress, whose end crawls along behind the playback cursor. `MainWindow` names the
  reference instead.
- `Layer::setSavedInSession(false)`: `View::toXml()` leaves the layer out.
- `Layer::setCachedInView(false)`: `View` draws the layer, and every layer in front of it,
  at every paint instead of keeping it in its cache, and a change to its model leaves the
  cache alone. For the live dots ([recording.md](recording.md)).
- `View::paintEvent()` on a cache hit no longer has the cached layers draw into its buffer,
  where the cache then covered them. Upstream has done that since 2018, so the cache saved
  nothing and every paint, down to the play pointer's few pixels, drew every layer.
- Plot elements keep their size in logical pixels (for the Android port). `View` draws its
  layers at the whole pixel ratio (3 on a phone at 2.75), but `TimeValueLayer`'s points
  (2 px high) and `FlexiNoteLayer`'s notes (`NOTE_HEIGHT`) were sized in those physical
  pixels and pens scaled by only the square root of the ratio, so on a phone pitch and
  notes were a third of their size. Now `LayerGeometryProvider::scalePlotSize()` (logical
  px x ratio x plot scale, no font factor: unchanged at ratio 1 and scale 1) sizes them,
  the notes' hit areas use it too, and `ViewProxy::scalePenWidth()` scales by the whole
  ratio and the plot scale. `ViewManager::setPlotScale()` / `plotScaleChanged()` is Tony's
  View > Plot Size; each view drops its cache on a change. At ratio 1 and scale 1 a pane
  drew exactly as before. On a hi-DPI desktop (ratio 2) this doubles points and notes, and
  thickens the pens of every layer drawn through a `ViewProxy`. Not sized by it: the
  coverage strip and the lyrics (`scalePixelSize()`, which follows the font; the lyrics
  have a size of their own), and the waveform, spectrogram and time ruler, whose lines
  stay one physical pixel. A pen wider than a pixel leaves Qt's fast path: at ratio 1,
  150 % and 200 % double the pitch track's paint time.

### bqaudioio

The driver project ([audio-drivers.md](audio-drivers.md)), on the fork branch
`tony-customizations`, which upstream's `master` does not have:

- **An implementation per Windows host API.** `mme`, `directsound` and `wasapi` are
  PortAudio restricted to that host API: their device lists hold its devices only, and
  with no device named its own default devices are used. `port` is as upstream has it:
  every host API's devices, the first whose name matches, and PortAudio's default
  devices. The three are reported only on Windows and only where PortAudio has the host
  API; asking for them elsewhere would initialise PortAudio for nothing.
- **WASAPI converts rates.** A WASAPI device opens with `paWinWasapiAutoConvert`: in shared
  mode each side runs at its own mixer's rate, and the stream opens at the output's. The
  header comes from PortAudio (`pa_win_wasapi.h`, found with `__has_include`).
- **A settable latency.** `AudioFactory::setSuggestedLatency()` sets what both sides ask
  for, for the streams opened after; 0.2 s, upstream's fixed figure, when unset.

Only the Windows cross-compile (MinGW-w64 against PortAudio 19.7.0's headers) and the
user's PC see the Windows part; Linux builds none of it. The Android build has no
bqaudioio backend at all: its device is Tony's own `OboeAudioIO`, a `SystemAudioIO` as
bqaudioio's are, installed by `MainWindow::createAudioIO()`, so the port needed no change
here.

A `bqaudioio/` cloned from the mirror before the fork was pinned does not have the pin:
`git remote add jhhr https://github.com/jhhr/bqaudioio`, `git fetch jhhr`, then
`git checkout -B tony-customizations jhhr/tony-customizations`. `container-setup.sh`
does the equivalent by itself in a cloud session.

### pyin

On the fork branch `tony-customizations`, which upstream's `default` does not have:

- **Its tests run with Tony's.** They build as one executable on Boost.Test's
  header-only variant (`test/TestMain.cpp`), so they need nothing but the Boost headers
  the plugin needs already; upstream built four of its five test files as executables
  of their own, linked to Boost's library, and `TestYinUtil` (ported here to the current
  `YinUtil`) not at all. Tony's meson builds them as `test-pyin`, which `meson test` runs
  as `pyin` ([testing.md](testing.md)); `PYIN_SOURCE_DIR` says where `testdata/` and
  `test/` are.
- **The regression test needs no host.** `TestRegression` does in-process what
  `test/regression/regression.sh` did through sonic-annotator: the plugin, at its
  defaults, on `testdata/bob_02.wav`, its smoothed pitch track and notes with fixed-lag
  smoothing off and on, against `test/regression/expected-*.csv` to within their six
  significant digits. A change to what the analysis gives at its defaults fails it; a
  change meant to alter that brings new expected files with it, as upstream's did.
- **Parameters for vocals with harmonies, doubled parts and echoes**, as vocals separated
  from a mix have. On those pYIN's own analysis goes wrong in four ways: two or three
  voices together make YIN's deepest dip at the period they share, an octave or two below
  them all; its candidate rule (see the known defects) lets the dip at twice a rough low
  note's period win; the pitch HMM breaks the track wherever one frame has no candidate
  near it; and echo tails, as periodic as the voice, are tracked. Each parameter is 0 by
  default, which is pYIN 1.2 exactly (the plugin version stays 3):
  - `salmode` 1: the candidates come from a harmonic salience of the spectrum
    (`HarmonicSalience`), which peaks at each voice's own f0, its negative lobes between
    the harmonics keeping it off the octave above. A partial is a peak that stands out
    from the window's leakage of any stronger one near it, so that the skirt of a rumble
    or a kick below the voice is not taken for one; and a candidate's pitch is the median
    of what each of its harmonics' peaks says, as a louder voice's partial next to one of
    them would pull a single estimate off. YIN still runs: its probability that
    the frame is voiced is what the candidates share, by salience to the fourth power,
    with a floor from the salience's harmonicity, since YIN's aperiodicity stays high
    where several harmonic voices sound at once.
  - `candidaterule` 1: YIN's dips get their probabilities in order of period, mostly the
    first below each threshold (`YinUtil::yinProbOrdered()`). With `salmode` it only
    changes the probability that the frame is voiced.
  - `commonf0`: a salience candidate with two others at multiples of it and no partial of
    its own, the common fundamental of a fifth or a fourth, loses this share of its
    salience.
  - `levelfloor`: frames more than this many dB below the level of the last ten seconds'
    singing fade out of voicing over 6 dB (`LevelGate`): echo and reverb tails are
    quieter than what they repeat. The singing's level is that of the frames pYIN finds
    sung (a voiced probability of a quarter or more in all), so that rests and silence do
    not lower it; with none in the ten seconds, nothing fades.
  - `voicedfloor`: every voiced state's observation is at least this times an unvoiced
    state's, so that the track crosses a few frames with no candidate near it rather than
    breaking; such a frame keeps its state's pitch. Less in frames the level floor fades;
    without a level floor, less in frames quieter than 0.04 RMS (about -28 dBFS), down to
    none at 0.02, so that it bridges no rest.
  - `voicehold`: an unvoiced state's moves to another pitch cost 1 + this times more, so
    that after a gap the track comes back near the voice it left.
  - `voicefollow` (with `salmode` only): where two or three voices sing, the track follows
    one of them, 1 the lower, 2 the higher, 3 the middle, rather than whichever is the
    loudest from note to note (`VoiceFollower`). A second pitch HMM decodes the same
    frames, each with the candidates of the ten either side filled in, every candidate
    likely to be a voice of its own (`HarmonicSalience::voiceLikelihoods()`) made as
    probable as the strongest, then kept as far as no voice sings below it (above it,
    for the higher; for the middle, as far as it is the middle one of three). The track
    takes that path's pitch only inside the passages a gate finds: two seconds in which
    four fifths of the voiced frames have a second voice 1 to 9.5 semitones from the
    first path's, widened by a second each way. Its voicing is the first path's
    everywhere, so that a solo's echo or bleed below the voice is not taken for a lower
    voice, and a frame's pitch is that of its own candidate nearest the path, not of a
    neighbour's filled in. An entry comes out at most 2.6 s after its frame, so that a
    host still sees the track grow as the analysis runs.
  - `voicetracks` 1 (with `salmode`): three outputs more, `strongestvoice`, `lowervoice`
    and `highervoice`, the track as it would be following each, from the same run: a
    follower for each voice the main output does not follow already. On the first song
    the run took 23.1 s with them against 17.2 s without (following the lower voice, 23.9
    against 19.8), where pYIN's own takes 8.3.

  Tony sets them for the reference's analysis only ([architecture.md](architecture.md#who-owns-what)):
  `salmode` 1, `candidaterule` 1, `commonf0` 0.8, `levelfloor` 13, `voicedfloor` 0.2,
  `voicehold` 3, `voicefollow` 0 or 1 by Analysis > Voice to Follow, and `voicetracks` 1,
  from which a selection's pitch candidates and Prefer Lower and Higher Voice come. These
  values and the fixed tuning in the code were chosen in the pitch lab (`tools/pitchlab/`)
  on a test set of real and synthetic singing with harmonies, echo, reverb and separation
  damage, and checked on three songs' separated vocals annotated by hand: the share of
  sung frames within 50 cents of a sung voice went from 0.72 to 0.97 on the test set and
  from 0.24, 0.58 and 0.46 to 0.93, 0.96 and 0.96 on the songs, for about twice pYIN's
  time.
  `TestPYinVamp` checks them end to end on a fifth, a third, a period-doubled tone and an
  echo, each of which pYIN's own analysis gets wrong; `TestHarmonicSalience`,
  `TestLevelGate`, `TestMonoPitchHMM` and `TestYinUtil` check the parts.

  `voicefollow` was worked out in the same lab, on synthetic singing in two and three
  parts whose levels cross (`make_voicesets.py`), judged by the share of the frames where
  several sing that the track spends on the voice asked for, and its changes of voice a
  minute (`voice_follow.py`). Following the lower voice, the two songs with harmonies kept
  it in 0.92 and 0.91 of those frames, against 0.34 and 0.05 following the strongest, and
  changed voice 10 and 21 times a minute there rather than 51 and 77; the one-voice
  song's scores are the same, and the test set's share of sung frames on a voice is
  within 0.01. The higher keeps the upper of two voices less well (0.87 on the first
  song), and neither it nor the middle holds where three sing
  ([open-points.md](open-points.md)). It costs about a quarter more time (2.7 times
  pYIN's own) and 6 MB more memory. `TestVoiceFollower` checks the follower on
  candidates made up frame by frame, and `TestPYinVamp` a third whose levels cross, end
  to end.

A `pyin/` cloned from `c4dm` before the fork does not have the pin: `git remote add jhhr
https://github.com/jhhr/pyin`, `git fetch jhhr`, then `git checkout -B tony-customizations
jhhr/tony-customizations`. `container-setup.sh` moves `origin` to the fork by itself in a
cloud session.

## Known defects in the forks, not fixed

- `svapp/audio/AudioCallbackRecordTarget.cpp` connects to `SIGNAL(aboutToBeDeleted())`,
  which the model class does not have, so its `modelAboutToBeDeleted()` never runs, and
  every take logs "No such signal sv::WritableWaveFileModel::aboutToBeDeleted()", on any
  Qt: expected in test logs, not a new fault. The
  signal to use would be `Document::modelAboutToBeReleased(ModelId)`. Tony avoids the
  consequence by stopping the recording and releasing the model in a fixed order (see
  [recording.md](recording.md)); whether `m_model` can dangle otherwise was not looked into.
- The play-start callback is passed the frames actually got, not the requested block size.
- `View::removeLayer()` does not disconnect `layerMeasurementRectsChanged`.
- `svcore/base/PlayParameterRepository.cpp` keeps its play parameters in a `std::map` with
  no lock, which the audio fill thread reads (`AudioGenerator::mixModel()` through
  `getPlayParameters()`) while the GUI thread adds and removes playables. Seen once as a
  crash of `test-tony-app` in the fill thread, in a sharded run whose processes shared
  their settings; not seen otherwise.
- `svcore/base/RecentFiles.cpp` leaves a file in a temporary directory out of Open Recent,
  as a preference that is on by default asks, but knows such a directory only by a `tmp`
  or `temp` in its path. macOS's own, `/var/folders/…/T`, has neither, so a file opened
  from there goes into the list. Checking against `QDir::tempPath()` as well would cover
  it. The tests allow for it ([testing.md](testing.md), `settingsChangedByOpening()`).
- pYIN in fixed-lag mode (the default, and what Tony runs) stamps one frame of every run
  twice: the last frame `process()` emits comes again as the first of
  `getRemainingFeatures()`. `Analyser::mergeRangedAnalysis()` drops it; a fix in the fork
  would remove that workaround, and change the fixed-lag expected files.
- pYIN's `YinUtil::yinProb()` gives every dip of the difference function below 1 a
  share of the probability, where the published pYIN gives each threshold's share to the
  first dip below it (the paper's code is still in the function, commented out). So the
  sub-harmonic dips of a voice compete with its own period, and only the HMM's continuity
  keeps the octave. Left as it is for the defaults, which are pYIN 1.2; `candidaterule` 1
  gives the fork's ordered rule instead, which the reference's analysis uses.
- `YinUtil::fastDifference()` keeps its running power term one sample out (it brings in
  `x[tau + half]` where the window ends at `x[tau + half - 1]`), so the difference
  departs a little from YIN's; harmless for pitch at pYIN's window. Tony's live tracker
  does the same on purpose, to stay in step: `TestRealtimeYin` says so and allows for it.

## Changes considered but not made

- svgui's `SpectrogramLayer` keeps its display range in whole Hz (`int`, rounded in
  `setDisplayExtents()`), and Tony's vertical zoom is that range: near its narrowest it
  moves in steps, up to about 12 px at a major third about 110 Hz in a 300 px pane.
  `double` bounds, no rounding there, and `toDouble()` in `setProperties()` would smooth
  it.
- svgui's `TimeValueLayer::paint()` works out for every point what could be worked out
  once a paint (`getModelsEndFrame()`, font metrics, a pen and a brush): about 10 µs a
  dot on the cloud machine. On a core four or five times slower, the play pointer's
  strip paints during a take would take half of it or more, by that machine's figures;
  they coalesce, so that means fewer frames, not a lag.
- `Document::setModelSource()` (or any way to set or clear a derivation record) would
  replace `MainWindow::adoptTakeLayers()` setting source models by hand.
- A hook in `MainWindowBase::toXml()` would save `MainWindow::toXml()` buffering the whole
  document to insert `<takes>` and `<analysis>`.
