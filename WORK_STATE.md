# Work State

GitHub branch, pull-request, and CI state are authoritative. Do not claim tests unless their results are confirmed.

## Current focus

- Simplify the build/install path by replacing the C-based `nobuild` utility with a POSIX shell script.
- Preserve portable ncursesw/terminfo linker probing, strict C23 builds, and unprivileged installation under a configurable `PREFIX`.
- PR #60 contains the escaped-space extended-query implementation and its CLI, interactive C API, and streaming regressions.
- Before this change, the latest PR-head CI run was #347 (`38998896234`) and all five jobs passed on `9065b3a085e16192b5f6685bc7e7545263ec8a17`. A new commit requires fresh exact-head validation.

## Validation requirements

- GCC release, Clang release, GCC sanitizers, and Clang sanitizers must pass on the exact current PR head.
- ShellCheck must include the build script and its regression test.
- The build regression must verify standalone build output, installed executable behavior, installed man page, and successful groff formatting.
- Do not proceed to unrelated implementation work while the current change has failing CI or unresolved implementation debt.
