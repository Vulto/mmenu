# Autonomous Work State

GitHub branch, pull-request, review, and CI state are authoritative. Do not claim local tests unless they ran.

## Current focus

- Active branch: `m3-layout-presets`.
- Reverse-prompt layout is merged to master; all four required GCC/Clang release and sanitizer jobs passed on that exact merged commit.
- Current change adds `--layout MODE` and `--layout=MODE` for `default`, `reverse`, and `reverse-list`, preserving the `--reverse` and `--reverse-list` aliases.
- Conflicting CLI selectors and contradictory C API layout flags are rejected before terminal initialization. CLI/API regression coverage and README/changelog documentation are included.
- No local checkout is mounted for compilation; validate the current change through exact-head CI.
- Next: inspect every required job and its logs on the exact PR head, fix failures on the same branch, then continue with the next dependency-ready M3 item.

## Validation requirements

- GCC release, Clang release, GCC sanitizers, and Clang sanitizers must pass on the exact current PR head.
- Agent 1 does not post QA markers or merge.
