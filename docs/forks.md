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

`pyin/` and the rest are upstream and must stay untouched.

## Changing a fork

The forks are free to change when Tony needs it; prefer a small, general addition to the
library over a workaround in `main/`.

1. Edit and commit inside the library's directory (it is its own git repository, on the
   fork branch). Commit messages there follow that repository's style: `area: what`.
2. Push to the remote named **`jhhr`**. In `svcore`, `svgui` and `svapp`, `origin` is
   upstream sonic-visualiser — do not push there.
3. Put the new commit hash in `repoint-lock.json` as that library's `pin`, and commit that
   in Tony together with the code that needs it.
4. A sub-agent that was told to work only in `main/` does not edit a fork: it reports
   exactly which change it needs, and the lead session makes it.

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
- `Pane::getTopFlexiNoteLayer()` skips dormant layers, so note tools cannot edit the
  notes of a take that is put away.
- `Layer::setSavedInSession(false)`: `View::toXml()` leaves the layer out.

## Known defects in the forks, not fixed

- `svapp/audio/AudioCallbackRecordTarget.cpp` connects to `SIGNAL(aboutToBeDeleted())`,
  which the model class does not have, so its `modelAboutToBeDeleted()` never runs. The
  signal to use would be `Document::modelAboutToBeReleased(ModelId)`. Tony avoids the
  consequence by stopping the recording and releasing the model in a fixed order (see
  [recording.md](recording.md)); whether `m_model` can dangle otherwise was not looked into.
- The play-start callback is passed the frames actually got, not the requested block size.
- `View::removeLayer()` does not disconnect `layerMeasurementRectsChanged`.

## Changes that would tidy Tony up but were not made

- `Document::setModelSource()` (or any way to set or clear a derivation record) would
  replace `MainWindow::adoptTakeLayers()` setting source models by hand.
- A hook in `MainWindowBase::toXml()` would save `MainWindow::toXml()` buffering the whole
  document to insert `<takes>`.
- Fixing pYIN's doubled frame in fixed-lag mode would remove the workaround in
  `Analyser::mergeRangedAnalysis()`; `pyin` is not a fork today.
