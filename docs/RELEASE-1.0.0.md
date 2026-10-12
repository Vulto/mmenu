# mmenu 1.0.0

**Release date:** 2026-10-11

mmenu is a small ncurses-based menu for C programs and shell scripts. Version 1.0.0 consolidates the interactive menu, batch filtering, fuzzy ranking, configurable interaction, and preview functionality behind a documented CLI and public C API.

## Highlights

- **Batch filtering:** substring, exact, and fuzzy matching; inverted results; optional original indexes; output templates; and NUL-delimited records.
- **Interactive selection:** live query filtering, optional fuzzy ranking, multi-selection, initial queries, configurable key aliases, and preview panes.
- **C API:** single- and multi-selection entry points, configurable behavior, and a generic preview callback. Returned multi-selection index arrays are caller-owned and must be freed.
- **Robustness:** checked allocation-size arithmetic, safe handling of long records and allocation failures, terminal-resize handling, Unicode display-width cursor positioning, and validation of CLI/API arguments.
- **Verification:** GCC/Clang strict builds, release and sanitizer CI, static analysis, CLI and interactive regressions, Valgrind, exhaustive allocation-failure sweeps, matcher fuzzing, performance comparisons, and ranking-quality baselines.

## Build from source

Requirements: a C11 compiler and the wide-character ncurses development headers and libraries.

```sh
cc -std=c11 -Wall -Wextra -Wpedantic -o mmenu main.c -lncursesw
./mmenu --help
```

The release is source-first; no prebuilt executable is attached. Build locally for your platform.

## Compatibility notes

- Existing substring matching remains the default. Fuzzy and exact matching are opt-in.
- Interactive preview commands are executed by the user's shell and are **not sandboxed**. Treat the command as trusted code. The `{}`, `{index}`, and `{query}` placeholders are shell-quoted by mmenu, but this does not make an untrusted command safe.
- Batch mode is byte-oriented; the interactive C API requires valid multibyte text in the active locale.
- The public C API requires ncursesw at link time. See the README for CLI/API usage and SECURITY.md for the preview-command trust boundary.

## Sources

- [Source repository](https://github.com/Vulto/mmenu)
- [Changelog](https://github.com/Vulto/mmenu/blob/v1.0.0/CHANGELOG.md)
- [Security policy](https://github.com/Vulto/mmenu/blob/v1.0.0/SECURITY.md)
- [Release checklist](https://github.com/Vulto/mmenu/blob/v1.0.0/docs/RELEASE.md)
