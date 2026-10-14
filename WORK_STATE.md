# Autonomous Work State

This file is a handoff record for scheduled runs. GitHub branch/PR state and CI remain the source of truth.

## Current focus

- Active PR: #51, `feat: add cyclic navigation`.
- M3 navigation item: opt-in `--cycle` wraps Up/Down at the first/last filtered result; default behavior is unchanged.
- Branch: `m3-cycle-navigation`.
- Changes include CLI parsing/help, additive `mmenu_config.cycle`, PTY regressions for both wrap directions, batch-mode validation, and PLAN.md tracking.
- Do not mark the feature complete or merge until QA and every required CI check pass on the exact current head.

## Latest verified checkpoint

- PR #51 head before this handoff update: `df49cbb42d23590cb23c6a9cd08fdfc9de8dc6d4`.
- Source/test commits include `422659e79b36ad72907515dc20e2552113b084ac`, `51bd35988a55969ad46b1f673c6db60961e5c48f`, `a4d2495e35252819185f2d8c7bd028f6c6cefa79`, and `cb67571ea28dd67c83c7f52398c05d54f3c5b2d4`.
- The PR was opened at `https://github.com/Vulto/mmenu/pull/51`. CI for the latest head must be queried again after this handoff update; no local test execution is claimed because this environment could not resolve github.com for cloning.
- The docs-only PR #49 was merged at `391c7e17c2ee72062ed2aa46bd9f779288918100`; there are no other open PRs besides #51.

## Next actions

1. Inspect CI for the exact latest PR #51 head after the QA correction.
2. Confirm the new C API wrap tests pass under GCC/Clang release and sanitizer jobs.
3. Re-review the exact current head; post the QA marker only if no defects remain.
4. Merge only after all required checks and the exact-head QA marker are valid; then resume the next incomplete PLAN.md item.

## Run log

- 2026-10-14: Resolved the stale PR #49 gate by merging its documentation-only change after exact-head CI success and a matching QA marker.
- 2026-10-14: Opened PR #51 for cyclic navigation. Initial CI passed all four GCC/Clang release and sanitizer jobs.
- 2026-10-14: QA found that mmenu_config.cycle had no direct C API regression coverage. Added PTY tests for both wrap directions over a filtered result set, default non-wrapping behavior, and empty-result safety. Clarified the public requirement to zero-initialize mmenu_config. This creates a new head; prior CI and review evidence must not be reused.
