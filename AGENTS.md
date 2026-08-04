# Workbench AGENTS.md (project-specific conventions)

## Build & test

- Native build: `./build.sh` (Linux target, Release, C++20).
  Add `-a` for parallel Win+Linux, `-w` for Windows cross-compile, `-c` to clean first.
- Native test: `./build.sh -t` (builds with `-DBUILD_TESTS=ON
  -DENABLE_COVERAGE=ON`, runs `./build_linux/WorkbenchTests`, prints gcov coverage).
- Docker remains the reproducible CI/release wrapper around the same script.
- Quill runs two local gates before commit: `./build.sh --test`, then `./build.sh --all`. Both
  return nonzero on failure and surface detailed output for the implementation retry.
- Logs land in `logs/` (`build_linux.log`, `build_win.log`, `test_output.log`).
- `build.sh` runs `clang-format -i` on sources before every build.

## CMake

- **Globbing:** sources formatted/collected from `src/ include/models include/panels
  include/system` (see `build.sh` `format_code`). Keep new sources under these roots.
- **Explicit test list:** tests are listed explicitly in `tests/CMakeLists.txt` — add a new
  test to that list; do not rely on glob auto-discovery.

## Code conventions

- **Config-struct patterns:** server/UI config is struct-of-fields mirrored into
  `~/.workbench/config.json`; new options thread through the struct + JSON load/save.
- **Build-edit rule:** new source files or test files must be reflected in the relevant
  `CMakeLists.txt` (and the explicit test list) in the same change — never leave them
  uncompiled.

## Project board

- Board: `Workbench` (mirrored in `.shipticket.toml` -> `[repo].project_board`). The driver's
  phase-6 board step resolves the numeric ID via `gh project list --owner "@me"`.
