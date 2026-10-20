# Work State

GitHub is authoritative for branch, pull-request, and CI state. Report only tests and CI results confirmed by evidence.

## Current state

- PR #60 merged as `488c01ad8b1ccd3ef3f80cb8b2cfd61562b71173`.
- PR #61 merged as `c14b7e01527da07e6d9a4fe702d57c78d2d5a6c5`.
- PR #62 merged as `1c192ba43af6da34dbe37564b55ef107588a49e0`; its exact-head CI run #371 passed all five jobs, including exhaustive allocation-failure sweeps.
- PR #63 merged as `c85dd1866414847b892aa39e3f7b6cfd9916c5f7`; it removes `AGENTS.md` and makes `PLAN.md` the task and priority roadmap.
- The build/install path is the POSIX `build.sh`; `nobuild.c` and `nobuild.h` have been removed.
- The opt-in extended query grammar supports escaped spaces and syntax metacharacters, with CLI and interactive C API regressions.
- CI concurrency distinguishes PR runs by PR number and push runs by commit SHA.

## Development priorities

Use `PLAN.md` as the task list and priority roadmap. Resolve existing technical debt and blocking defects first, then continue the highest-priority unfinished task in the ordered milestone list. Keep the plan and this status file aligned with verified repository state.

## Validation state

No commit status checks are currently reported for `c85dd1866414847b892aa39e3f7b6cfd9916c5f7`. Do not treat the absence of reported checks as a successful CI run. The latest recorded complete master CI run remains #39075005536 on commit `d74d84659f3f87a0cece65e9d2539f03c389265c`.
