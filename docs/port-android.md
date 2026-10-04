# Android port: facts and what was built

Researched 2026-09-24. Built from 2026-09-25, starting with a test port (an APK without
audio), and tried on the user's Pixel 9a at each step. Read [mobile-port.md](mobile-port.md)
first: it has the decisions, the facts about Tony's own code that any port depends on, and
what was built that any phone port shares (the compact layout, touch, sizes, the song
scroll bar, menus and dialogs). How to build, and why the build is as it is, is in
[building.md](building.md#building-for-android); how the phone's code is tested on the
desktop, in [testing.md](testing.md#the-phone-on-the-desktop); what is left to try on
the phone, in [manual-checklist.md](manual-checklist.md), section 6.

The platform facts are the research's, dated 2026-09-24. **Confirmed** or **Disproved**
marks what the port settled, unmarked ones it did not test; *(snippet)* marks a fact seen
only in search results.

## Platform facts

### Qt for Android

- **Versions, from Qt's `macros.qdocconf`:**

  | Qt | NDK | JDK | Android API | Build tools, Gradle / AGP |
  | --- | --- | --- | --- | --- |
  | 6.11 | r27c (27.2.12479018) | 21 | min 28 (Android 9), target 36 | 36.0.0, 9.3.1 / 9.0.0 |
  | 6.8 | r26b, r27c | 17 | 28 to 35 | not noted |
  | 6.12 (dev) | r27c | not noted | 28 to 36 | Gradle 9.5.1 |

  **Confirmed** for 6.11: the port uses Qt 6.11.2 with NDK r27c, JDK 21, API 28 to 36 and
  build tools 36.0.0. Qt 6.12 LTS was due around 2026-09-22; whether it has shipped was
  not confirmed.
- **Qt Widgets are supported, but QML is the expected route.** Qt's Android page describes
  Widgets as something to add "if needed". There is no official list of Widgets
  limitations. From the Android platform plugin's source:
  - `QMenuBar` becomes the Android options (⋮) menu (`QAndroidPlatformMenuBar`) unless
    `setNativeMenuBar(false)` is called. Tony calls it under `Q_OS_LINUX`, which Android
    also defines, and keeps the bar in the window
    ([mobile-port.md](mobile-port.md#the-window)).
  - `QFileDialog` is always the native system picker. **Confirmed.** The picker is no Qt
    widget: `QApplication::activeModalWidget()` does not see it, while the nested event
    loop it runs in shows in `QThread::loopLevel()`, which is how the dev checks and the
    save on suspend tell that a dialog is up.
  - `QMessageBox` is native only with the environment variable
    `QT_USE_ANDROID_NATIVE_DIALOGS=1`; otherwise Qt draws it. **Confirmed** in Qt 6.11.2's
    `qandroidplatformtheme.cpp`; Tony does not set it. Android's own box cannot be closed
    from code (its helper's `hide()` does not end `exec()`), so a box Tony closes itself
    says `DontUseNativeDialog` whatever the environment.
  - Widgets use the "android" style plugin or fall back to Fusion.
  - The back key is a navigation request, not a close, and `closeEvent()` does not run.
    Since Qt 6.12 it sends the app to the background.
  - There is no hover, so tooltips and hover states are lost.
  - Kinetic scrolling of item views needs `QScroller`, reported janky *(snippet)*.
    Calibrate Audio's texts scroll by one finger through it.
  - High-DPI scaling is always on in Qt 6. **Confirmed**: Qt's logical pixels are
    Android's dp (about 160 to the inch). The user's phone has a device pixel ratio of
    2.75, which svgui's panes paint at 3, and is 923 x 411 dp in landscape, with safe area
    margins of 58, 24, 48 and 0 dp (left, top, right, bottom).
- **16 KB page alignment** matters for Google Play, and on a device whose kernel uses
  16 KB pages (Android 15 and later can):
  - NDK r28 and later align native libraries by default; with r27 add
    `-Wl,-z,max-page-size=16384`. The port does, and `build-apk.sh` checks the APK's
    alignment.
  - Qt fixed its own libraries in 6.8.6, 6.9.3 and 6.10.0.

### Build and packaging

- **Packaging is done by androiddeployqt.** It reads
  `android-<target>-deployment-settings.json`. CMake and qmake write that file, and Qt's
  documentation says not to edit it by hand.
- **Without CMake, the file must be written by the build.** The pcons build tool does this
  (`examples/79_qt_android_apk`, `tests/toolchains/test_qt_android_settings.py`).
  - androiddeployqt will not start without eight keys: `qt`, `sdk`, `ndk`, `ndk-host`,
    `architectures`, `application-binary`, `toolchain-prefix`, `stdcpp-path`.
  - The application must be a shared library named like `lib<app>_arm64-v8a.so`, not an
    executable.
  - **Confirmed**: `deploy/android/build-apk.sh` writes the file for the meson build, and
    Tony is `libTony_arm64-v8a.so`, exporting `main`.
- **Meson:**
  - No public example of a meson-built Qt Android app was found.
  - meson's Qt 6 module has trouble finding the host `moc` and `rcc` when
    cross-compiling (meson issue 13018, open; issue 6089, the build machine's qmake picked
    up). **Did not arise**: meson 1.3.2 finds Qt for Android through its `qmake`, named in
    a second cross file (`deploy/android/qt-arm64-v8a.ini`), and takes `moc`, `rcc` and
    `uic` from the host Qt that `qmake` names.
  - Mesa's Android documentation shows the usual NDK cross-file.
- **Two routes were open:** (a) meson with an NDK cross file and host Qt tools, a script
  that writes the deployment JSON, then androiddeployqt; (b) a `CMakeLists.txt` for
  Android only. **Route (a) was taken** and worked at the first attempt.
- **Libraries:**
  - libsndfile documents Android builds (`Building-for-Android.md`, autotools or CMake),
    without its codec libraries. **Confirmed**, and so it reads no FLAC or Ogg there.
  - The others (libsamplerate, fftw3, Rubber Band, opusfile, serd/sord, mad/id3tag) are
    cross-compiled, as static libraries, by `deploy/android/build-deps.sh`. It turned out
    that bzip2 (svcore includes `bzlib.h` unconditionally), Boost's headers (pYIN), zix
    (sord) and Oboe were needed too.
  - JACK, PulseAudio, ALSA, PortAudio, oggz and fishsound are left out.
- **Built on Linux, not on the MSYS2 machine**, which has no Android toolchain: in a cloud
  session, and by `.github/workflows/android.yml` for every pull request and every merge
  into `default`, which keeps the APK as the run's artifact `Tony-debug-apk`
  ([building.md](building.md#building-for-android)).

### Audio

- **Do not use Qt Multimedia for this.** Tony does not.
  - Up to Qt 6.9, `QAudioSource` and `QAudioSink` used OpenSL ES. From 6.10 they use AAudio
    (QTBUG-132951).
  - Output streams are low-latency, with a three-burst buffer.
  - Input is deliberately opened with `AAUDIO_PERFORMANCE_MODE_NONE`, because some devices
    limit how many low-latency streams can be open. No input preset is set yet.
  - There is no latency or timestamp API.
  - Google measured 205 ms round trip without low-latency mode, so Qt's input path may be
    slow. That is an inference, not measured.
- **Google recommends Oboe.** Tony uses Oboe 1.11.0.
  - Oboe uses AAudio on API 27 and later and OpenSL ES below that. OpenSL ES is "not
    recommended for new designs".
  - `oboe::FullDuplexStream` gives input and output in one callback, which is what the
    start-gap measurement assumes (see [recording.md](recording.md#latency)).
    **Confirmed, with a flaw**: it reads only as much input as the output asks for, so
    input that piled up while a callback was held up stays piled up, and late, for as long
    as the streams run. `OboeAudioIO` reads all the input there is instead.
  - Devices run at 48 kHz natively. **Confirmed** on the user's phone. Tony opens the
    streams at the device's rate and converts takes to the reference's
    ([mobile-port.md](mobile-port.md#sample-rate)).
- **Latency figures:**
  - About 20 ms round trip when every recommendation is followed; 205 ms when the
    performance mode is not low-latency.
  - Popular phones averaged about 39 ms in 2021, down from 109 ms in 2017.
  - The feature flag `android.hardware.audio.low_latency` means output latency of 45 ms or
    less; `android.hardware.audio.pro` means round trip of 20 ms or less.
  - Android has "no API to determine audio latency at runtime". Latency is estimated from
    `AAudioStream_getTimestamp()`, which the compatibility requirements say is accurate to
    ±2 ms. Oboe's `calculateLatencyMillis()` does the estimate. **Confirmed**, and the
    estimate moves: one log had the output at 5.2 then 8.4 ms, and the input at 134, 154
    and 222 frames, from one start to the next. So a figure Calibrate Audio stored for a
    route goes stale when its streams open otherwise, not when these move
    ([calibrate-audio.md](calibrate-audio.md)).
  - Measured by Calibrate Audio on the user's phone, Bluetooth earphones (Sony
    WF-1000XM6, A2DP) out and the phone's microphone in: 276 ms round trip.
- **PortAudio with Oboe** was the alternative to a backend of Tony's own: upstream
  PortAudio has no Android host API, and the one in pull request 1084 (by the Mixxx
  developer acolombier) was still open in September 2026; forks exist
  (`NetResultsIT/portaudio-oboe`, and `croissanne/portaudio_opensles` on the deprecated
  OpenSL ES). Not taken: it depends on unmerged code.

### Files, storage and cloud apps

- **The file dialog is the Storage Access Framework** (`qandroidplatformfiledialoghelper.cpp`):
  - Open uses `ACTION_OPEN_DOCUMENT` with `CATEGORY_OPENABLE`, plus `EXTRA_ALLOW_MULTIPLE`
    for several files.
  - Save uses `ACTION_CREATE_DOCUMENT`, with the suggested name in `EXTRA_TITLE`, and
    creates the document before the app writes to it.
  - Directory mode uses `ACTION_OPEN_DOCUMENT_TREE`.
  - Name filters are turned into MIME types. **Confirmed, and harmful**: a provider
    greys out the files whose type is not among those asked for, and Android knows no type
    for `.ton`; files in Google Drive could not be picked at all. Tony's own pickers ask
    for no type and check the extension of what was picked.
  - Every result gets a persistable URI permission.
  - `exec()` blocks in a nested event loop.
  - Results are `content://` URIs, also from `getOpenFileName()`. Android matches its
    grants by the URI's exact string, and `QFileDialog::selectedFiles()` gives it partly
    decoded (`AndroidFiles::grantedUri()` takes it from `selectedUrls()` as Android wrote
    it).
- **`QFile` opens `content://` URIs** through Qt's Android content file engine
  (`androidcontentfileengine.cpp`, using `ContentResolver.openFileDescriptor`). It also
  supports size, time, MIME type, iterating a tree URI, mkdir, remove, rename and creating
  files under a tree. **Disproved for some names**: the engine rebuilds the URI with `(`
  and `)` (and `!'*`) encoded, which Android leaves as they are, finds no grant for it,
  and reports the file missing. Tony reads and writes picked documents through the
  `ContentResolver`'s descriptors instead (`AndroidStorage::Document`).
- **Pitfalls:**
  - Writing uses mode "w", which since Android 10 may not truncate, depending on the
    provider. Overwriting a longer file can leave old bytes at the end. Tony writes "wt".
  - A descriptor taken with `ParcelFileDescriptor.detachFd()` tells the provider at once
    that the app has finished, before anything is written; a provider that takes the file
    when it is closed (MediaStore behind Downloads, a cloud app) may then keep nothing,
    which is the likely reason a saved log once came out empty (read from Android's
    source). Tony keeps the `ParcelFileDescriptor` and closes through it.
  - Cloud providers may return a pipe, which cannot seek.
  - `QFileInfo::path()` and `absolutePath()` return the URI string, so code that builds
    paths, changes suffixes, or writes a temporary file and renames it breaks.
  - Picking one file grants no access to the files next to it. **Confirmed**: a session
    cannot be opened from a cloud app, which hands over the `.ton` alone.
  - C libraries that open by path need a local copy.
- **Cloud apps in the picker:**
  - Google Drive, Dropbox and OneDrive each have a document provider, so they appear when
    opening a file.
  - Whether each supports creating files and picking a folder is **unconfirmed**; reports
    say Drive does not support folder picking.
  - KeePassDX issues report data lost writing through the Dropbox provider (2023) and the
    OneDrive provider (2023). Export a new copy rather than overwriting.
  - A whole session could travel through the picker only as a one-file bundle; decided
    against ([mobile-port.md](mobile-port.md#decisions)).
- **Sync apps** mirror a real folder:
  - FolderSync (many clouds); Dropsync (Dropbox) and OneSync (OneDrive), both from MetaCtrl.
  - Syncthing-Fork (researchxxl, v2.1.5.0 on 2026-09-08): phone to PC directly, no cloud.
    The official Syncthing Android app was discontinued in December 2024.
- **Reading such a folder by path needs a permission:**
  - Reading non-media files (sessions) in shared storage needs either a folder grant, which
    gives `content://` URIs again, or `MANAGE_EXTERNAL_STORAGE` ("All files access",
    Android 11 and later). Google Play restricts that permission; a sideloaded APK can
    use it.
  - Audio alone can be read through MediaStore with `READ_MEDIA_AUDIO` (API 33 and later).
  - With "All files access" the existing path-based code works unchanged. **Confirmed**:
    a desktop session in a synced folder opened in place, its reference found by name
    beside the `.ton` although the session named a Windows path.
  - With it, MediaStore also shows the paths (its `_data` column) of the files other apps
    put in shared storage: the picker's Downloads (`msf:` ids), Recent and Audio
    (`audio:` ids). The external storage provider's ids (`primary:`, a volume's UUID) and
    the downloads provider's `raw:` ids carry the path in the URI.

### Permissions and lifecycle

- **Microphone:** `QMicrophonePermission` (Qt 6.5 and later), through
  `qApp->checkPermission()` and the asynchronous `qApp->requestPermission()`.
  **Confirmed.**
  - It maps to `RECORD_AUDIO`, which must also be in the manifest.
  - androiddeployqt inserts it only when Qt's multimedia plugins are deployed. Tony does
    not use them, and adds it in its own manifest
    (`deploy/android/package/AndroidManifest.xml`).
- **Background:** the system may stop a backgrounded app. On `Qt::ApplicationSuspended`
  Qt 6.11 (`androidjnimain.cpp`) posts the event and then holds the GUI thread's event
  dispatch until the app is active again: a nested event loop in the slot blocks until
  Tony is back, so nothing there may wait for the event loop.
- **The screen going off** after the phone's timeout, with no one touching it, sends Tony
  to the background just the same.

### The pYIN plugin

- Apps targeting API 29 or later may still `dlopen` libraries; they may not `exec` files.
  Libraries in the app's native library directory load normally. **Confirmed.**
- Reportedly only files named `lib*.so` are packaged. **Confirmed**, so the APK holds the
  plugins as `libpyin.so` and `libchp.so`.
- With modern packaging the libraries stay inside the APK and the native library directory
  can be empty, so svcore's scan of `VAMP_PATH` finds nothing. Two fixes were open:
  legacy packaging with the plugin named `libpyin.so`, or linking pYIN into the app with a
  small svcore fork change. **Legacy packaging was not enough by itself**: svcore names a
  plugin after its file (`vamp:libpyin:...`), Tony asks for `vamp:pyin:...`, and the
  native library folder holds all of Qt's libraries, which the scan would open one by one.
  So `main.cpp` links `pyin.so` and `chp.so` to them in a folder of Tony's own storage at
  every start (the library folder moves with every install), and that folder is
  `VAMP_PATH`.
- `QCoreApplication::applicationDirPath()` is the native library folder: `argv[0]` is the
  application library's path.
- Precedents link Vamp plugin code straight in rather than loading it, for example
  `recifra/cordova-plugin-chordino`.
- No other Android port of Sonic Visualiser or Tony exists.

## What was built

Android-only code is in `main/`, behind `Q_OS_ANDROID` or in files only the Android build
compiles; its plain parts are in `tony_core`, tested on the desktop.

### The APK

- `deploy/android/`: `setup-toolchain.sh`, `build-qt.sh`, `build-deps.sh`,
  `build-tony.sh` and `build-apk.sh`, in that order
  ([building.md](building.md#building-for-android)).
- Package `io.github.jhhr.tony`, Android 9 (API 28) or later, target API 36, arm64-v8a
  only, debug-signed, installed by hand. `debugoptimized`, as the desktop builds: asserts
  on, and the dev checks compiled in.
- **Landscape**, either way up (`sensorLandscape`).
- The version name is Tony's version and the commit it was built from, with a `+` if the
  working tree had changes; the version code is the number of commits. The log's first
  line gives the version name, the Android API level and Qt's version: which build a log
  came from.
- **The logs**: Tony's output (its own and svcore's `cerr`) goes to logcat under the tag
  `Tony`, and is kept in `log/tony.log` in Tony's storage, 512 KB and one older part
  (`LogFile`), which **Help > Save Log...** saves, to send without a computer. svcore's
  `SVDEBUG` goes only to `log/sv-debug.log` beside it, which a computer reads with
  `adb shell run-as io.github.jhhr.tony cat files/log/sv-debug.log`.

### Audio: `OboeAudioIO`

A `breakfastquay::SystemAudioIO`, as `PortAudioIO` is on the desktop, which
`MainWindow::createAudioIO()` installs on Android.

- **Full duplex**: a stereo float output at the device's own rate and, once recording has
  been asked for and the microphone allowed, a mono float input at the same rate, read in
  the output's callback through `oboe::FullDuplexStream`. Every callback hands the input
  to the record target before it asks for output (the start gap, as above), and reads
  **all** the input waiting, not only what the output asks for. Before any take the output
  runs alone; after one the device stays duplex (svapp), so Play opens the microphone too.
- **Low latency**: both streams ask for low-latency performance mode and exclusive
  sharing, which AAudio gives as an MMAP stream where the phone has one (the user's phone
  does) and turns into a shared one where not. The input uses the VoicePerformance preset:
  low latency, no automatic gain or noise suppression (Android 10 and later).
- FullDuplexStream spends its first 50 or so callbacks after each start draining the
  input: sound starts about 0.1 to 0.25 s after Play or Record.
- **Latency from timestamps** (`StreamLatency`, in `tony_core`): read on the GUI thread,
  never in the callback, nine readings, the median round trip. Measured when the device
  opens (the constructor runs the streams until they have timestamps, up to a second, and
  leaves them suspended) and each time it is suspended after running; the next take uses
  that. A reading is refused, the figures kept as they were, and the log says why, when:
  - **input waited**: more input was waiting than a callback leaves, so the reading is of
    how far behind the reading was;
  - **the input overran** while running (`getXRunCount()`): what was lost reads as
    latency, a buffer's worth each;
  - **input was lost beyond the buffer**: an input latency longer than the input's buffer
    holds cannot be the device's (`StreamLatency::inputLatencyPossible()`), even with no
    overrun counted.

  Each came from the phone's logs, where a take's input latency is 100 to 190 frames: an
  input left unread while idle read 11691 to 11752 frames (244 ms, its buffer full), and
  after takes it read 23194 to 23262 frames (484 ms, two buffers) with no overrun
  counted.
- **Streams disconnected while stopped** (after an idle spell, or a route that went away)
  say so only when started again: `resume()` then opens them afresh on the route there is
  now, measures them and starts them, so the take or the playback goes ahead. A stream
  that fails while running (headphones plugged in or out) is found by
  `MainWindow::checkAudioDevice()`, every 250 ms, which stops a take the way Stop does (or
  playback) and opens the device again, three times in ten seconds at most.
- **The route**: the output and input devices Android opened (from `AudioManager`, by
  device id: their type and product name) and how each stream opened (the audio API,
  MMAP, sharing and performance mode, burst, buffer, preset) are looked up at each open
  and logged. Calibrate Audio keeps a figure per route (`AudioRoute`,
  [calibrate-audio.md](calibrate-audio.md)).
- **Kept running between takes**, as on the desktop: restarting the streams at every take
  moved the input against the output, and a Bluetooth calibration's punch-ins landed
  8.5 ms apart. The device is suspended once it has neither played nor recorded for two
  minutes (`MainWindow::audioIdleSuspendMillis()`), so that the microphone does not stay
  open and the battery drain, and when Tony goes to the background; the next Play or
  Record resumes it ([recording.md](recording.md#latency)).
- **The microphone permission** is asked for when Record is first pressed (and by
  Calibrate Audio's Start); the take starts once it is granted. Refused, a box says where
  to allow it, and Tony plays but does not record.

### Storage and files

- **Opening** a session or audio (File > Open, Load Singing Track, Load Background Music)
  goes through Tony's own picker, which offers every file and checks the extension of the
  one picked. Its path is found from the URI, from
  MediaStore, or for a numbered download by its name and size
  (`AndroidFiles::pathLookupFor()`, `AndroidStorage::pathFor()`).
  - With All files access and a path, the file opens where it is, as on the desktop: a
    session with its audio and takes folder beside it, audio so that a session saved later
    lands beside it. Tony asks for the access with a short explanation and then Android's
    settings page: with a session, which cannot do without it, and the first time with
    audio.
  - Without a path or without the access, audio is copied into `imported/` in Tony's own
    storage through the provider's descriptor (a copy from a pipe that reports an error at
    its close is dropped), and a session is refused. Every refusal names the file's
    provider, where Tony looked and why that did not do, for the user to pass on.
- **Save Session As** asks for the access before the picker (without it there is nowhere a
  session can go), suggests the session's or the reference's name (Android's picker
  suggests none), and saves at the path of the document the picker made, with `.ton` added
  if the name had none. An empty document left behind is removed; one with something in
  it is overwritten only after asking. A cloud app's folder is refused with a message.
- **Open Recent** lists only files that are still there, and says so when one has moved.
- **Formats**: WAV, MP3 and Opus are read as on the desktop (libsndfile, libmad,
  opusfile). M4A, MP4, AAC, 3GP, AMR, FLAC, Ogg (Vorbis or Opus), WebM and Matroska go
  through Android's own extractors and decoders (`AndroidMediaReadStream`, registered with
  bqaudiostream's factory as its own readers are): the first audio track, start to end,
  no seeking. An M4A's encoder delay and padding are kept (the decoder is told of none),
  as Windows' Media Foundation seems to keep them, so that a desktop session's reference
  is the same audio on the phone and its pitch lines up.
- **Help > Save Log...** and Calibrate Audio's **Save Report...** write through the save
  picker (`MainWindow::saveTextThroughPicker()`): with "wt", closed through the provider,
  and then the size the provider reports compared with what was written, in the log and,
  if they differ, in a box.
- Audio copied in stays in `imported/`, which nothing empties; a copy of the same name
  replaces the one before.

### Lifecycle

- **When Tony goes to the background**, a take being recorded is stopped and kept as Stop
  does, playback stops, and the audio device is suspended (Android silences a microphone
  in the background anyway). Then a changed session that has a file of its own is saved,
  except:
  - one never saved: there is no one to ask where it should go;
  - one that loaded incomplete (audio it names could not be read): its file would lose
    that audio. Save, Save As and Save in Audio Path ask first for such a session;
  - while a dialog or the picker is up (`loopLevel() > 1`): the picker and the settings
    page send Tony to the background too, and what is open may be about to save, or to
    decide not to.

  A save that must wait for a take's ranged analysis to be merged is made once Tony is
  back, since the event loop is held until then.
- **The screen is kept on** (`AndroidScreen`) while Calibrate Audio or the dev checks run,
  which record and judge for minutes untouched: a screen going off would stop the take
  and hold the event loop, and the check would end wrongly.

## Known limitations

More, with their reasons, in [open-points.md](open-points.md).

- **A Bluetooth microphone only through SCO**: Android gives a Bluetooth headset's
  microphone only through a call's link, at call quality both ways, which Tony does not
  ask for. With Bluetooth output Tony records from the phone's microphone.
- **The GUI thread is at about 70 % of a core during takes** on the user's phone (the live
  dots' log line, "GUI thread ...% of a core"). The dots keep up, but there is little
  room left.
- **A steady drift of about 1 ms a minute** between Bluetooth output and the phone's
  microphone (+0.1 to +1.5 ms over 81 s of a dev run). Whether it goes on growing or
  corrects itself is not known.
- **The first Record press**, and each reopening of the device, holds the GUI thread for
  up to a second while the streams open and are measured.
- **Before Android 11** there is no All files access: no sessions in place, and audio is
  always copied in.
- **Leaving Tony during a check** (the power key, a call) ends the run wrongly, and a long
  take of the user's own gets no screen kept on.
- **Lyrics files, layer import and the exports** still go through Qt's or svgui's own
  dialogs and a `content://` URI: not adapted to the phone, and not tried there.

## Unconfirmed

- Whether Google Drive, Dropbox and OneDrive support creating files and picking folders.
- Whether Qt 6.12 has shipped.
- Google Play's deadline for 16 KB page alignment: 1 Nov 2025 or 1 Feb 2027; sources
  disagree.
- Whether Android's decoder gives an M4A the same length as Media Foundation does on
  Windows (the phone's log gives the frames decoded, to compare).

## Sources

- Qt:
  - https://raw.githubusercontent.com/qt/qtdoc/dev/doc/src/platforms/android/android.qdoc
  - https://raw.githubusercontent.com/qt/qtdoc/dev/doc/src/platforms/android/android-platform-notes.qdoc
  - https://raw.githubusercontent.com/qt/qtdoc/dev/doc/src/platforms/android/android-deploying-application.qdoc
  - https://raw.githubusercontent.com/qt/qtbase/6.11/doc/global/macros.qdocconf
  - https://raw.githubusercontent.com/qt/qtbase/dev/src/plugins/platforms/android/qandroidplatformtheme.cpp
  - https://raw.githubusercontent.com/qt/qtbase/dev/src/plugins/platforms/android/qandroidplatformfiledialoghelper.cpp
  - https://raw.githubusercontent.com/qt/qtbase/dev/src/plugins/platforms/android/androidcontentfileengine.cpp
  - https://raw.githubusercontent.com/qt/qtbase/dev/src/plugins/platforms/android/androidjnimain.cpp
  - https://raw.githubusercontent.com/qt/qtbase/dev/src/corelib/kernel/qpermissions.cpp
  - https://raw.githubusercontent.com/qt/qtbase/dev/src/android/templates/AndroidManifest.xml
  - https://raw.githubusercontent.com/qt/qtbase/dev/src/android/templates/build.gradle
  - https://github.com/qt/qtmultimedia/tree/6.10/src/multimedia/android
- Build:
  - https://github.com/DarkStarSystems/pcons
  - https://github.com/mesonbuild/meson/issues/13018
  - https://github.com/mesonbuild/meson/issues/6089
  - https://docs.mesa3d.org/android.html
  - https://developer.android.com/guide/practices/page-sizes
  - https://github.com/libsndfile/libsndfile/blob/master/Building-for-Android.md
- Audio:
  - https://developer.android.com/ndk/guides/audio
  - https://developer.android.com/ndk/guides/audio/audio-latency
  - https://developer.android.com/games/sdk/oboe/low-latency-audio
  - https://github.com/google/oboe/releases
  - https://github.com/PortAudio/portaudio/pull/1084
  - https://github.com/NetResultsIT/portaudio-oboe
- Files and sync:
  - https://github.com/Kunzisoft/KeePassDX/issues/1594
  - https://github.com/Kunzisoft/KeePassDX/issues/1487
  - https://github.com/OneDrive/onedrive-api-docs/issues/1134
  - https://github.com/PhilippC/keepass2android/wiki/Keepass2Android-file-handling
  - https://github.com/researchxxl/syncthing-android/releases
  - https://foldersync.io/
  - https://metactrl.com/
  - https://developer.android.com/training/data-storage/manage-all-files
- Vamp plugins:
  - https://github.com/recifra/cordova-plugin-chordino
  - https://github.com/vamp-plugins/jvamp
