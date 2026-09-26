# Calibrate Audio: plan

Version 2. One button that measures the audio path. In development builds it also runs
every manual-checklist item that a speaker-to-mic loopback can settle. Nothing is built
yet. The research behind the figures quoted here (OboeTester, Bucket Brigade, Ardour's
MTDM, PortAudio's latency reporting, Audacity's measurements) was a separate report,
not kept in the repository.

Setup assumed: wired headphones and a wired mic. For the run, hold one earcup against the
mic, off your ears. No cable is needed. A 3.5 mm loop cable would give the same result
with less noise.

## 1. Why

- **Every take is shifted by the wrong amount.** Tony shifts it by *reported output
  latency + reported input latency + start gap* (`MainWindow::recordingStarted()`,
  `LatencyUtils.h`). The start gap is measured and right. The reported pair comes from
  `Pa_GetStreamInfo()`, which on MME, DirectSound and WASAPI is buffer sizes only.
  Audacity measured it off by −5 to +155 ms. bqaudioio opens the stream with
  `suggestedLatency = 0.2` on both sides.
- **The device's sample rate is not checked.** The device opens at PortAudio's default
  rate. For "(System Default)" through MME that is most likely 44.1 kHz. For a device
  chosen from the menu whose name exists only under WASAPI or WDM-KS, it is often 48 kHz.
  `TakeAudio::splice()` writes the first recording of a take at that rate and places it
  with frames of the 44.1 kHz reference.
- **Most of the manual checklist has never been run** (`docs/manual-checklist.md`,
  36 items). Many items ask whether logic that the app suite proves on `FakeAudioIO`
  also holds on a real device. A loopback run in the real app can answer that without a
  person listening.

## 2. The button

**Playback ▸ Calibrate Audio…** is in every build.

1. **Instructions:**
   - an earcup against the mic, off your ears;
   - moderate volume, quiet room;
   - names the output and input device and the latency now in use (reported or
     measured).
2. **Test session.** Tony writes a generated test reference and opens it the way
   File ▸ Open does. You are asked to save your work first. The run never touches your
   song's takes or undo history.
   *Since C1a:* not when the session open is a check's own (never saved, its reference
   in the check's directory): that is replaced without asking.
3. **Calibration, about 30 s.** Four punch-ins, each spanning three of the reference's
   sweeps with the room the finder needs around them. The ranges come from the layout,
   not from fixed times: a 5 s punch-in judges only one or two sweeps. They use the
   ordinary take path with Play Reference While Recording on. Every punch-in restarts the
   stream, as a real take does.
   *Found in B1:* the calibration layout cannot hold four times three. Where one
   punch-in ends and the next begins, two sweeps must be 1.9 s apart (what the finder
   reads around each), and the layout's boundaries after its 3rd, 6th and 9th sweeps
   are 2.5, 1.7 and 1.8 s. `punchInsFor()` returns nothing for 4 × 3; 4 × 2 and 3 × 3
   fit. Swapping two pairs of spacings (`{21,16,25,19,17,23,26,20,24,18,22}`) would
   make 4 × 3 fit, ending at 25.2 s.
4. **Result page:**
   - **Round trip:** measured, next to the driver's figure.
   - **Spread between punch-ins:** how much the driver's timing moves from one stream
     start to the next.
   - **Sample rates:** the recording's rate against the reference's 44100 Hz.
   - **Mic channel:** which input channel carries the mic.
   - **Levels:** noise floor, and the input peak (clipping).
   - **Verdict:** in plain words, with the fix for each failure.

   **Use this latency** stores the figure.
5. **Development builds only:** the dialog carries on into the **dev checks**, about
   4 minutes (section 5), unless a checkbox on the instructions page (on by default)
   is cleared. It ends with a report page and a report file.

The test session stays open afterwards, so the reference's and the takes' pitch tracks
can be looked at. You go back to your song through Recent Files.

## 3. Dev mode

`meson.build` already switches on the build type (`WANT_TIMING` versus `NO_TIMING`). Add:

- `-DTONY_DEV_CHECKS` when the build type does not start with `release`;
- the dev-check source files, compiled only then.

`build.bat` sets up `build_mingw` as `debugoptimized`, so your builds have the checks.
`meson.build`'s default and the deploy scripts use `release`, so packages have none of
the code.

A runtime flag, so that a release build on someone else's PC could run the checks, is
possible later. It would ship the check code in every package, so it is not part of this
plan.

## 4. What the loopback run settles, item by item

*Since the merge of `default` (2026-09-26):* `default` automated much of the checklist on
its own. `TestUiChecks` covers the screen, keyboard and dialog items on the fake device,
the smoke items below among them. `test-tony-device`, run by hand, covers the device items
(1, 2, 4, 5, 6, 9). The user decided that the dev run takes the device items over and
`test-tony-device` is retired; the smoke group and items 15 and 16 are dropped from the dev
run. The table keeps the old numbering, which `default` has since rewritten; phase D
brings the two together.

The numbers are those of `docs/manual-checklist.md` before that merge.

- **Automated:** the dev checks pass or fail it.
- **Measured:** the dev checks report numbers; a person still judges how it looks or
  feels.
- **Smoke:** the logic is already covered by the app suite; the dev checks repeat it on
  the real device and real timing. Optional, last.
- **Manual:** stays on the checklist.

| # | Item | Verdict | How |
| --- | --- | --- | --- |
| 1 | Latency on this machine, also after save and reopen | **Automated** | Every sweep of every punch-in lands within ±2 ms (§6). Save the test session to a temp `.ton`, reopen it, analyse again: unchanged. |
| 2 | Several phrases in one take | **Automated** | Punch-ins at four positions of one take, each placed right. Each measures its own start gap. |
| 3 | Live dots | **Measured** | *Automated:* dots sit on the reference's tones on the timeline (±1 hop; *found in C1b:* a dot trails the sound it heard by up to half the tracker's window, so from a tone's start to half a window past its end, ±1 hop); they stay after Stop until the pitch track arrives, then go; the status bar stops changing; the take's own pitch and notes are hidden during the take and back after. *Reported:* how far behind the cursor a dot appears, in ms. *Eyes:* does it look right. |
| 4 | Nothing of the take in the speakers | **Automated** | Re-record over earlier material. Tony's output peak (`getOutputLevels()`) is exactly zero in the reference's silent gaps, so no take audio and no synth. The mic hears no second arrival of each sweep; one would mean the input is monitored somewhere (Windows "Listen to this device", or an interface's direct monitor). Play Singing Audio keeps its state. |
| 5 | Mic on input 2 of a stereo interface | **Measured** | Per-channel input peaks show which input the mic is on. If it is input 2, dots appearing is the check; otherwise "not applicable here". |
| 6 | No input device / device in use | **Manual** | Needs the device gone or busy. |
| 7 | Record from a position; overwrite question | **Automated** (placement) | Placement as in 1. Outside the new range, the take's audio is bit-identical and pitch and notes are unchanged beyond ±0.25 s. The question, No, and "Don't ask again" stay with the app suite (`record_over_existing_question`) and a glance. |
| 8 | Cursor from P, pane follows, all in one place | **Measured** | *Automated:* the cursor is at S when the take starts, and inside the visible range throughout. *Reported:* the dot-to-cursor offset. *Eyes:* the rest. |
| 9 | Stop on a 4-minute song | **Automated** | A generated 4-minute reference, punch-in near the end. Time from Stop to new pitch, against a threshold. Pitch outside the range unchanged, which proves the ranged path ran. |
| 10 | The joins | **Automated** | Two punch-ins that meet in the middle of a held tone: no step in the samples at the join; pitch continuous (no gap, no doubled frame); **one** note across the join; nothing moves outside ±0.25 s. |
| 11 | Is 3 s right, is the countdown readable | **Manual** | A judgement. |
| 12 | Lead-in: nothing heard back, nothing before P changed | **Automated** | Output peaks during the lead-in are the reference's only. Audio and events before P are unchanged. |
| 13 | Pre-roll near the start | **Automated** | Punch-in at P = 1 s: playback runs from 0, the countdown starts at 1, placement is right. |
| 14 | Record into Selection stops by itself | **Automated** | Stops within 0.25 s plus one poll of the end. The coverage added is exactly the selection. No dialog. |
| 15 | The practice loop | **Measured** | *Automated:* it stops by itself, the playhead is back at P, and Play plays the take. *Yours:* "is anything else needed?". |
| 16 | Constrain Playback to Selection with a pre-roll | **Measured** | Reports whether the lead-in was played in full. The decision is yours. |
| 17 | Coverage band readable | **Manual** | Eyes. |
| 18 | Band cannot be touched | **Manual** | Could become an app-suite test with synthetic mouse events; it needs no device. |
| 19 | Select Recording at Playhead, then Erase | Smoke | Audio zero in the range, band and events gone, no analysis. |
| 20 | Erase/Select greyed out when they should be | Smoke | Action states sampled through a real take and its real analysis time. |
| 21 | Ctrl+Z three times | Smoke | On real takes, with the menu texts. |
| 22 | Ctrl+Z straight after Stop | Smoke | With real analysis timing. |
| 23 | Undo of the very first recording | Smoke | |
| 24 | New Empty Take, switching | Smoke | Switch time measured. The inactive take is silent: output peaks over its region while the active take is empty there. |
| 25 | Duplicate, record into the copy | Smoke | The original's file is bit-identical afterwards. |
| 26 | Delete asks first; undo clearing acceptable? | **Manual** | Dialogs and a judgement. |
| 27 | Combo and Takes menu greyed during a take | Smoke | Sampled during a real take. |
| 28 | Analyse Now on a take | Smoke | |
| 29–35 | Sessions and files | **Manual** | File system and OS. Mostly covered by the app suite already; item 35 needs a log-out. |
| 36 | Looks | **Manual** | Eyes. |

**Tally:**

| Category | Items | Count |
| --- | --- | --- |
| Automated | 1, 2, 4, 7, 9, 10, 12, 13, 14 | 9 |
| Measured, judgement left | 3, 5, 8, 15, 16 | 5 |
| Smoke | 19–25, 27, 28 | 9 |
| Manual | 6, 11, 17, 18, 26, 29–36 | 13 |

Also newly checked, though not a checklist item yet: the device's sample rate.

## 5. Architecture

Following `AGENTS.md`: state gets its own class, `MainWindow` only wires, and pure logic
goes in `tony_core`.

### `tony_core` (pure, core suite)

- **`LatencyCheck`**
  - **Generator.** Fixed layouts:
    - *calibration*: 26 s;
    - *dev*: adds held tones of 3 s for the join check;
    - *long*: 4 minutes.

    Each event is a sweep of 1 → 8 kHz, 200 ms, with 10 ms edges and a −12 dBFS peak,
    followed by a tone at a pitch pYIN tracks (196, 220.5, 245 or 294 Hz; see
    `docs/testing.md`). Gaps between events are irregular (1.6–2.6 s, all different).
    Only eleven gaps can differ by 0.1 s in that range, so the *long* layout repeats
    the calibration's eleven, and the gaps around *dev*'s held tones are longer.
    The generator returns every sweep's exact frame.
  - **Analysis:**
    - FFT matched filter in the sweep band (bqfft), then its envelope;
    - the **earliest** peak within 6 dB of the largest;
    - confidence: ≥ 15 dB over the window's median, ≥ 6 dB over the second peak outside
      ±10 ms (starting values, to tune);
    - error per sweep; median and spread per punch-in and across punch-ins; slope over
      position;
    - **second-arrival detection**, for monitoring echo;
    - verdicts: Ok, NoSignal, Fading, Clipped, Scattered, Unsteady, PositionDependent.
  - **Calibration arithmetic:** `newRoundTrip = usedRoundTrip + median offset`. A take
    that lands late was spliced from too early a frame.
- **`TakeDiff`**
  - audio bit-identity outside a range;
  - pitch and note events unchanged outside a range ± margin;
  - pitch continuity across a join (gaps, doubled frames);
  - notes spanning a frame;
  - sample step at a join.
- **`LatencyCalibration`**
  - **Key:** the Preferences values `createAudioIO()` reads, plus the recording rate.
  - **Value:** round trip in seconds, spread, date, and the reported pair at the time,
    which works as a staleness fingerprint.

### App, every build

- **`AudioCheckRunner`.** Primitives on the live window:
  - generate and open a test reference;
  - `punchIn(P, E)`, recorded with Record into Selection, Play Reference While Recording
    on and a 1 s pre-roll, **without writing the user's settings** (the three actions
    write QSettings when toggled, so this needs an override inside `record()`, not
    `setChecked()`);
  - wait for the splice and for the analysis;
  - read the take's audio, coverage and events.

  A **`TakeObserver`** polls every 20 ms during a punch-in and records:
  - output and input peaks per channel;
  - each live dot, with the cursor frame when it was added;
  - playback frame, pane centre, status text, action states;
  - frames received, and when the take stopped.
- **`CalibrateAudioDialog`.** Instructions, progress, the result page, Use this latency.
- **`MainWindow`.**
  - The menu entry.
  - In `recordingStarted()`: "stored round trip if valid for this key, else the reported
    sum", logged either way; converted at the recording's rate.
  - A record, kept at take start, of the round trip each take used.
  - "Forget Measured Latency".

### App, development builds only (`main/dev/`)

- **`DevChecks`.** A list of checks. Each returns a plain
  `CheckResult { item, name, verdict (Pass/Fail/Measured/Skipped), numbers, message }`.
  The run is a list of stages, each starting something and saying when it is done,
  driven by the runner's `finished()` and a polling timer as the runner itself is, and
  never by a nested event loop: the window can be closed at any moment. Cancel stops
  the take and leaves the test session open.

  They are **not** QtTest functions. A QVERIFY failure cannot be asserted from inside
  another QtTest, and the app suite has to prove each check can fail.
- **Access.** `DevChecks` is a `friend` of `MainWindow` under `#ifdef TONY_DEV_CHECKS`,
  so there is no public surface in release builds.
- **Report.** A page in the dialog, grouped by checklist item, with the measured numbers.
  A text file goes to `TONY_TEST_LOG_DIR` if that is set, else to the app data directory,
  and ends with a `Totals:` line like the suites.
- **Scratch files.** The run saves its test session into a numbered scratch folder, so
  every take file lands in `<session>.takes/`, and the report names it. The folder stays
  after the run, since the session open then lives in it; the next run removes every
  one the open session does not use.

### The dev run, one scripted sequence (~4 min)

Calibration first. The dev checks then use the new figure for the run only; your stored
setting changes only through Use this latency.

| Step | What it does | Items | Phase |
| --- | --- | --- | --- |
| 1 | Two punch-ins into fresh regions, observer on | 1, 2, 3, 4, 5, 8 | C1a, C1b |
| 2 | Re-record over an earlier punch-in, through its lead-in | 7, 12, 14 | C1c |
| 3 | Punch-in at P = 1 s with a 3 s pre-roll | 13 | C1c |
| 4 | Two adjacent punch-ins meeting inside a held tone | 10 | C2 |
| 5 | Save to the scratch `.ton` and reopen | 1 | C1a |
| 6 | Long reference, two punch-ins far apart | 9 (and 1, 2) | C2 |

Items 15 and 16 and the smoke group are no longer in the dev run (§4).

## 6. Tests

- **Core suite, `TestLatencyCheck` and `TestTakeDiff`.** Synthetic takes:
  - every shift from −0.75 to +0.75 s, including a fractional one;
  - noise at 0 and −10 dB SNR;
  - small-speaker band limiting;
  - polarity inverted;
  - a stronger reflection 7 ms after the direct sound;
  - an event missing, silence, fading events, clipping;
  - resampled by 48000/44100;
  - two punch-ins 20 ms apart;
  - a monitoring echo;
  - the join cases.
- **App suite.** `TestMainWindow` with `FakeAudioIO` `loopback = true`:
  - **Calibration.** Wrong reported latencies are measured right. With the stored
    figure, `latency_end_to_end`'s recipe lines up; without it, the same test fails. A
    stale key falls back to the reported sum. The user's three toggles and their
    settings are untouched. A 48 kHz fake is reported as a rate mismatch, naming
    both rates. This comes from comparing the recording's rate with the reference's,
    not from the sweeps: from about 10 s into the reference, a 48 kHz take lands further
    off than the finder searches, and the sweeps then read as Scattered (found in A2).
  - **Every dev check at least twice.** Once passing on a calibrated fake, and once
    **failing** under an injected fault:
    - an uncalibrated offset, for items 1, 2, 7 and 13;
    - a monitoring echo, from a second loopback tap in the fake, for item 4;
    - a splice offset broken on purpose, for item 10;
    - a 48 kHz device.

    That is the "can fail" proof for each one.
  - **`FakeAudioIO` additions**, all new fields that change no existing meaning:
    loopback gain, a second echo tap and noise.
  - **Where they run.** If the dev-check tests add more than about a minute of real time
    to the app suite, they move to a third executable, `test-tony-dev`. That would
    change the "run both whole suites" rule in `AGENTS.md`, so ask first.

## 7. Order of work

Every step ends with both whole suites green. The work is split into phases for a line
of agents; their work orders are in
[calibrate-audio-work-orders.md](calibrate-audio-work-orders.md). Each phase's line is
marked "Done" when it is committed.

1. **Core.**
   - **A1** Test reference and sweep finder: `LatencyCheck` generator and per-event
     analysis. Done.
   - **A2** Verdicts and calibration arithmetic: aggregation over events and punch-ins.
     Done.
2. **Runner, dialog and calibration page** (every build), with app tests.
   - **B1** The alignment check runner and its app tests. Done.
   - **B2** Storing the measured round trip and using it in takes (`LatencyCalibration`,
     `recordingStarted()`, staleness). This was step 3 below; it moved up because the
     dialog needs it. Done.
   - **B3** The check's playback: the reference centred at −12 dBFS, the sonification
     silent, and a progress signal. Done.
   - **B4** The Calibrate Audio dialog and menu entry. Done.

   **Then you run it on your PC.** Its numbers settle three things: how wrong the
   driver's figure is, whether the offset holds across stream restarts on MME, and
   whether your device's rate hits the takes. Work goes on meanwhile: only the
   thresholds and the restart-jitter remedy wait on those numbers.
   *First run, 2026-09-26* (MME, wired mic and headphones): round trip 301 and 295 ms
   with one earcup to the mic, verdict Unsteady both times (5–15 ms); Scattered with the
   mic between both cups. The detailed figures are awaited.
3. **Calibration in use:** built in B2 (Use this latency and Forget in B4).
4. **Dev-check framework:**
   - **C0** `TakeDiff`, pure. Done.
   - **C1a** Build flag, `DevChecks`, report, friend access, the dialog's dev run.
     Items 1 and 2. Done.
   - *After the merge of `default`:* `TestDevChecks` moved into an executable of its
     own, `test-tony-dev`, run when a change touches what the checks drive (the user's
     decision). The phases below were cut again (§4).
   - **C1b** `TakeObserver`. Items 3, 4, 5 (and 8's number). Done.
   - **C1c** Re-record and pre-roll stages. Items 7, 12, 13, 14.
5. **C2** Long song and joins: items 9 and 10.
6. **C3** Retire `test-tony-device`, once all it checks is in the dev run.
7. **Release build** by the lead (§8, "Release builds must stay clean").
8. **D** Docs, from the code and the phase log:
   - `manual-checklist.md`: an automated item keeps its text and gets "*automated:
     dev check `<name>`*"; a measured item keeps only the question for a person. The
     list becomes what a person must do after a dev run.
   - `testing.md`: a Dev checks section.
   - `recording.md`: the latency section.
   - `open-points.md`.

**Separate task:** fix the device-rate mismatch. For example the record target could ask
for the session's rate, a one-line change to
`AudioCallbackRecordTarget::getApplicationSampleRate()` in the svapp fork; or the splice
could convert. The button then shows the fix working on each device.

## 8. Risks

- **Restart jitter.** If the spread across punch-ins is ≥ 10 ms, no stored figure fits
  every take. The remedy would be to keep the stream running between takes instead of
  suspending it in `stop()`, an svapp change. Decide on step 2's numbers.
- **Windows enhancements or echo cancellation** can remove the sweeps. Tony cannot ask for
  raw capture: bqaudioio is upstream and MME has no raw mode. The NoSignal and Fading
  verdicts point to *Sound settings ▸ device ▸ Audio enhancements: Off*.
- **Thresholds are guesses** until real runs exist. Every check reports its numbers as
  well as pass or fail, and the report file is what tunes them.
- **The live app during a run.** No nested event loops: the runner and the dev checks
  are driven by timers, and the dialog is not modal, so the window can be used, and
  closed, while they run. Cancel, and a session closed mid-run, must always leave a
  clean state. `closeSession()` already stops take polling.
- **Loudness.** The sweeps are −12 dBFS with earcups off the ears; the dialog says so
  before starting. *Found in B1:* not as played. Tony normalises every audio file to
  full scale as it reads it (`Preferences::setNormaliseAudio(true)`), so the reference
  plays at 0 dBFS, in the left channel only (§11). *Since B3* the check's session
  plays it centred, with a play gain that brings it back to −12 dBFS, and its
  sonification silent. Other sessions play as before.
- **Cursor versus dots.** The cursor subtracts the *reported* output latency. With a
  measured round trip the dots move to the right place and may sit off the cursor. Item
  8's number will show how much. Fixing it needs the round trip split between output
  and input, which is not in this plan.
- **Release builds must stay clean.** A `release`-type build must compile with no
  `main/dev/` file, so build one before calling step 4 done.

## 9. Later candidates for the button

- **A noise gate for live dots.** `RealtimePitchTracker` has no level gate (YIN is
  scale-free), so room noise can make dots. The measured noise floor could set one.
- **A quick re-measure** after a Bluetooth reconnect, without a test session.
- **Items 18 and 6**, as app-suite tests: synthetic mouse events, and a fake device that
  fails to open.

## 10. Decisions

| Question | Decision |
| --- | --- |
| Where the check runs | In a session of its own, opened from a generated WAV; the user is asked to save first |
| How the round trip is measured | Through ordinary takes (§2), not a separate audio IO |
| When a measured figure is used | After **Use this latency**; a dev run uses the new figure for itself only |
| Dev mode | Any build type that does not start with `release` (`TONY_DEV_CHECKS`) |
| Form of a dev check | A function returning a plain `CheckResult`, not a QtTest function |
| Checkpoint after B3 | The user runs it on Windows when they can; C0 onwards does not wait |
| Commits | The lead commits each phase after review and pushes `feat/calibrateaudiotests` |
| `test-tony-device` (from `default`) | Its checks move into the dev run, and it is retired (C3) |
| Where the dev checks' tests run | `test-tony-dev`, a third executable in dev builds, run when a change touches the take path, the audio check or the dev checks |

## 11. Facts checked in the code

Checked on 2026-09-25, so that phases do not re-derive them.

- **The latency today.** In `MainWindow::recordingStarted()`'s deferred lambda: L =
  `computeRecordingLatency(getTargetPlayLatency(), getSystemRecordLatency())` + start
  gap. It applies only with Play Reference While Recording on; otherwise L = 0. The
  start gap comes from the play-start callback set in `MainWindow`'s constructor:
  `getFramesReceived() − blockFrames` on the first output block with audio.
  `refineRecordingLatency()` and `currentRecordingLatency()` swap the estimate for the
  measurement. *Found in B1:* `getTargetPlayLatency()` counts frames of the session's
  rate (bqaudioio's `ResamplerWrapper` converts it), `getSystemRecordLatency()` the
  device's, and L is taken off the recording, in the device's. They differ only when
  the device is not at 44.1 kHz. *Found in B2:* the wrapper converts only if the
  session had a rate when the device was opened. A device chosen before any file is
  opened gets its figure passed on in its own frames, and the play source is told a
  device rate of 0. So the output latency counts frames at the play source's
  `getDeviceSampleRate()`, or at the device's rate when that is 0.
  *Since B2* the round trip is the stored figure (`LatencyCalibration`) when one is
  valid, otherwise the reported pair, each converted to seconds at its own rate. It
  is then turned into recording frames. `computeRecordingLatency()` is no longer
  called.
- **Where the reference is heard** (found in B1). `Analyser` pans the reference hard
  left and its pitch and notes sonification hard right, so only the left earcup
  carries the sweeps; the right one carries the synth. *Since B3* the check's own
  session sets its models' play parameters instead: the reference centred at
  −12 dBFS, the sonification muted.
- **bqaudioio `PortAudioIO`** (upstream, not a fork):
  - one duplex `Pa_OpenStream`, `suggestedLatency = 0.2`, no host-API stream info;
  - input goes to the record target **before** output is asked for, in the same
    callback;
  - `suspend()`/`resume()` are `Pa_StopStream`/`Pa_StartStream`. `MainWindowBase::stop()`
    suspends and `record()` resumes, so **every take restarts the stream**;
  - it exposes no device names and ignores PortAudio's callback time info.
- **Device rate.**
  - `AudioCallbackPlaySource::getApplicationSampleRate()` and
    `AudioCallbackRecordTarget::getApplicationSampleRate()` both return 0, so the device
    opens at PortAudio's default rate.
  - `ResamplerWrapper` resamples the play source to it. The record target records at it.
  - `MainWindow` sets `Preferences::setFixedSampleRate(44100)`.
  - `TakeAudio::splice()` writes a take's first recording at the recording's rate
    without converting positions. Later recordings are refused only when their rate
    differs from the take file's.
  - PortAudio's MME default rate is the first of {44100, 48000, …} that the device
    accepts.
- **Device choice.** `getDeviceIndex()` takes the first PortAudio device with the given
  name, across host APIs. MME names are cut to 31 characters.
- **Settings the check must not write.** The toggles `m_recordIntoSelection`
  (`MainWindow/recordintoselection`), `m_playRefWhileRecording` and `m_preRoll` write
  QSettings when toggled. `wantedPreRollFrames()` reads `MainWindow/prerollseconds`.
- **Opening a reference.**
  - The tests use `openPath(path, MainWindow::ReplaceSession)` after
    `discardModifications()`.
  - `checkSaveModified()` is what asks the user to save.
  - The reference is analysed when `Analyser::getInitialAnalysisCompletion() >= 100` and
    the layers exist. See `analysed()` in `TestRecordWorkflow.h`.
  - *Found in C1a:* with "normalise audio" on, a model's `getLocalFilename()` is the
    reader's decoded copy in the temporary directory, not the file opened. The file
    opened is `getLocation()` resolved through `FileSource`
    (`AudioCheckRunner::mainModelFile()`).
- **The take after Stop.**
  - Its audio is the model `analyser2()->getMainModelId()`, and its file is
    `m_takes->getAudioPath()`. *Found in B1:* the model is normalised to full scale
    and resampled to 44.1 kHz as it is read, so every take read from it is Clipped;
    the check reads the file.
  - Coverage is `m_takes->getCoverage().getRanges()`.
  - The take is analysed when `analysed(analyser2())` holds.
- **Levels.** `getOutputLevels()` and `getInputLevels()`, on the play source and record
  target, return per-channel peaks since the last call. *Found in C1b:* while recording,
  lead-in included, `ViewManager::checkPlayStatus()` reads the input levels only, so the
  observer is then the output levels' one reader. They are of what is handed to the
  device, before its output latency. `FakeAudioIO` reports levels only with
  `reportLevels`. The play source's `getTargetBlockSize()` is always its default 1024:
  bqaudioio's `ResamplerWrapper` does not pass the device's block on.
- **Fake device.** `FakeAudioIO::Config::loopback` adds the output to the input
  `inputDelay` frames late; `TestAudioCheck` uses it. The reported latencies are
  independent of the real delay. `TestMainWindow::createAudioIO()` installs the fake.
  Its output is the mean of the channels, so a hard-left reference loops back at
  half level; the check's, centred since B3, at the level it is played at.
- **Menus.** The Playback menu is built in `MainWindow::setupToolbars()`
  (`m_playbackMenu`). The audio device submenus are there too.
- **Build types.** `build.bat` uses `debugoptimized`; `meson.build` defaults to
  `release`, which the deploy scripts use. `meson.build` already switches on
  `buildtype.startswith('release')` for `WANT_TIMING` / `NO_TIMING`.
- **Qt Test** is already in `qt_dep`'s modules, so every target links it.
