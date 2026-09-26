# Manual checklist: what no automated test can tell

The suites run against a fake audio device and an offscreen window. Most of what used to
be on this page is now checked automatically:

- **`TestUiChecks`** (in `test-tony-app`) shows the real window, drives it with key
  presses, mouse gestures and its own dialogs, and judges pane 0 by the pixels on the
  screen. Each of its tests begins with a comment `// Checklist:` quoting the item it
  replaced; `grep -n "Checklist:" main/test/*.h` lists them. With `TONY_TEST_SHOT_DIR` set
  it saves what it looked at as PNG files.
- **Calibrate Audio**, carrying on into the development checks, tries the real device in
  the app itself: section 1.

What is left needs a real device, real ears, or a decision. When an item has been checked,
note the date and the result next to it; when a change touches an area, the items of that
area are what to ask the user to try. Launch with `.\build.bat run`.

## 1. The device check: Calibrate Audio with the dev checks

Once per machine, and again for each output device sung with (Bluetooth headphones have a
latency of their own). It covers: latency on this machine; several recordings in one take,
each in time; nothing of the take coming back out of the speakers; how long Stop takes on a
four-minute song; live dots from whichever input the microphone is on; and a device that
records nothing. Besides those: recording over part of a take, the lead-in, the pre-roll
near the start, two takes meeting inside a note, and Record into Selection stopping by
itself.

It calibrates, then records test references of sweeps and tones through the air, placing
the takes with the round trip it has just measured, for the run only: a four-minute song
with two takes far apart, then a shorter reference with punch-ins, a re-recording over one
of them, a take near the start and two takes that meet. It finds each sweep in the take's
file, and compares the take before and after each punch-in.

1. A development build: any build type but `release` (`build.bat` builds
   `debugoptimized`).
2. Choose the devices under **Playback > Audio Output Device** and **Audio Input Device**
   (or leave the system default): the run records with them, as any take does.
3. With wired headphones, hold one earcup against the microphone, off your ears; with
   speakers, a moderate volume and the microphone where it hears them. A quiet room.
4. **Playback > Calibrate Audio...**, with **Run the dev checks after calibrating** on (it
   is by default), then **Start**. A few minutes; leave the window alone meanwhile (Cancel
   stops the run). The dev checks run only after a calibration that can be used (verdict
   Ok or Unsteady, recorded at the reference's rate, 44.1 kHz). No signal, or a fading one,
   means the microphone did not hear the sweeps, or Windows' audio enhancements took them
   out. The stored latency changes only through **Use this latency**.
5. The report is on the dialog's result page and in `DevChecks.txt` in Tony's application
   data folder (`%APPDATA%\sonic-visualiser\Tony` on Windows). The test session is saved
   beside it in a `dev-checks-<n>` folder and left open, to be looked at and played.

The report's header names the devices, the audio drivers built in, the playback and record
latencies the device reports, and the round trip used. Then, item by item:

- **1** `latency_on_this_machine`: where each sweep of each punch-in landed against the
  reference, in ms (+ is late), within ±2 ms; the same after saving and reopening.
- **2** `several_phrases_in_one_take`: each punch-in's median offset and measured start
  gap; every punch-in of one take within ±2 ms.
- **3** `live_dots`: the dots drawn in each punch-in (more than 10, lying on the reference's
  tones), and how far they trail the cursor, median and spread.
- **4** `nothing_of_the_take_in_the_speakers`: a second arrival of the sweeps (the input
  played back out and heard again), and the loudest output while the reference is silent;
  it passes with neither.
- **5** `mic_on_input_2`: each input's peak, and which one carries the microphone. Judged
  only when that is input 2, where the dots must still be drawn; measured otherwise. With a
  stereo interface, run it once with the microphone on input 2.
- **7** `record_from_a_position`: a re-recording's placement; the take's audio outside its
  range the same bit for bit, its pitch and notes beyond ±0.25 s unchanged.
- **9** `stop_on_a_long_song`: the whole song's analysis time, and each punch-in's time
  from Stop to its pitch merged, which must be under half of it.
- **10** `the_joins`: where two punch-ins meet inside a held tone, the step in the samples
  (dB), the largest gap in the pitch, and one note across the join.
- **12** `nothing_heard_or_changed_in_the_lead_in`: the output in the silent gaps of a
  re-recording's lead-in, and the take before the punch-in unchanged; "not judged" when the
  window stalled over every gap.
- **13** `pre_roll_near_the_start`: a punch-in at 1 s asking for a 3 s pre-roll: playback
  from the song's start and never before it, a countdown no longer than the lead-in there
  is, and placement.
- **14** `record_into_selection_stops_by_itself`: how far past its selection each take
  recorded before it stopped itself, the coverage it added, and no dialog during it.

**What fails on MME today, and why.** Every take restarts the audio stream, and on MME the
offset between input and output moves by about 13 ms from one restart to the next, while
the sweeps within one take agree to 0.3 ms; the start gap does not see it. No one round
trip then places every take within ±2 ms: expect items 1 and 2 to fail, and items 7 and 13
whenever their punch-in lands more than 2 ms off. Item 10 shows two punch-ins that landed
apart only in its number "second punch-in against the first": the join is a 10 ms dip, which
hides a jump. That is the true reading, not a fault of the check: the remedy is a
lower-latency driver, the next project ([calibrate-audio.md](calibrate-audio.md), §10).

A device that opens but delivers nothing ends the run with "The audio device delivered no
input" once the take's lead-in and range and 2 s more have gone by without one frame, and
leaves no take; a device that cannot be opened ends it with "The take did not start".
`TestAudioCheck` checks both on the fake device.

User's PC, 2026-09-26 (MME, wired microphone and headphones, one earcup to the microphone):
Calibrate Audio measured 301 and 295 ms, verdict Unsteady; with the microphone between both
cups, Scattered. A dev run with items 1 and 2 only: round trip 303.5 ms, the two punch-ins
at +0.6 and -12.9 ms, the sweeps of each within 0.3 ms. **The whole dev run not yet.**

Cloud run, 2026-09-25 (Linux, no sound card, with the test executable this replaced): with
no device at all Record does no harm and the next file is analysed (the "Couldn't open
audio device" warning comes back once per file opened); with a device that opens but
delivers nothing the take is dropped quietly, no harm.

## 2. Still by hand

1. **A device in use**: another program holding the microphone exclusively. Record does
   nothing harmful, and the next file opened is analysed as usual.
2. **The practice loop**, the one this is all for: select, Record, hear the lead-in, sing,
   and be back with nothing to press; Play hears it, and it sits in time by ear. Is
   anything else needed to make repeating that pleasant?
3. **Is a 3 s pre-roll right, and is the countdown readable while singing?** (QSettings
   `MainWindow/prerollseconds`; there is deliberately no UI yet.)
4. **Looks**, from the screenshots (`TONY_TEST_SHOT_DIR=../tmp/shots` on a run of
   `test-tony-app`) and in the app: the band along the bottom over waveform and dots; the
   faded and dark brown of the alternate pitch track and the `8vb` / `8va` buttons (in the
   `alternate_pitch_track_colours-window` shot).
   Cloud review, 2026-09-25: the band is a clear orange strip over the grey waveform at
   every zoom; faded brown reads as secondary to the black reference; during a take the
   reference pitch is hidden, so dark brown only has to stand out from the dots, which it
   does. Even 1920 px wide, the bottom toolbar overflows: what comes after Pre-roll, the
   octave buttons included, is behind its » menu. Fonts there are Linux ones; how wide the
   toolbars are on Windows is for the Windows screenshots.
5. **Take operations clear the undo history with no prompt** (all but Rename): acceptable
   in use?
6. **Log out with unsaved takes** on Windows: its test does not run there, because
   `commitData()` writes into the real profile. Afterwards `~/.sv1/tmp-*.ton` is on the
   Recent Files list, opens, and its takes play.
7. **Live dots on this machine**: during a take the dots keep up with the cursor and grow
   smoothly, and neither they nor the cursor stutter, in a maximised window.

## 3. Lyrics

1.  **Legibility**: the words, dark on light boxes along the bottom of the pane, are
    readable over the waveform, the pitch tracks, the alternate pitch track and the live
    dots low in the range. The waveform is pale grey (225, 225, 225) while the lyrics are
    on show: faint enough for the words, still enough to see where the singing is? Where
    a word is longer than its box it runs over the box's edges with a light halo: still
    readable over the waveform, and still clearly that box's word?
2.  **Box edges and rows** at the zoom used while singing: each box's edges are the
    word's start and end, never widened, and words next to each other share an edge. Do
    the words stay in one row, and is the space between two labels enough to tell them
    apart? The font grows as you zoom in (twice the usual size up to four times, never
    more than an eighth of the pane's height), more slowly than the boxes: does zooming in
    put the words that were in the second row back in the first?
3.  **Density**: zoom out until words go to the second row, above the boxes, and then
    drop out (their boxes stay; the highlighted one's word is still drawn) and back in
    (they return). At the zoom you sing at, how many words leave the first row on a fast
    song?
4.  **Bold line starts**: do they read as the start of a phrase, or as noise?
5.  **The left edge**: a word in the first ~30 px of the view is under the pane's vertical
    scale, at the bottom left (scroll so that a word is at the left edge). How much does
    that matter in use?
6.  **Inferred ends**, with an LRC file, which has no end times (the exporter's TTML has
    them). With word timing the last word of a line ends at the next line but at most 2 s
    after it starts, unless a `♪` line marks the end; with line timing a line lasts until
    the next one, and the last line 5 s. Do those boxes mislead, and does the last word of
    a line stay highlighted too long? The start times are exact.
7.  **Hover readout**: with the lyrics shown, hovering over the pitch tracks gives the same
    readout and the same vertical scale as without them, also after turning the alternate
    pitch track off and after deleting a take.
8.  **A real Moises export** of one of your songs, as TTML, word by word, offset 0,
    imported onto the Moises stem or the original mix: the words line up with the vocal, by
    eye and while playing, each box ends where the word does, and the highlight moves with
    the voice and goes off in the gaps. A word Moises split into syllables is one box;
    punctuation is on its word. All early or late by the same amount means the reference
    is not the recording Moises timed, or the exporter's offset was not 0: Shift Lyrics
    (section 4) moves them all. Compare with an LRC export of the same song, gap
    threshold low.
9.  **Finnish text**: ä and ö come out right in the pane, and again after save and reopen.
10. **Show Lyrics and Remove Lyrics**: hiding keeps the words for later, Remove takes them
    out, a second import replaces the first; each makes Close ask whether to save. The
    waveform is pale while the words are on show and grey again when they are hidden or
    removed. The status bar after an import counts words and lines and names anything
    skipped (a TTML file's background vocals and translations, say). Edit Lyrics goes off
    and is greyed out when the words are hidden or removed.
11. **Session**: save and reopen: the same words, hidden or shown as saved, and the
    waveform pale or grey to match; playback still ends at the end of the song, even with
    words past it.
12. **During a take**: the words stay on show and readable while recording, with the
    countdown and the live dots, and the take's waveform is pale as well; Import Lyrics
    and Edit Lyrics are greyed out while recording, and Edit Lyrics is off after it.
13. **The highlight while playing**: the word being sung turns amber as the reference
    reaches it and light again when it ends, in step with the voice, without flicker or
    visible lag. With playback stopped, a click or seek into a word highlights it at once,
    and one into a gap highlights nothing. Is amber readable, and distinct enough?
14. **The highlight while recording**: with a pre-roll, the highlight runs with the
    reference through the lead-in, while the countdown is on the status bar, and on
    through the take from its position; after Stop it is back on the word at the take's
    position. The same with Play Reference While Recording off.

## 4. Lyrics: import and export dialogs, editing

1.  **The file dialogs on Windows.** Import Lyrics opens beside the reference and offers
    "Lyrics (*.ttml *.lrc)" first, then TTML, LRC and all files. Export Lyrics offers
    the reference's name with `.ttml`, beside it; a name typed without `.ttml` gets it
    from the dialog (Tony adds none); replacing a file asks first. A folder that cannot
    be written gives "Could not export lyrics" and leaves any file there as it was. After
    an export the status bar says how many words and lines, and Close does not ask to
    save because of it.
2.  **Export after edits, imported again**: edit a few words, Export Lyrics, Remove
    Lyrics, import the file: the same words, times, bold line starts and title. Does
    another tool (AMLL TTML Tool, say) read the file?
3.  **Edit mode**: with Edit > Edit Lyrics on, the pointer over a box edge in the row
    shows the horizontal-resize cursor and nowhere else; the status bar says what the
    mouse does there and clears when the pointer leaves the row. A click still moves the
    playback cursor, and above the row, or with edit mode off, the mouse does what it
    always did. Is Edit Lyrics easy to find, with no shortcut?
4.  **Dragging an edge**: where two words touch, just left of the line moves the earlier
    word's end and just right of it the later word's start. Is the grab (6 px each side)
    right? An edge stops at the neighbouring word and a word at 20 ms, without jumping;
    the words, their labels and the highlight follow the pointer; one Ctrl+Z takes back
    one whole drag, and Close asks whether to save.
5.  **The text dialog and the words' menu**: a double-click in a word asks for its text
    (the first click of it also moves the playback cursor there: acceptable?); empty text
    or Cancel changes nothing. A right-click in the row gives "Edit Word Text..." and
    "Delete Word" on a word, "Add Word..." between words (greyed where the gap is under
    20 ms); a new word is 0.5 s long or reaches the next word, joins the nearer
    neighbour's line and is bold if it starts it. A right-click above the row still gives
    Tony's own menu. Is the wording right?
6.  **High-DPI**: on a screen scaled to 150 % or 200 %, the resize cursor appears exactly
    over the box edges, the row the mouse edits is the row of boxes (not above or below
    it), and a drag keeps the edge under the pointer.
7.  **Drag smoothness**: with a whole song's words (hundreds) in view, and again while the
    reference plays, a drag follows the pointer without stutter and playback does not
    break up.
8.  **Shifting the whole song**: on a real Moises file that is all early or late, with
    Edit Lyrics on, Shift-drag anywhere in the row (on a word, an edge or a gap): the
    cursor is a closed hand, all the words and the highlight follow the pointer, none
    goes before the start of the song, and one Ctrl+Z takes the whole drag back. Is
    lining them up by eye and ear while playing easy enough, and does a whole song's worth
    move without stutter? Then Edit > Shift Lyrics... with a known offset (0.2 for an
    export made with the exporter's default offset, -0.2 the other way): the words move
    by that much, the status bar says how far and which way, and an Export writes the
    shifted times. Is the dialog's wording clear about which sign is earlier?

The questions the automated checks raised, and the facts they established for the
decisions above, are in [open-points.md](open-points.md).
