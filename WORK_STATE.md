# Work State

GitHub is authoritative for branch, pull-request, and CI state. Do not claim tests unless their results are confirmed.

## Current focus

- PR #60 merged as `488c01ad8b1ccd3ef3f80cb8b2cfd61562b71173`; exact-head and post-merge CI both passed.
- The build/install path is now the POSIX `build.sh`; `nobuild.c` and `nobuild.h` are removed.
- Current implementation extends the opt-in query grammar so backslash escapes spaces and syntax metacharacters. Regression coverage covers CLI and interactive C API behavior.
- The previous allocation-sweep broken-pipe noise and ncurses linker-probe failure-path gaps have regression coverage.

## Validation requirements

- GCC release, Clang release, GCC sanitizers, and Clang sanitizers must pass on the exact current PR head.
- ShellCheck must include all changed shell scripts.
- Do not proceed to unrelated implementation work while the current change has failing CI or unresolved implementation debt.
