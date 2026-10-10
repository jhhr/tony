Tony for singing practice
=========================

A singing practice aid: record yourself over a reference song and see your
pitch next to the singer's, phrase by phrase, with takes and timed lyrics.

This is a fork of [Tony](https://github.com/sonic-visualiser/tony), the
melody transcription program from Queen Mary, University of London. It keeps
Tony's pitch and note analysis (the pYIN plugin) and builds a practice tool
around it. It is developed separately and is not a release of Tony: the Tony
home page and its downloads are for the original program, not this one.

It runs on Windows, Linux and macOS, and on Android phones. There are no
releases of this fork yet: the desktop version is built from source (see
[Building](#building) below), and the Android one comes as a test build (see
[On Android](#on-android)).


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
   and Playback -> Audio Output Device. On a phone, Audio Input Device lists the
   phone's microphone, a wired headset and a USB microphone (a wireless
   receiver, say), and the output is the phone's own choice. Android records
   from a USB or wired microphone by itself when one is plugged in; the menu
   is for choosing the phone's microphone instead, or one of two. A choice
   that is not plugged in records from the phone's choice, and the status bar
   says so
 * a microphone on one input of an interface with two: Playback -> Input
   Channel -> Input 1 (or Input 2) makes each take from that input alone, so
   that it plays back in both ears and the live dots, the analysis and the
   voice threshold hear it at its own level. With Both Inputs, the default, a
   take of a microphone on one input plays in one ear only. The choice is kept
   for each input device
 * on Windows, Playback -> Audio Driver chooses MME, DirectSound or WASAPI, and
   Playback -> Audio Latency how much latency is asked of it (10 to 200 ms);
   each driver keeps its own devices, latency and measured round trip. WASAPI
   at 20 ms is the default
 * the latency of the audio devices is taken off the start of each recording,
   so that it lines up with the reference. Drivers often report that latency
   wrongly: Playback -> Calibrate Audio... measures it, with one earcup of
   wired headphones held against the microphone, and "Use this latency" places
   every take with the measured figure. Playback -> Forget Measured Latency
   goes back to the figure the driver reports
 * the audio device is kept open between takes, so that the latency stays the
   same from one take to the next. The microphone therefore shows as in use
   from the first take until Tony quits
 * singing with the music on speakers instead of headphones: the microphone
   then hears the music as well, and its pitch would be drawn as yours.
   Playback -> Voice Threshold sets a level (Off, or -50 to -15 dBFS) under
   which what the microphone hears is not taken for singing: it gets no dots
   while you record, and no pitch or notes when the take is analysed. The
   audio itself is recorded as it is. To choose it, record the music alone
   with the threshold Off, then raise it a step at a time until the music
   alone gives no dots, or play the music in Playback -> Check Input Level's
   silence and take the threshold it suggests. Your voice has to be well above that: a microphone of
   low sensitivity, sung into from close by, keeps the music far below it. A
   new threshold applies from the next take on; Analysis -> Analyse Now!
   applies it to what the take on show has recorded already, so a threshold
   set too high is put right by lowering it and analysing again
 * File -> Load Singing Track... (Ctrl+Shift+R) loads a recording made
   elsewhere as the singing track instead


Microphone and levels
---------------------

 * a close microphone: a headset, or a microphone a hand's width from the
   mouth. It hears the voice far above the room and the music, which the live
   dots and the voice threshold rely on
 * an audio interface with zero-latency (direct) monitoring, so that you hear
   your own voice in the headphones as you sing. Tony does not play the
   microphone back to you itself: its round trip is tens of milliseconds, and
   you would hear it as an echo of the voice you hear through your own head
 * turn off the interface's processing: automatic level, noise reduction,
   compression. They change the take, and the level the meter and the voice
   threshold read; Windows' own audio enhancements likewise
 * a microphone on one input of an interface with two: Playback -> Input
   Channel (see Recording above)
 * set the gain on the interface so that your loudest singing peaks around
   -10 dBFS. Playback -> Check Input Level... listens to two seconds of
   silence and then your loudest phrase, and says how many dB to turn the gain
   up or down, the noise floor, and a voice threshold over it. The meter beside
   Record shows the input's peak as you sing, with a tick at the voice
   threshold (which compares a level about 10 dB under a voice's peaks: your
   peaks should be well over the tick) and a light that turns red when the
   input reaches full scale; a click puts it out, and so does the next take.
   After each take the status bar gives the take's peak, or where it clipped.
   Leave Windows' input volume for the interface at 100 and set the gain on
   the interface: the meter can only see clipping in the converter. A
   microphone that distorts in its own electronics has to be heard
 * avoid a Bluetooth headset's microphone (its "Hands-Free" device on
   Windows): Bluetooth gives it only through a call's link, which puts the
   microphone and the headphones both at telephone quality
 * after changing the microphone, the interface, the headphones or the driver,
   run Playback -> Calibrate Audio... again: each has its own latency
 * a wireless microphone, such as a headset on a RØDE Wireless PRO or GO
   transmitter, its receiver plugged in as a USB microphone or into an
   interface's input:
    * set the receiver to merged mode with no safety channel, so that the voice
      is on both channels at its own level. In split mode, or with the safety
      channel (a copy 10 dB quieter on the second channel), choose its channel
      in Playback -> Input Channel: Both Inputs would average it with the other
      channel and read it 6 or 3.6 dB low
    * turn GainAssist, or any automatic level, off: it changes the take's level
      as you sing, and the level the voice threshold reads
    * keep any high-pass filter off, or at its lowest: low sung notes are near
      80 Hz
    * wear the transmitter at the front of your body, in sight of the receiver:
      its 2.4 GHz radio does not pass through a person, and a dropout is a gap
      of silence in the take
    * run Playback -> Calibrate Audio... again for the wireless chain: the
      radio adds its own latency
    * use one device for both the input and the headphones (the receiver's
      analog output into an interface, say). With the receiver as the input and
      the headphones on another device, the two run on two clocks, and takes
      drift against the reference by a few milliseconds over a long session
    * let the transmitter record on its own as well, as a backup: where the
      radio dropped out during a take, Takes -> Replace Take Audio from
      Recording... finds the take's singing in the transmitter's file and puts
      it in the take's place (below)


Takes and editing
-----------------

 * a take is one recording or many. A strip along the bottom of the pane shows
   where there is singing and where there is not; recording over a part of it
   replaces just that part, and only the part that changed is analysed again
 * Edit -> Select Recording at Playhead and Edit -> Erase Singing in Selection
   (Ctrl+D) remove, trim or split what has been recorded. A range is selected by
   dragging in the thin ruler strip below the pane
 * recordings and erases can be undone and redone
 * Takes -> Replace Take Audio from Recording... puts the take's singing back
   from a recording of it made elsewhere, such as a wireless transmitter's own
   backup, where the radio dropped out during the take. Pick the transmitter's
   WAV file: Tony finds where in it each part of the take was sung (a
   punch-in was sung at another time, and is found on its own), refuses if
   any part is not there, and otherwise puts the file's audio in place of the
   take's, at the take's level, and analyses it again. It says where each part
   was found and how alike it was, and whether the transmitter's clock and
   the receiver's drifted apart over the take (the audio is not stretched to
   fit). One Undo puts the take back as it was
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
   button after "Background:" in the toolbar turns it on and off. The session
   remembers it, with its level and pan and whether it was on


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


Sizes and layout
----------------

 * View -> Plot Size draws the pitch tracks, the live dots and the notes at
   100, 150 or 200 % of their normal size, and View -> Lyrics Size the words of
   the lyrics at 35 to 100 %
 * View -> Compact Layout puts one toolbar of large buttons in place of the
   menu bar and the other toolbars, as on a phone (see below). Tony starts
   with it when given --compact on the command line


On Android
----------

Tony also runs on Android phones (Android 9 or later, 64-bit ARM), laid out
for a phone held in landscape. On the phone it is for practising: open a
session, choose a take, select a phrase, record, listen, erase and undo.
Editing notes and the reference is left to the desktop, and their tools are
hidden.

 * there is no release yet. Each run of the Android CI workflow (under
   Actions) builds a test version and keeps it as its Tony-debug-apk
   artifact, which GitHub lets you download once signed in. Unzip it and open
   Tony-debug.apk on the phone, allowing the app you open it with to install
   unknown apps
 * one toolbar of large buttons replaces the menus and the other toolbars: a
   menu button that holds every menu, Play, Record and Record into Selection,
   the take box, Undo and Redo, Erase, Zoom In and Zoom Out, and a button
   that shows and hides the Show and Play controls. A thin strip above the
   pane shows the whole song, with a faint line of the reference's pitch:
   drag it, or tap it, to move the view along the song
 * one finger works as the mouse does: a tap moves the playhead, and a drag
   in the ruler strip below the pane selects. Pinch to zoom, across the pane
   for time and up and down it for the pitch range; drag with two fingers to
   scroll; press and hold for the pane's menu
 * with All files access (Android 11 and later; Tony asks the first time it
   needs it) Tony opens and saves files where they are, as on the desktop.
   Keep sessions, with their audio and takes folder beside them, in a folder
   of the phone's storage, such as one a sync app (Syncthing, FolderSync)
   keeps in step with a computer. Without it, Tony copies audio into its own
   storage and cannot open a session kept elsewhere. Nor can a session be
   opened from a cloud app such as Drive, which hands Tony the one file only
 * M4A (AAC), FLAC and Ogg files are read through Android's own decoders
 * Tony plays and records through whatever the phone is using: its speaker and
   microphone, a wired or USB headset, or Bluetooth. Calibrate Audio keeps a
   figure for each, since a Bluetooth route is 100 to 200 ms longer than the
   speaker's. The screen stays on while the check runs
 * when Tony goes into the background, a take being recorded is stopped and
   kept, as Stop does, and a session that already has a file is saved. The
   audio device is let go then, and after a while idle
 * pitch tracks and notes are drawn at 150 % and lyrics at 50 % by default
 * Help -> Save Log... saves a copy of Tony's log, to send when something has
   gone wrong


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

The Android app is cross-compiled on Linux by the scripts in
[deploy/android/](deploy/android/): `setup-toolchain.sh`, `build-qt.sh`,
`build-deps.sh`, `build-tony.sh` and `build-apk.sh`, in that order, as the
Android CI workflow runs them.


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
 * Android CI build: [![Build Status](https://github.com/jhhr/tony/actions/workflows/android.yml/badge.svg)](https://github.com/jhhr/tony/actions/workflows/android.yml)
