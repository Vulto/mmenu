# Work State

GitHub is authoritative for branch, pull-request, and CI state. Do not claim tests unless their results are confirmed.

## Current state

- PR #60 was merged into `master` on 2026-10-17 as `488c01ad8b1ccd3ef3f80cb8b2cfd61562b71173`.
- Exact PR-head CI run #39068543111 and post-merge `master` run #39069090746 both passed all five workflow jobs, including GCC/Clang release and sanitizer jobs.
- The C-based `nobuild.c`/`nobuild.h` utility has been removed. `build.sh` now provides `build` and `install` modes, portable ncursesw/terminfo probing, and configurable `CC`, `CFLAGS`, `MMENU_BUILD_OUTPUT`, and `PREFIX`.
- The build/install regression, ncurses linker fallback/failure-path regression, ShellCheck, and man-page formatting passed in CI.
- Preview allocation-failure sweeps now suppress only the expected producer broken-pipe diagnostic; allocation-count, exit-status, and sanitizer checks remain active and passed without the prior `printf: I/O error` messages.
- The escaped-space extended-query grammar is implemented and covered in CLI, interactive C API, fuzzy, and streaming paths.
- No open pull requests were present after PR #60 merged.

## Next action

Continue with the highest-priority incomplete item in `PLAN.md`: broader fzf extended-query compatibility. Do not mark the entire extended grammar complete while upstream syntax remains unsupported.
