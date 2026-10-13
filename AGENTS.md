# Agent Instructions

These instructions apply to all work in this repository.

## Mission

Work autonomously through the prioritized, unfinished items in `PLAN.md`. The goal is verified implementation, not status reporting. Do not ask the user whether to continue between tasks.

## Required execution loop

1. Read this file, `PLAN.md`, `docs/QUALITY.md`, `docs/RELEASE.md`, `SECURITY.md`, and the relevant source/tests before acting.
2. Inspect the default branch, open pull requests, their review comments, and the latest GitHub Actions runs. Resolve existing work before starting unrelated work.
3. Select the highest-priority eligible incomplete task from `PLAN.md`. Respect dependencies and compatibility requirements.
4. Reproduce the relevant behavior or defect. Record the expected contract before changing code.
5. Implement the smallest complete change and add regression coverage for every discovered defect.
6. Run the relevant tests locally when an execution environment is available. Do not claim a test ran unless it actually ran.
7. Create or update a focused branch and pull request. Describe behavior, tests, risks, and known limitations.
8. Inspect CI checks and logs for the exact PR head commit. Fix failures on that branch, push a correction, and wait for the replacement checks. Re-check after every new commit.
9. Merge only when all required checks pass for the exact current head commit and repository policy permits merging. Never merge a stale or unvalidated head.
10. Update `PLAN.md` and `WORK_STATE.md` with evidence-backed progress, then move to the next eligible task.

## Quality and safety constraints

- Treat `docs/QUALITY.md` as the test contract. Never delete, weaken, skip, or loosen a test merely to obtain a green CI result.
- Do not conceal failures, mark incomplete work complete, or infer that a running check passed.
- Preserve public API and CLI compatibility unless the plan explicitly authorizes a change and the documentation/tests are updated.
- Keep changes small enough to review and validate. Avoid unrelated refactors and duplicate PRs.
- Do not leave known implementation debt introduced by the current task, unresolved review threads, or a failed CI run behind when moving on.
- Never commit credentials, tokens, private user data, generated binaries, or benchmark output that the repository treats as artifacts.
- Follow repository security policy. Do not execute untrusted input or commands without understanding their effects.
- If blocked by missing permissions, unavailable CI, a required human decision, or a reproducible external outage, document exact evidence and the smallest next action. Continue other independent, useful work when safe.
- Never claim repository edits, tests, commits, PRs, merges, or CI outcomes that were not confirmed by tool results.

## Completion criteria

A task is complete only when implementation, regression tests, documentation where needed, and required CI are complete on the validated commit. The project is complete only when all planned items are either implemented and verified or explicitly documented as blocked/deferred with evidence and rationale. A status report alone is not completion.
