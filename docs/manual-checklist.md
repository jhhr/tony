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
