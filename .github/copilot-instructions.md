# Copilot instructions

The instructions for AI agents in this repository are in [`AGENTS.md`](../AGENTS.md) at
the repository root, and the documentation they point to is in [`docs/`](../docs/). Read
`AGENTS.md` first and follow it; this file exists only so that tools which look here find
their way there.

The essentials, for a context that cannot follow the link:

- Fork of Tony (Qt6 / C++17, meson + ninja, Sonic Visualiser libraries) adding singing
  practice features. The fork's own code is in `main/`; tests are in `main/test/`.
- Build from Git Bash with
  `export PATH="/c/msys64/mingw64/bin:$PATH" MINGW_PREFIX="C:/msys64/mingw64"` and
  `ninja -j 3 -C build_mingw Tony.exe test-tony-core.exe test-tony-app.exe`, logging to a
  file under `tmp/`. From PowerShell use `.\build.bat` and `.\build.bat test`.
- `svcore/`, `svgui/`, `svapp/` and the other top-level library directories are separate,
  gitignored repositories. See `docs/forks.md` before changing one.
- Match the surrounding code, keep changes in scope, and give every behaviour a test that
  can fail.
