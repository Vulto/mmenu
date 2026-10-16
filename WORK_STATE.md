# Autonomous Work State

GitHub branch, pull-request, review, and CI state are authoritative. Do not claim local tests unless they ran.

## Current focus

- Extended-query escaped-space parsing on m2-extended-escaped-spaces; added a streaming C API regression for escaped-space AND matching.
- CLI and interactive C API regressions cover escaped-space terms and OR groups; README, changelog, and man page document the syntax.
- Local tests remain unavailable because GitHub DNS resolution fails in this environment; validate the complete suite through exact-head CI before accepting the change.

## Validation requirements

- GCC release, Clang release, GCC sanitizers, and Clang sanitizers must pass on the exact current PR head.
- Inspect all required job logs and unresolved review threads after each new commit.
- Agent 1 does not post QA markers or merge.
