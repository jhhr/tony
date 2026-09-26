# Audio drivers: WASAPI next to MME

On Windows, **Playback > Audio Driver** chooses which of Windows' audio APIs Tony plays and
records through (MME, DirectSound or WASAPI), and **Playback > Audio Latency** how much
latency it asks that driver for (10 to 200 ms). Each driver keeps its own devices, its own
latency and its own measured round trip. MME, which every stream went through before there
was a choice, is the default until measurements on the user's PC show another better.

This page is the design as built: why, the decisions, the facts it rests on, the design in
Tony, the tests, what the measurements on the user's PC are to settle, and what is open.
The bqaudioio fork's side is in [forks.md](forks.md#bqaudioio); how the latency asked for
differs from the one takes are placed with, in [recording.md](recording.md#latency); what
to try by hand, in [manual-checklist.md](manual-checklist.md), sections 1 and 2.

## 1. Why

- **MME is slow and unsteady.** On the user's PC Calibrate Audio measured a round trip of
  about 300 ms, with every stream asking for 0.2 s on both sides, and the offset between
  input and output moved by about 13 ms from one take to the next (every take restarts the
  stream), 5 to 20 ms over three calibrations, while the sweeps within one take agreed to
  0.3 ms ([calibrate-audio.md](calibrate-audio.md), §10). No one stored figure then places
  every take; the dev checks' items 1 and 2 fail on it.
- **WASAPI** is the Windows audio engine itself; MME and DirectSound are layers over it.
  Its shared mode mixes with other programs as MME does, with buffers as small as the
  engine's period (typically 10 ms). Whether it is steadier across stream restarts is what
  the measurements of §7 are for.
- **The latency asked for was fixed at 0.2 s.** At that figure WASAPI would keep MME's
  buffers and gain nothing.
- **The device menus mixed every host API.** PortAudio lists every device once per host
  API (MME, DirectSound, WASAPI, WDM-KS). bqaudioio's `port` takes the first name that
  matches, usually MME's, but MME cuts names to 31 characters, so a long name picked from
  the menu could only match WASAPI's or WDM-KS's entry, which then opened at the Windows
  mixer's rate (usually 48 kHz). That is where the "48 kHz device" of the calibrate-audio
  work came from. Each driver now lists its own devices only.

## 2. What the user sees

- **Audio Driver**, in the Playback menu before the two device submenus: MME, DirectSound,
  WASAPI, in that order, the one in use ticked. **Audio Latency** next to it: 10, 20, 50,
  100 and 200 ms, the one chosen for that driver ticked, 200 ms where none has been. Both
  are shown only where two drivers or more are built in, which is on Windows; on Linux and
  Android nothing changed.
- **Choosing a driver** stops playback and opens the device again through it, with the
  devices chosen under it (the driver's own default devices where none are) and its
  latency. The device submenus then list that driver's devices and write its keys; going
  back to another driver finds its devices as they were left. **Choosing a latency** stops
  playback and opens the device again. Choosing what is ticked does nothing.
- Both are **greyed out during a take and while a check runs**: either would open the
  device again under the take.
- **The first start with the menus** names MME and carries the devices chosen before over
  to it. A device name MME does not list (a long one, which only WASAPI or WDM-KS had)
  shows as "(not connected)", ticked, and MME's own default device opens instead.
- **A round trip measured before the menus** was kept under no driver, and is not carried
  over as the devices are: the Playback menu's latency line reads "driver's figure" until
  Calibrate Audio is run, and its figure kept, on each driver used.
- **The reports** name the driver: Calibrate Audio's instructions and result page
  ("Driver:"), and `DevChecks.txt`'s header ("Audio driver:", and "Latency asked for:"),
  so that runs on two drivers, or at two latencies, can be told apart.

## 3. Decisions

| Decision | By, when | Why |
| --- | --- | --- |
| Work on `feat/wasapi`, branched from `feat/tonyandroid` after the calibrate-audio merge, merged back when done | user, 2026-09-26 | Another session works on `feat/tonyandroid` |
| **MME stays the default** until Calibrate Audio and a dev run on the user's PC show WASAPI better | user, 2026-09-26 | Not to change what works before it is measured |
| The driver changes are in the fork `jhhr/bqaudioio`; Tony pins it | user, 2026-09-26 | bqaudioio chooses the host API, the stream's rate and its latency; upstream is on sourcehut |
| **A driver is a bqaudioio implementation**: `mme`, `directsound`, `wasapi`, each PortAudio restricted to that host API; `port` stays as it was (all host APIs) and is not offered | lead | Tony's device menus, the saved devices, svapp's `createAudioIO()` and the stored round trip (`LatencyCalibration::Key::implementation`) were already per implementation: no svapp change |
| WASAPI in **shared** mode, with `paWinWasapiAutoConvert` on both sides | lead | The input's and the output's mixers can run at different rates, and the stream opens at the output's; shared mode leaves other programs' sound alone |
| A device at 48 kHz needs nothing more for placement | `feat/tonyandroid` | The recording is resampled to the reference's rate before the splice, and `TakeTiming` converts device frames; Calibrate Audio judges such a device like any other ([calibrate-audio.md](calibrate-audio.md), §1) |
| The latency chosen per driver from 10, 20, 50, 100 and 200 ms; 200 when unset | lead, 2026-09-26 | 200 ms is what every stream asked for before; a choice lets the measurements compare |
| The menus only with two drivers or more | lead | One driver is no choice; elsewhere the device menus are all there is |
| MME named where no driver is, before the first device opens and before the Playback menu shows the device menus, not at start-up | lead | With four PortAudio implementations the device menus have no driver to list the devices of; the first device opens lazily, with the first file or take |

## 4. Facts checked

- **PortAudio on the Windows build** is MSYS2's `mingw-w64-x86_64-portaudio` 19.7.0, built
  with CMake's defaults: `PA_USE_WMME`, `PA_USE_DS`, `PA_USE_WASAPI` and `PA_USE_WDMKS` are
  all ON, and `pa_win_wasapi.h` is installed. Its `PaWasapiStreamInfo` has
  `paWinWasapiAutoConvert` (`1 << 6`). (MSYS2's PKGBUILD and PortAudio's `v19.7.0`
  `CMakeLists.txt`, 2026-09-26.) An ASIO build is a separate package, not used.
- **bqaudioio's `port`**, as upstream has it: its device lists hold every host API's
  devices; `getDeviceIndex()` returns the first whose name matches, else PortAudio's
  global default device (the default host API's, MME's on Windows). The stream opens at
  the output device's `defaultSampleRate`, as neither side of svapp asks for a rate;
  `paFramesPerBufferUnspecified`, then 1024 if that fails, then 2×2 channels.
- **The fork's drivers**: a device name their host API does not list opens that host
  API's default device, as no name does.
- **svapp** `MainWindowBase::createAudioIO()` reads `Preferences/audio-target` and the
  devices `audio-record-device` / `audio-playback-device`, suffixed `-<implementation>`
  when one is named; `auto` means none.
- **Tony's device submenus** are rebuilt on `aboutToShow`, from the implementation
  `audioImplementationName()` gives: `audio-target` if set, else the only implementation
  built in. On Windows there are four (`port` and the three drivers), so with no driver
  named the menus would be empty: hence the default.
- **`LatencyCalibration::Key`** holds `implementation` (from `audio-target`), both device
  names and the recording rate: a figure is kept per driver, and not per latency.
- **Every take restarts the stream** (`MainWindowBase::stop()` suspends, `record()`
  resumes: `Pa_StopStream` / `Pa_StartStream`).
- **At 48 kHz**: `SingingTakes::spliceRecording()` resamples the recording to the
  reference's rate, `TakeTiming` keeps the latency and the frames received in device
  frames, and the cursor keeps the reference's pace during a take (the svgui fork).

## 5. Design in Tony

The fork's part (an implementation per host API, WASAPI's rate conversion, the settable
latency) is in [forks.md](forks.md#bqaudioio). Linux builds none of its Windows code; it
is checked by cross-compiling ([building.md](building.md#checking-the-forks-windows-code)).

- **`AudioDriverSettings`** (`tony_core`): the Preferences, as plain functions: which
  implementations are drivers and in what order, the latencies offered, the default's
  naming and the carry-over of the devices. Its keys, all in `Preferences`:
  `audio-target` (the driver), `audio-playback-device-<driver>` and
  `audio-record-device-<driver>`, and `audio-latency-<driver>` in seconds, as text, as
  `LatencyCalibration` keeps its figures.
- **`AudioDriverMenus`** (`tony_app`): the two submenus. Rebuilt as either opens, so that
  the ticks follow the Preferences, which the other menu or the default may have changed.
  A choice is written to the Preferences, then signalled; `MainWindow` does the rest.
- **`MainWindow::createAudioIO()`** (desktop): names the default driver, hands the
  latency chosen for the driver to bqaudioio (`AudioFactory::setSuggestedLatency()`, for
  the streams opened after), then `openAudioIO()`, which opens the device as svapp does.
  Every device is opened through it: the first, and each recreate. The Playback menu names
  the default too, as it opens, before the device menus read the driver.
- **A choice** stops playback, forgets the device's rate when the driver changed (another
  driver may record at another rate, as another device may), and recreates the audio IO.
- **The report's latency** is the one last handed to bqaudioio, not the Preferences', so
  that it says what the device was opened with.

## 6. Tests

- **Core** (`TestAudioDriverSettings`): the drivers picked out of any list in their order;
  MME named where nothing or `auto` is, the unsuffixed devices carried over only where MME
  has none, and nothing named again, nor where another driver is named or MME is not
  built in; the latency kept per driver, read back from a file, 200 ms where unset or
  unreadable.
- **App** (`TestAudioCheck`): the menus from a given list, in order, with the ticks, before
  the device menus, and hidden with one driver; WASAPI chosen, its setting written, the
  device opened again with WASAPI's devices, MME's kept, and a device then chosen written
  to WASAPI's key; the default named before the first device and as the Playback menu
  opens, and a named driver left alone; the latency kept per driver and handed over at
  each opening; a round trip kept for MME not used under WASAPI, and used again on the way
  back, with the dialog naming the driver; both menus greyed out during a take and a
  check. The drivers are given through `TestMainWindow` ([testing.md](testing.md)), as
  Linux's bqaudioio has none.
- **Dev** (`TestDevChecks`): the report's header names the driver and the latency.
- Seen failing with the code broken: the default, the latency per driver, the greying.
- Not tested: that a choice stops playback. Nothing here runs the fork's Windows code.

## 7. State, and the measurements on the user's PC

**Built**, on `feat/wasapi`: the fork, pinned; Calibrate Audio at any device rate; the two
menus, the default and the reports. The fork's Windows part is compiled by the
cross-compile only: nothing of it has run on Windows yet, and no figure has been measured
through DirectSound or WASAPI.

**To do: the user's runs on Windows** (W5), wired headphones with one earcup against the
microphone, as in [manual-checklist.md](manual-checklist.md), section 1:

1. The menus themselves (section 2 of the checklist).
2. Calibrate Audio, then a whole dev run, on **MME at 200 ms**: the same setting as the
   earlier runs, now with every item.
3. The same on **WASAPI at 20 ms**, with WASAPI's own devices chosen.
4. The same on **WASAPI at 10 ms**.

For each: the result page's text and that run's `DevChecks.txt`, copied aside before the
next run writes over it; and whether anything crackled or dropped out. DirectSound is
offered but is not among these runs.

What they settle:

- **Steadiness:** whether WASAPI's offset moves less from one take to the next than MME's
  13 ms: Calibrate Audio's verdict (Ok or Unsteady), and items 1 and 2 within ±2 ms.
- **The round trip** at each latency, measured against the driver's figure.
- **Whether 10 ms holds** without dropouts, or 20 ms is needed.
- **The rate** WASAPI records at, which is its mixer's, and that takes still line up.

Then the user decides the default. If WASAPI's restarts are as unsteady as MME's, the next
step is keeping the stream running between takes (§8).

## 8. Open points

- **The default driver**: MME until the runs of §7.
- **A round trip is kept per driver, not per latency.** After a latency change the kept
  figure is used unless the latencies the device reports moved by more than 1 ms (then it
  is stale, and the menu line says so): calibrate again after changing the latency.
- **Before a device's first take** the menu line, Forget Measured Latency and the dialog
  look the figure up at the session's rate, and choosing a driver forgets the device's rate
  as choosing a device does. On a device at 48 kHz they then show the driver's figure
  although a 48 kHz one is kept; takes use the kept one. A fix needs the device's rate
  before the first take, from svapp ([calibrate-audio.md](calibrate-audio.md), §5).
- WDM-KS (in PortAudio's build too) and WASAPI's exclusive mode would be lower still, but
  take the device from every other program; not built.
- Keeping the stream running between takes would take the restart out of the take path
  altogether (an svapp change); only if WASAPI's restarts are as unsteady as MME's.
- Tony does not ask WASAPI for raw capture, so Windows' enhancements apply on every
  driver ([calibrate-audio.md](calibrate-audio.md), §10).
