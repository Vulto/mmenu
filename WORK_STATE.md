# Work State

GitHub is authoritative for branch, pull-request, and CI state. Do not claim tests unless their results are confirmed.

## Current focus

- PR #60 merged as `488c01ad8b1ccd3ef3f80cb8b2cfd61562b71173`; exact-head and post-merge CI both passed.
- PR #61 merged as `c14b7e01527da07e6d9a4fe702d57c78d2d5a6c5`; exact-head CI run #39072576746 passed all four required jobs.
- Full master CI run #39075005536 passed all five jobs on commit `d74d84659f3f87a0cece65e9d2539f03c389265c`; GCC sanitizer exhaustive allocation-failure sweeps passed all scenarios.
- The earlier allocation-sweep report came from runner cancellation: the log records a runner shutdown signal during API fuzzy-result-growth allocation 43, not an allocator assertion or sanitizer finding.
- CI concurrency now uses the PR number for pull-request runs and the commit SHA for push runs, preventing a later master push from cancelling an ongoing full master run.
- The build/install path is now the POSIX `build.sh`; `nobuild.c` and `nobuild.h` are removed.
- Current implementation extends the opt-in query grammar so backslash escapes spaces and syntax metacharacters. Regression coverage covers CLI and interactive C API behavior.
- The previous allocation-sweep broken-pipe noise and ncurses linker-probe failure-path gaps have regression coverage.

## Validation requirements

- GCC release, Clang release, GCC sanitizers, and Clang sanitizers must pass on the exact current PR head.
- ShellCheck must include all changed shell scripts.
- Do not proceed to unrelated implementation work while the current change has failing CI or unresolved implementation debt.
