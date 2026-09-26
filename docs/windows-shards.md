# Sharded test runs on Windows: plan

Built and proven on Linux; the last section is left for the Windows machine. When that is
done, what lasts goes into [testing.md](testing.md) and [AGENTS.md](../AGENTS.md), and
this file and its row in [README.md](README.md) are deleted.

## Goal

On Linux the test executables already run as shards in parallel processes
(`TONY_TEST_SHARD`, `main/test/RunSuite.h`, `deploy/linux/run-tests.sh`): the app suite
takes a minute and a half instead of eight. On the Windows machine the same runs are still
one process each, about 13.5 minutes for core, app and dev together. Make the runner work
from Git Bash on Windows, with one way of keeping shards apart that serves both platforms.

## Measured on Windows (2026-09-26, one process)

| Suite | Tests | Wall | CPU |
| --- | --- | --- | --- |
| `TestRecordWorkflow` | 165 | 430 s | 164 s |
| `TestAudioCheck` | 23 | 185 s | 69 s |
| `TestUiChecks` | 19 | 116 s | 56 s |
| the other four app suites | 60 | 11 s | 6 s |
| whole `test-tony-app` | | 743 s | 295 s |
| `test-tony-dev` | 9 | 67 s | |
| `test-tony-core` | | 17 s | |

The app suite uses 0.4 of one core on average (Linux: nearer 0.25); the machine has four
cores and no hyper-threading. So there is room for several processes at once, fewer than on
Linux.

## Why the Linux runner did not work on Windows

Processes sharing a settings store clear each other's settings: `initTestCase()` of
`TestRecordWorkflow` calls `QSettings().clear()`, and the shards of `test-tony-dev` failed
on Linux in exactly this way until each got a `HOME` of its own. `run-tests.sh` gave every
process its own `HOME` and XDG directories.

On Windows neither moves anything. QSettings in its native format is the registry key
`HKCU\Software\tony-tests\<application name>`, and `QStandardPaths`, which svcore's
`ResourceFinder` uses, asks Windows for its known folders instead of reading environment
variables. These are also shared by processes of one application name:

- `AppDataLocation`, where `AudioCheckRunner` and `DevChecks` keep files.
- svcore's `TempDirectory`, under the user resource prefix. At start-up it deletes every
  `sv_*` directory with no `.pid` file in it, which includes a directory another process
  has created a moment ago and not yet written its `.pid` into.
- svcore's debug log, under the same prefix.

All of them are keyed by the application name. So a shard runs under an application name
of its own, on both platforms.

## Design (built)

1. **`RunSuite.h`**: a pure function that gives the application name for a shard, taking
   the base name and the value of `TONY_TEST_SHARD`. No value: the base name, unchanged. A
   valid `i/n`: `<base>-shard<i>of<n>`. An invalid value: the base name (`runSuite()`
   already refuses to run with it). One function parses `i/n` for both this and
   `runSuite()`, so that the two cannot disagree.
2. **The mains** that run suites, `tony-core-test.cpp`, `tony-app-test.cpp` and
   `tony-dev-test.cpp`, set the application name through that function instead of the
   literal, right after constructing the application and before anything reads settings.
3. **`deploy/linux/run-tests.sh`**: drop the per-process `HOME` and XDG directories, so
   that Linux runs rely on the same thing Windows will, and keep proving it. Keep the
   per-process log directories. The script stays where it is. Its header comment says it
   runs from Git Bash on Windows too, with the environment AGENTS.md gives and executable
   names with `.exe`. Use nothing Git for Windows' bash lacks: `nproc`, `seq`, `awk`,
   `grep` and `xargs` are there. Leave the default `-j` (twice the cores) alone; the
   Windows step sets its own.

All three are built as decided, but for one thing: the parse of `i/n` is stricter than
`runSuite()`'s was. A part that is not a whole number (`a/2`, `1x/2`, `1.0/2`) used to
run as shard 0; it is now invalid, so `runSuite()` refuses it and the name stays the base
name.

Not part of this: sharded `meson test` definitions (`meson test` and `build.bat test` stay
the one-process run that [testing.md](testing.md) asks for after changes to lifetimes,
threads or teardown), a PowerShell runner, any change in the library forks. If the work
seems to need a fork change (for instance to `TempDirectory`), do not make it: report it.

## Proven on Linux

A copy of the old script in which every process shared one `HOME` and one set of XDG
directories stood in for Windows: with one `HOME`, as on Windows, nothing but the
application name can keep shards apart.

- **Before the change**, both suites failed. The suites clear the settings in
  `initTestCase()` and write their own in `init()` and `cleanup()`, and with one settings
  file the processes did it under each other: a setting such as the pre-roll read back as
  another process had left it, and the network-permission setting, once cleared, brought
  its dialog up. `test-tony-dev` failed two tests; `test-tony-app` had nine failures in
  one run and twelve in another, nearly all of them that way.
- **After it**, three runs of `test-tony-dev` with the shared `HOME` were clean, and so
  were those of `test-tony-app`, but for a race in one test (a take stopped before the
  fake device's first block), which failed with a `HOME` per process as well and is since
  fixed. Now that the script itself shares `HOME`, three runs each of core, app and dev
  through it, and one-process runs of all three, failed only the Linux failures
  [testing.md](testing.md#running) lists.
- **What each shard left**: one settings file under `~/.config/tony-tests/` and one data
  folder, holding its log and any temp directory of svcore's, under
  `~/.local/share/tony-tests/`, both named for the shard; the two suites that turn on
  `QStandardPaths`' test mode put theirs under `~/.qttest/`, named the same way. One set
  per `i` and `n`, so their number stays bounded. A one-process run adds only the base
  name's.

## On the Windows machine, after the merge

From Git Bash with AGENTS.md's environment, after a build:

1. `deploy/linux/run-tests.sh -j N build_mingw test-tony-app.exe` for N = 4, 6 and 8,
   watching CPU in Task Manager. Take the largest N that passes three runs in a row with
   the CPU clearly below full; tests that race the analysis are the ones to watch. On the
   first run, check that each shard's results land in `tmp/tl/test-tony-app.exe/<i>/`:
   the script passes `TONY_TEST_LOG_DIR` as a `/c/...` path and relies on Git Bash
   converting it for a native program. If they are not there, the summary has no suite
   counts.
2. The same for `test-tony-dev.exe` and `test-tony-core.exe`.
3. Check that each shard left its own key under `HKCU\Software\tony-tests` and its own
   folders under `%APPDATA%` and `%LOCALAPPDATA%` (`qttest` included, for the two suites
   that turn on `QStandardPaths`' test mode). There should be one per `i` and `n`, so their
   number stays bounded.
4. AGENTS.md: the full Windows run goes through `run-tests.sh` with that N and the
   measured times, and gets a tool timeout to match. The one-process command stays, for
   named tests and for changes to lifetimes, threads or teardown. Update the times in
   [testing.md](testing.md) too.
5. Delete this file, its row in [README.md](README.md) and the link to it in
   [testing.md](testing.md#running).
