# Sharded test runs on Windows: plan

Plan, not built. For a Linux cloud session to carry out; the last section is for the
Windows machine afterwards. When all of it is done, what lasts goes into
[testing.md](testing.md) and [AGENTS.md](../AGENTS.md), and this file and its row in
[README.md](README.md) are deleted.

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

## Why the Linux runner does not work on Windows

Processes sharing a settings store clear each other's settings: `initTestCase()` of
`TestRecordWorkflow` calls `QSettings().clear()`, and the shards of `test-tony-dev` failed
on Linux in exactly this way until each got a `HOME` of its own. `run-tests.sh` gives every
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

## Design (decided)

1. **`RunSuite.h`**: a pure function that gives the application name for a shard, taking
   the base name and the value of `TONY_TEST_SHARD`. No value: the base name, unchanged. A
   valid `i/n`: `<base>-shard<i>of<n>`. An invalid value: the base name (`runSuite()`
   already refuses to run with it). One function parses `i/n` for both this and
   `runSuite()`, so that the two cannot disagree.
2. **The mains** that run suites, `tony-core-test.cpp`, `tony-app-test.cpp` and
   `tony-dev-test.cpp`, set the application name through that function instead of the
   literal, right after constructing the application and before anything reads settings.
   `tony-device-check.cpp` runs by hand against a real device and is never sharded: leave
   it.
3. **`deploy/linux/run-tests.sh`**: drop the per-process `HOME` and XDG directories, so
   that Linux runs rely on the same thing Windows will, and keep proving it. Keep the
   per-process log directories. The script stays where it is. Its header comment says it
   runs from Git Bash on Windows too, with the environment AGENTS.md gives and executable
   names with `.exe`. Use nothing Git for Windows' bash lacks: `nproc`, `seq`, `awk`,
   `grep` and `xargs` are there. Leave the default `-j` (twice the cores) alone; the
   Windows step sets its own.

Not part of this: sharded `meson test` definitions (`meson test` and `build.bat test` stay
the one-process run that [testing.md](testing.md) asks for after changes to lifetimes,
threads or teardown), a PowerShell runner, any change in the library forks. If the work
seems to need a fork change (for instance to `TempDirectory`), do not make it: report it.

## Steps for the cloud session

Read AGENTS.md, then [testing.md](testing.md) (Running, Shards, the Linux failures, Timing
and races), `run-tests.sh`, `RunSuite.h` and `TestRunSuite.h`. Build as
[building.md](building.md#building-on-linux) says.

1. **Baseline.** Run `run-tests.sh` for `test-tony-core`, `test-tony-app` and
   `test-tony-dev` as it is, and record what fails. Only tests from testing.md's list of
   Linux failures should.
2. **The failure this prevents, before the change.** Copy the script to `tmp/` (not
   committed) and make every process share one `HOME` and one set of XDG directories.
   That is the Windows situation on Linux: nothing but the application name can then keep
   shards apart. Run it for `test-tony-dev` and `test-tony-app` and record what fails. If
   nothing does, run it three times. If still nothing fails, say so in the report: step 4
   then proves less.
3. **Build design 1 and 2**, with tests in `TestRunSuite`:
   - no shard gives exactly the base name;
   - every `i` of one `n` gives a different name;
   - the same `i` with a different `n` gives a different name;
   - an invalid value gives the base name.

   Break the function for a moment and see a test fail (AGENTS.md), then put it back.
4. **Proof.** The shared-`HOME` script from step 2 must now pass three runs in a row for
   `test-tony-dev` and `test-tony-app`, apart from the known Linux failures. If it does
   not, stop there and report what failed: Windows would fail the same way, and a
   per-process `HOME` cannot help it there.
5. **Design 3**, then run core, app and dev through the real script three times each.
6. **One-process runs** of all three executables, as AGENTS.md gives them, from `build/`:
   the path without `TONY_TEST_SHARD` is the one `meson test` and named tests use.
7. **Docs.** [testing.md](testing.md)'s Shards paragraph: shards are kept apart by their
   application name (settings, data directory, temp directories), on Linux and Windows;
   remove "so the script is for Linux". Fix anything else the change makes false
   ([building.md](building.md#building-on-linux) mentions the script). Leave AGENTS.md's
   Windows commands and times alone: they need measuring on Windows.
8. **Commits and PR.** One commit per step as AGENTS.md says. For example: the shard's
   application name with its tests (`test:`), the script (`test:`), the docs (`docs:`).
   Push the session's branch and open a PR against `default` with `gh`. Report as
   AGENTS.md asks: the Totals of the final runs, the failures seen in steps 1 and 2, and
   anything that did not go as planned.

## On the Windows machine, after the merge

From Git Bash with AGENTS.md's environment, after a build:

1. `deploy/linux/run-tests.sh -j N build_mingw test-tony-app.exe` for N = 4, 6 and 8,
   watching CPU in Task Manager. Take the largest N that passes three runs in a row with
   the CPU clearly below full; tests that race the analysis are the ones to watch.
2. The same for `test-tony-dev.exe` and `test-tony-core.exe`.
3. Check that each shard left its own key under `HKCU\Software\tony-tests` and its own
   folders under `%APPDATA%` and `%LOCALAPPDATA%` (`qttest` included, for the two suites
   that turn on `QStandardPaths`' test mode). There should be one per `i` and `n`, so their
   number stays bounded.
4. AGENTS.md: the full Windows run goes through `run-tests.sh` with that N and the
   measured times, and gets a tool timeout to match. The one-process command stays, for
   named tests and for changes to lifetimes, threads or teardown. Update the times in
   [testing.md](testing.md) too.
5. Delete this file and its row in [README.md](README.md).
