# Manual checklist: what no automated test can tell

The automated suites run against a fake audio device and an offscreen window. Everything
below needs a real device, real ears or real eyes. **As of 2026-09-20 none of it has been
tried by hand.** When an item has been checked, note the date and the result next to it;
when a change touches an area, the items of that area are what to ask the user to try.

Launch with `.\build.bat run`.

## Latency and live feedback

1. **Latency on this machine.** Play Reference While Recording on, headphones, clap along
   with a reference with a clear onset. Afterwards the take lines up with the reference by
   eye and by ear; still does after save and reopen.
2. **Several phrases in one take.** Record two or three phrases at different positions:
   every one sits in time, not just the first (each recording measures its start gap).
3. **Live dots** appear under the playback cursor, not behind it; stay after Stop until the
   orange pitch track replaces them; the status bar stops changing when the take stops.
   Recording over singing that is there: that take's own pitch track and notes are out of
   sight for the take, so only the dots and the track being followed are on the pane, and
   they are back when the take stops.
4. **Nothing of the take in the speakers while recording**: with speakers on, neither your
   voice nor a synth tone comes back. Play Singing Audio keeps its state through the take.
5. **Stereo interface with the mic on input 2**: dots appear.
6. **No input device / device in use**: Record does nothing harmful, and the next file
   opened is analysed as usual.

## Recording from a position

7. Seek into the song, Record, sing, Stop: the singing is where it was sung and the part
   before it is untouched. Record again inside it: the overwrite question comes; No records
   nothing. "Don't ask again" with Yes holds across sessions.
8. During a take at P > 0 the cursor starts at P, the pane follows it, and cursor,
   reference and dots are in the same place.
9. **How long Stop takes on a 4-minute song**: a moment for the file copy, then new pitch
   only where the singing was. A pause like a whole-song analysis means the ranged path
   did not happen.
10. **The joins**: no click at the edges of a new range; the pitch track runs through the
    join without a hole, a doubled dot or one note showing as two. Pitch and notes outside
    the recorded range (± about 0.25 s) must not flicker or move at all.

## Pre-roll and Record into Selection

11. **Is 3 s right, is the countdown readable while singing?** (QSettings
    `MainWindow/prerollseconds`; there is deliberately no UI yet.)
12. Sing through the lead-in: nothing of it is heard back, and nothing before P changed.
13. Pre-roll less than 3 s from the start of the song: shorter countdown, no attempt to
    run from before frame 0.
14. Record into Selection stops by itself about 0.25 s after the end has been sung; what is
    added is exactly the selection; no overwrite question.
15. **Both together — the practice loop this is all for**: select, Record, hear the
    lead-in, sing, and be back with nothing to press; Play hears it. Is anything else
    needed to make repeating that pleasant?
16. Constrain Playback to Selection together with a pre-roll: the lead-in is probably cut
    short. Should the two be kept apart?

## Coverage strip and erase

17. The band is readable over waveform and dots at every zoom: height, colour, gaps.
18. It cannot be touched: clicking and dragging on it with either tool creates, moves or
    selects nothing and does not change the pane's scale.
19. Select Recording at Playhead then Erase: audio silent, bar gone, pitch and notes gone,
    no analysis afterwards. Erasing the middle of a long note leaves two.
20. Erase and Select Recording are greyed out with no take, no selection, while recording,
    and for the second or two of analysis after Stop — and come back by themselves.

## Undo

21. Three recordings, Ctrl+Z three times: each takes back exactly one (audio, coverage,
    band, pitch together). The menu says "Record Singing" / "Erase Singing", never anything
    about a layer or pane.
22. Ctrl+Z immediately after Stop, before the pitch appears: nothing of that analysis
    lands later; redo analyses again.
23. Undo of the very first recording leaves no singing track at all, and Record still
    works.

## Takes

24. New Empty Take, record, switch back and forth: fast, no analysis, each take with its
    own audio, pitch, notes and band. The inactive take is silent, and playback stops at
    the end of the take on show even when another is longer.
25. Duplicate, record into the copy: the original is untouched.
26. Delete asks first and never deletes an audio file. Rename keeps the undo history —
    every other take operation clears it without a prompt: acceptable in use?
27. Combo and Takes menu are greyed out during a take.
28. Analyse Now on a take re-analyses all of its coverage in place.

## Sessions and files

29. Before the first save, take files go to the record directory; after it, to
    `<session>.takes/`. Closing leaves each take's file, files any saved session named,
    and nothing else Tony wrote. `recorded-*.wav` are never deleted.
30. Move `.ton` and folder together: everything plays. Move the `.ton` alone: exactly one
    warning naming the folder, takes shown without sound, no "locate it?" question.
31. Save As copies the takes; the old `.ton` still opens and plays.
32. A `.ton` from before the takes work opens with the reference only and no dialog.
33. Open a `.ton`, Load Singing Track or Load Background Music, then Record: no crash, time
    ruler still there. Closing afterwards asks whether to save.
34. Stop a take and close the window at once: no crash.
35. Log out with unsaved takes: what `commitData` writes into `~/.sv1` is playable.

## Looks

36. Alternate pitch track: faded brown is readable but secondary; dark brown during a take
    is distinct from black; `8vb` / `8va` buttons look acceptable; the track stays in view
    after an octave step.

## Lyrics

37. **Legibility**: the words, dark on light boxes along the bottom of the pane, are
    readable over the waveform, the pitch tracks, the alternate pitch track and the live
    dots low in the range. The waveform is pale grey (225, 225, 225) while the lyrics are
    on show: faint enough for the words, still enough to see where the singing is? Where
    a word is longer than its box it runs over the box's edges with a light halo: still
    readable over the waveform, and still clearly that box's word?
38. **Box edges and rows** at the zoom used while singing: each box's edges are the
    word's start and end, never widened, and words next to each other share an edge. Do
    the words stay in one row, and is the space between two labels enough to tell them
    apart? The font grows as you zoom in (twice the usual size up to four times, never
    more than an eighth of the pane's height), more slowly than the boxes: does zooming in
    put the words that were in the second row back in the first?
39. **Density**: zoom out until words go to the second row, above the boxes, and then
    drop out (their boxes stay; the highlighted one's word is still drawn) and back in
    (they return). At the zoom you sing at, how many words leave the first row on a fast
    song?
40. **Bold line starts**: do they read as the start of a phrase, or as noise?
41. **The left edge**: a word in the first ~30 px of the view is under the pane's vertical
    scale, at the bottom left (scroll so that a word is at the left edge). How much does
    that matter in use?
42. **Inferred ends**, with an LRC file, which has no end times (the exporter's TTML has
    them). With word timing the last word of a line ends at the next line but at most 2 s
    after it starts, unless a `♪` line marks the end; with line timing a line lasts until
    the next one, and the last line 5 s. Do those boxes mislead, and does the last word of
    a line stay highlighted too long? The start times are exact.
43. **Hover readout**: with the lyrics shown, hovering over the pitch tracks gives the same
    readout and the same vertical scale as without them, also after turning the alternate
    pitch track off and after deleting a take.
44. **A real Moises export** of one of your songs, as TTML, word by word, offset 0,
    imported onto the Moises stem or the original mix: the words line up with the vocal, by
    eye and while playing, each box ends where the word does, and the highlight moves with
    the voice and goes off in the gaps. A word Moises split into syllables is one box;
    punctuation is on its word. All early or late by the same amount means the reference
    is not the recording Moises timed (Tony cannot shift the whole song; an `[offset:]`
    line added to an LRC file can). Compare with an LRC export of the same song, gap
    threshold low.
45. **Finnish text**: ä and ö come out right in the pane, and again after save and reopen.
46. **Show Lyrics and Remove Lyrics**: hiding keeps the words for later, Remove takes them
    out, a second import replaces the first; each makes Close ask whether to save. The
    waveform is pale while the words are on show and grey again when they are hidden or
    removed. The status bar after an import counts words and lines and names anything
    skipped (a TTML file's background vocals and translations, say). Edit Lyrics goes off
    and is greyed out when the words are hidden or removed.
47. **Session**: save and reopen: the same words, hidden or shown as saved, and the
    waveform pale or grey to match; playback still ends at the end of the song, even with
    words past it.
48. **During a take**: the words stay on show and readable while recording, with the
    countdown and the live dots, and the take's waveform is pale as well; Import Lyrics
    and Edit Lyrics are greyed out while recording, and Edit Lyrics is off after it.
49. **The highlight while playing**: the word being sung turns amber as the reference
    reaches it and light again when it ends, in step with the voice, without flicker or
    visible lag. With playback stopped, a click or seek into a word highlights it at once,
    and one into a gap highlights nothing. Is amber readable, and distinct enough?
50. **The highlight while recording**: with a pre-roll, the highlight runs with the
    reference through the lead-in, while the countdown is on the status bar, and on
    through the take from its position; after Stop it is back on the word at the take's
    position. The same with Play Reference While Recording off.

## Lyrics: import and export dialogs, editing

51. **The file dialogs on Windows.** Import Lyrics opens beside the reference and offers
    "Lyrics (*.ttml *.lrc)" first, then TTML, LRC and all files. Export Lyrics offers
    the reference's name with `.ttml`, beside it; a name typed without `.ttml` gets it
    from the dialog (Tony adds none); replacing a file asks first. A folder that cannot
    be written gives "Could not export lyrics" and leaves any file there as it was. After
    an export the status bar says how many words and lines, and Close does not ask to
    save because of it.
52. **Export after edits, imported again**: edit a few words, Export Lyrics, Remove
    Lyrics, import the file: the same words, times, bold line starts and title. Does
    another tool (AMLL TTML Tool, say) read the file?
53. **Edit mode**: with Edit > Edit Lyrics on, the pointer over a box edge in the row
    shows the horizontal-resize cursor and nowhere else; the status bar says what the
    mouse does there and clears when the pointer leaves the row. A click still moves the
    playback cursor, and above the row, or with edit mode off, the mouse does what it
    always did. Is Edit Lyrics easy to find, with no shortcut?
54. **Dragging an edge**: where two words touch, just left of the line moves the earlier
    word's end and just right of it the later word's start. Is the grab (6 px each side)
    right? An edge stops at the neighbouring word and a word at 20 ms, without jumping;
    the words, their labels and the highlight follow the pointer; one Ctrl+Z takes back
    one whole drag, and Close asks whether to save.
55. **The text dialog and the words' menu**: a double-click in a word asks for its text
    (the first click of it also moves the playback cursor there: acceptable?); empty text
    or Cancel changes nothing. A right-click in the row gives "Edit Word Text..." and
    "Delete Word" on a word, "Add Word..." between words (greyed where the gap is under
    20 ms); a new word is 0.5 s long or reaches the next word, joins the nearer
    neighbour's line and is bold if it starts it. A right-click above the row still gives
    Tony's own menu. Is the wording right?
56. **High-DPI**: on a screen scaled to 150 % or 200 %, the resize cursor appears exactly
    over the box edges, the row the mouse edits is the row of boxes (not above or below
    it), and a drag keeps the edge under the pointer.
57. **Drag smoothness**: with a whole song's words (hundreds) in view, and again while the
    reference plays, a drag follows the pointer without stutter and playback does not
    break up.
