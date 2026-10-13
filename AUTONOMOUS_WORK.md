# Autonomous Engineering Workflow

## Purpose

This document defines how a scheduled ChatGPT run should resume work on mmenu without a user having to type a continuation prompt.

## Every scheduled run

1. Read `AGENTS.md`, `WORK_STATE.md`, and the relevant sections of `PLAN.md`.
2. Inspect open PRs and their latest checks before choosing new work.
3. For each in-progress PR, compare the checked-out/head SHA with the SHA validated by CI. Inspect every required job and its logs.
4. If a check is pending, do useful independent source review, test design, or analysis; do not report pending CI as success. Revisit it on the next scheduled run.
5. If a check failed, identify the failing command and root cause, make a targeted fix on the existing branch, add a regression test when appropriate, and push the fix. Wait for and inspect the new CI run.
6. If CI is green, resolve review feedback and merge only if repository policy and required checks permit it. Do not move to an unrelated task while a current PR still has unresolved implementation debt.
7. Once current PR work is integrated, select the next incomplete, dependency-ready item from `PLAN.md`.
8. Implement, test, document, open/update a PR, and repeat the CI recovery loop.
9. Update `WORK_STATE.md` and `PLAN.md` with confirmed evidence and the next concrete action before ending the run.

## Task selection

- Follow the priority order and dependencies in `PLAN.md`; do not substitute a convenient task for a higher-priority eligible task.
- Prefer a small, measurable change with a clear behavioral contract and regression test.
- Do not mark an item complete because code exists; verify its contract and CI.
- Do not start duplicate work already covered by an open PR.
- If the planned item is blocked, document why and work on the next independent eligible item without abandoning the blocker.

## CI and pull request rules

- Check all required CI jobs, not only the summary status.
- Use job logs and artifacts to diagnose failures; do not guess from a red badge.
- A new commit invalidates validation of the previous head. Re-check the new head's complete required check set.
- Never bypass failing required checks, alter tests to hide a defect, or merge a commit different from the validated head.
- Keep PRs focused and include reproduction, implementation summary, test commands/results, and remaining limitations.
- Avoid leaving multiple overlapping PRs or unresolved review threads. Finish the current change before opening unrelated work.

## Scope and truthful reporting

The repository files specify engineering behavior; they do not schedule or launch ChatGPT by themselves. The hourly ChatGPT scheduled task is the external trigger. GitHub Actions CI is the validation system, not an autonomous coding agent unless a separate workflow explicitly implements that behavior.

Each run must make a concrete engineering contribution whenever safely possible. If blocked, report the exact blocker and evidence in `WORK_STATE.md`; never fabricate work or claim a CI result that has not completed.
