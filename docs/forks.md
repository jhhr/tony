# Dependency forks

Tony's libraries are separate repositories checked out into the top-level directories by
[repoint](../repoint-project.json) and **gitignored in this repository**. Four of them are
forks under `github.com/jhhr` that exist only for this Tony fork:

| Directory | Fork branch | Why it is forked |
| --- | --- | --- |
| `svcore/` | `jhhr/svcore` `tony-customizations` | Qt 6.11 build fixes only. No behaviour change. |
| `svgui/` | `jhhr/svgui` `tony-customizations` | See below. |
| `svapp/` | `jhhr/svapp` `tony-customizations` | See below. |
| `bqaudiostream/` | `jhhr/bqaudiostream` `master` | `<shobjidl.h>` instead of `<shobjidl_core.h>` under MinGW, needed for `-DHAVE_MEDIAFOUNDATION`. |

`pyin/` and the rest are upstream and must stay untouched. `bqaudioio/` too, for now: a
fork of it, `jhhr/bqaudioio`, was created on 2026-09-26 for the lower-latency driver work
([open-points.md](open-points.md)), and the checkout has it as the remote `jhhr`, but
`repoint-project.json` still takes bqaudioio from sourcehut and nothing is pinned to the
fork. It joins the table when that work first pins it.

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
   upstream sonic-visualiser — do not push there. In a cloud session the checkouts are
   `container-setup.sh`'s, whose `origin` is the fork, and two checks stand in the way:
   - The session's git proxy refuses a push to a repository not attached to the session,
     a new branch included (HTTP 403). The session's add-repository tool attaches it, with
     push access.
   - Auto mode trusts only the repository the session started in and its remotes, and so
     blocks committing in a fork's checkout, attaching the fork and pushing to it. The
     environment's setup script names the four forks as trusted as well, which the user
     chose ([building.md](building.md#building-on-linux)); `claude auto-mode config`
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
out: CI and anyone else's checkout get exactly what the lock file says.

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
  (upstream: a no-op); `getSystemRecordLatency()`; `getFramesReceived()`.
- `AudioCallbackPlaySource`: `setPlayStartCallback(std::function<void(int)>)`, run from
  the audio callback with the first block after `play()` (start-gap measurement);
  `getModels()` for tests; `removeModel()` tolerates a model already gone.
  `AudioGenerator::removeModel()`/`clearModels()` also delete the continuous synth.
- `SVFileReader` restores the `start` attribute of wave file models (upstream wrote it and
  never read it). Only older `.ton` files need it now.

### svgui

- `ViewManager`: `checkPlayStatus` timer 500 ms → 20 ms, so the cursor keeps up with the
  live dots.
- `ViewManager::setRecordStartFrame()` / `getRecordStartFrame()`: while recording, the
  playback frame is this plus the recorded duration, not the duration alone. Without it a
  take recorded at P > 0 showed the cursor crawling from frame 0 and the pane scrolling
  away from the dots.
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
  words room (their boxes grow with the zoom itself). No vertical scale, no feature
  description, and not editable by the pane's tools: Tony's `LyricsEditor` edits the
  model itself.
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

## Changes that would tidy Tony up but were not made

- `Document::setModelSource()` (or any way to set or clear a derivation record) would
  replace `MainWindow::adoptTakeLayers()` setting source models by hand.
- A hook in `MainWindowBase::toXml()` would save `MainWindow::toXml()` buffering the whole
  document to insert `<takes>`.
- Fixing pYIN's doubled frame in fixed-lag mode would remove the workaround in
  `Analyser::mergeRangedAnalysis()`; `pyin` is not a fork today.
