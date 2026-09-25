# Android port: findings and first steps

Researched 2026-09-24; nothing built. Read [mobile-port.md](mobile-port.md) first: it has
the decisions, the facts about Tony's own code that any port depends on, and the work
common to both platforms. *(snippet)* marks a fact seen only in search results.

## Platform facts

### Qt for Android

- **Versions, from Qt's `macros.qdocconf`:**

  | Qt | NDK | JDK | Android API | Build tools, Gradle / AGP |
  | --- | --- | --- | --- | --- |
  | 6.11 | r27c (27.2.12479018) | 21 | min 28 (Android 9), target 36 | 36.0.0, 9.3.1 / 9.0.0 |
  | 6.8 | r26b, r27c | 17 | 28 to 35 | not noted |
  | 6.12 (dev) | r27c | not noted | 28 to 36 | Gradle 9.5.1 |

  Qt 6.12 LTS was due around 2026-09-22; whether it has shipped was not confirmed.
- **Qt Widgets are supported, but QML is the expected route.** Qt's Android page describes
  Widgets as something to add "if needed". There is no official list of Widgets
  limitations. From the Android platform plugin's source:
  - `QMenuBar` becomes the Android options (⋮) menu (`QAndroidPlatformMenuBar`) unless
    `setNativeMenuBar(false)` is called. Tony calls it under `Q_OS_LINUX`, which Android
    also defines (see [mobile-port.md](mobile-port.md#the-window)).
  - `QFileDialog` is always the native system picker.
  - `QMessageBox` is native only with the environment variable
    `QT_USE_ANDROID_NATIVE_DIALOGS=1`; otherwise Qt draws it.
  - Widgets use the "android" style plugin or fall back to Fusion.
  - The back key is a navigation request, not a close, and `closeEvent()` does not run.
    Since Qt 6.12 it sends the app to the background.
  - There is no hover, so tooltips and hover states are lost.
  - Kinetic scrolling of item views needs `QScroller`, reported janky *(snippet)*.
  - High-DPI scaling is always on in Qt 6.
- **16 KB page alignment** matters only for Google Play:
  - NDK r28 and later align native libraries by default; with r27 add
    `-Wl,-z,max-page-size=16384`.
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
- **Meson:**
  - No public example of a meson-built Qt Android app was found.
  - meson's Qt 6 module has trouble finding the host `moc` and `rcc` when
    cross-compiling (meson issue 13018, open; issue 6089, the build machine's qmake picked
    up).
  - Mesa's Android documentation shows the usual NDK cross-file.
- **Two routes; decide in the test port:**
  - (a) meson with an NDK cross file and host Qt tools, a script that writes the deployment
    JSON, then androiddeployqt.
  - (b) A `CMakeLists.txt` for Android only, using `qt_add_executable`. This is the
    supported route, but it duplicates the source lists of `meson.build`, which must then
    be kept in step.
- **Libraries:**
  - libsndfile documents Android builds (`Building-for-Android.md`, autotools or CMake),
    without its codec libraries.
  - The others (libsamplerate, fftw3, Rubber Band, opusfile, serd/sord, mad/id3tag) are to
    be cross-compiled.
  - Drop JACK, PulseAudio, ALSA, PortAudio, oggz and fishsound.
- **Build in CI, not on the MSYS2 machine**: a GitHub Actions job on Linux producing an APK
  to sideload, modelled on `.github/workflows/linux.yml`. The development machine has no
  Android toolchain.

### Audio

- **Do not use Qt Multimedia for this.**
  - Up to Qt 6.9, `QAudioSource` and `QAudioSink` used OpenSL ES. From 6.10 they use AAudio
    (QTBUG-132951).
  - Output streams are low-latency, with a three-burst buffer.
  - Input is deliberately opened with `AAUDIO_PERFORMANCE_MODE_NONE`, because some devices
    limit how many low-latency streams can be open. No input preset is set yet.
  - There is no latency or timestamp API.
  - Google measured 205 ms round trip without low-latency mode, so Qt's input path may be
    slow. That is an inference, not measured.
- **Google recommends Oboe.**
  - Oboe uses AAudio on API 27 and later and OpenSL ES below that. OpenSL ES is "not
    recommended for new designs".
  - Recent releases are 1.10 and 1.11; the repository is active.
  - `oboe::FullDuplexStream` gives input and output in one callback, which is what the
    start-gap measurement assumes (see [recording.md](recording.md#latency)).
  - Devices run at 48 kHz natively, and Tony's models at 44.1 kHz. Either open the streams
    at 44.1 kHz and let Oboe convert (which may lose the lowest-latency path), or fix the
    splice to resample; see [mobile-port.md](mobile-port.md#sample-rate).
- **Latency figures:**
  - About 20 ms round trip when every recommendation is followed; 205 ms when the
    performance mode is not low-latency.
  - Popular phones averaged about 39 ms in 2021, down from 109 ms in 2017.
  - The feature flag `android.hardware.audio.low_latency` means output latency of 45 ms or
    less; `android.hardware.audio.pro` means round trip of 20 ms or less.
  - Android has "no API to determine audio latency at runtime". Latency is estimated from
    `AAudioStream_getTimestamp()`, which the compatibility requirements say is accurate to
    ±2 ms. Oboe's `calculateLatencyMillis()` does the estimate.
- **Alternative to a new bqaudioio backend: PortAudio with Oboe.**
  - Upstream PortAudio has no Android host API.
  - A pull request adding one (PortAudio pull request 1084, by the Mixxx developer
    acolombier) was still open in September 2026.
  - Forks exist: `NetResultsIT/portaudio-oboe`, and `croissanne/portaudio_opensles`
    (deprecated OpenSL ES).
  - This route would keep bqaudioio's existing `PortAudioIO` and need no bqaudioio fork,
    but it depends on unmerged code. Compare the two in the audio step.

### Files, storage and cloud apps

- **The file dialog is the Storage Access Framework** (`qandroidplatformfiledialoghelper.cpp`):
  - Open uses `ACTION_OPEN_DOCUMENT` with `CATEGORY_OPENABLE`, plus `EXTRA_ALLOW_MULTIPLE`
    for several files.
  - Save uses `ACTION_CREATE_DOCUMENT`, with the suggested name in `EXTRA_TITLE`.
  - Directory mode uses `ACTION_OPEN_DOCUMENT_TREE`.
  - Name filters are turned into MIME types.
  - Every result gets a persistable URI permission.
  - `exec()` blocks in a nested event loop.
  - Results are `content://` URIs, also from `getOpenFileName()`.
- **`QFile` opens `content://` URIs** through Qt's Android content file engine
  (`androidcontentfileengine.cpp`, using `ContentResolver.openFileDescriptor`). It also
  supports size, time, MIME type, iterating a tree URI, mkdir, remove, rename and creating
  files under a tree.
- **Pitfalls:**
  - Writing uses mode "w", which since Android 10 may not truncate, depending on the
    provider. Overwriting a longer file can leave old bytes at the end.
  - Cloud providers may return a pipe, which cannot seek.
  - `QFileInfo::path()` and `absolutePath()` return the URI string, so code that builds
    paths, changes suffixes, or writes a temporary file and renames it breaks.
  - Picking one file grants no access to the files next to it.
  - C libraries that open by path need a local copy. For Tony: **copy into app storage on
    open, write locally and copy out on export**, never edit through the URI.
- **Cloud apps in the picker:**
  - Google Drive, Dropbox and OneDrive each have a document provider, so they appear when
    opening a file.
  - Whether each supports creating files and picking a folder is **unconfirmed**; reports
    say Drive does not support folder picking.
  - KeePassDX issues report data lost writing through the Dropbox provider (2023) and the
    OneDrive provider (2023). Export a new copy rather than overwriting.
  - A whole session (`.ton`, reference audio, takes folder) therefore needs a one-file
    bundle if it is to travel through the picker, for example a zip with Export and Import
    Session Bundle on both desktop and phone.
- **Sync apps** mirror a real folder:
  - FolderSync (many clouds); Dropsync (Dropbox) and OneSync (OneDrive), both from MetaCtrl.
  - Syncthing-Fork (researchxxl, v2.1.5.0 on 2026-09-08): phone to PC directly, no cloud.
    The official Syncthing Android app was discontinued in December 2024.
- **Reading such a folder by path needs a permission:**
  - Reading non-media files (sessions) in shared storage needs either a folder grant, which
    gives `content://` URIs again, or `MANAGE_EXTERNAL_STORAGE` ("All files access").
    Google Play restricts that permission; a sideloaded APK can use it.
  - Audio alone can be read through MediaStore with `READ_MEDIA_AUDIO` (API 33 and later).
  - With "All files access" the existing path-based code works unchanged. That is the
    cheapest first sharing option.

### Permissions and lifecycle

- **Microphone:** `QMicrophonePermission` (Qt 6.5 and later), through
  `qApp->checkPermission()` and the asynchronous `qApp->requestPermission()`.
  - It maps to `RECORD_AUDIO`, which must also be in the manifest.
  - androiddeployqt inserts it only when Qt's multimedia plugins are deployed. Tony does
    not use them, so add it at the `<!-- %%INSERT_PERMISSIONS -->` placeholder of a custom
    manifest.
- **Background:** the system may stop a backgrounded app. On
  `Qt::ApplicationSuspended`, stop audio and save; Tony has no autosave today.

### The pYIN plugin

- Apps targeting API 29 or later may still `dlopen` libraries; they may not `exec` files.
  Libraries in the app's native library directory load normally.
- Reportedly only files named `lib*.so` are packaged *(unconfirmed)*, so `pyin.so` needs a
  `lib` prefix.
- With modern packaging the libraries stay inside the APK and the native library directory
  can be empty, so svcore's scan of `VAMP_PATH` finds nothing. Two fixes:
  - Turn on legacy packaging (`android-legacy-packaging` for androiddeployqt, or
    `useLegacyPackaging` in Qt's gradle template) and name the plugin `libpyin.so`.
  - Link pYIN into the app and have svcore load it without a scan (a small svcore fork
    change).
- Precedents link Vamp plugin code straight in rather than loading it, for example
  `recifra/cordova-plugin-chordino`.
- No Android port of Sonic Visualiser or Tony exists.

## What has to be built

| Item | Size | Notes |
| --- | --- | --- |
| Android build and APK in CI | Medium, fiddly | The largest risk to the schedule |
| Audio: Oboe backend in a bqaudioio fork, or PortAudio with Oboe | Medium | Must report latency and deliver input before output |
| pYIN loading | Small | Renaming and legacy packaging, or linking it in |
| File import and export through app storage | Small to medium | Or "All files access" and a sync app first |
| Menu bar under `Q_OS_ANDROID`, microphone permission, save on suspend | Small | |
| Compact touch mode and gestures | Medium to large | Common to both ports |
| Sample-rate check, latency calibration | Small each | Common to both ports |

## Test port

This step decides whether to go on.

1. **CI job** (Linux runner):
   - Qt 6.11 for `android_arm64_v8a` plus the host Qt, NDK r27c, JDK 21, API 28 to 36.
   - Cross-compile the C libraries.
   - Build the app as `libTony_arm64-v8a.so` and package it with androiddeployqt, by route
     (a) or (b) above.
   - Upload the APK as an artifact.
2. **No audio**: `AUDIO_NONE`, by `#ifdef Q_OS_ANDROID` for the test, or `--no-audio`
   through the manifest's `android.app.arguments` metadata (check Qt's manifest
   template; this was not verified).
3. **pYIN** found through one of the two fixes above; check the log line "Setting VAMP_PATH
   to ...".
4. **On a phone**, with a WAV pushed into app storage with `adb`:
   - open it, and see pYIN run and the pitch track drawn;
   - pan with one finger, drag a selection in the ruler strip, try menus and dialogs by
     touch.

If that works, the rest is known work, in this order:
- the audio backend: playback, then recording, then checklist items 1 to 3 on the phone;
- sharing: "All files access" and a sync app, then "Open reference audio from..." through
  the picker;
- the compact touch mode and gestures;
- lifecycle and permissions;
- a session bundle, if whole sessions are to travel through the picker.

## Unconfirmed

- Whether Google Drive, Dropbox and OneDrive support creating files and picking folders.
- Whether only `lib*.so` files are packaged.
- Whether Qt 6.12 has shipped.
- Google Play's deadline for 16 KB page alignment: 1 Nov 2025 or 1 Feb 2027; sources
  disagree.

## Sources

- Qt:
  - https://raw.githubusercontent.com/qt/qtdoc/dev/doc/src/platforms/android/android.qdoc
  - https://raw.githubusercontent.com/qt/qtdoc/dev/doc/src/platforms/android/android-platform-notes.qdoc
  - https://raw.githubusercontent.com/qt/qtdoc/dev/doc/src/platforms/android/android-deploying-application.qdoc
  - https://raw.githubusercontent.com/qt/qtbase/6.11/doc/global/macros.qdocconf
  - https://raw.githubusercontent.com/qt/qtbase/dev/src/plugins/platforms/android/qandroidplatformtheme.cpp
  - https://raw.githubusercontent.com/qt/qtbase/dev/src/plugins/platforms/android/qandroidplatformfiledialoghelper.cpp
  - https://raw.githubusercontent.com/qt/qtbase/dev/src/plugins/platforms/android/androidcontentfileengine.cpp
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
