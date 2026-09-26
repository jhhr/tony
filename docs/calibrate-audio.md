# Calibrate Audio and the development checks

**Playback ▸ Calibrate Audio…**, in every build, measures the round trip of the audio path
(from Tony handing a sound to the output device to the microphone's recording of it coming
back) through the ordinary take path, with one earcup of wired headphones held against the
microphone. **Use this latency** then places every take on those devices with the measured
figure instead of the one the driver reports. In development builds the same dialog can
carry on into the **dev checks**: a scripted run that records test references through the
air and settles, with a verdict and numbers, the device items of the manual checklist.

This page is the design as built: why, what the button and the dev run do, what each
verdict and check means, which numbers to send back, the architecture, the tests, the
decisions and what is still open. How to run it on a machine, and what fails on MME today,
is in [manual-checklist.md](manual-checklist.md), section 1. The research behind the
approach (OboeTester, Bucket Brigade, Ardour's MTDM, PortAudio's latency reporting,
Audacity's measurements) was a separate report, not kept in the repository.

## 1. Why

- **The driver's latency is wrong.** A take is placed by taking the round trip and the
  start gap off the front of its recording ([recording.md](recording.md#latency)). Without
  a measured figure the round trip is the sum of the output and input latency the device
  reports. The start gap is measured and right; the reported pair comes from
  `Pa_GetStreamInfo()`, which on MME, DirectSound and WASAPI is buffer sizes only.
  Audacity measured it off by −5 to +155 ms. bqaudioio opens the stream with the
  `suggestedLatency` chosen for the driver under Playback > Audio Latency on both sides,
  0.2 s unless another is chosen ([recording.md](recording.md#latency)).
- **The device need not run at the reference's rate.** It opens at PortAudio's default
  rate: through MME most likely 44.1 kHz, through WASAPI the rate of Windows' mixer, often
  48 kHz ([audio-drivers.md](audio-drivers.md)). A recording is converted to the
  reference's rate as it is spliced, and the round trip is counted in seconds and turned
  into frames of the recording ([recording.md](recording.md#latency)), so a check on such
  a device is judged, and its figure kept, like any other. The result names the device's
  rate among its figures (§4).
- **The manual checklist's device items had never been run.** Many ask whether logic the
  app suite proves on `FakeAudioIO` holds on a real device. A loopback run in the real app
  answers that without a person listening.

## 2. The button

**The Playback menu**, after the two audio device submenus:

- **Calibrate Audio…**, disabled during any take and while a check runs;
- a line that cannot be chosen, the latency takes are placed with now: "Latency: measured
  281 ms, 26 Sep" (the year only when it is not this one), or "Latency: driver's figure,
  279 ms", with "(the measured one is out of date)" when a stored figure is stale, or "not
  known yet" while the device reports nothing. Brought up to date whenever the menu opens;
- **Forget Measured Latency**, enabled while a figure is kept for these devices, stale or
  not.

While a check runs, Record and both device submenus are disabled as well.

**The dialog** is not modal: the check's session is in the window and can be looked at
meanwhile. Closing the dialog while its check runs cancels the check, since nothing else
would show how the run ended. Three pages:

1. **Instructions:** one earcup against the microphone, off your ears; a moderate volume
   and a quiet room; the driver, the output and input devices and the latency in use; how
   long it takes. In development builds the checkbox **Run the dev checks after
   calibrating**, on every time the page is shown and not remembered. **Start**.
2. **Progress:** the step and the punch-in, a bar and the time left (the recording to come,
   plus a guess of 3 s for each analysis), **Cancel**. During the dev checks, their stage.
3. **Result** (§4): **Use this latency** (only when the calibration is usable), **Check
   Again**, **Close**. The text can be selected and copied.

**What a check does:**

1. **A session of its own.** Tony writes the calibration reference (§3), a WAV of sweeps
   and tones, into the application data directory and opens it as File ▸ Open does,
   replacing the session. It asks to save the session open first, unless that is a
   check's own: never saved, and playing a reference from that directory. The reference is
   `calibrate-audio-reference-N.wav` with the lowest N free, after every other such file
   in the directory is removed: a check never writes over the file the session open now
   plays (Windows will not let it), and Recent Files, which lists every file opened and has
   no remove, gets two of these names at most.
2. **Four punch-ins of three sweeps each** into the reference's take, about 6 s each.
   Each is an ordinary take: its range is selected, `record()` is called with Record into
   Selection, Play Reference While Recording and a 1 s pre-roll whatever the toolbar says,
   and the take stops itself at the end of the selection through the Stop path, is spliced
   and analysed. So every punch-in meets the audio stream as a real take does (kept
   running between takes on desktop, started again for each on Android), and is placed
   with the round trip every take is placed with, which is what is being measured.
   The next punch-in waits for the take's analysis, which keeps pYIN's load out of its
   timing.
3. **The check's playback.** Tony normalises every audio file to full scale as it reads it
   and pans the reference hard left and its pitch and notes sonification hard right. The
   check's session plays the reference centred, at a gain that brings it back to the
   −12 dBFS it was made at, and the sonification silent. It stays so after the run; a
   session opened afterwards plays as before.
4. **Judged from the take's file** (`AudioCheckRunner::readTakeFile()`), mixed to one
   channel at the file's rate (the reference's), never from the take's model: the model
   is normalised to full scale as it is read, so every take would read as clipped.

Under a minute in all. Every step has a limit and ends the run with a reason: 60 s for the
reference's analysis, 30 s for a take's, a take's lead-in and range plus 10 s to stop
itself. A device that opens but has delivered not one frame 2 s after the take's lead-in
and range should have gone by ends the run with "The audio device delivered no input",
stopping the take through the Stop path, which then finds nothing to splice. Timed from
the take's own length, not from `record()`: a device slow to start (a Bluetooth headset
switching to its microphone) is never taken for a dead one. A device that cannot be opened
ends it with "The take did not start". Cancel, and a session closed during a run, stop a
take in progress through the Stop path. The window's destructor abandons a run without the
Stop path, which would splice and start pYIN in the middle of the teardown.

The test session stays open afterwards, so that its pitch tracks can be looked at. The
song comes back through Recent Files.

## 3. Finding the sweeps, and the verdicts

**The reference** (`LatencyCheck`). Each event is a linear sweep from 1 to 8 kHz, 200 ms,
with 10 ms raised-cosine edges and a −12 dBFS peak; 0.1 s of silence; a tone of 0.8 s at
196, 220.5, 245 or 294 Hz in turn (a whole number of samples per period at 44.1 kHz, or
pYIN reports a subharmonic: [testing.md](testing.md)); silence to the next event. Linear
rather than exponential: its spectrum is flat, so its matched filter gives the narrowest
peak, and the harmonics a small speaker adds to an exponential sweep match the sweep itself
shifted 67 and 106 ms earlier, where the earliest-peak rule looks. The spacings from sweep to
sweep are irregular, 1.6 to 2.6 s and all different by 0.1 s at least, so that a take
misplaced by whole events cannot look right.

- *Calibration*, 26 s: twelve events, sweeps at 1.0, 3.1, 4.7, 7.2, 9.1, 10.8, 13.1, 15.7,
  17.7, 20.1, 21.9 and 24.1 s. The order of the spacings is what lets four punch-ins of
  three events fit: where one punch-in ends and the next begins, 1.9 s of spacing is lost.
- *Dev*, 40 s: the calibration's events, then three with 3 s held tones, after sweeps at
  26.9, 30.9 and 35.2 s.
- *Long*, 240 s or any length (the start of the 240 s one): the calibration's spacings over
  and over. No more than eleven spacings can differ by 0.1 s within 1.6 to 2.6 s.

**The finder** looks within ±0.8 s of where a sweep should be: an FFT matched filter, the
envelope of its output, and the **earliest** peak within 6 dB of the largest (at least 1 ms
before it), so that a reflection stronger than the direct sound is not taken for it. A sweep
is *found* when its peak stands 15 dB over the window's median and 6 dB over the highest
peak more than 10 ms from it. Its **offset** is where it was found minus where the
reference has it: positive is late.

**Judging a take.** An event is judged in a punch-in only when all the finder reads for it
(its window, and a sweep's length past it) lies inside the punch-in's range with 50 ms to
spare: the splice cuts and crossfades at the range's ends, and a sweep cut in half says
nothing about the audio path. Where a later punch-in overlaps an earlier one, the event is
judged in the later only. Each punch-in has the median and spread of its offsets; across
punch-ins, the offset is the median of their medians (each take counts once), with
their spread and a line fitted over position.

**Verdicts**, in order of precedence: the first that applies is the verdict, and all are
kept.

| Verdict | When | What it means, and the fix the result page gives | Usable |
| --- | --- | --- | --- |
| NoSignal | fewer than 2/3 of the judged sweeps found, or none | the microphone did not hear the sweeps: volume, a muted or wrong input, Windows' audio enhancements, a Bluetooth headset's Hands-Free device | no |
| Clipped | the input reached −0.2 dBFS in the punch-ins | too loud, which can move where sweeps are found: turn down, or hold the earcup a little away | no |
| Fading | the sweeps' level fell by 10 dB from the first half of the judged events to the second (six at least) | something filters the microphone: echo cancellation, noise suppression, audio enhancements | no |
| PositionDependent | over three punch-ins at least, the offsets lie on a line steeper than 0.5 % that leaves less than 5 ms | recording and playback run at different speeds | no |
| Scattered | the offsets disagree by more than 15 ms, across punch-ins or within one | the driver's timing varies too much for one latency: close other programs that use sound | no |
| Unsteady | they disagree by 5 to 15 ms | small enough: the measured round trip is the middle of it, and a take may land up to half the spread off | yes |
| Ok | none of these | the sweeps came back steadily | yes |

One finding stands beside the verdict: **an echo**, a second peak at the same delay,
within 3 ms, after more than half of the sweeps heard and three at least, 20 ms late or
more and no more than 30 dB down: the input is played back out somewhere (Windows' "Listen
to this device", an interface's monitor) and heard again. A paragraph on the result page;
not a verdict. An echo under 20 ms is not seen: that is where the tail of a close
reflection lies.

A device at another rate than the reference's is judged like any other: no verdict comes
from the rates.

**The calibrated round trip** is the one the takes were placed with plus the median offset.
A take that landed late was spliced from too early a frame of its recording, so the round
trip grows.

Every threshold is a starting value. The finder's were checked on synthetic takes (noise
alone reads 6 to 12 dB over the median, against 15; noise at 0 and −10 dB SNR still reads 38
and 28 dB over it); none has been tuned on a real device yet.

## 4. The result page

The verdict in one sentence and its fix; the echo, if one was heard; then a table: the
round trip measured (not for NoSignal) against the driver's, output plus input; what the
takes were placed with (measured before, or the driver's figure); where each punch-in
landed (+ is late); the spread; sweeps found of those judged; both rates ("recorded at
48000 Hz, converted to the reference's 44100 Hz" where they differ); the input peak in
dBFS; the echo; the driver and the devices. A failed run shows why it ended instead.

**Use this latency** keeps the calibrated round trip for the devices the check started on,
not for those the Preferences name when it is pressed (the result stays on show for as long
as the user likes, and the device submenus open again when the run ends), and for the rate
the takes were recorded at.

## 5. The measured round trip in use

`LatencyCalibration` keeps one figure per **key**: the audio driver and the playback and
record devices as the Preferences name them for `MainWindowBase::createAudioIO()`
(`audio-target`, then `audio-playback-device` and `audio-record-device`, each suffixed with
the driver when one is named), and the rate the device records at. In QSettings:
`LatencyCalibration/<driver>|<playback>|<record>/<rate>/`. Device names can hold `/` and
non-ASCII characters; only `%`, `/`, `\` and `|` are percent-encoded, so that no subgroups
are made and a long non-ASCII name stays within the 255 characters of a Windows registry
key. The key follows `createAudioIO()`, not `audioDeviceSettingKey()`: they differ only for
an `audio-target` of "auto", which Tony never writes.

Stored: the round trip and its spread in seconds, the date, and the output and input
latency the device reported then, in seconds. That pair is the **staleness fingerprint**:
when either latency the device reports now differs by more than 1 ms, its buffers have
changed and the round trip with them, and takes go back to the reported pair until the
check is run again. A stale figure is not deleted: it applies again if the driver goes back
to its old buffers.

At every take, `MainWindow::roundTripAt()` gives the stored figure for the devices and the
recording's rate if there is one and it is not stale, else the reported sum, each reported
latency converted to seconds at the rate it is counted in. Then it is turned into frames of
the recording ([recording.md](recording.md#latency)). With no figure stored, at 44.1 kHz,
the round trip is exactly the old sum; a core test checks it over a grid of values.

The menu line, Forget Measured Latency and the dialog's instructions use the rate of the
last take placed with a round trip, or before any take the session's: the device's rate is
not known before a take (`AudioCallbackRecordTarget` has no getter for it). Choosing a
device or a driver from the menu resets it. So on a device at another rate than the
session's, the three see a figure kept for it only once a take has been recorded since
Tony started or the device or driver was chosen; takes are placed with it from the first.

A dev run places its takes with the round trip the calibration before it measured, for the
run only: nothing is stored, the menu line goes on describing the window's own figure, and
the log calls it the audio check's own.

## 6. Development builds

`meson.build`: a build type that does not start with `release` (debug, debugoptimized,
plain, minsize, custom) adds `-DTONY_DEV_CHECKS` to the compiler's and moc's defines,
compiles `main/dev/`, and builds `test-tony-dev`. `build.bat` sets `build_mingw` up as
`debugoptimized`, so the development machine's builds have the checks; `meson.build`'s
default and the CI workflows use `release`, so packages have none of the code. Every use of
`main/dev/` elsewhere (a `friend` line, a member, the dialog's checkbox, the test class) is
inside `#ifdef TONY_DEV_CHECKS`: a `release` build must compile with no `main/dev/` file,
and building one is how to check it.

Verified by the lead on 2026-09-26, on Linux: `meson setup build_release
--buildtype=release`, then `tony`, `test-tony-core` and `test-tony-app` built. No compile
command carried `-DTONY_DEV_CHECKS`, no `main/dev/` file was compiled, there was no
`test-tony-dev`, and `nm -C tony` found no `DevChecks` or `TakeObserver` symbol. Both
release suites were green: the core suite but for the four `TestTakesFile` tests that fail
on Linux only ([building.md](building.md)), the app suite whole (`TestAudioCheck` 25 passed,
`TestRecordWorkflow` 99, `TestUiChecks` 19, among others). `TestAudioCheck` runs the same in
both kinds of build: its fixture deletes the dev checks of a development build's window.

Not done: a runtime flag, so that a release build on someone else's PC could run the
checks. It would ship the check code in every package.

## 7. The dev run

It starts once the calibration is done, when the checkbox was on and the calibration is
usable (otherwise the result page says why it did not run), and ends on the result page:
the calibration as above, a line for each check, and the report file's path. Cancel ends it.
A few minutes in all.

### Stages

| # | Stage | What it records | Items |
| --- | --- | --- | --- |
| 1 | Long song | a 240 s long reference as a session of its own; two punch-ins of about 2 s, the shortest ranges that judge the first sweep from a quarter and from five eighths of the way in (61.0–63.0 and 150.8–152.8 s) | 9; and 1, 2, 4 |
| 2 | Fresh punch-ins | the dev reference as a session of its own; [6.3, 10.2] and [16.8, 21.2] s, judging two sweeps each | 1, 2, 3, 4, 5 |
| 3 | Re-record | into the same take, [19.2, 21.2] s over the end of stage 2's second punch-in, with a 2.4 s pre-roll | 7, 12, 14; and 1, 2, 4 |
| 4 | Pre-roll near the start | [1.0, 4.2] s, judging the sweep at 3.1 s, asking for a 3 s pre-roll | 13, 14; and 1, 2, 4 |
| 5 | Joins | [26.0, 28.7] then [28.7, 32.0] s, meeting at J = 28.7 s, the middle of the held tone from 27.2 to 30.2 s | 10; and 1, 2, 4 |
| 6 | Save and reopen | the session saved into a scratch folder the way Save As saves once it has a name, opened again, and its take's file judged again | 1 |

Why so:

- **The long song first**, so that the dev reference then replaces its session as a
  check's own, unsaved, without asking, and the run still ends on the saved session: no
  saved session is ever replaced. Far into a song is also where a take converted at the
  wrong rate would show.
- **Stage 2's ranges** leave room for the rest: the start (before 4.3 s) for stage 4; the
  held tones for stage 5; and each range holds two sweeps, so that stage 3 can start inside
  the second one past its first sweep and still judge the other.
- **Stage 3** starts at 19.2 s: the shortest range that judges the sweep at 20.1 s, with a
  whole note of the take (18 to 18.8 s) before it that the ranged analysis must leave
  alone. Its lead-in plays from 16.8 s over what stage 2 recorded: two gaps where the
  reference is silent (16.8–17.7 and 18.8–19.2 s), a sweep and a tone. Two gaps, so that a
  window held up over one still has the other for item 12 to judge.
- **Stage 5's** second punch-in's 1 s lead-in plays over what the first recorded; J lies
  1.5 s from either end of the tone, further than item 10 looks around it.

A stage has 4 minutes (the reopen 1 minute), a backstop behind the runner's own limits,
which end a run first and give their reason. A stage that fails ends the run; the checks are
then worked out from the stages it got through, and the rest are Skipped with the reason.

A **`TakeObserver`** watches every punch-in from its Recording step until its analysis is
done. Every 20 ms it keeps the cursor (the `ViewManager`'s playback frame), the frames
received just before and just after reading the output levels, the output levels, the input
levels (from the level meter's signal), the status bar's text and whether a modal dialog is
up; each live dot as it first appears, with the cursor at that moment; the raw recording's
path, when the take stopped, and Play Singing Audio before and after.

### The checks

The items are numbered as the manual checklist was when they were planned, before `default`
rewrote it; the report and [manual-checklist.md](manual-checklist.md) §1 use these numbers.
"Placed within ±2 ms" means every judged sweep found, with its offset within ±2 ms.

| Item | Check | Passes when | Numbers |
| --- | --- | --- | --- |
| 1 | `latency_on_this_machine` | every punch-in of every stage placed within ±2 ms; after the reopen, the take's file judged again over the dev take's punch-ins gives the same offsets to the frame, and the pitch and notes the session restored are the same events (values as the file rounds them) | offsets of each punch-in, the largest, the round trip used, the same after reopening |
| 2 | `several_phrases_in_one_take` | at least two punch-ins; each wholly in the take's coverage, its median offset within ±2 ms, its own start gap measured | each punch-in's median offset and start gap |
| 3 | `live_dots` | stage 2: more than 10 dots in each punch-in, each on one of the reference's sounds, and those on tones within 50 cents of the tone. A sound's dots lie from its start, less a hop, to half the tracker's window and a hop past its end: a dot is drawn at the middle of its window, but YIN hears mostly the first half. Counted apart and not judged: dots on the sweeps (a subharmonic of their top), and dots at an edge, whose window straddles it: within one window of the tracker (46 ms) after a tone's start or the punch-in's, or within half a window (23 ms) either side of a tone's end, where the window holds the tone's decay through the room and what follows. On the fake they are on pitch, but through a real speaker, room and microphone they wander 50 to 75 cents (`TakeDiff::placeLiveDot()`) | dots per punch-in, on tones, at edges, on sweeps, elsewhere, and the message says how many were at edges; how far behind the cursor they appeared, median and spread |
| 4 | `nothing_of_the_take_in_the_speakers` | no echo in any stage; an output level of exactly 0 at every look that lies wholly in one of the reference's silent gaps; Play Singing Audio the same after each take as before, and the take heard or not as it says | the second arrival; the largest output level in the gaps, and over how many looks; the largest output level; the margin of a look |
| 5 | `mic_on_input_2` | stage 2: judged only when the microphone is on input 2 alone (an input within 20 dB of the loudest carries it), and then passes when every punch-in drew more than 10 dots; otherwise Measured, "not applicable here"; a Fail when no input recorded anything | each input's peak in each punch-in; which inputs carry the microphone |
| 7 | `record_from_a_position` | stage 3: placed within ±2 ms; outside the selection the take's audio the same bit for bit, and its pitch and notes beyond ±0.25 s unchanged | the range, offsets, audio, pitch and notes outside |
| 9 | `stop_on_a_long_song` | stage 1: each punch-in's time from Stop to its pitch merged under half the whole song's analysis time, and the take's pitch beyond ±0.25 s of each range unchanged, which shows only the range was analysed | the whole song's analysis, and each punch-in's time and share of it |
| 10 | `the_joins` | stage 5, at J: no step in the samples (the largest first difference within ±2 ms of J at most 10 dB over the 95th percentile of the 50 ms around); the pitch track running through (no gap over one hop within ±0.5 s, no frame twice, in order); exactly one note holding J and no note beginning or ending within 0.5 s; outside the two ranges ± 0.25 s, pitch and notes unchanged. A failure names its part: "step:", "pitch:", "note:", "outside:" | offsets, the second punch-in against the first, the step in dB, the pitch across J, the notes at J and within 1 s, the nearest note edge |
| 12 | `nothing_heard_or_changed_in_the_lead_in` | stage 3: before P the take's audio the same bit for bit and its pitch and notes beyond 0.25 s unchanged; and item 4's looks that end by P, now over the take's own audio, read 0 in the silent gaps | audio, pitch and notes before P; the output in the lead-in's gaps, and the looks; the longest wait between two looks |
| 13 | `pre_roll_near_the_start` | stage 4: a lead-in no longer than the 1 s of song before P; playback from frame 0, and the cursor never before it; a countdown shown, ending at 1, and starting no higher than the lead-in, round trip and start gap (and 50 ms) rounded up; placed within ±2 ms | the lead-in, where playback started, the cursor's lowest, the countdown, offsets |
| 14 | `record_into_selection_stops_by_itself` | stages 3 and 4: what was recorded past the selection's end, from the raw recording, between 0 and 0.25 s plus one look of the take timer (100 ms) and a block; the coverage after is the coverage before plus the selection, exactly; no modal dialog from the take's start to its analysis done | seconds past the end, and allowed; the coverage after; dialogs |

Item 10 does not judge placement: items 1 and 2 do. Item 14 works out what the take needed
from the round trip, start gap, lead-in and range itself, not with `TakeTiming`, whose
margin is part of what it checks; each in seconds at its own rate, since the raw
recording, the round trip and the start gap count the device's frames and the lead-in and
range the reference's. Added up as frames, they read a take on a 48 kHz device about 8 % of
its lead-in and range too long, 0.34 and 0.36 s for stages 4 and 3, and fail it.

**How items 4 and 12 read the output.** A look's output level is the loudest sample handed
to the device since the look before. It is placed on the reference's timeline from the
frames received, since each callback takes in a block of input and then hands out a block
of output, and from the measured start gap; not from the clock or the output latency, since
the levels are of what was handed to the device, before its latency. Either side of a look
lies a margin of one block: the most frames received between two looks that were not held
up. A look counts only when it lies wholly in a gap where the reference is silent.

- A take played back out shows in a gap only if the take holds something there. A real
  microphone records the room; a noiseless loopback would record the reference alone, and
  the tests give the fake a noise floor for that reason.
- A stall of the GUI thread (a disk, the system) makes the frames received jump, and a look
  across it spans a sound and is left out. When no look at all lay in a gap, that part is
  **not judged**: the message says so, with the longest wait and the margin, and the
  verdict is left to the check's other parts, so it can read Pass (§10).

**Verdicts:** Pass; Fail; Measured, numbers only (item 5 when the microphone is not on
input 2 alone); Skipped, when the run ended before the stage the check needs, or the long
song was left out (`Options::longSeconds` of 0, for tests only).

### The report

`DevChecks.txt` in `TONY_TEST_LOG_DIR` if that is set, else in the application data
directory (`%APPDATA%\sonic-visualiser\Tony` on Windows), written over by each run. Its
header: the date, the output and input devices, the audio driver they were opened through
and the latency asked of it, the audio drivers built in, the playback and record latencies
the device reports (frames and ms), the round trip for the run, the scratch folder, the
session saved, and why the run ended early if it did. Then each check
under "Item N", with its verdict, message and numbers, and last `Totals: N passed, N failed,
N measured, N skipped`, as the suites end.

The run saves its session as `dev-checks.ton` in a scratch folder `dev-checks-N` in the
application data directory, so that its take files land in a folder of their own, and
leaves it open to be looked at and played. The next run removes every such folder the
session open then does not use.

## 8. What to send back

After a run on a new machine or device: the result page's text (select it all and copy)
and the whole `DevChecks.txt`. What the numbers decide:

- **The round trip measured against the driver's, and where each punch-in landed:** how
  wrong the driver is, and how far the offset moves from one take to the next (the
  Unsteady and Scattered thresholds, and whether the stream kept running holds one
  alignment, §10).
- **Sweeps found, the input peak, the echo:** the finder's thresholds, NoSignal and Clipped.
- **The recording's rate:** whether the device runs at the reference's 44.1 kHz or its
  takes are converted; a figure is kept for each rate.
- **The report's header:** the driver and the latency asked of it, the drivers built in,
  and what the device reports: which run on which driver is which
  ([audio-drivers.md](audio-drivers.md), §7).
- **Items 1 and 2**, each sweep's offset and each start gap: the ±2 ms. **Item 3**, how far
  the dots trail the cursor. **Items 4 and 12**, the margin, the looks in the gaps and the
  longest wait: how the gap check fares with a real device's blocks. **Item 5**, each
  input's peak. **Item 9**, both times. **Item 10**, the step in dB and the notes at J.
  **Item 13**, the countdown. **Item 14**, the seconds past the selection's end.

## 9. Architecture

Following `AGENTS.md`: pure logic in `tony_core`, state in classes of its own, `MainWindow`
only wires.

- **`tony_core`** (the core suite): `LatencyCheck` (the layouts and the generator, the
  finder, `judgeTake()` and its verdicts, `punchInsFor()`, the calibration arithmetic),
  `LatencyCalibration` (§5), and `TakeDiff`, the comparisons the dev checks make on a take:
  audio bit-identical outside a range, events unchanged outside a range ± 0.25 s, pitch and
  notes across a join, a step in the samples at a join. `TakeDiff` excuses nothing outside
  the range it is given: the splice's fades lie inside the range (`weightAt()` in
  `TakeAudio.cpp`).
- **`tony_app`, every build:** `AudioCheckRunner` (a run of the check: its plan, its steps,
  its result), `CalibrateAudioDialog` (a view over the runner: it starts and cancels the
  runs it started, shows their results and hands one to `storeMeasuredLatency()`; it
  touches no model, layer or take), and in `MainWindow` the menu, `roundTripAt()`, the
  record of what the last take was placed with (`m_takeLatency`) and the override for the
  check's takes.
- **`main/dev/`, development builds only:** `DevChecks` (the stages, the checks, the
  report), which drives the runner, and `TakeObserver`, which it owns and starts for each
  punch-in.

What it rests on:

- **No nested event loop.** The runner and the dev checks are driven by polling timers and
  the runner's `finished()`: they run in the live window, which can be closed, or its
  session replaced, at any moment. Each says `finished()` once, however the run ends:
  never from inside `start()` (whatever goes wrong is found at the first poll), but at once
  from inside `cancel()`, so that the caller knows the run is over when it returns.
  `progress()` never comes from inside either. A step that shows a dialog runs an event
  loop of its own in which the timer goes on firing, so `poll()` does not re-enter.
- **Ownership.** `MainWindow` owns the runner (made with the window), the dialog (made the
  first time it is asked for) and, in development builds, the dev checks (made with the
  window). `~MainWindow` deletes the dialog, then the dev checks, then the runner, before
  anything they read; the dev checks and the runner then end a run without a word and
  without the Stop path, leaving a take in progress to the window. `closeSession()` tells
  the runner first, then the dev checks: a run that is itself replacing the session (the
  runner opening a reference, the dev checks' reopen) carries on.
- **Friends.** The runner, the dev checks and the observer are friends of `MainWindow`:
  they drive the take path and read the take's state. The development ones' `friend`
  lines are inside the `#ifdef`, so a release build has no such surface.
- **The check's takes never go through the toolbar.** Record into Selection, Play Reference
  While Recording and Pre-roll write QSettings when toggled, so the runner sets an override
  (`m_audioCheckTakes`, with the plan's pre-roll and round trip) that `record()`,
  `recordingStarted()` and `wantedPreRollFrames()` read, and clears it when the take stops
  or the run ends.
- **Record during a check.** The Record action goes to `recordPressed()`, which ignores a
  press while a check runs: `record()` itself cannot tell a press from the runner's calls or
  `pollTakeProgress()`'s, and a press would stop the check's take or record one of the
  user's into the check's session.
- **The check's playback** is set on the play parameters of the session's own models,
  never through `Analyser::setAudible()` and the like, which write settings every session
  reads ([architecture.md](architecture.md)). The toolbar's reference level control
  answers a gain between its notches by moving to the nearest and saying so, and the window
  then sets that gain through `Analyser::setGain()` and `setAudible()`: the runner moves the
  control first, under a `QSignalBlocker`.
- **The levels have one reader each.** `getOutputLevels()` and `getInputLevels()` give the
  peak since the previous call and reset it. While recording, lead-in included,
  `ViewManager::checkPlayStatus()` reads the input levels only, for the meter's signal; the
  observer then reads the output levels, and only then, and takes the input levels from
  that signal.
- **A take's audio is read from its file**, never from its model (§2).

## 10. Known limitations and open points

The open points are also in [open-points.md](open-points.md), briefly; this section has
the reasons.

**The driver project** (the user's decision, 2026-09-26, from the restart jitter below) is
in [audio-drivers.md](audio-drivers.md): Playback > Audio Driver and Audio Latency put
WASAPI and DirectSound next to MME, each with its own devices, latency and stored round
trip. The user's runs on each made **WASAPI at 20 ms the default**. The device's rate came first,
as WASAPI opens at the Windows mixer's rate. A recording is converted as it is spliced,
whatever the device's rate; the other way, the record target asking for the session's
rate through `getApplicationSampleRate()` in the svapp fork, fails where the device runs
only at its mixer's rate.

**Restart jitter.** Every take restarted the stream. On the user's PC the offset between
input and output moved by about 13 ms between two takes and by 5 to 20 ms over three
calibrations on MME, and by up to about 8 ms either way on WASAPI as well
([audio-drivers.md](audio-drivers.md), §7), while the sweeps within one take agreed to
0.3 ms; the start gap, measured at 0 frames both times, does not see it. No one stored
figure then placed every take. Considered: widening items 1 and 2 to ±15 ms; the driver
project, whose WASAPI restarted as unsteadily; keeping the stream running between takes.
**Chosen, and built: the stream is kept running between takes on desktop**
([recording.md](recording.md#latency)), so that a session's takes share one alignment,
which the calibration at its start measures. Items 1 and 2 keep ±2 ms. Not yet measured
on a real device: the next dev run, on WASAPI at 20 ms, should pass items 1, 2, 7 and 13.
What still moves the alignment is opening the device again (a driver, latency or device
chosen, a device menu opened, Tony started again): a figure kept from an earlier session
is up to about 8 ms off, and takes after a reopen land that far out until the next
calibration. Item 10, by reading the code, does not fail for a moved alignment (the join is
a dip, below).

**For the user to decide:**

- **"Not judged" reads Pass.** Items 4 and 12 pass when their gap part could not be judged
  (no look lay in a gap), with a message that says so; the Totals line then overstates.
  Whether that should count otherwise (Measured, say) is not decided.
- **The thresholds**, all starting values, from the report files of real runs (§8).

**Weak spots:**

- **Release builds must stay clean**, with no `main/dev/` code: checked 2026-09-26 (§6).
  Build a `release` directory again after a change to how the dev checks are wired in.
- The runner allows the reference's analysis 60 s (`kReferenceTimeoutMs`), and the long
  song's took 10.4 s on the cloud machine: a PC six times slower ends the dev run at its
  first stage, "The test reference was not analysed in time".
- **The countdown** at P = 1 s with a 3 s pre-roll reads 1, 2, 1: `record()` shows it
  before the round trip is known, and from the deferred start on it counts the lead-in and
  the round trip still to come, the time until what is sung is kept. Harmless; item 13
  allows it.
- **Live dots trail the cursor by about the round trip.** During a take the cursor is where
  playback started plus what has been recorded (the svgui fork's `ViewManager`), which runs
  ahead of what is heard, while each dot is drawn where its sound belongs on the reference.
  On the fake they trail it by the round trip and about 40 ms more. Item 3 reports it; the
  manual checklist asks how it looks. Not changed.
- **A join is a 10 ms dip.** Two punch-ins that meet each fade over 5 ms against the
  silence the file held there, not into each other ([takes.md](takes.md#known-limitations)).
  Item 10's step reads the dip as no step, and so, by reading the code, cannot see a jump
  in phase either: two punch-ins that a restart placed differently show only in its number
  "second punch-in against the first", not in its verdict. Not seen on a device. (A comment
  in `DevChecks::joinsCheck()` still expects them to show in the step and the pitch.)
- Item 3 is no check of placement: dots 20 ms early (the round trip 20 ms off) pass or fail
  with where the tracker's hops fall, and the test allows either. Items 1 and 2 judge
  placement.
- Items 3 and 5 judge stage 2's punch-ins only. Item 14 cannot see an overwrite question:
  `record()` would ask it before the observer starts. The check's takes record into a
  selection, where none is asked, so "no question" is watched for, not arranged.
- The calibration's result page does not give the microphone's channel or the noise floor:
  nothing in the calibration measures them (item 5 of the dev run finds the channel).
- Windows' audio enhancements, echo cancellation or noise suppression can take the sweeps
  out, and Tony does not ask for raw capture (MME has no raw mode, and the bqaudioio
  fork does not ask WASAPI for its own): NoSignal and Fading tell the user to turn them
  off.
- Not in the dev run, of what the retired `test-tony-device` did: a take with no lead-in
  and not into a selection, stopped by hand; "no dialog" over every take (item 14 watches
  stages 3 and 4); with no device at all, the "Couldn't open audio device" warning once for
  every file opened (a dated note in the manual checklist).
- Every take logs "No such signal sv::WritableWaveFileModel::aboutToBeDeleted()", from
  svapp's `AudioCallbackRecordTarget` ([forks.md](forks.md), known defects). Harmless here.
- A dev run's session refers to its reference in the application data directory, which the
  next check removes unless that session is open, and the next dev run removes its scratch
  folder: only the latest run can be looked at.

**Later candidates:**

- A noise gate for live dots: `RealtimePitchTracker` has none (YIN is scale-free), so room
  noise can make dots. A measured noise floor could set one.
- A quick re-measure after a Bluetooth reconnect, without a test session.
- The microphone's channel and the noise floor on the calibration's result page.
- A getter for the rate the record target records at (svapp fork), so that the menu line
  and Forget know the device's rate before the first take (§5).

## 11. Tests

- **Core** (`test-tony-core`): `TestLatencyCheck`: the generator; the finder on takes made
  by shifting the reference (every shift across ±0.75 s and a fractional one), with noise at
  0 and −10 dB SNR, band limits, the polarity inverted, a reflection 7 ms late and
  stronger than the direct sound (5.5 dB: the direct sound is taken; 7 dB: the reflection),
  and silence; the verdicts, with the 48 kHz case built as punch-ins displaced by
  P·(1 − 44100/48000) (a take stretched by 8.8 % finds nothing); the arithmetic's sign;
  `punchInsFor()`; long layouts of other lengths. `TestLatencyCalibration`: a figure
  stored and read back under a device name holding `/` and `ä`, keys kept apart, the key
  as the Preferences give it, staleness either side of the tolerance, the round trip in use
  and its frames at the recording's rate. `TestTakeDiff`: each comparison passing and
  failing on purpose, and the real `splice()` and `erase()` through files, whose fades lie
  inside the range; item 3's places for a live dot, with dots 67 cents sharp at the times
  of the user's runs (7.516 to 7.528 s, the tone of 245 Hz from 7.5 s; 16.803 to 16.822 s,
  the punch-in from 16.8 s, where that tone also ends) not judged, and off pitch one window
  later, also at a punch-in from inside a tone; dots 71 and 62 cents sharp at 8.302 and
  18.797 s, 2 ms after the tone of 245 Hz ends and 3 ms before that of 220.5 Hz does, not
  judged, and off pitch one window earlier, the allowance half a window either side of
  the end and no more; and the reach of a sound. `TestAudioDriverSettings`: the drivers,
  the default and the latency kept per driver ([audio-drivers.md](audio-drivers.md), §6).
- **The round trip in the take path** (`TestRecordWorkflow`, `latency_*`): a stored figure
  lines a take up where the reported pair does not, a stale one is ignored, and the
  reported pair is converted at the device's rate, also when the device was opened before
  any file.
- **`TestAudioCheck`** (`test-tony-app`): the check on the loopback fake, whose device
  reports 2 × 4096 frames out and 4096 in while the true round trip is 123 frames longer.
  The check measures the true one, and once it is stored a second check finds its takes
  in place; the same on a 48 kHz fake, the figure kept under 48000 Hz and checked against
  the fake's delay in seconds (bqaudioio's `ResamplerWrapper` adds about 1 ms, unreported,
  that belongs to the round trip); cancel, a closed session, no device, a device
  that never calls back; the check's playback, and a session opened after it playing as
  before; the user's toggles and their settings untouched; plans refused; the plan's round
  trip and pre-roll; keeping the session; replacing a check's own session without asking,
  and asking before the user's; Record ignored; the menu and the dialog, the dialog naming
  the driver. The driver and latency menus are tested here too, as they open the device
  again: a driver chosen, WASAPI (or MME) named by default, the latency and the round trip kept per
  driver, both greyed out during a take and a check ([audio-drivers.md](audio-drivers.md),
  §6). On a fake whose loopback moves 10 ms at each restart, a check whose window suspends
  at Stop lands its punch-ins 10 ms apart (Unsteady), and one kept running, as the
  application keeps it on desktop, lands them alike (Ok), the fake resumed once. Its runs
  are two punch-ins of two sweeps on the first 11 s of the calibration reference, about
  13 s each.
- **`TestDevChecks`** (`test-tony-dev`, development builds only): whole dev runs on the
  loopback fake. Passing, with the fake's true round trip and a long song of 60 s (240 s
  would add most of a minute to every passing run); the same on a 48 kHz fake, every
  item passing and item 14 reading about as at 44.1 kHz; and the same with the fake's
  loopback moving 10 ms at each restart and the stream kept running, every item passing
  (suspended at each Stop, items 1, 2, 7 and 13 fail). Failing: the round trip 20 ms off
  (items 1, 2, 7 and 13; item 10 still passes, both punch-ins moved alike); an echo tap,
  with the microphone on input 2 (item 4 fails, item 5 judged on input 2); the take made
  audible during the re-record's lead-in (items 4 and 12, also through a stall); a stall
  of the GUI thread in the lead-in (item 12 still judged, or not judged, never failed).
  Also cancel, a closed session, the dev checks deleted during a run, the scratch folders,
  the report's header with the driver and the latency asked for, and the dialog carrying
  on into them. Parts that no fault run makes fail (among them item 3's dots on the tones,
  item 9, and item 10's step) were seen failing with the code broken for a moment. About 6
  minutes.

How the tests are built, and what to watch for: [testing.md](testing.md), "The audio check
and the dev checks".

**What a passing run reads on the fake** (Linux, 2026-09-26), to set a real report against:

- every sweep at 0 frames, since the fake's delay is exactly its round trip; its reported
  pair is 2.8 ms short, so a run that ignored the calibrated figure would fail item 1;
- the dots trail the cursor by the round trip and about 40 ms more (322 ms at a 281 ms round
  trip), their spread 40 to 230 ms;
- item 9: a 60 s song analysed in 2.9 to 3.0 s, its punch-ins merged in 0.59 to 0.70 s (20
  to 24 %); a 240 s song in 10.4 s, its punch-ins in 0.63 and 0.75 s (6 to 7 %);
- item 10: the step reads −8.3 dB, the dip; the largest pitch gap is one hop;
- item 3: 24 and 25 of the punch-ins' 279 and 280 dots at edges;
- item 12: 57 looks in the lead-in's gaps; item 14: 0.25 to 0.35 s past the selection's end;
- at 48 kHz, every sweep at +1.0 to +1.1 ms, the resampler's hold-back, which the round trip
  given leaves out; item 14: 0.28 and 0.32 s.

## 12. Decisions

| Question | Decision |
| --- | --- |
| Where the check runs | In a session of its own, opened from a generated WAV; the user is asked to save first, unless the session open is a check's own |
| How the round trip is measured | Through ordinary takes, not a separate audio IO |
| When a measured figure is used | After **Use this latency**, for the devices and rate it was measured on; a dev run uses the new figure for itself only |
| Dev mode | Any build type that does not start with `release` (`TONY_DEV_CHECKS`); no runtime flag |
| Form of a dev check | A function returning a plain `CheckResult`, not a QtTest function: the suite has to show that each check can fail, which a QVERIFY inside another test cannot |
| How runs are driven | Polling timers and signals, never a nested event loop |
| `test-tony-device` (from `default`) | Its checks moved into the dev run, and it is retired |
| Where the dev checks' tests run | `test-tony-dev`, a third executable in development builds, run when a change touches what the checks drive (`AGENTS.md`) |
| Restart jitter on MME (about 13 ms) | Not tuned away: WASAPI, measured against it, restarts as unsteadily, and is the default for its lower round trip; the stream is kept running between takes on desktop ([audio-drivers.md](audio-drivers.md), §5) |
| The notes merge at a join inside a held note | Fixed on this branch: one note across the join ([takes.md](takes.md)) |

## 13. Facts checked in the code

So that later work does not derive them again.

- **bqaudioio's `PortAudioIO`**, as upstream has it and the fork's `port` still does
  (the fork's per-host-API implementations and settable latency:
  [forks.md](forks.md#bqaudioio)):
  - one duplex `Pa_OpenStream`, `suggestedLatency = 0.2`, no host-API stream info;
  - input goes to the record target **before** output is asked for, in the same callback;
  - `suspend()`/`resume()` are `Pa_StopStream`/`Pa_StartStream`, and do nothing in the
    state they would bring about. `record()` resumes; `MainWindowBase::stop()` suspends
    only where `suspendAudioOnStop()` says so (the svapp fork), which Tony's does on
    Android alone, so on desktop the stream runs from the first take on;
  - it exposes no device names and ignores PortAudio's callback time info.
- **Device rate.**
  - `AudioCallbackPlaySource::getApplicationSampleRate()` and
    `AudioCallbackRecordTarget::getApplicationSampleRate()` both return 0, so the device
    opens at the output device's default rate. PortAudio's MME default is the first of
    44100, 48000, … that the device accepts.
  - `ResamplerWrapper` resamples the play source to it; the record target records at it.
    `MainWindow` sets `Preferences::setFixedSampleRate(44100)`. The wrapper pads with
    silence what its resampler holds back, so at 48 kHz the reference goes out about 53
    frames (1.1 ms) later than the play source counts, which nothing reports; on the fake
    the check measures it as part of the round trip.
  - `TakeAudio::splice()` refuses a recording whose rate differs from the take file's;
    `SingingTakes::spliceRecording()` converts one at another rate than the reference's
    (`TakeAudio::resample()`) before it splices, so a take's file is at the reference's
    rate, and so is what the check judges.
- **The reported latencies** count frames at two rates: `getTargetPlayLatency()` at the
  play source's `getDeviceSampleRate()`, which is the session's when bqaudioio's
  `ResamplerWrapper` converted it, but the device's own when a device was opened before
  any file (the wrapper then passes the figure through and tells the play source 0);
  `getSystemRecordLatency()` at the device's. They differ only when the device is not at
  44.1 kHz.
- **Device choice.** Under `port`, `getDeviceIndex()` takes the first PortAudio device
  with the given name, across host APIs; under a driver, the first of its host API, else
  that host API's default device. MME names are cut to 31 characters.
- **Settings a check must not write:** the toggles `m_recordIntoSelection`
  (`MainWindow/recordintoselection`), `m_playRefWhileRecording` and `m_preRoll` write
  QSettings when toggled; `wantedPreRollFrames()` reads `MainWindow/prerollseconds`.
- **Opening a file.** With "normalise audio" on, a model's `getLocalFilename()` is the
  reader's decoded copy in the temporary directory, not the file opened; the file opened is
  `getLocation()` resolved through `FileSource` (`AudioCheckRunner::mainModelFile()`).
- **When analysis is done** (`AudioCheckRunner::analysing()`): no transform running, no
  ranged run, and the first analysis complete if there is a pitch layer. With automatic
  analysis off the reference never gets one, and the check needs none.
- **After a reopen** a take's pitch and notes are restored from the session, not analysed.
- **The take after Stop:** its model is `analyser2()->getMainModelId()`, normalised and
  resampled as it is read; its file is `m_takes->getAudioPath()`; its coverage
  `m_takes->getCoverage()`.
- **Levels** are of what is handed to the device, before its output latency. The play
  source's `getTargetBlockSize()` is always its default, 1024: `ResamplerWrapper` does not
  pass the device's block on, so the observer bounds a block by the frames received.
- **The fake device.** `FakeAudioIO::Config::loopback` adds the output, the mean of its
  channels, to the input `inputDelay` frames late; the latencies it reports are independent
  of that delay, and `restartShift` moves it at each resume after the first. It reports
  levels only with `reportLevels`.

## 14. The user's runs

- **2026-09-26**, Windows, MME, wired microphone and headphones, one earcup to the
  microphone: Calibrate Audio measured 301 and 295 ms, verdict Unsteady both times (5 to
  15 ms); with the microphone between both cups, Scattered.
- **2026-09-26**, a dev run of an early build, items 1 and 2 only: round trip 303.5 ms; the
  two sweeps of each punch-in agree to 0.2–0.3 ms, but punch-in 1 landed at +0.6 ms and
  punch-in 2 at −12.9 ms; the measured start gap was 0 frames both times. So the finder is
  precise, and what moves is the stream's offset between input and output at each restart.
- **2026-09-26**, whole dev runs on MME at 200 ms and on WASAPI at 20 and 10 ms, each
  after Calibrate Audio: the figures are in [audio-drivers.md](audio-drivers.md), §7.
  Items 4, 9, 10 and 12 passed on all three; 1, 2, 7 and 13 failed on the restart jitter
  on all three; 3 failed on dots at a sound's onset, and 14 on WASAPI on its own misreading
  of a 48 kHz take, both since fixed in the checks.
