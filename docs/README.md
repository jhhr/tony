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
| [testing.md](testing.md) | The two test executables, the fakes and helpers, how tests turned out to be worthless, races |
| [architecture.md](architecture.md) | What the fork adds, `tony_core` / `tony_app`, who owns what, and the rules for layers, models, commands, playback and session files; the smaller features, the lyrics and their editor among them |
| [recording.md](recording.md) | Record to Stop, step by step and why in that order; latency; pre-roll; record into selection; the live tracker |
| [takes.md](takes.md) | Takes: decisions, the audio swap, ranged analysis and merge, undo, the coverage strip, files and sessions, limitations |
| [forks.md](forks.md) | The `jhhr/*` library forks: how to change one, what each adds, known defects |
| [open-points.md](open-points.md) | Decisions waiting for the user, things not built, weak spots |
| [manual-checklist.md](manual-checklist.md) | What needs a real device, real ears or real eyes — none of it tried yet |
| [calibrate-audio.md](calibrate-audio.md) | Plan, not built: a Calibrate Audio button that measures the round trip through a speaker-to-mic loopback, and dev checks that automate most of the manual checklist |
| [windows-shards.md](windows-shards.md) | Plan, not built: sharded test runs from Git Bash on Windows, each shard kept apart by an application name of its own |
