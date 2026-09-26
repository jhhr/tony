# Audio drivers: work orders for phase agents

You are one of a line of agents, each building **one small phase** of the driver project
([audio-drivers.md](audio-drivers.md)). A lead reviews your work when you report back,
and commits it. You have no memory of earlier phases; what you need is here. This file is
working memory for the branch `feat/wasapi`, and the documentation phase removes it.

**Your context is the budget.** Aim to finish well under 200k tokens. The rules below
say how. They are about not reading huge files whole and not maintaining big documents;
they are **not** a licence to skip what you need to understand. Careful, correct work
comes first.

## 1. What to read, and what not to

1. This file, all of it.
2. `AGENTS.md` at the repository root. Its rules apply, except that the **build and test
   commands in section 2 below replace its Windows ones**.
3. `docs/audio-drivers.md` (the spec, short): all of it.
4. The docs your work order names, by section: search for the heading and read that
   range. `docs/calibrate-audio.md` is about 600 lines; `docs/testing.md` about 350.
5. Code:
   - `main/MainWindow.cpp` is over 6000 lines and `main/test/TestRecordWorkflow.h` over
     8000. **Never read them whole**: search for the function, then read that range.
   - Before writing a test, read an existing test next to where yours will go and copy
     its shape.

## 2. Rules

**Scope**

- Build your phase only. Where the spec is silent, choose the simpler option and say so.
  Where it is **wrong or impossible**, do not improvise another design: finish what can
  be finished, leave the tree building and green, and report.
- **Do not edit** any top-level library directory (`svcore/`, `svgui/`, `svapp/`,
  `bqaudioio/`, `bqaudiostream/`, `pyin/`, …). They are separate repositories, gitignored
  here. If one needs a change, report exactly which; the lead makes it.
- Match the surrounding code: naming, comment density, idiom. Comments say why, in plain
  words. Every new source file starts with the project's GPL header.
- **The user builds on Windows** with MinGW and Qt 6.11. Nothing specific to Linux; and
  no identifiers named `near`, `far`, `min`, `max`, `ERROR`, `IN` or `OUT` (Windows
  headers define them as macros).

**Build and test.** This container builds in `build/` with Qt 6.11 (conda-forge).

- Build, from the repository root:

      ninja -j 4 -C build tony pyin.so test-tony-core test-tony-app test-tony-dev > tmp/build.log 2>&1; echo "exit:$?" >> tmp/build.log; tail -5 tmp/build.log

  Search the log for `error:`; never read it whole. No `.exe`. `pyin.so` must be built:
  without it every app test that waits for an analysis hangs.
- Named tests while working, from `build/`:

      mkdir -p ../tmp/tl && rm -f ../tmp/tl/*.txt
      TONY_TEST_LOG_DIR=../tmp/tl ./test-tony-app some_test other_test > ../tmp/test.log 2>&1
      grep -a "^FAIL\|^XFAIL\|^   Loc\|^Totals" ../tmp/tl/*.txt

  A name goes to every suite of the executable; the others report it unknown, so the
  exit status of a run with names means nothing. Read results from the per-suite files.
- Whole suites, **once at the end**, from the repository root:

      deploy/linux/run-tests.sh test-tony-core
      deploy/linux/run-tests.sh test-tony-app     # about a minute and a half
      deploy/linux/run-tests.sh test-tony-dev     # about a minute

  Each runs eight processes and prints a summary with every failure. Never run two at
  once, nor one while building: the app tests record in real time.
- Every behaviour gets a test that can fail. Show it for the two or three that matter by
  breaking the code for a moment; undo the break **by hand**, never with `git checkout`
  or `git restore`.
- Do not weaken or delete a test to get green. If one is wrong because the behaviour was
  meant to change, change it and say so.
- App tests run in real time against `FakeAudioIO`: keep them to seconds.

**Git.** Do not commit, push, or stage. The lead does, after review. Never `git add -A`.
Edit documents with the Edit and Write tools, not shell one-liners.

**Docs.** Fix any statement in `docs/` your change makes false, in the page that makes
it. The big documentation pass is W4's; before that, only such fixes.

**Report** (under 60 lines): what you built, by file; the three `run-tests.sh` summaries
verbatim; which tests you saw fail when you broke the code; decisions you took where the
spec was silent; anything fragile, unfinished, or needed from a library.

## 3. State of the code

- `feat/wasapi` starts from `feat/tonyandroid` with `default` merged in: Calibrate Audio
  and the dev checks, the Android port, and a device at another rate than the
  reference's handled (A1: the recording resampled before the splice, `TakeTiming`
  converting; A11: the cursor keeps the reference's pace).
- All three suites green, sharded and in one process each. No expected failures left.
- W1 and W2 are done (the spec's §6). bqaudioio is the fork, at its `feat/wasapi`: the
  implementations `mme`, `directsound` and `wasapi` exist **on Windows only**; on Linux
  `AudioFactory::getImplementationNames()` is as before (`pulse`, `port`, `jack`). So a
  test of anything that lists or chooses a driver cannot get the list from the factory
  here: give `MainWindow` a virtual that returns it, as `createAudioIO()` is virtual for
  `FakeAudioIO`, and have `TestMainWindow` override it.
- `AudioFactory::setSuggestedLatency(seconds)` exists (0 or less: the default, 0.2 s),
  for streams opened after the call.

## 4. Work orders

### W1 — Calibrate Audio at 48 kHz

Read: calibrate-audio.md §1, §3 (the verdicts), §8 (what the report says) and the part of
§10 or later that lists the device facts ("Device rate"); `main/AudioCheckRunner.h` and
`.cpp` around `rateMismatch`; `main/CalibrateAudioDialog.cpp` around `rateMismatch`;
`main/test/TestAudioCheck.h`, `check_flags_a_rate_mismatch` and the test that stores a
figure and checks again with it.

- `AudioCheckResult::rateMismatch` and everything it decides go: a check on a device at
  another rate than the reference's is judged, and its figure kept, like any other.
- The report still names the device's rate where it differs from the reference's (the
  dialog's details, and `DevChecks.txt` if it prints the result), as a fact, not a fault.
- Tests, with `FakeAudioIO` at 48000 Hz as a loopback with a known delay:
  - the check finds the round trip, its verdict is Ok, and the figure can be stored with
    the key's rate 48000;
  - a second check with the stored figure places the takes (the offsets near 0);
  - `check_flags_a_rate_mismatch` becomes one of those; the details test that builds a
    result at 48000 Hz changes to the new wording.
- Docs: calibrate-audio.md's statements about the device rate that A1 and this phase
  made false (§1 "The device's sample rate is not checked", §10's driver project step 1,
  the device facts on `TakeAudio::splice()` and on the check naming the mismatch).

## 5. Log

Capped at 25 lines per entry. Newest last.

**W1** (agent; lead reviewed and committed). The rate-mismatch verdict went; two 48 kHz
tests (measure, then place with the kept figure), both seen failing with the mismatch
blocking put back, and the second with the kept figure turned into frames at the
session's rate. A 48 kHz check measures about 1.1 ms over the fake's delay: bqaudioio's
`ResamplerWrapper` holds that back, unreported, and a real device has it too.

**W2** (lead). The fork as the spec's §4; pinned. Tony's build unchanged on Linux; the
suites green against the fork.
