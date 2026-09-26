# Manual checklist: what no automated test can tell

The suites run against a fake audio device and an offscreen window. Most of what used to
be on this page is now checked automatically:

- **`TestUiChecks`** (in `test-tony-app`) shows the real window, drives it with key
  presses, mouse gestures and its own dialogs, and judges pane 0 by the pixels on the
  screen. Each of its tests begins with a comment `// Checklist:` quoting the item it
  replaced; `grep -n "Checklist:" main/test/*.h` lists them. With `TONY_TEST_SHOT_DIR` set
  it saves what it looked at as PNG files.
- **`test-tony-device`** checks the real device: section 1.

What is left needs a real device, real ears, or a decision. When an item has been checked,
note the date and the result next to it; when a change touches an area, the items of that
area are what to ask the user to try. Launch with `.\build.bat run`.

## 1. The device check

Once per machine, and again for each output device sung with (Bluetooth headphones have a
latency of their own). It covers: latency on this machine; several recordings in one take,
each in time; nothing of the take coming back out of the speakers; how long Stop takes on a
four-minute song; live dots from whichever input the microphone is on; and a device that
records nothing.

It plays a four-minute reference of a tone and clicks, records it through the air at two
places into one take, and measures where the clicks landed in the take.

1. In Tony, choose the devices under **Playback > Audio Output Device** and **Audio Input
   Device** (or leave the system default). The check reads that choice, and nothing else,
   from Tony's settings.
2. Speakers at a moderate volume and the microphone where it hears them; with headphones,
   hold an ear cup against the microphone. A quiet room. It takes about a minute.
3. From Git Bash:

   ```sh
   export PATH="/c/msys64/mingw64/bin:$PATH" MINGW_PREFIX="C:/msys64/mingw64"
   ninja -j 3 -C build_mingw test-tony-device.exe > tmp/build.log 2>&1; echo "exit:$?" >> tmp/build.log
   cd build_mingw && mkdir -p ../tmp/tl
   TONY_TEST_LOG_DIR=../tmp/tl ./test-tony-device.exe > ../tmp/test.log 2>&1; echo "exit:$?"
   grep -a "^FAIL\|^   Loc\|^QINFO\|^Totals" ../tmp/tl/TestRealDevice.txt
   ```

4. Read the `QINFO` lines, one per recording: `clicks +x ms from the reference` (within
   ±10 ms passes; the sign says late or early), `match` (below 0.1 the microphone did not
   hear the speakers), `stop took`, `live dots`, and `channels (dB)`, which shows which
   input the microphone is on. With a stereo interface, run it with the microphone on
   input 2: the dots must still be there.

`TONY_DEVICE_CHECK_FAKE=n` runs it with no hardware, on the fake device with its output fed
back into its input `n` frames later than it reports: for checking the check.

Cloud run, 2026-09-25 (Linux, no sound card): with no device at all Record does no harm and
the next file is analysed (the "Couldn't open audio device" warning comes back once per file
opened); with a device that opens but delivers nothing the take is dropped quietly, no
harm. On the fake: +0.0 ms at both places with `n = 0`, and +50.0 ms, failing, with
`n = 2205`. **Not yet run on real hardware.**

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

The questions the automated checks raised, and the facts they established for the
decisions above, are in [open-points.md](open-points.md).
