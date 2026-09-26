# Audio drivers: WASAPI next to MME

On Windows, **Playback > Audio Driver** chooses which of Windows' audio APIs Tony plays and
records through (MME, DirectSound or WASAPI), and **Playback > Audio Latency** how much
latency it asks that driver for (10 to 200 ms). Each driver keeps its own devices, its own
latency and its own measured round trip. WASAPI at 20 ms is the default where it is built
in, and MME, which every stream went through before there was a choice, elsewhere: the
user's runs (§7) gave WASAPI a third of MME's round trip, with takes placed no less
steadily.

This page is the design as built: why, the decisions, the facts it rests on, the design in
Tony, the tests, what the measurements on the user's PC are to settle, and what is open.
The bqaudioio fork's side is in [forks.md](forks.md#bqaudioio); how the latency asked for
differs from the one takes are placed with, in [recording.md](recording.md#latency); what
to try by hand, in [manual-checklist.md](manual-checklist.md), sections 1 and 2.

## 1. Why

- **MME is slow and unsteady.** On the user's PC Calibrate Audio measured a round trip of
  about 300 ms, with every stream asking for 0.2 s on both sides, and the offset between
  input and output moved by about 13 ms from one take to the next (every take then
  restarted the stream), 5 to 20 ms over three calibrations, while the sweeps within one
  take agreed to 0.3 ms ([calibrate-audio.md](calibrate-audio.md), §10). No one stored
  figure then placed every take; the dev checks' items 1 and 2 failed on it.
- **WASAPI** is the Windows audio engine itself; MME and DirectSound are layers over it.
  Its shared mode mixes with other programs as MME does, with buffers as small as the
  engine's period (typically 10 ms). It turned out no steadier across stream restarts
  (§7), so the stream is now kept running between takes (§5).
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
  100 and 200 ms, the one chosen for that driver ticked; where none has been, 20 ms on
  WASAPI and 200 ms on the others. Both
  are shown only where two drivers or more are built in, which is on Windows; on Linux and
  Android nothing changed.
- **Choosing a driver** stops playback and opens the device again through it, with the
  devices chosen under it (the driver's own default devices where none are) and its
  latency. The device submenus then list that driver's devices and write its keys; going
  back to another driver finds its devices as they were left. **Choosing a latency** stops
  playback and opens the device again. Choosing what is ticked does nothing.
- Both are **greyed out during a take and while a check runs**: either would open the
  device again under the take.
- **The first start with the menus** names WASAPI (MME where there is no WASAPI) and
  carries the devices chosen before over to it. A device name it does not list shows as
  "(not connected)", ticked, and its own default device opens instead. A driver already
  named, by the user or by an earlier build, is left as it is.
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
| **WASAPI at 20 ms is the default** where it is built in, MME elsewhere; a driver already named stays | user, 2026-09-26, from the runs of §7 | A third of MME's round trip and the dots half as far behind the cursor, takes as steady; 10 ms gained 7 ms more with half the buffer. Until measured, MME had stayed the default |
| The driver changes are in the fork `jhhr/bqaudioio`; Tony pins it | user, 2026-09-26 | bqaudioio chooses the host API, the stream's rate and its latency; upstream is on sourcehut |
| **A driver is a bqaudioio implementation**: `mme`, `directsound`, `wasapi`, each PortAudio restricted to that host API; `port` stays as it was (all host APIs) and is not offered | lead | Tony's device menus, the saved devices, svapp's `createAudioIO()` and the stored round trip (`LatencyCalibration::Key::implementation`) were already per implementation: no svapp change |
| WASAPI in **shared** mode, with `paWinWasapiAutoConvert` on both sides | lead | The input's and the output's mixers can run at different rates, and the stream opens at the output's; shared mode leaves other programs' sound alone |
| A device at 48 kHz needs nothing more for placement | `feat/tonyandroid` | The recording is resampled to the reference's rate before the splice, and `TakeTiming` converts device frames; Calibrate Audio judges such a device like any other ([calibrate-audio.md](calibrate-audio.md), §1) |
| The latency chosen per driver from 10, 20, 50, 100 and 200 ms; when unset, 20 on WASAPI and 200 on the others | lead, 2026-09-26; WASAPI's from the runs of §7 | 200 ms is what every stream asked for before; a choice lets the measurements compare |
| The menus only with two drivers or more | lead | One driver is no choice; elsewhere the device menus are all there is |
| The default named where no driver is, before the first device opens and before the Playback menu shows the device menus, not at start-up | lead | With four PortAudio implementations the device menus have no driver to list the devices of; the first device opens lazily, with the first file or take |

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
- **Every take restarted the stream** (`MainWindowBase::stop()` suspended, `record()`
  resumed: `Pa_StopStream` / `Pa_StartStream`). The svapp fork's `stop()` now asks
  `suspendAudioOnStop()` first. bqaudioio's `PortAudioIO::resume()` and `suspend()` do
  nothing when the stream is already in that state, so a running stream survives the
  next take's `resume()`; svapp copes with a device that never stops, as it never
  suspends JACK.
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
- **The stream kept running between takes** (desktop): `MainWindow::suspendAudioOnStop()`
  is false, so neither Stop nor the end of a take suspends the device, and every take
  until the device is opened again shares one alignment of input against output. Opening
  it again (a driver, a latency or a device chosen, either device menu opened, Tony
  started again) moves the alignment, by up to about 8 ms on the user's PC. The input
  stays open from the first take on, and Windows shows the microphone in use until Tony
  quits. Android suspends as before ([recording.md](recording.md#latency)).

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
- **The stream kept running**, with a fake whose loopback moves 10 ms at each restart:
  `TestAudioCheck`'s check lands its punch-ins 10 ms apart when the window suspends at
  Stop, and alike when it keeps the stream running, as the application does on desktop,
  resumed once and never suspended; `TestDevChecks`' whole run passes kept running
  ([testing.md](testing.md), "The audio check and the dev checks").
- Seen failing with the code broken: the default, the latency per driver, the greying;
  the kept-running check and dev run with the stream suspended at Stop (items 1, 2, 7 and
  13), and the check with the end of a take suspending again.
- Not tested: that a choice stops playback. Nothing here runs the fork's Windows code.

## 7. State, and the measurements on the user's PC

**Built**, on `feat/wasapi`: the fork, pinned; Calibrate Audio at any device rate; the two
menus, the default and the reports; the stream kept running between takes (§5). The
fork's Windows part was compiled by the cross-compile only until the user's runs below.
Nothing has been measured through DirectSound.

**The user's runs, 2026-09-26** (Windows; wired headphones with one earcup against the
microphone; (System Default) devices; on each, Calibrate Audio and Use this latency, then
a whole dev run):

| | MME, 200 ms | WASAPI, 20 ms | WASAPI, 10 ms |
| --- | --- | --- | --- |
| Latencies reported, out / in | 200.6 / 28.7 ms | 40.0 / 30.0 ms | 32.0 / 22.0 ms |
| Round trip measured | 297.7 ms | 98.7 ms | 91.3 ms |
| The dev run's punch-ins, lowest to highest offset | −7.8 to +7.8 ms | +3.0 to +19.4 ms | −4.4 to +9.6 ms |
| Start gap, per take | 0 | 0, 10 or 20 ms | 10 or 20 ms |
| Live dots behind the cursor, median | 361 ms | 171 ms | 167 ms |
| Totals | 5 passed, 5 failed, 1 measured | 4, 6, 1 | 4, 6, 1 |

What they showed:

- **WASAPI's round trip is a third of MME's**, and the dots trail the cursor half as far.
  10 ms gains 7 ms over 20 ms.
- **Takes are no steadier.** On every driver each take landed up to about 8 ms either way,
  15 ms from lowest to highest, while one take's sweeps agreed within 0.3 ms: the stream's
  restart at every take moved input against output, whatever the driver. Items 1, 2, 7
  and 13 failed on it on all three.
- WASAPI's start gap comes in whole periods of its engine, 10 ms.
- Two failures were the checks' own, since fixed: item 14 misread a 48 kHz take's overrun
  (it reads about 0.3 s, as on MME), and item 3 judged dots in a sound's first window,
  which through a real room are off pitch on every driver
  ([calibrate-audio.md](calibrate-audio.md), §7).
- Whether 10 ms crackled was not reported.

**Decided** (the user, 2026-09-26): WASAPI at 20 ms is the default, and the stream is
kept running between takes (§5).

**The stream kept running**, WASAPI at 20 ms, 2026-09-26: Calibrate Audio, then a whole
dev run without the device opened again in between; headphones on a PCI sound card (Xonar
Essence STX), the microphone on a USB one. Round trip 91.8 ms. The punch-ins, in the
order they were taken: −3.2, −3.7, −4.5, −5.2, +4.0, +3.4, +2.9, +2.2 ms: 9 ms from lowest
to highest, where the restarts had given 15.

- **The alignment drifts, then slips by a period.** Each take lands about 0.6 ms earlier
  than the one before, and once, between the fourth and the fifth, the offset jumps back
  by 9 ms: WASAPI's 10 ms period, less the drift. Two sound cards run on two clocks; kept
  running, the difference builds up until PortAudio drops or repeats one period of
  input to match. (One start gap read 478 frames where every other read 0 or 480.)
- So a running stream keeps every take within one period, 10 ms, of the others; where that
  window sits against the kept figure depends on when Calibrate Audio measured it. On
  one device, one clock, there would be no drift; not measured.
- Consecutive takes agree: item 10's second punch-in against the first, −0.6 ms.
- Items 4, 9, 10, 12 and 14 passed. Item 3 failed on three dots at a tone's end, as at
  onsets, and sets those apart since ([calibrate-audio.md](calibrate-audio.md), §7).

**Decided** (the user, 2026-09-26): accept it. Items 1, 2, 7 and 13 allow ±6 ms instead of
±2 ms: half a period and a margin, as this run's figures lay. A run whose calibration
happened near a slip can read up to about ±10 ms; calibrating again then puts it back in
the middle. Whether Windows shows the microphone in use until Tony quits, and whether a
take starts with a click, were not watched for.

## 8. Open points

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
- **Kept running on two sound cards** the alignment drifts and slips by a period (§7):
  every take within 10 ms of the others. On one device it should not drift; not measured.
  Opening the device again still moves it, so a figure kept from an earlier session is up
  to about 8 ms off: calibrate at the start of a session for the best placement. The
  microphone shows as in use from the first take until Tony quits.
- On the loopback fake at 48 kHz, two runs in four read an output peak near 0 dBFS in the
  first block after a stream started, where the reference peaks at −12 dBFS; never at
  44.1 kHz. It may be an audible click at a take's start on a 48 kHz device.
- Tony does not ask WASAPI for raw capture, so Windows' enhancements apply on every
  driver ([calibrate-audio.md](calibrate-audio.md), §10).
