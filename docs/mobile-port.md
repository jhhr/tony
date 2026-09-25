# Porting Tony to a phone: Android or Sailfish OS

**Status: researched 2026-09-24 and 2026-09-25; nothing built.** This page and its two
platform pages hold what that research found, so that a session starting a port does not
have to find it again:

- [port-android.md](port-android.md): Android facts, the work, the first test port.
- [port-sailfish.md](port-sailfish.md): Sailfish OS (Jolla phones), the same.

Facts about Tony and its libraries below were read from the code and hold until the code
changes. Facts about the platforms come from the web and are dated. Anything marked
*(snippet)* was seen only in search results, because the proxy blocked the page. Check a
web fact again before a step depends on it.

## Decisions so far

- **Port the existing Qt Widgets app; do not rewrite it.** Every rewrite considered amounts
  to a new application and loses the `.ton` compatibility with the desktop (see
  [Rejected](#rejected-alternatives)).
- **On the phone the app is for practice**: open a session, choose a take, select a phrase,
  record, listen, erase, undo. Editing notes and the reference stays on the desktop.
- **No platform has been chosen.** In short:
  - Sailfish needs less new code: its audio backend, file access, pYIN loading and build
    are mostly there already.
  - Sailfish carries more risk: Qt 6 is community-packaged there, window-manager support
    for such apps only appeared in April 2026, and apps are routed to a high-latency audio
    path.
  - Android needs more code: an audio backend, file import and export, packaging.
  - Android's unknowns are amounts of work, not open questions.
- **Before either**, run [manual-checklist.md](manual-checklist.md) items 1 to 3 on the
  desktop. As of this research the latency compensation had never been checked by hand; do
  not add a phone's variability to an unverified mechanism.
- Each port starts with a **test port** that answers the platform's open questions before
  anything else is built. The platform pages describe it.

## Comparison by area

| Area | Android | Sailfish OS | Easier on |
| --- | --- | --- | --- |
| Qt 6 | Official Qt 6.11 for Android | System Qt is 5.6; community Qt 6.8.4 from the Chum repository | Android |
| Windows and dialogs | One full-screen window, dialogs inside it | Every window and dialog shown maximised, no title bar | Android |
| Build | NDK cross-compile, androiddeployqt, no meson precedent | Sailfish SDK builds RPMs; meson is in the platform | Sailfish |
| C libraries | All cross-compiled | Four in the OS, six to build | Sailfish |
| Audio backend | New Oboe backend in a bqaudioio fork | Existing PulseAudio backend | Sailfish |
| Latency | AAudio low-latency path; latency estimated from timestamps | Deep-buffer sink; fixed latency estimate from the HAL | Android |
| pYIN loading | Packaging change or svcore fork change | Unchanged | Sailfish |
| File access | `content://` URIs: copy in and out | Ordinary paths | Sailfish |
| Sharing with the desktop | Sync app plus "All files access", or the system picker | Syncthing, or rclone | Tie |
| Touch layout | Compact mode and gestures | The same, plus edge-swipe conflicts | Android |
| Distribution | Sideloaded APK | Sideloaded RPM or Chum; not the Jolla Store | Tie |
| Platform risk | Mainstream | Small, active ecosystem; Qt 6 is community-maintained | Android |

An Android APK would also run on a Jolla phone through Android App Support. Its audio goes
through the same PulseAudio, with unmeasured latency. See
[port-sailfish.md](port-sailfish.md#android-app-support).

## What in Tony and its libraries matters to any port

The library directories are separate repositories (see [forks.md](forks.md)). To read
them, clone `jhhr/svcore`, `jhhr/svgui` and `jhhr/svapp` (branch `tony-customizations`),
`jhhr/bqaudiostream`, `breakfastquay/bqaudioio` and `c4dm/pyin` from GitHub. That worked
from a cloud session.

### Qt 6 only

- svgui and `main/` use Qt 6-only API with no Qt 5 fallback:
  - `QMouseEvent::position()`: about 160 uses in 12 files.
  - `enterEvent(QEnterEvent *)` overrides: about 30 files.
- svcore has `QT_VERSION` guards around `QStringConverter`.
- A Qt 5.15 build would need a mechanical backport in the forks. Qt 5.6 is out of reach
  (`Qt::SkipEmptyParts`, `horizontalAdvance`, `QOverload` and more).
- Built with Qt 6.11 on the development machine. `main.cpp` guards only the colour-scheme
  call, at 6.8, so 6.8 is plausible but untried.

### Audio I/O

- **bqaudioio is upstream, not a fork.** It is on sourcehut (Mercurial), mirrored at
  `github.com/breakfastquay/bqaudioio`. `AudioFactory.cpp` knows JACK, PulseAudio and
  PortAudio only. A new backend, or any buffer change, means forking it by the procedure
  in [forks.md](forks.md) and adding it to `repoint-project.json`.
- `PortAudioIO.cpp` (about 760 lines) is the model for a new backend:
  - one duplex stream, input and output in the same callback;
  - input and output latency from the stream info, handed to `setSystemRecordLatency()`
    and `setSystemPlaybackLatency()`.
- `PulseAudioIO.cpp` (about 840 lines):
  - separate capture and playback streams on one `pa_mainloop` thread;
  - flags `PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE` and **no buffer
    attributes**, so the server's default buffering applies, which can be large;
  - latency from `pa_stream_get_latency()`.
- Tony's compensation ([recording.md](recording.md#latency)) is reported play latency plus
  reported record latency plus the measured start gap.
  - The start-gap measurement assumes the driver hands over a block's input before asking
    for its output. A duplex stream does that; two PulseAudio streams need not.
  - Wherever the reported latency is only an estimate, the compensation is only as good as
    the estimate.
- **There is no manual latency offset or calibration setting.** It is the fallback on any
  platform whose reported latency is wrong, and it would help the desktop too. Build it if a
  test port shows takes landing off the beat.
- Development has used PortAudio on Windows only. The PulseAudio recording path has never
  been exercised with takes.

### Sample rate

What the code does:
- `MainWindow` sets `Preferences` to resample on load, at a fixed 44100 Hz. So the
  reference, and every file opened, is a 44.1 kHz model.
- Neither side of the audio device asks for a rate:
  - `AudioCallbackPlaySource::getApplicationSampleRate()` and
    `AudioCallbackRecordTarget::getApplicationSampleRate()` both return 0.
  - Playback is resampled to whatever the device runs at.
- The backends then choose:
  - `PortAudioIO` opens at the output device's **default** rate;
  - `PulseAudioIO` asks PulseAudio for **44100**, which resamples to the hardware.
- Recordings are written at the device's rate.
- Take timing uses the main model's rate: `currentTakeTiming()` sets `TakeTiming::rate`
  from `getMainModel()`, and the pre-roll is computed the same way.
- `TakeAudio::splice()` does not resample. It refuses only a recording whose rate differs
  from the take so far.

So with a device that is not at 44.1 kHz, take audio would presumably be placed at the
wrong scale. Nothing verifies this; [takes.md](takes.md#known-limitations) calls a device
rate different from the reference's unexercised. It may already affect a Windows machine
whose default output device runs at 48 kHz.

Phones run at 48 kHz natively. **Check this on the desktop with a 48 kHz device before a
port.** The fixes:
- resample the recording to the main model's rate before the splice, in `tony_core`,
  which is testable;
- or open the device at 44.1 kHz (Oboe can convert, at some cost in latency).

### The pYIN plugin

- `meson.build` builds `pyin` and `chp` as shared libraries with `name_prefix: ''`, which
  gives `pyin.so` and `chp.so`.
- `setupTonyVampPath()` in `main.cpp`:
  - `TONY_VAMP_PATH` overrides the default;
  - otherwise, off Windows and macOS, `VAMP_PATH` is `<bindir>/../lib/<binary name>`,
    `<bindir>/../lib/<app name>` and `<bindir>`.
- `HAVE_PLUGIN_CHECKER_HELPER` is not defined, so svcore's `NativeVampPluginFactory`
  scans the `VAMP_PATH` directories for `*.so` itself and `dlopen`s them. No helper process
  runs; the `checker/` sources are compiled but not used for Vamp plugins.

### Files and sessions

- `FileSource` handles local files, `http`, `https` and `ftp`. Readers open paths:
  `WavFileReader` through `sf_open()`, bqaudiostream's WAV reader through `std::ifstream`.
  **A `content://` URI does not open.**
- Opening goes through svgui's `InteractiveFileFinder`, which runs its own `QFileDialog`
  instance and then checks the result with `QFileInfo`.
- **Sessions already move between machines:**
  - The reference is saved as an absolute `file=` path.
  - On load, `SVFileReader` calls `FileFinder::find()` with the session's location, and
    `InteractiveFileFinder::findRelative()` finds a file of the same name next to the
    `.ton`.
  - Take audio is in `<session>.takes/` with relative paths.
  - So `Song.ton`, `Song.mp3` and `Song.takes/` move together without prompts, provided the
    reference sits next to the `.ton`.
- Take files are uncompressed WAV, several MB per minute, which matters for syncing.

### The window

- `MainWindow` has 7 menus: File, Edit, View, Analysis, Takes, Playback, Help.
- Its toolbars are File, Tools, Playback, Play Mode, Playback Controls (speed and gain) and
  the bottom "Show and Play" bar.
- Size: about 190 `addAction` calls, about 47 shortcuts and about 88 message-box or dialog
  calls.
- `menuBar()->setNativeMenuBar(false)` sits under `#ifdef Q_OS_LINUX`. **Qt defines
  `Q_OS_LINUX` on Android too**, so on Android this forces the in-window menu bar instead
  of Qt's ⋮ options menu. Exclude `Q_OS_ANDROID` there if the ⋮ menu is wanted.
- **svgui has no touch or gesture code** (no `QGesture` or `QTouchEvent`).
  - Qt synthesises mouse events from unhandled touches. Tapping, one-finger drag to pan in
    Navigate mode (`Pane::dragTopLayer()`) and dragging in the selection strip (SelectMode
    through `setToolModeFor()`) should therefore work.
  - Pinch to zoom, two-finger scrolling and long-press for the right-button menu (the pane
    emits `rightButtonMenuRequested` on a right click) need adding in the svgui fork.
- `main.cpp`:
  - `--no-audio` selects `AUDIO_NONE`, which is useful for a first test port;
  - the window is sized from the screen;
  - the colour scheme is forced light.

### Build and tests

- `meson.build` has `linux`, `darwin` and `windows` branches.
  - The Linux branch (`if system == 'linux'`) lists the C libraries and their `HAVE_*`
    defines: fftw3, sndfile, samplerate, rubberband ≥ 3, sord/serd, oggz, fishsound, mad,
    id3tag, opusfile, optional opusenc, jack, libpulse, alsa and optional portaudio.
  - RtMidi uses the ALSA defines.
  - Qt modules: Core, Gui, Widgets, Xml, Network, Svg, Test.
- For a phone:
  - drop JACK, ALSA (and RtMidi's `__LINUX_ALSA*__` defines), oggz and fishsound;
  - keep the rest, or leave out a library's `HAVE_*` define where svcore allows it.
- `.github/workflows/linux.yml` is the model for a CI job: Ubuntu, apt packages, meson from
  a release tarball, Rubber Band from source, `./repoint install`.
- The test suites use `FakeAudioIO` and keep running on the desktop. A phone audio backend
  can only be tested on the phone.

## Work common to both ports

- **Compact touch mode.** Following the rules in [AGENTS.md](../AGENTS.md), it is a class of
  its own and `MainWindow` only wires it.
  - Landscape only.
  - One toolbar: Play, Record, Record into Selection, the Take box, Undo, Redo, Erase (the
    Ctrl+D action as a button), zoom.
  - The Show and Play toggles and gains in a slide-out panel.
  - Hidden: the note-editing tools, the audio device menus, perhaps the spectrogram.
- **Gestures in the svgui fork**: pinch zoom, two-finger scroll, long-press menu.
- **The latency calibration setting** described above, if a test port needs it.
- **The sample-rate check** described above.
- **Headphones.**
  - Wired or USB-C headphones with the phone's own microphone are the safe setup.
  - Bluetooth output adds a large, variable latency.
  - On Android, using a Bluetooth headset's microphone switches to call-quality audio.

## Rejected alternatives

- **A Qt Quick (QML) interface.** svgui's panes are `QWidget`s painted with `QPainter`,
  and `MainWindow` builds on svapp's `MainWindowBase`, a `QMainWindow`. It would be a new
  application.
- **A native Kotlin app calling pYIN through JNI.** It would feel best on a phone, but
  takes, undo and sessions would be rewritten, and Sonic Visualiser's session XML is hard to
  write from outside, so desktop compatibility would be lost. Months of work.
- **A web app, or Qt for WebAssembly.**
  - Up to Qt 6.11, microphone input sits on deprecated browser API.
  - An AudioWorklet backend landed on Qt's dev branch in May 2026 (QTBUG-115191),
    probably for 6.12.
  - It has the same interface problems, plus browser limits.
