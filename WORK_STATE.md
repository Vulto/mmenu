# Autonomous Work State

GitHub branch, pull-request, review, and CI state are authoritative. Do not claim local tests unless they ran.

## Current focus

- M3e layout presets merged as `b641929d5b609a613dbd2b1d74727e80173699f0` after CI run `38804630978` passed all four required GCC/Clang release and sanitizer jobs on PR head `050d7b2e1ec571fa86bc203400b257c27f913296`.
- Current branch: `m3-height-auto`. Adds adaptive `--height=~VALUE` and inverted `--height=-N`/`--height=-PCT%`; C API exposes `heightAuto`, `heightInverted`, and `heightPercentInverted`. CLI and PTY/API regressions plus README/changelog updates are included.
- Local compilation is unavailable in this environment; no local test pass is claimed. Exact-head CI is required.
- Next: inspect all four required jobs and failed logs on the exact PR head, correct failures on this branch, then continue with the next dependency-ready M3 item.

## Validation requirements

- GCC release, Clang release, GCC sanitizers, and Clang sanitizers must pass on the exact current PR head.
- Agent 1 does not post QA markers or merge.
