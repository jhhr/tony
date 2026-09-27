Tony for singing practice
=========================

A singing practice aid: record yourself over a reference song and see your
pitch next to the singer's, phrase by phrase, with takes and timed lyrics.

This is a fork of [Tony](https://github.com/sonic-visualiser/tony), the
melody transcription program from Queen Mary, University of London. It keeps
Tony's pitch and note analysis (the pYIN plugin) and builds a practice tool
around it. It is developed separately and is not a release of Tony: the Tony
home page and its downloads are for the original program, not this one. There
are no ready-made builds of this fork yet; see [Building](#building) below.


How it works
------------

Open a recording of the song with File -> Open. The pitch tracker follows one
voice, so the reference works best as the vocal on its own, such as a stem
separated from the mix. Tony analyses it and draws its pitch as a black line.

In the toolbar, turn on the speaker button after "While recording:", which
plays the reference while you record. Put on headphones (a microphone that
hears the reference picks up its pitch as well as yours), then press Record
(Ctrl+Space) and sing along. Your pitch appears as orange dots as you sing.
When you press Stop, the recording is analysed in full and becomes an orange
pitch track in the same pane as the reference's, so you can see where you were
sharp, flat, early or late.


Recording
---------

 * record from the playback position, not only from the start of the song, so
   you can practise one phrase without singing everything before it
 * an optional pre-roll: the reference starts a few seconds early and the
   status bar counts you in, and nothing sung during the lead-in is kept
 * an optional "record into the selection only": select the phrase, and the
   recording starts and stops at the ends of the selection by itself. This
   and the pre-roll are buttons in the toolbar, after "While recording:"
 * the microphone and the output are chosen in Playback -> Audio Input Device
   and Playback -> Audio Output Device
 * the latency of the audio devices is taken off the start of each recording,
   so that it lines up with the reference. Drivers often report that latency
   wrongly: Playback -> Calibrate Audio... measures it, with one earcup of
   wired headphones held against the microphone, and "Use this latency" places
   every take with the measured figure. Playback -> Forget Measured Latency
   goes back to the figure the driver reports
 * File -> Load Singing Track... (Ctrl+Shift+R) loads a recording made
   elsewhere as the singing track instead


Takes and editing
-----------------

 * a take is one recording or many. A strip along the bottom of the pane shows
   where there is singing and where there is not; recording over a part of it
   replaces just that part, and only the part that changed is analysed again
 * Edit -> Select Recording at Playhead and Edit -> Erase Singing in Selection
   (Ctrl+D) remove, trim or split what has been recorded. A range is selected by
   dragging in the thin ruler strip below the pane
 * recordings and erases can be undone and redone
 * several takes of a song in one session (the Takes menu and the "Take:" box in
   the toolbar): each keeps its own audio, pitch track and notes, and switching
   between them needs no re-analysis. The audio of a session's takes is kept in
   a folder named after the session beside it, so the two can be moved together


Following the reference
-----------------------

 * an alternate pitch track: a copy of the reference's pitch track moved by
   one to three octaves, for a song written for a higher or lower voice than
   yours. Turn it on with the button after "Follow:" in the toolbar and move
   it with the 8vb and 8va buttons. While it is on, the reference's own track
   is hidden as you record, so that the one you follow is the one on show
 * background music: File -> Load Background Music... loads a second audio
   file, such as the accompaniment or the full mix, that plays along with the
   reference, while recording as well, but is never analysed. The speaker
   button after "Background:" in the toolbar turns it on and off. It is not
   saved with the session


Timed lyrics
------------

 * File -> Import Lyrics... reads a TTML or LRC file, timed by word or by line,
   and shows the words in boxes along the bottom of the pane, at the time and
   for as long as each is sung. The word at the playback position is
   highlighted, while playing, while recording and wherever you click, and the
   waveform is faded while the words are on show so that they can be read over
   it. They are saved with the session; View -> Show Lyrics hides them, File ->
   Remove Lyrics takes them out, and File -> Export Lyrics... writes them, as
   they are now, to a TTML file. The reference must be the recording the lyrics
   were timed to (a Moises stem and its original mix share a timeline)
 * the lyrics can be corrected by ear, with the song there to listen to: with
   Edit -> Edit Lyrics on, drag the start or end of a word in its box,
   double-click a word to change its text, or right-click to delete a word or
   add one between words. Lyrics that are all early or late move together:
   Shift-drag in the row, or Edit -> Shift Lyrics... by a number of seconds.
   Each edit can be undone; the edits are saved with the session, and Export
   Lyrics writes them
 * lyrics can be exported from Moises with the Moises-Lyric-Exporter browser
   extension. Set it to TTML, word by word, with the offset at 0 (otherwise
   every line is 0.2 s early, and Tony cannot tell): TTML gives every word
   its end as well as its start, which LRC cannot. Its gap threshold only
   matters for LRC. It is unofficial and not made by Moises: install it
   unpacked from a commit that has been reviewed, and do not update it without
   reviewing the change


From Tony
---------

Everything the original program does is still here, for the reference and the
singing alike:

 * robust monophonic pitch track extraction (using pYIN)
 * note track extraction
 * facility to manually adjust pitch track and note track
 * facility to audition pitch and note track
 * note pitch automatically snaps to pitch track
 * import/export of pitch track and note track


Building
--------

The fork is developed on Windows with MSYS2's MinGW-w64 toolchain, Qt 6,
meson and ninja; it also builds on Linux and macOS. How to build, and every way
the build has gone wrong, is in [docs/building.md](docs/building.md). The rest
of [docs/](docs/) is the developer documentation.


Authors, Citation, License and Use
----------------------------------

Tony was developed at Queen Mary, University of London in
collaboration with New York University.

Code copyright 2005-2019 Chris Cannam, Queen Mary University of
London, and the Tony project authors: Matthias Mauch, George Fazekas,
Justin Salamon, and Rachel Bittner, except where indicated in the
individual source files. Thanks also to Simon Dixon and Juan Bello.

The singing practice features were added in this fork, under the same
license.

If you make use of this software for any public or commercial purpose,
we ask you to kindly mention the authors and Queen Mary, University of
London in your user-visible documentation. We're very happy to see
this sort of use but would much appreciate being credited, separately
from the requirements of the software license itself (see below).

If you make use of this software for academic purposes, please cite
the Tony paper given in the [CITATION](CITATION) file.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or (at
your option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details. You should have received a
copy of the GNU General Public License along with this program. If
not, see http://www.gnu.org/licenses/.


Automated build reports
-----------------------

 * Linux CI build: [![Build Status](https://github.com/jhhr/tony/actions/workflows/linux.yml/badge.svg)](https://github.com/jhhr/tony/actions/workflows/linux.yml)
 * macOS CI build: [![Build Status](https://github.com/jhhr/tony/actions/workflows/macos.yml/badge.svg)](https://github.com/jhhr/tony/actions/workflows/macos.yml)
 * Windows CI build: [![Build Status](https://github.com/jhhr/tony/actions/workflows/windows.yml/badge.svg)](https://github.com/jhhr/tony/actions/workflows/windows.yml)
