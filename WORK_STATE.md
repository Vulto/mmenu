# Autonomous Work State

This file records verified handoff state. GitHub branch/PR state and CI remain authoritative.

## Current focus

- Active branch: `m3-height-percent`; continuing M3 height semantics after the fixed-row subset merged.
- PR #52 implemented and merged fixed-row `--height N` and `mmenu_config.height` (merge commit `c8ba971537f54dadb54bd17461712e0a5f0d8ca2`).
- Current change adds percentage `--height PCT%` and `mmenu_config.height_percent`, based on the available option rows; allowed percentages are 1–100.
- Fixed and percentage settings are mutually exclusive in the C API; zero values preserve existing default sizing.
- Added CLI and PTY/API regression coverage for percentage sizing and invalid percentages.
- Automatic and inverted height modes remain incomplete.
- No local test execution is claimed. The container cannot resolve GitHub for cloning; validate through exact-head CI.

## Verified implementation and CI

- PR #51 cyclic navigation merged after CI run 38770402228 passed all four required matrix jobs on exact head `4a95b8f32fd49337040a58d049291fb02b49cd72`.
- PR #52 fixed-row height merged after CI run 38771951888 passed all four required matrix jobs on exact head `8215d6577e438261f8ed6458d5da8ed9d6829750`.
- Fixed-row height is marked complete in `PLAN.md`; percentage height is the current unmerged subtask and requires fresh exact-head CI and QA.
- Full M3 and full height-mode parity remain incomplete.

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

- 2026-10-15: CI run 38724852069 failed only in the GCC sanitizer exhaustive allocation-failure sweep: allocation 54 was not reached in the `incremental-realloc` API scenario. The other three required matrix jobs passed, and CLI, interactive C API, and streaming suites passed. Stabilized that scenario's PTY input by waiting for initialization and spacing the query/backspace/accept keys in `tests/allocation-sweep.sh`. This correction and handoff update require fresh exact-head CI and QA; no local test execution is claimed.
- 2026-10-15: CI run 38727535977 passed all four required GCC/Clang release and sanitizer jobs on exact code/test head `cca5d932bbe89fb3da688acaad6add185e2cb25e`. CLI, interactive C API, streaming, and applicable allocation-failure steps passed; conditional optional steps were skipped as configured. No local test execution is claimed. The documentation follow-up records this evidence but changes the PR head, so fresh exact-head CI and QA remain required.

- 2026-10-15: Merged PR #51 after exact-head CI run 38770402228 passed GCC release, Clang release, GCC sanitizers, Clang sanitizers, and change detection. Continued directly to M3 and started fixed-row interactive height on branch `m3-height-layout`.
- 2026-10-15: Added `--height N`, additive `mmenu_config.height`, validation, CLI/PTy/API regressions, README/changelog updates, and marked full height modes partial pending exact-head CI and merge.

- 2026-10-15: Merged PR #52 after CI run 38771951888 passed GCC release, Clang release, GCC sanitizers, Clang sanitizers, and change detection on exact head `8215d6577e438261f8ed6458d5da8ed9d6829750`. Marked fixed-row height complete and continued to percentage height on `m3-height-percent`.
- 2026-10-15: Added percentage height parsing, C API configuration/validation, CLI and PTY/API regressions, and documentation. Exact-head CI is required before merge; automatic and inverted modes remain incomplete.
