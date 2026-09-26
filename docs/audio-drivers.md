# Audio drivers: WASAPI next to MME

Plan and state of the driver project on the branch `feat/wasapi` (from `feat/tonyandroid`,
to be merged back into it). What is built is marked **Done** in §6; the reasons stay here.

## 1. Why

- **MME is slow and unsteady.** On the user's PC Calibrate Audio measured a round trip of
  about 300 ms (bqaudioio opens every stream with `suggestedLatency = 0.2` on both sides),
  and the offset between input and output moved by about 13 ms from one take to the next
  (every take restarts the stream), 5 to 20 ms over three calibrations, while the sweeps
  within one take agreed to 0.3 ms ([calibrate-audio.md](calibrate-audio.md), §10). No
  one stored figure then places every take; the dev checks' items 1 and 2 fail on it.
- **WASAPI** is the Windows audio engine itself; MME and DirectSound are layers over it.
  Its shared mode mixes with other programs as MME does, with buffers as small as the
  engine's period (typically 10 ms). Whether it is steadier across stream restarts is what
  the measurements in §6, W5 are for.
- **Today the device menus mix all host APIs.** PortAudio lists every device once per host
  API (MME, DirectSound, WASAPI, WDM-KS); bqaudioio's `getDeviceIndex()` takes the first
  name that matches, which is usually MME's, but MME cuts names to 31 characters, so a
  long name picked from the menu can only match WASAPI's or WDM-KS's entry, which then
  opens at the Windows mixer's rate (usually 48 kHz). That is where the "48 kHz device"
  of the calibrate-audio work came from.

## 2. Decisions

| Decision | By, when | Why |
| --- | --- | --- |
| Work on `feat/wasapi`, branched from `feat/tonyandroid` after the calibrate-audio merge, merged back when done | user, 2026-09-26 | Another session works on `feat/tonyandroid` |
| **MME stays the default** until Calibrate Audio and a dev run on the user's PC show WASAPI better | user, 2026-09-26 | Not to change what works before it is measured |
| The driver changes are in the fork `jhhr/bqaudioio`, made by the lead; Tony pins it | user, 2026-09-26 | bqaudioio chooses the host API, the stream's rate and its latency; upstream is on sourcehut |
| **A driver is a bqaudioio implementation**: `mme`, `directsound`, `wasapi`, each PortAudio restricted to that host API; `port` stays as it is (all host APIs) | lead | Tony's device menus, the saved devices (`audio-*-device-<implementation>`), svapp's `createAudioIO()` and the stored round trip (`LatencyCalibration::Key::implementation`) are already per implementation: no svapp change |
| WASAPI in **shared** mode, with `paWinWasapiAutoConvert` on both sides | lead | The input's and the output's mixers can run at different rates, and the stream opens at the output's; shared mode leaves other programs' sound alone. Exclusive mode is not in this project |
| A device at 48 kHz needs nothing more for placement | `feat/tonyandroid` A1 | The recording is resampled to the reference's rate before the splice, and `TakeTiming` converts device frames (§3). Calibrate Audio's "rate mismatch" verdict is out of date and goes (W1) |
| `suggestedLatency` settable per driver | lead; the Tony-side choice is open (§7) | At 0.2 s WASAPI would keep MME's buffers and gain nothing |

## 3. Facts checked

- **PortAudio on the Windows build** is MSYS2's `mingw-w64-x86_64-portaudio` 19.7.0, built
  with CMake's defaults: `PA_USE_WMME`, `PA_USE_DS`, `PA_USE_WASAPI` and `PA_USE_WDMKS` are
  all ON, and `pa_win_wasapi.h` is installed. Its `PaWasapiStreamInfo` has
  `paWinWasapiAutoConvert` (`1 << 6`). (MSYS2's PKGBUILD and PortAudio's `v19.7.0`
  `CMakeLists.txt`, 2026-09-26.) An ASIO build is a separate package, not used.
- **bqaudioio** (`src/PortAudioIO.cpp`, at the pin `017ab3ed3a33`, git `7ab6de9`, which is
  also `jhhr/bqaudioio`'s `master`):
  - `getDeviceNames()` lists every device of every host API with input (or output)
    channels; `getDeviceIndex()` returns the first whose name matches, else PortAudio's
    global default device (the default host API's, MME's on Windows).
  - The stream opens at the output device's `defaultSampleRate`, as neither side of
    svapp asks for a rate; `suggestedLatency = 0.2` and no host-API stream info on both
    sides; `paFramesPerBufferUnspecified`, then 1024 if that fails, then 2×2 channels.
  - `AudioFactory::getImplementationNames()` gives `pulse`, `port`, `jack` as built;
    `createIO()` with no implementation named tries each in turn.
- **svapp** `MainWindowBase::createAudioIO()` reads `Preferences/audio-target` and the
  devices `audio-record-device` / `audio-playback-device`, suffixed `-<implementation>`
  when one is named; `auto` means none.
- **Tony**: the Playback menu's "Audio Output Device" and "Audio Input Device" submenus
  are rebuilt on `aboutToShow` (`rescanAudioDevices()`), from the implementation
  `audioImplementationName()` gives: `audio-target` if set, else **the only
  implementation built in**. With several PortAudio implementations that is none, and
  the menus would be empty: the driver has to be named.
- **`LatencyCalibration::Key`** holds `implementation` (from `audio-target`), both device
  names and the recording rate: a figure is kept per driver as it is.
- **Every take restarts the stream** (`MainWindowBase::stop()` suspends, `record()`
  resumes: `Pa_StopStream` / `Pa_StartStream`).
- **At 48 kHz** (`feat/tonyandroid`, A1): `SingingTakes::spliceRecording()` resamples the
  recording to the reference's rate, and `TakeTiming` keeps the latency and the frames
  received in device frames (`recordRate`). Left: the play cursor runs fast during a take
  (svgui's `ViewManager`), marked by a `QEXPECT_FAIL`; `feat/tonyandroid`'s A11 is about
  it.

## 4. Design

### In the fork

- `PortAudioIO` takes the host API it is restricted to (none: all, as today). Device
  lists and `getDeviceIndex()` then look at that host API's devices only, and "no device
  named" means **that host API's** default input and output device.
- `AudioFactory` reports `mme`, `directsound` and `wasapi` on Windows when PortAudio is
  built in, and each opens a `PortAudioIO` restricted to its host API. `createIO()` with
  no implementation named still tries only `port` (and the others built in), as today.
- WASAPI: a `PaWasapiStreamInfo` with `paWinWasapiAutoConvert` on both sides.
- `suggestedLatency` settable, per implementation, with 0.2 s as the default.
- Checked here by building Tony on Linux (the host API restriction with ALSA's devices is
  the same code) and by cross-compiling `PortAudioIO.cpp` for Windows with MinGW-w64
  against PortAudio 19.7.0's headers; the rest only on the user's PC.

### In Tony

- A **Driver** submenu in the Playback menu, before the two device submenus, listing the
  implementations the fork reports that are drivers (MME, DirectSound, WASAPI), when
  there is more than one. Choosing one sets `audio-target`, recreates the audio IO
  (not during a take or a check) and the device menus follow.
- When `audio-target` is empty and `mme` is built in, Tony names `mme` and carries the
  devices saved without a suffix over to the `-mme` keys, once.
- Calibrate Audio: nothing to change for the key; its report names the driver.

## 5. Phases

Each leaves the tree building and all three suites green, committed and pushed to
`feat/wasapi`.

- **W1 — Calibrate Audio at 48 kHz** (agent). The rate-mismatch verdict goes: a device at
  another rate than the reference's is measured, its figure can be kept, and a second
  check with it places the takes. Tests with `FakeAudioIO` at 48 kHz (a loopback with a
  known delay); the old `check_flags_a_rate_mismatch` changes, as the behaviour does.
  Docs: calibrate-audio.md §1, §10 and the test list.
- **W2 — The fork** (lead). As §4; then `repoint-project.json` takes bqaudioio from
  `jhhr/bqaudioio` (git), `repoint-lock.json` pins it, `deploy/linux/container-setup.sh`
  checks it out from there, and forks.md gets its row.
- **W3 — Driver menu** (agent). As §4, "In Tony", with app tests that name the driver
  through the Preferences and read back what `createAudioIO()` was asked for.
- **W4 — Docs** (agent). recording.md, calibrate-audio.md, manual-checklist.md,
  open-points.md, building.md from the code and the log.
- **W5 — Measure** (user). Calibrate Audio and a dev run on MME and on WASAPI, each at the
  latencies offered; the report files back. Then the default is decided.

## 6. State

- Nothing built yet.

## 7. Open

- **How the latency is chosen in Tony**: a fixed smaller figure for WASAPI, or a submenu
  of a few (10, 20, 50, 100, 200 ms). To settle before W3.
- WDM-KS (in PortAudio's build too) and WASAPI's exclusive mode would be lower still, but
  take the device from every other program; not in this project.
- Keeping the stream running between takes would remove the restart from the take path
  altogether (an svapp change); only if WASAPI's restarts are as unsteady as MME's.
