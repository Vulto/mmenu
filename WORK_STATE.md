# Autonomous Work State

GitHub branch, pull-request, review, and CI state are authoritative. Do not claim local tests unless they ran.

## Current focus

- Layout presets (`--layout default`, `reverse`, and `reverse-list`) merged to master in commit `b641929d5b609a613dbd2b1d74727e80173699f0`; all four required GCC/Clang release and sanitizer jobs passed on the exact PR head.
- Current change adds a concise `mmenu(1)` manual, a `nobuild.c` driver, and a regression script that formats the manual and verifies installation into a temporary prefix.
- The build driver defaults to `/usr/local`, never calls `sudo` or `doas`, and honors `PREFIX` for installations where the current user has write permission.
- Validate the new change through exact-head CI before merging.

## Validation requirements

- GCC release, Clang release, GCC sanitizers, and Clang sanitizers must pass on the exact current PR head.
- The build-driver test must verify the installed executable, installed man page, and successful groff formatting.
- Agent 1 does not post QA markers or merge.
