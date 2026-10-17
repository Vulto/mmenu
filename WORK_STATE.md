# Work State

GitHub branch, pull-request, and CI state are authoritative. Do not claim tests unless their results are confirmed.

## Current focus

- Replace the C-based `nobuild` utility with the POSIX `build.sh` build/install script; `nobuild.c`, `nobuild.h`, the old driver test, and the obsolete scheduled-agent workflow document have been removed.
- Keep the portable ncursesw/terminfo linker probe and document separate terminfo link dependencies.
- PR #60 includes escaped-space extended-query support and CLI, interactive C API, and streaming regressions.
- Technical-debt fixes in this change include the unavailable-ncurses-linker failure-path regression and suppressing expected PTY EPIPE noise during allocation-failure sweeps.
- Exact-head CI is required after the final commit. The container cannot reach GitHub and lacks ncurses development headers, ShellCheck, and groff; do not claim local integration tests passed.

## Validation requirements

- GCC release, Clang release, GCC sanitizers, and Clang sanitizers must pass on the exact current PR head.
- ShellCheck must include the build script and its regression test.
- The build regression must verify standalone build output, installed executable behavior, installed man page, and successful groff formatting.
- Allocation-failure sweeps must still validate allocation counts, exit behavior, and sanitizer safety; only expected broken-pipe noise from the test producer may be suppressed.
- Do not proceed to unrelated implementation work while the current change has failing CI or unresolved implementation debt.
