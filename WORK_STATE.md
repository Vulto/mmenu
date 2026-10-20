# Work State

GitHub is authoritative for branch, pull-request, and CI state. Report only tests and CI results confirmed by evidence.

## Current state

- PR #60 merged as `488c01ad8b1ccd3ef3f80cb8b2cfd61562b71173`.
- PR #61 merged as `c14b7e01527da07e6d9a4fe702d57c78d2d5a6c5`.
- PR #62 merged as `1c192ba43af6da34dbe37564b55ef107588a49e0`; its exact-head CI run #371 passed all five jobs, including exhaustive allocation-failure sweeps.
- The build/install path is the POSIX `build.sh`; `nobuild.c` and `nobuild.h` have been removed.
- The opt-in extended query grammar supports escaped spaces and syntax metacharacters, with CLI and interactive C API regressions.
- CI concurrency distinguishes PR runs by PR number and push runs by commit SHA.

## Development priorities

Use `PLAN.md` as the task list and priority roadmap. Resolve existing technical debt and blocking defects first, then continue the highest-priority unfinished task in the ordered milestone list. Keep the plan and this status file aligned with verified repository state.

## Validation state

The last confirmed complete master CI result in the recorded history is run #39075005536 on commit `d74d84659f3f87a0cece65e9d2539f03c389265c`. The post-merge CI status for the newer PR #62 merge commit has not yet been verified here.
