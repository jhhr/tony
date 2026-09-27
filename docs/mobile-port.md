# Porting Tony to a phone: Android or Sailfish OS

**Status: the Android port is built (September 2026) and runs on the user's phone;
Sailfish OS was researched (2026-09-24 and 2026-09-25) and not built.** This page holds
the decisions, the facts about Tony's own code that any port depends on, and what was
built that a second port would share. The platform pages hold the rest:

- [port-android.md](port-android.md): Android facts, what was built for it, its known
  limitations.
- [port-sailfish.md](port-sailfish.md): Sailfish OS (Jolla phones): facts and a plan.

Facts about Tony and its libraries below were read from the code and hold until the code
changes. Facts about the platforms come from the web and are dated. Anything marked
*(snippet)* was seen only in search results, because the proxy blocked the page. Check a
web fact again before a step depends on it.

## Decisions

- **Port the existing Qt Widgets app; do not rewrite it.** Every rewrite considered amounts
  to a new application and loses the `.ton` compatibility with the desktop (see
  [Rejected](#rejected-alternatives)).
- **On the phone the app is for practice**: open a session, choose a take, select a phrase,
  record, listen, erase, undo. Editing notes and the reference stays on the desktop, and
  the compact layout hides their tools.
- **Android, not Sailfish OS** (the user's choice, 2026-09-25). The comparison it was made
  on:
  - Sailfish needs less new code: its audio backend, file access, pYIN loading and build
    are mostly there already.
  - Sailfish carries more risk: Qt 6 is community-packaged there, window-manager support
    for such apps only appeared in April 2026, and apps are routed to a high-latency audio
    path.
  - Android needs more code: an audio backend, file access, packaging.
  - Android's unknowns were amounts of work, not open questions.
- **Tony's code is built and tested on the desktop.** Android-only code sits behind
  `Q_OS_ANDROID`, or in files only the Android build compiles, and changes nothing on the
  desktop. What needs no phone (mapping a picked file to its path, the latency arithmetic,
  the zoom rules, PCM conversion, the song scroll bar's geometry) is plain functions in
  `tony_core`, tested there. The compact layout, the gestures and the menu style are built
  on every platform and tested on the desktop.
- **The audio backend is Tony's own**: `OboeAudioIO` in `main/`, which `MainWindow`
  installs by overriding `createAudioIO()` on Android, as the tests install `FakeAudioIO`.
  Not a backend in the bqaudioio fork, nor PortAudio with an Oboe host API; no Qt
  Multimedia ([port-android.md](port-android.md#audio)).
- **Gestures are an event filter in `main/`** (`TouchGestures`), not a change in the svgui
  fork.
- **Sessions live in ordinary folders on the phone**, which a sync app keeps in step with
  the desktop, and Tony opens and saves them in place with Android's All files access
  (decided with the user, 2026-09-26). No session bundle.
- **Where takes land is judged by measurement on each device**, not by the latency a
  platform reports: Calibrate Audio and the dev checks, run on the user's PC and on the
  phone alike ([manual-checklist.md](manual-checklist.md), sections 1 and 6).
- Each port starts with a **test port** that answers the platform's open questions before
  anything else is built. Android's, an APK without audio, passed the user's first phone
  test; the platform pages describe each.

## Comparison by area

The research's comparison. Where the Android port found otherwise, the Android column
says what was done.

| Area | Android | Sailfish OS | Easier on |
| --- | --- | --- | --- |
| Qt 6 | Official Qt 6.11 for Android (built from source) | System Qt is 5.6; community Qt 6.8.4 from the Chum repository | Android |
| Windows and dialogs | One full-screen window, dialogs inside it | Every window and dialog shown maximised, no title bar | Android |
| Build | NDK cross-compile with meson, androiddeployqt | Sailfish SDK builds RPMs; meson is in the platform | Sailfish |
| C libraries | All cross-compiled | Four in the OS, six to build | Sailfish |
| Audio backend | Oboe, in a class of Tony's own | Existing PulseAudio backend | Sailfish |
| Latency | AAudio low-latency path; latency estimated from timestamps | Deep-buffer sink; fixed latency estimate from the HAL | Android |
| pYIN loading | Packaging change, plugin links made at start | Unchanged | Sailfish |
| File access | Paths, with "All files access"; audio from a cloud app copied in | Ordinary paths | Sailfish |
| Sharing with the desktop | Sync app plus "All files access" | Syncthing, or rclone | Tie |
| Touch layout | Compact layout and gestures, built | The same, plus edge-swipe conflicts | Android |
| Distribution | Sideloaded APK | Sideloaded RPM or Chum; not the Jolla Store | Tie |
| Platform risk | Mainstream | Small, active ecosystem; Qt 6 is community-maintained | Android |

An Android APK would also run on a Jolla phone through Android App Support. Its audio goes
through the same PulseAudio, with unmeasured latency. See
[port-sailfish.md](port-sailfish.md#android-app-support).

## What in Tony and its libraries matters to any port

The library directories are separate repositories ([forks.md](forks.md)); in a cloud
session `deploy/linux/container-setup.sh` checks them out at their pins.

### Qt 6 only

- svgui and `main/` use Qt 6-only API with no Qt 5 fallback:
  - `QMouseEvent::position()`: about 160 uses in 12 files.
  - `enterEvent(QEnterEvent *)` overrides: about 30 files.
- svcore has `QT_VERSION` guards around `QStringConverter`.
- A Qt 5.15 build would need a mechanical backport in the forks. Qt 5.6 is out of reach
  (`Qt::SkipEmptyParts`, `horizontalAdvance`, `QOverload` and more).
- Built with Qt 6.11 (the development machine, cloud sessions, and Android's 6.11.2) and
  with Ubuntu 24.04's 6.4 in the Linux CI. `main.cpp` guards only the colour-scheme call,
  at 6.8, so 6.8 is plausible but untried.

### Audio I/O

- **bqaudioio is the fork `jhhr/bqaudioio`** since the driver project
  ([audio-drivers.md](audio-drivers.md)); upstream is on sourcehut (Mercurial), mirrored
  at `github.com/breakfastquay/bqaudioio`. `AudioFactory.cpp` knows JACK, PulseAudio and
  PortAudio only (on Windows, PortAudio also restricted to one host API). Android's
  backend is not in it: `OboeAudioIO` is in `main/`.
- `PortAudioIO.cpp` (about 760 lines) was the model for `OboeAudioIO`:
  - one duplex stream, input and output in the same callback;
  - input and output latency from the stream info, handed to `setSystemRecordLatency()`
    and `setSystemPlaybackLatency()`.
- `PulseAudioIO.cpp` (about 840 lines):
  - separate capture and playback streams on one `pa_mainloop` thread;
  - flags `PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE` and **no buffer
    attributes**, so the server's default buffering applies, which can be large;
  - latency from `pa_stream_get_latency()`.
- Tony's compensation ([recording.md](recording.md#latency)) is reported play latency plus
  reported record latency plus the measured start gap, or a measured round trip plus the
  start gap.
  - The start-gap measurement assumes the driver hands over a block's input before asking
    for its output. A duplex stream does that, `OboeAudioIO`'s included; two PulseAudio
    streams need not.
  - Wherever the reported latency is only an estimate, the compensation is only as good as
    the estimate. On the phone Oboe's reported figures moved by several ms from one start
    to the next.
- **Calibrate Audio** measures the round trip through an earcup held to the microphone and
  keeps it per driver and devices; on Android per route, since a Bluetooth route is 100
  to 200 ms longer than the speaker's ([calibrate-audio.md](calibrate-audio.md)). It is
  the fallback on any platform whose reported latency is wrong.
- The stream is kept running between takes on the desktop and on Android; on the phone it
  is suspended after two idle minutes and when Tony goes to the background
  ([recording.md](recording.md#latency)).
- Takes have been recorded through PortAudio (Windows: MME and WASAPI) and through Oboe
  (the user's phone). The PulseAudio recording path has never been exercised with takes.

### Sample rate

What the code does:
- `MainWindow` sets `Preferences` to resample on load, at a fixed 44100 Hz. So the
  reference, and every file opened, is a 44.1 kHz model.
- Neither side of the audio device asks for a rate: the play source and the record target
  both answer 0 to `getApplicationSampleRate()`, and playback is resampled to whatever the
  device runs at. The backends choose:
  - `PortAudioIO` opens at the output device's **default** rate;
  - `PulseAudioIO` asks PulseAudio for **44100**, which resamples to the hardware;
  - `OboeAudioIO` opens at the device's own rate (48 kHz on the user's phone), the input at
    the output's.
- Recordings are written at the device's rate. `SingingTakes::spliceRecording()` converts
  one at another rate to the reference's (`TakeAudio::resample()`) before the splice, so a
  take's WAV is always at the reference's rate.
- `TakeTiming` converts between the device's frames (the latency, the frames received,
  the live tracker's frames; `recordRate`) and the reference's (`rate`).
- While recording, the play cursor is where the take started plus the recorded duration
  times the ratio `record()` gives `ViewManager::setRecordFrameRatio()` (svgui fork) once
  the take's rate is known. So the cursor, and the lyrics highlight that follows it, keep
  to the reference's rate.

Tested with `FakeAudioIO` at 48 kHz against a 44.1 kHz reference, and seen on the phone.
The whole of it is in [recording.md](recording.md#a-device-at-another-rate). Left: a
singing track loaded at another rate ([takes.md](takes.md#known-limitations)), and
bqaudioio's resampler holding playback back about 1.1 ms at 48 kHz, unreported.

### The pYIN plugin

- `meson.build` builds `pyin` and `chp` as shared libraries with `name_prefix: ''`, which
  gives `pyin.so` and `chp.so`.
- `setupTonyVampPath()` in `main.cpp`:
  - `TONY_VAMP_PATH` overrides the default;
  - otherwise, off Windows, macOS and Android, `VAMP_PATH` is
    `<bindir>/../lib/<binary name>`, `<bindir>/../lib/<app name>` and `<bindir>`;
  - on Android it is a folder of links, remade at each start
    ([port-android.md](port-android.md#the-pyin-plugin)).
- `HAVE_PLUGIN_CHECKER_HELPER` is not defined, so svcore's `NativeVampPluginFactory`
  scans the `VAMP_PATH` directories for `*.so` itself and `dlopen`s them. No helper process
  runs; the `checker/` sources are compiled but not used for Vamp plugins.

### Files and sessions

- `FileSource` handles local files, `http`, `https` and `ftp`. Readers open paths:
  `WavFileReader` through `sf_open()`, bqaudiostream's WAV reader through `std::ifstream`.
  **A `content://` URI does not open.**
- Opening goes through svgui's `InteractiveFileFinder`, which runs its own `QFileDialog`
  instance and then checks the result with `QFileInfo`. On Android `MainWindow` overrides
  `getOpenFileName()` and `getSaveFileName()` for sessions and audio, and hands on a path;
  layers and exports still go through svgui's dialog.
- **Sessions already move between machines:**
  - The reference is saved as an absolute `file=` path.
  - On load, `SVFileReader` calls `FileFinder::find()` with the session's location, and
    `InteractiveFileFinder::findRelative()` finds a file of the same name next to the
    `.ton`.
  - Take audio is in `<session>.takes/` with relative paths.
  - So `Song.ton`, `Song.mp3` and `Song.takes/` move together without prompts, provided the
    reference sits next to the `.ton`. A desktop session with a Windows path, in a folder
    a sync app kept on the phone, opened so.
- Take files are uncompressed WAV, several MB per minute, which matters for syncing.

### The window

- `MainWindow` has 7 menus: File, Edit, View, Analysis, Takes, Playback, Help.
- Its toolbars are File, Tools, Playback, Play Mode, Playback Controls (speed and gain) and
  the bottom "Show and Play" bar.
- `menuBar()->setNativeMenuBar(false)` sits under `#ifdef Q_OS_LINUX`, and **Qt defines
  `Q_OS_LINUX` on Android too**, so the menu bar stays in the window. That is kept on
  purpose: the native one would be an options (⋮) menu in an action bar that Android adds
  above the window, taller than the bar it replaces. The compact layout hides it anyway.
- **svgui has no touch or gesture code** (no `QGesture` or `QTouchEvent`). Qt synthesises
  mouse events from touches nothing accepts, so tapping, one-finger drag to pan in
  Navigate mode (`Pane::dragTopLayer()`) and dragging in the selection strip (SelectMode
  through `setToolModeFor()`) work as they are; on the phone they do.
- `main.cpp`:
  - `--no-audio` selects `AUDIO_NONE`, `--compact` the compact layout;
  - the window is sized from the screen;
  - the colour scheme is forced light (Tony's icons are black), on a phone as well.

### Build and tests

- `meson.build` has `linux`, `darwin`, `windows` and `android` branches.
  - The Linux branch (`if system == 'linux'`) lists the C libraries and their `HAVE_*`
    defines: fftw3, sndfile, samplerate, rubberband ≥ 3, sord/serd, oggz, fishsound, mad,
    id3tag, opusfile, optional opusenc, jack, libpulse, alsa and optional portaudio.
  - RtMidi uses the ALSA defines.
  - Qt modules: Core, Gui, Widgets, Xml, Network, Svg, Test.
  - The Android branch is the Linux one less JACK, PulseAudio, ALSA (and RtMidi's
    `__LINUX_ALSA*__` defines), PortAudio, oggz and fishsound, with Opus read-only, every
    library static, and Oboe. Tony is built as the shared library
    `libTony_arm64-v8a.so` that Qt for Android loads, and the test executables are left
    out. How to build it, and why so: [building.md](building.md#building-for-android).
- `.github/workflows/android.yml` builds the APK on every push, as a cloud session does.
- The test suites use `FakeAudioIO` and run on the desktop only. `OboeAudioIO`,
  `AndroidStorage`, `AndroidMediaReadStream` and `AndroidScreen` are compiled for Android
  only and can be judged on the phone only; their plain parts (`StreamLatency`,
  `AndroidFiles`, `DecodedPcm`, `AudioRoute`) are in `tony_core`, tested on the desktop,
  and the fake plays a phone's device
  ([testing.md](testing.md#the-phone-on-the-desktop)).

## What was built that any phone port shares

### The compact layout

`CompactLayout`, which `MainWindow::setupCompactLayout()` hands the window's own actions
and widgets. `main()` switches it on before the window is shown on Android, so a phone
never shows the desktop layout; on the desktop View > Compact Layout and `--compact` switch
it, which is how it is tested. It is not kept in the settings.

- One toolbar of large buttons (40 dp icons) in place of the menu bar and every other
  toolbar: a menu button whose popup holds every menu of the menu bar, Play, Record and
  Record into Selection, the take box, Undo and Redo, Erase (the Ctrl+D action), Zoom In
  and Zoom Out, and a button that shows and hides the two bottom toolbars (Playback
  Controls and Show and Play).
- The menus are in the popup rather than left out because a shortcut needs a visible
  widget holding its action or its menu: with only the hidden menu bar holding them, every
  shortcut that is only in a menu would stop working.
- Hidden while compact: the Navigate and Note Edit tools (Navigate is selected first), the
  audio device menus (and on Windows the driver and latency menus), the overview (the song
  scroll bar takes its place) and the menus' tear-off handles, which a finger catches.
- Kept: the status bar, for the pre-roll countdown and the sung note; the Edit menu's and
  the long-press menu's pitch and note actions (only the tool modes go); the spectrogram's
  toggle, in the bottom toolbars.
- Switching off puts back exactly what switching on changed, except the tool mode, which
  stays Navigate.
- The toolbar is about 700 dp wide: on a screen narrower than that in landscape, its last
  buttons go into the toolbar's » menu.
- Landscape only: the Android manifest's `sensorLandscape`.

### Touch

`TouchGestures`, an event filter that `MainWindow::paneAdded()` gives each pane. Why the
pane subscribes to a gesture that never happens, and how Qt decides which touches become
mouse events, is in `TouchGestures.cpp`.

- **One finger is left to Qt's mouse events**, as above. The press is held back until the
  finger moves, lifts or has rested 500 ms, so a long press or a second finger leaves no
  selection, drag or playhead move behind; then the pane gets what was held, in order.
- **A long press** (500 ms) opens the pane's right-button menu under the finger. That
  finger's events are kept from the menu until it lifts, so the menu waits for a tap:
  QMenu takes the lift of a finger that moved over an item as a choice, and its first item
  is Undo.
- **Two fingers** zoom each axis by their spread along it and scroll it as they move. An
  axis moves only once the fingers plainly move along it (`PinchZoom::AxisMovement`: past
  a dead zone, and at least half as far along it as across), so a pinch across the pane
  leaves the pitch range alone and one up the pane the time zoom. The time axis keeps to
  the wheel's zoom steps and limits (`PinchZoom`).
- **The vertical range** is the frequency range of the reference's pane, which View > Edit
  Display Extents sets: that of the reference analyser's dormant spectrogram (log, 40 to
  1500 Hz at first), to which the pitch and note layers align. Two fingers keep it between
  A0 and C8 and at least four semitones wide (`VerticalZoom`). A change makes no undo step
  and does not mark the session modified; it is saved with the session, and a new
  reference resets it.
- **The vertical zoom keeps the pitch in view.** A pinch zooms about the middle of the
  pitch on show in the pane's time range (the reference's and the singing's pitch tracks
  and notes, those not hidden), half way from the lowest to the highest less a twentieth
  at each end, so that an octave jump or a breath does not move it; and it brings that
  middle towards the middle of the pane as it zooms in, so that a low voice stays in view
  until it fills most of the pane. With no pitch on show it zooms about the fingers. The
  median was rejected: it centres a skewed phrase badly. The alternate pitch track, other
  takes' layers and the live dots are not counted.

### Sizes of what the panes draw

- **Plot Size** (`PlotSize`, View > Plot Size): the pitch tracks, the live dots and the
  notes at 100, 150 or 200 %, 150 % by default on Android and 100 % elsewhere. The svgui
  fork draws them in logical pixels at any pixel ratio (the view manager's plot scale,
  [forks.md](forks.md)): before, on a phone (ratio 2.75, painted at 3) a pitch point and a
  note were a third of their desktop height. At ratio 1 and 100 % the desktop draws
  exactly as before; a hi-DPI desktop's points and notes grew too.
- **Lyrics Size** (`LyricsSize`, View > Lyrics Size): the words at 35 to 100 % of the size
  svgui gives them, 50 % by default on Android, where the whole size left room for three
  or four words in the pane.

### The song scroll bar

In the compact layout the overview, 60 px of a phone's height, gives way to
`SongScrollBar`, a 24 dp strip in its place (its arithmetic is `SongScroll`, in
`tony_core`): the whole song, with a faint contour of the reference's pitch so that the
sung parts can be told from the rests; a thumb, at least 16 dp wide, for what the panes
show, the rest dimmed; and the playhead.

- A drag on the thumb moves the panes by as much; a press elsewhere centres them there,
  and a drag carries on from it. Through the view manager, so every pane follows. The
  playhead is left where it is (the simpler option).
- The thumb follows scrolling, zoom and playback's paging.
- The song's extent is the reference's audio; nothing of the singing or the takes is drawn
  (the simpler option).
- On the phone the user judged its size right.

### Menus and popups within the safe area

`TouchMenuStyle`, the application's style on Android only (`main()` installs it before any
widget is made), with the arithmetic in `PopupArea`:

- A menu taller than the screen scrolls in one column, where Qt's styles lay it out in
  columns that run off a phone's screen. Its scroll arrows are tall enough for a finger; a
  finger dragged on it scrolls it a row at a time, and the lift that ends a drag chooses
  nothing.
- Qt places a popup within the screen's available geometry, which on Android takes in the
  system bars (from Android 15 an app draws edge to edge, under them), and a tap on a menu
  item under the status bar goes to the status bar. So menus and combo box lists keep
  inside the main window's safe area margins, and a finger's width (8 mm) from the
  screen's top and bottom, where a tap hardly registers.

### Dialogs

- Calibrate Audio's dialog fits the window less its safe area, with buttons a finger
  high, a smaller font and one-finger scrolling on Android, and collapses to an indicator
  in the status bar while a check runs ([calibrate-audio.md](calibrate-audio.md)).
- The other dialogs are Qt's, as on the desktop. In a window the size of the user's phone
  (923 x 411 dp less its safe area: 817 x 387) with the compact layout, at fonts of 12, 15
  and 17 px, these fit: message boxes, the take name, Open Location, Edit Display Extents,
  the lyrics' word and shift dialogs. What's New does not (its minimum of 520 x 450 grows
  with the font, to about 624 x 540), nor About at 17 px (537 x 393), and the Key and
  Mouse Reference has no parent, so Qt places it. No cause common to them was found. Tony
  has no Preferences dialog.

### Headphones

- Wired or USB-C headphones with the phone's own microphone are the safe setup.
- Bluetooth output adds a large latency (100 to 200 ms), which Calibrate Audio measures per
  route.
- On Android, using a Bluetooth headset's microphone switches to call-quality audio; Tony
  does not ask for it, and records from the phone's microphone while Bluetooth plays.

## What stays open

In [open-points.md](open-points.md): the dialogs that do not fit a phone, for the user to
decide; nothing that follows the pitch vertically during playback, and no "fit the pitch"
action; and under "Android and touch", among others, no touch margin around the song
scroll bar's thumb, the GUI thread's load during takes on the phone, and touch on a
Windows touch screen, untried. What is left to try on the phone is in
[manual-checklist.md](manual-checklist.md), section 6.

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
- **The phone's audio in the bqaudioio fork, or PortAudio with an Oboe host API.**
  `createAudioIO()` is virtual in svapp's `MainWindowBase`, so a class in `main/` does it
  with no fork change; PortAudio's Android host API is an unmerged pull request.
- **An Android-only `CMakeLists.txt`.** It would copy `meson.build`'s source lists, to be
  kept in step; meson with an NDK cross file worked at the first attempt.
- **A session bundle** (a zip to carry a session through the picker). Sessions stay in the
  phone's storage, synced, and open in place.
- **Qt's native options menu** in place of the in-window menu bar (see
  [The window](#the-window)).
