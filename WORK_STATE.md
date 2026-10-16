# Autonomous Work State

GitHub branch, pull-request, review, and CI state are authoritative. Do not claim local tests unless they ran.

## Current focus

- Extended-query escaped-space parsing on m2-extended-escaped-spaces.
- CLI and interactive C API regressions cover escaped-space terms and OR groups; README and changelog document the syntax.
- Local tests were not run in this environment. Validate the complete regression suite through exact-head CI before accepting the change.

## Validation requirements

- GCC release, Clang release, GCC sanitizers, and Clang sanitizers must pass on the exact current PR head.
- Inspect all required job logs and unresolved review threads after each new commit.
- Agent 1 does not post QA markers or merge.
