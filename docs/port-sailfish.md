# Sailfish OS port: findings and first steps

Researched 2026-09-25; nothing built. Read [mobile-port.md](mobile-port.md) first: it has
the decisions, the facts about Tony's own code that any port depends on, and the work
common to both platforms.

The research proxy blocked forum.sailfishos.org, docs.sailfishos.org, jolla.com,
openrepos.net and build.sailfishos.org. The facts below come from Sailfish's source
repositories on GitHub, including `sailfishos/docs.sailfishos.org`, the documentation's
source. *(snippet)* marks a fact seen only in search results.

## Platform facts

### The phones

- **Jolla Phone (2026)** *(specs, dates and prices snippet only)*:
  - Announced 2025-12-05 as a community pre-order; deliveries began 2026-07-08, and an
    October 2026 batch was on sale.
  - MediaTek Dimensity 7100 (4× Cortex-A78, 4× A55, Mali-G610 MC2), so aarch64
    (inferred).
  - 8 or 12 GB RAM, 256 GB storage, microSD; 6.36" AMOLED at 1080×2260.
  - **No 3.5 mm jack**, USB-C only. A forum thread lists USB-C adapters and DACs known to
    work, and an official USB-C headset was announced for August 2026.
  - A community analysis of a MediaTek device (possibly this one) says USB headsets go
    through the Android USB audio HAL, and the headset mic works everywhere except in calls
    (`github.com/smatkovi/usb-headset-call-mic`).
  - Runs **Sailfish OS 5.2 "Finlayson"**, a Jolla Phone-only branch (5.2.0.11 to .17).
- **Jolla C2 (2024)**: Unisoc T606, 8 GB, 720×1600, **has a 3.5 mm jack**. Android App
  Support 13 (API 33).

### Sailfish OS and Qt

- **Releases:** 5.0 "Tampella" (2024-2025), 5.1 "Pispala" (June 2026 *(snippet)*), 5.2 for
  the Jolla Phone only (July 2026). There is no 6.x.
- **The system Qt is 5.6.**
  - `sailfishos/qtbase` is 5.6.3 on branch `mer-5.6`, and the docs say "Sailfish currently
    uses Qt version 5.6".
  - No official plan for Qt 5.15 or 6, and no Silica (Sailfish's own QML components) for
    Qt 6, was found.
  - Tony cannot use Qt 5.6 (see [mobile-port.md](mobile-port.md#qt-6-only)).
- **Community Qt 6 from Chum** (Sailfish's community repository, built on the Sailfish
  OBS):
  - Qt 6.8.4 LTS, updated in July 2026, published to `sailfishos:chum` and
    `sailfishos:chum:testing`.
  - Installed system-wide: `/usr/lib64`, plugins in `/usr/lib64/qt6/plugins`.
  - `qt6-qtbase-gui` includes `libQt6Widgets`. It is Wayland only, with no XCB.
  - Also packaged: qtwayland, qtmultimedia, qtsvg, qtdeclarative, qtwebengine, and a
    Maliit on-screen keyboard plugin (`qt6-sfos-maliit-platforminputcontext`).
  - Apps using it: NeoChat, Angelfish, Kirigami gallery. All are QML or Kirigami; none is a
    Qt Widgets desktop app.
  - Community Qt 5.15 ("opt-qt5", under `/opt/qt5`) also exists *(snippet)*. It would need
    the backport described in mobile-port.md, and is not a good target.
- **How a Qt 6 app is shown on screen:**
  - **Lipstick**, Sailfish's compositor, gained xdg-shell support in April 2026. It
    implements "only the basic parts needed to show maximized toplevel windows and position
    popups".
  - NeoChat (in Chum since July 2026) is launched through `qt6-start.sh`, which only sets
    the environment and runs the program:
    - `QT_WAYLAND_DISABLE_WINDOWDECORATION=1`;
    - `QT_WAYLAND_FORCE_DPI=260`, which users can override.
  - The older route, `qt-runner`, is a nested compositor that supports wl-shell only: one
    window, no dialogs. Angelfish still uses it.
  - Whether the xdg-shell support shipped in 5.1 or first in 5.2 is unconfirmed. NeoChat's
    direct launch suggests current releases have it.
- **What that means for Tony's window:**
  - Every top-level window, including every dialog (about 88 call sites in `MainWindow`),
    will be shown maximised with no title bar.
  - Menus and pop-ups are positioned.
  - A developer reported that xdg-shell reports scale 1.0 on the Jolla Phone *(snippet)*.
  - Conflicts between Sailfish's edge swipes (system navigation) and panning in the pane
    have not been reported, nor tested.
- **Qt Widgets and the Jolla Store (Harbour):**
  - `libQt5Widgets` is not on Harbour's allowed-library list, and the Harbour FAQ calls
    Widgets not touch-optimised and software-rendered *(snippet)*.
  - A bundled Qt 6 may link only whitelisted system libraries. freetype, harfbuzz, ICU and
    xkbcommon are not on that list, so they would have to be bundled too.
  - No precedent was found. **Tony would be distributed as a sideloaded RPM or through
    Chum, not the store.**

### Audio

- **PulseAudio 17.0**, not PipeWire, over the Android audio HAL through
  `mer-hybris/pulseaudio-modules-droid` (libhybris), actively maintained. There is no
  official PipeWire plan.
- **bqaudioio's existing PulseAudio backend should work unchanged.** It is compiled into
  Tony's Linux build already. What it does is described in
  [mobile-port.md](mobile-port.md#audio-io).
- It asks PulseAudio for 44.1 kHz, the rate Tony's models run at, and PulseAudio resamples
  to the hardware. The sample-rate question in
  [mobile-port.md](mobile-port.md#sample-rate) is therefore probably moot here.
- **Routing** (`xpolicy.conf` in `mer-hybris/droid-hal-configs`):
  - Ordinary media and app streams go to the **"media_latency" sink**. That is the
    deep-buffer output when the HAL has one, otherwise the primary.
  - The droid card's low-latency ("FAST") sink is used only for keyboard feedback, voice
    UI, VoIP and calls.
  - No documented way for an app to request it was found.
- **Reported latency is an estimate.**
  - The droid sink sets a fixed latency equal to the HAL's `get_latency()`; the source
    reports one HAL buffer (`droid-sink.c`, `droid-source.c`).
  - `pa_stream_get_latency()` therefore returns the HAL's figure, not a measurement.
  - No published round-trip figures for any Sailfish device were found. Expect to
    calibrate on the device.
  - Possible levers, all untried:
    - buffer attributes and `PA_STREAM_ADJUST_LATENCY` in a bqaudioio fork;
    - a stream role that the policy routes to the low-latency sink;
    - Tony's missing latency calibration setting.
- **Microphone:** the Sailjail `Microphone` permission includes `Audio` ("playback and
  record streams cannot be separated on pulseaudio"). `libpulse` is on Harbour's allowed
  list.
- **Existing play-and-record apps:** SailTuner (`LouJo/SailTuner`, `pa_simple`, 16 kHz,
  last committed 2021) and Sounder (a tuner). No multitrack recorder or synth was found.

### Sandboxing (Sailjail)

- Firejail-based; apps have been sandboxed by default since Sailfish 4.4.
- Permissions are declared in the `.desktop` file:

  ```
  [X-Sailjail]
  Permissions=Audio;Microphone;Documents;Music
  OrganizationName=org.foo
  ApplicationName=Tony
  ```

- Each app may write `~/.local/share`, `~/.cache` and `~/.config` under
  `<OrganizationName>/<ApplicationName>`.
- File paths each permission opens:
  - `Documents`: `~/Documents` and `~/android_storage/Documents`.
  - `Music`: `~/Music`, `~/Playlists`, `~/android_storage/Music` and
    `~/android_storage/Podcasts`.
  - `Downloads`: `~/Downloads` and `~/android_storage/Download`.
  - `UserDirs`: all of these plus Pictures, Videos and Public.
  - `RemovableMedia`: memory cards and USB storage.
- An app with no `[X-Sailjail]` section gets a default profile that includes Audio,
  Microphone, Internet and UserDirs.
- **`Sandboxing=Disabled` works for a sideloaded RPM** (not allowed in the store). NeoChat,
  Angelfish and Ferry Sync ship with it.
- Tony's QSettings, record directory and temporary files then land in ordinary Linux
  locations.

### Build tooling and libraries

- **Sailfish SDK 3.13.5** (July 2026):
  - Build targets for 5.1.0 on aarch64, armv7hl and i486.
  - GCC 13.4.0, so C++17 is fine.
  - The build engine runs in Docker or VirtualBox, on Linux and Windows; macOS has
    VirtualBox only. The command-line tool is `sfdk`.
- **Meson works inside an RPM spec.**
  - The IDE knows qmake and CMake only, but any build system can run from a hand-written
    spec `%build` section (docs: "Building packages – advanced techniques").
  - Meson 1.11.1 is packaged in Sailfish, and platform packages such as PulseAudio build
    with it.
  - The build engine compiles in an emulated target, not by cross-compiling, so the
    meson-plus-Qt cross-compiling problems on the Android page should not arise (not
    tried).
- **Libraries:**
  - In Sailfish's own repositories: libsndfile 1.2.2, pulseaudio 17.0 (with -devel), boost
    1.81, opus 1.6.1, libvorbis, libogg, flac, mpg123.
  - **Not found** in the sailfishos or sailfishos-chum organisations: libsamplerate, fftw3,
    Rubber Band, opusfile, serd/sord, libmad (id3tag was not checked). Build them in Tony's
    spec, or package them for Chum. PulseAudio itself is built with fftw disabled.
- **Changes to `meson.build`:** a Sailfish branch, or options on the Linux branch, that
  drop JACK, ALSA (and RtMidi's ALSA defines), oggz and fishsound.
- **pYIN:** the plugin loads unchanged. Install it where `setupTonyVampPath()` looks
  (`<bindir>/../lib/tony`, that is `/usr/lib/tony` even on aarch64, where libraries are in
  `/usr/lib64`), or set `TONY_VAMP_PATH` in the launcher.
- **Installing on the phone:**
  - Settings > System > Untrusted software > Allow untrusted software, then tap the RPM in
    Transfers or the File manager.
  - Or, in developer mode, `devel-su` and `rpm -i`.
  - Chum's Qt 6 packages must be installed first; Chum has its own installer app.

### Files and sharing

- **Native apps use ordinary paths**, gated by the Sailjail permissions above. The
  existing file code, and the session layout described in
  [mobile-port.md](mobile-port.md#files-and-sessions), work as they are.
- **Built-in accounts do not sync folders.**
  - Dropbox and OneDrive are for backup and "share to" upload only.
  - Nextcloud covers gallery images, backup, calendar and contacts.
  - Google covers contacts, calendar and mail.
  - There is no system picker that reaches cloud storage.
- **Sync options:**
  - harbour-Syncthing (`ilpianista/harbour-Syncthing`, bundles Syncthing 2.1.3, 2026-08-26,
    on OpenRepos): phone to PC directly, no cloud. The simplest pairing with a Windows
    desktop running Syncthing.
  - rclone: the official aarch64 RPM works; it is also on OpenRepos. It reaches Google
    Drive, Dropbox and OneDrive, but is configured on the command line; a sync would run
    from a systemd timer.
  - Ferry Sync (`Dominik-h-hub/harbour-ferry`): an rclone front end for Nextcloud, WebDAV,
    Seafile, pCloud, SFTP and FTP.
  - GhostCloud, and a Chum package of the Nextcloud desktop client.
- **Android sync apps through App Support** (FolderSync, Dropsync) write under
  `~/android_storage`. The `Music` and `Documents` permissions include
  `~/android_storage/Music` and `~/android_storage/Documents`, so a native Tony should be
  able to read what they sync there. This is inferred, not tested, and file ownership is
  unverified.

### Android App Support

- An LXC container running AOSP: Android 13 (API 33) on the C2 and the Xperia 10 IV and V.
  Assumed, not confirmed, for the Jolla Phone.
- Android audio goes through the host's PulseAudio; audio passthrough improved in 5.1
  *(snippet)*.
- No latency measurements, and no statement about AAudio low-latency or MMAP paths, were
  found. Expect it to be no faster than native.
- **An APK from the Android port would run here.** It gives Jolla users a fallback without a
  native port, with unknown audio quality.

### Ecosystem

- In November 2023 Jolla's business moved to Jollyboys Ltd, owned by the former
  management, through a court-approved restructuring.
- Releases roughly yearly with frequent point releases. Community News appears every two
  weeks through 2026.
- Chum has about 312 package repositories. Lipstick and the droid audio modules had commits
  in September 2026.
- Small, but active.

## What has to be built

| Item | Size | Notes |
| --- | --- | --- |
| RPM spec with meson, the six missing libraries, and a Sailfish branch in `meson.build` | Medium | Build engine on the Windows machine through Docker, or in CI |
| `.desktop` file with `[X-Sailjail]`, and a launcher that sets the Qt 6 environment | Small | Model: `sailfishos-chum/neochat` and `qt6-sailfishos-util` |
| Qt 6 on 6.8.4 | Unknown | Tony has only been built with 6.11 |
| Audio | None to medium | The backend exists; latency may need buffer settings (a bqaudioio fork) or calibration |
| Dialogs that suit a maximised, undecorated window | Unknown | Depends on how the test port looks |
| Compact touch mode and gestures | Medium to large | Common to both ports, plus the edge-swipe check |
| Sample-rate check, latency calibration | Small each | Common to both ports |

## Test port

This step decides whether to go on. Its two questions cannot be answered from Tony's
side: whether the windows are usable, and whether the latency is usable and correctly
reported.

1. **RPM spec** building with meson in the Sailfish SDK for the aarch64 5.1 target:
   - `BuildRequires` on Chum's Qt 6 packages and on the libraries in the Sailfish
     repositories;
   - the six missing libraries built in the spec, or as sub-packages.
2. **Install** on the phone after Chum's Qt 6.
   - `Sandboxing=Disabled` for now.
   - Launch with the same environment as `qt6-start.sh`.
3. **Without audio first** (`--no-audio` on the launcher's command line):
   - open a WAV from `~/Music`, analyse it, see the pitch track;
   - pan with one finger, drag a selection in the ruler strip;
   - open the menus, trigger a message box and the preferences dialog;
   - check the scaling.
4. **With audio**:
   - play the reference;
   - do the clap test (manual checklist item 1) with a USB-C headset on the Jolla Phone,
     or wired on the C2;
   - read what latency PulseAudio reports (bqaudioio logs it: "playback latency = ... usec",
     "record latency = ...") and compare it with where the claps land.

If the windows are unusable, stop; nothing in Tony fixes lipstick. If only the latency is
wrong, the fix is buffer settings or the calibration setting.

## Unconfirmed

- The Jolla Phone's specs, dates and prices (search snippets only), and which Android level
  its App Support runs.
- Whether lipstick's xdg-shell support is in 5.1 or only 5.2.
- Whether a third-party app can get the low-latency sink, and what the real latency is.
- That libsamplerate, fftw3, Rubber Band, opusfile, serd/sord and libmad are absent from
  Chum. Only GitHub was searched; Chum packages can live in any repository.
- Whether files that Android apps write under `~/android_storage` are readable by native
  apps.

## Sources

- Documentation source (docs.sailfishos.org), `github.com/sailfishos/docs.sailfishos.org`:
  - `Reference/Qt/README.md` (Qt 5.6)
  - `Support/Supported_Devices/README.md`
  - `Support/Releases/README.md`
  - `Support/Help_Articles/Android_App_Support/README.md`
  - `Support/Help_Articles/Backup/Backup_and_Restore/README.md`
  - `Tools/Sailfish_SDK/`
  - `Develop/Apps/Tutorials/Building_packages_-_advanced_techniques/README.md`
- Qt:
  - https://github.com/sailfishos/qtbase
  - https://github.com/sailfishos-chum/qt6
  - https://github.com/sailfishos-chum/qt6-qtbase
  - https://github.com/sailfishos-chum/qt6-sailfishos-util (`qt6-start-env.sh`)
  - https://github.com/sailfishos-chum/neochat (`rpm/neochat.spec`)
  - https://github.com/rinigus/qt-runner
  - https://github.com/sailfishos/lipstick/tree/master/src/compositor/xdgshell
  - https://github.com/sailfishos/sdk-harbour-rpmvalidator/blob/master/allowed_libraries.conf
- Audio:
  - https://github.com/sailfishos/pulseaudio
  - https://github.com/mer-hybris/pulseaudio-modules-droid
  - https://github.com/mer-hybris/droid-hal-configs/blob/master/sparse/etc/pulse/xpolicy.conf
  - https://github.com/sailfishos/sailjail-permissions
  - https://github.com/LouJo/SailTuner
- Build and packages:
  - https://github.com/sailfishos/meson
  - https://github.com/sailfishos/libsndfile
  - https://github.com/sailfishos-chum/main
- Sync:
  - https://github.com/ilpianista/harbour-Syncthing
  - https://github.com/Dominik-h-hub/harbour-ferry
  - https://github.com/sailfishos-chum/nextcloud-client
- Forum *(snippets only)*:
  - https://forum.sailfishos.org/t/release-notes-finlayson-5-2-0-15-jolla-phone-only/30793
  - https://forum.sailfishos.org/t/usb-c-to-3-5mm-jack-adapters-known-to-work-with-sailfish-os/30685
  - https://forum.sailfishos.org/t/lipstick-questions/31318
  - https://forum.sailfishos.org/t/guide-setup-mount-webdav-resource-with-rclone-on-sailfishos/14518
