# Developer documentation

For people and AI agents developing the singing-practice features of this Tony fork. What
the features *are*, for a user, is in the [top-level README](../README.md); how to work in
the repository is in [AGENTS.md](../AGENTS.md).

These pages hold what the code cannot say for itself: why things are in the order they are
in, which rules of the Sonic Visualiser libraries the code depends on, what was decided
and rejected, and what is known to be weak. They do not list classes, members or
methods, and they do not tell the story of fixed bugs.

| Page | What is in it |
| --- | --- |
| [building.md](building.md) | The MinGW build, and every way the environment has gone wrong; the Linux build of a cloud session |
| [testing.md](testing.md) | The test executables (core, app, and the dev checks'), the fakes and helpers, how tests turned out to be worthless, races |
| [architecture.md](architecture.md) | What the fork adds, `tony_core` / `tony_app`, who owns what, and the rules for layers, models, commands, playback and session files; the smaller features, the lyrics and their editor among them |
| [recording.md](recording.md) | Record to Stop, step by step and why in that order; latency; pre-roll; record into selection; the live tracker |
| [takes.md](takes.md) | Takes: decisions, the audio swap, ranged analysis and merge, undo, the coverage strip, files and sessions, limitations |
| [forks.md](forks.md) | The `jhhr/*` library forks: how to change one, what each adds, known defects |
| [open-points.md](open-points.md) | Decisions waiting for the user, things not built, weak spots |
| [manual-checklist.md](manual-checklist.md) | What needs a real device, real ears or real eyes, starting with the device check (Calibrate Audio with the dev checks). Tried so far: Calibrate Audio and part of a dev run on the user's PC, and the looks from cloud screenshots; the rest not yet |
| [mobile-port.md](mobile-port.md) | Porting to a phone: decisions, what in the code any port depends on, Android against Sailfish OS |
| [port-android.md](port-android.md), [port-sailfish.md](port-sailfish.md) | Platform facts with sources, the work, and the first test port for each |
| [calibrate-audio.md](calibrate-audio.md) | Calibrate Audio, which measures the round trip through an earcup held to the mic, and the dev checks that settle the checklist's device items: what they do, what each verdict and check means, which numbers to send back, design, tests, decisions, open points, the user's runs |
| [windows-shards.md](windows-shards.md) | Plan, built and proven on Linux, left to try on Windows: sharded test runs from Git Bash on Windows, each shard kept apart by an application name of its own |
