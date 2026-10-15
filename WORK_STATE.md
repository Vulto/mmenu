# Autonomous Work State

This file records verified handoff state. GitHub branch/PR state and CI remain authoritative.

## Current focus

- Active PR: #51, [feat: add cyclic navigation](https://github.com/Vulto/mmenu/pull/51).
- Branch: `m3-cycle-navigation`.
- Feature: opt-in `--cycle` wraps Up/Down at the first/last filtered result; default navigation remains bounded.
- Public API adds `mmenu_config.cycle`; callers must zero-initialize `mmenu_config`.
- Regression coverage includes CLI validation and PTY C API tests for Down-wrap, Up-wrap, default non-wrapping, and empty filtered results.

## Verified implementation and CI

- Code/test head: `f09b650e2998413ac92ce4824781299dd2c9ac7c`.
- CI run [38573572153](https://github.com/Vulto/mmenu/actions/runs/38573572153) passed all four required GCC/Clang release and sanitizer jobs on that code/test head.
- Documentation head before this handoff update: `43deca1e2c2f838f7ca9f010a988bb23c52aa931`.
- CI run [38607360475](https://github.com/Vulto/mmenu/actions/runs/38607360475) completed successfully on that documentation head. All four required matrix jobs passed. The run also completed CLI, interactive C API, streaming, allocation-failure, and other jobs according to each job's conditions; skipped optional steps are not counted as executed tests.
- No local test execution is claimed.
- Latest verified CI before the current change: run 38719928231 passed all four required jobs on head `efefc3bc2529996c10fa782262a3f21bfd1b68c7`; the added filtered-query CLI regression requires a new exact-head run.
- Previous QA approval was tied to SHA `43deca1e2c2f838f7ca9f010a988bb23c52aa931`; it is stale for the current PR head.
- Prior QA review head: `19015ab1e4bc7366c55fd481127aa73b035a889d`; its marker is stale for later commits.
- Latest previously validated PR head: `ff794839ce4b440e3213461b9a56946964235f82`. Subsequent commits, including the bounded-navigation CLI regression, require fresh CI and QA.
- CI run [38647765508](https://github.com/Vulto/mmenu/actions/runs/38647765508) passed all four required GCC/Clang release and sanitizer jobs on that exact head. The jobs completed successfully; conditional optional steps were skipped where configured. No local test execution is claimed.
- The previously posted QA marker for SHA `43deca1e2c2f838f7ca9f010a988bb23c52aa931` is stale for the current PR head.
- This documentation-only handoff update creates a new head. Recheck required CI and repeat QA against the resulting SHA before merge.
- The PR changed files are CHANGELOG.md, PLAN.md, README.md, WORK_STATE.md, main.c, mmenu.h, tests/apiHarness.c, tests/cli.sh, and tests/interactive.sh.

## Current action

1. Agent 2 must review the latest exact PR head, including the filtered-query CLI regression and input-timing correction, and record approval only if no known defects remain.
2. Confirm all required CI checks pass on that exact same head; do not merge on absent, pending, stale, or failed checks.
3. Merge PR #51 only after exact-head QA and CI are both valid.
4. After merge, continue with the highest-priority dependency-ready incomplete item in PLAN.md.

## Run log

- 2026-10-14: Inspected AGENTS.md, AUTONOMOUS_WORK.md, WORK_STATE.md, PLAN.md, docs/QUALITY.md, docs/RELEASE.md, and SECURITY.md.
- 2026-10-14: Inspected PR #51 review submissions and inline review threads; none were returned.
- 2026-10-14: Verified CI run 38573572153 passed all four required jobs on code/test head `f09b650e2998413ac92ce4824781299dd2c9ac7c`.
- 2026-10-14: Verified CI run 38607360475 completed successfully on documentation head `43deca1e2c2f838f7ca9f010a988bb23c52aa931`; all four required matrix jobs concluded successfully.

- 2026-10-14: Added the missing Unreleased changelog entry for `--cycle` and `mmenu_config.cycle` in commit `ff13a4b11617b9b823094c170e19a8f96a6de30a`.
- 2026-10-14: CI run 38611917873 passed all four required jobs on `3174c81148cc91ad35972a8d666bf8933d3cbca4`. This run predates the changelog correction; verify CI and QA against the new head before integration.
- 2026-10-14: Documented the `--cycle` CLI option in `README.md` in commit `ee5bb1e614ad305061123cc8574095870c858f09`; the change is on `m3-cycle-navigation` and requires fresh CI and QA for the resulting head.

- 2026-10-14 19:25 America/Sao_Paulo: Agent 2 reviewed PR #51 head `19015ab1e4bc7366c55fd481127aa73b035a889d` and verified CI run 38640970419 passed all four required matrix jobs on that exact head. This documentation-only update changes the head; fresh CI and QA are required before merge.
- 2026-10-14 22:24 America/Sao_Paulo: Agent 2 verified CI run 38647765508 passed all four required jobs on PR head `ff794839ce4b440e3213461b9a56946964235f82`. This handoff update records that checkpoint; the resulting commit requires fresh exact-head CI and QA.

- 2026-10-15: Added explicit CLI coverage for bounded Down navigation without `--cycle` on `m3-cycle-navigation`. This commit requires fresh exact-head CI and QA; no local test execution is claimed.

- 2026-10-15: CI run 38696684577 failed the CLI regression suite in all four matrix jobs. Job logs show the new bounded-navigation assertion passed, but the following `--cycle` Down-wrap assertion expected `café-日本語` while the program correctly emitted `alpha` after four Down presses. Corrected `tests/cli.sh` to reset the expected output to `alpha` before the wrap test. Local execution remains unavailable because the environment cannot resolve `github.com`; fresh CI and QA are required for the resulting commit.

- 2026-10-15: CI run 38702730675 passed the CLI regression suite on GCC release and both sanitizer jobs, but Clang release failed the Down-wrap CLI assertion: the four-arrow burst selected the last option instead of wrapping. The bounded-navigation assertion passed; C API cycle tests and Up-wrap passed. Updated both Down-navigation PTY cases to deliver arrow sequences with spacing and a longer initialization delay, then send Enter separately. This test-harness correction requires fresh CI and QA. No local test execution is claimed.

- 2026-10-15: CI run 38709975562 passed all four required GCC/Clang release and sanitizer jobs on parent head `93f7b3f70c541725497e2133820e2b04b41e01e8`. The CLI, interactive C API, streaming, and allocation-failure regression steps passed; conditional optional checks were skipped where configured. Removed a redundant expected-output write in the bounded-navigation CLI regression and refreshed this plan/handoff evidence in one follow-up commit. The new head needs fresh exact-head CI and QA; no local test execution is claimed.

- 2026-10-15: Agent 2 reviewed PR #51 code/test head `bdaf65f2f2813671c3b296e743b17f1076cd2825`. CI run 38718350725 completed successfully with all four required jobs passing: GCC release, Clang release, GCC sanitizers, and Clang sanitizers. CLI, interactive C API, streaming, and allocation-failure regression steps passed where configured; conditional optional checks were skipped as configured. No confirmed source or test defect was found in the reviewed diff. This documentation-only update to PLAN.md and WORK_STATE.md changes the PR head, so the new head requires fresh CI and QA. No local test execution is claimed; no QA marker or merge is authorized for the new head.

- 2026-10-15: CI run 38719928231 passed all four required GCC/Clang release and sanitizer jobs on exact pre-change head `efefc3bc2529996c10fa782262a3f21bfd1b68c7`. Added a CLI regression that verifies `--cycle` wraps within the filtered results when `--query` is active, and updated the plan/handoff. The new head requires fresh exact-head CI and QA. No local test execution is claimed.
