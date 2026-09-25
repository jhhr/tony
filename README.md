
Tony
====

Tony is a program for computer-aided melody annotation. It has a
graphical interface based on the Sonic Visualiser libraries, and uses
the pYIN Vamp plugin to extract pitch track and notes from monophonic
audio.

![Tony small screenshot](https://code.soundsoftware.ac.uk/attachments/download/1069/tony-screeny-20140328-30pc.png)

Home page and downloads: https://code.soundsoftware.ac.uk/projects/tony


Features
--------

 * robust monophonic pitch track extraction (using pYIN)
 * note track extraction
 * facility to manually adjust pitch track and note track
 * facility to audition pitch and note track
 * note pitch automatically snaps to pitch track
 * import/export of pitch track and note track


Singing practice
----------------

This fork adds a singing practice mode: a reference recording is loaded and
analysed as usual, and your own singing is recorded alongside it as a second,
orange pitch track to compare with it.

 * record from the playback position, not only from the start of the song, so
   you can practise one phrase without singing everything before it
 * an optional pre-roll: the reference starts a few seconds early and the
   status bar counts you in, and nothing sung during the lead-in is kept
 * an optional "record into the selection only": select the phrase, and the
   recording starts and stops at the ends of the selection by itself
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
 * timed lyrics: File -> Import Lyrics... reads an LRC file, timed by line or
   by word, and shows the words in boxes along the bottom of the pane, at the
   time and for as long as each is sung. The word at the playback position is
   highlighted, while playing, while recording and wherever you click, and the
   waveform is faded while the words are on show so that they can be read over
   it. They are saved with the session; View -> Show Lyrics hides them and
   File -> Remove Lyrics takes them out. The reference must be the recording
   the lyrics were timed to (a Moises stem and its original mix share a
   timeline); to change a word or its time, edit the file and import it again
 * LRC files can be exported from Moises with the Moises-Lyric-Exporter browser
   extension. Set its offset to 0 (otherwise every line is 0.2 s early, and
   Tony cannot tell) and its gap threshold as low as it goes (so that it marks
   where lines end). It is unofficial and not made by Moises: install it
   unpacked from a commit that has been reviewed, and do not update it without
   reviewing the change


Authors, Citation, License and Use
----------------------------------

Tony was developed at Queen Mary, University of London in
collaboration with New York University.

Code copyright 2005-2019 Chris Cannam, Queen Mary University of
London, and the Tony project authors: Matthias Mauch, George Fazekas,
Justin Salamon, and Rachel Bittner, except where indicated in the
individual source files. Thanks also to Simon Dixon and Juan Bello.

If you make use of this software for any public or commercial purpose,
we ask you to kindly mention the authors and Queen Mary, University of
London in your user-visible documentation. We're very happy to see
this sort of use but would much appreciate being credited, separately
from the requirements of the software license itself (see below).

If you make use of this software for academic purposes, please cite
one of the publications indicated on the Publications page:
https://code.soundsoftware.ac.uk/publications?project_id=tony

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or (at
your option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A ARTICULAR PURPOSE. See the GNU
General Public License for more details. You should have received a
copy of the GNU General Public License along with this program. If
not, see http://www.gnu.org/licenses/.


Automated build reports
-----------------------

 * Linux CI build: [![Build Status](https://github.com/sonic-visualiser/tony/workflows/Linux%20CI/badge.svg)](https://github.com/sonic-visualiser/tony/actions?query=workflow%3A%22Linux+CI%22)
 * macOS CI build: [![Build Status](https://github.com/sonic-visualiser/tony/workflows/macOS%20CI/badge.svg)](https://github.com/sonic-visualiser/tony/actions?query=workflow%3A%22macOS+CI%22)
 * Windows CI build: [![Build Status](https://github.com/sonic-visualiser/tony/workflows/Windows%20CI/badge.svg)](https://github.com/sonic-visualiser/tony/actions?query=workflow%3A%22Windows+CI%22)

