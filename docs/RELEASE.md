# Release checklist

This project currently builds from source; do not announce a binary package or release artifact unless that packaging is added and tested explicitly.

## Before tagging

- [ ] Confirm the release candidate is merged to \`master\` and the working tree is clean.
- [ ] Review \`CHANGELOG.md\` and replace the Unreleased section with the release version/date. Do not invent a version number; choose it deliberately and use the same value for the Git tag and release notes.
- [ ] Review the public API in \`mmenu.h\`, including ownership of arrays returned by \`mmenuMulti\` and the behavior of configured callbacks.
- [ ] Review CLI help and README examples against the actual parser. Confirm that batch-only and interactive-only options are clearly separated.
- [ ] Review \`SECURITY.md\`, especially the explicit shell-execution boundary and quoting of preview placeholders.

## Required verification

All four required CI jobs must pass on the exact commit to be tagged:

- GCC release: strict build, GCC static analyzer, CLI/API regressions, Valgrind, performance and ranking baselines.
- Clang release: strict build and regression suites.
- GCC sanitizers: ASan/UBSan, allocation-failure injection and exhaustive allocation-failure sweeps.
- Clang sanitizers: ASan/UBSan and coverage-guided matcher fuzzing.

Additionally:

- [ ] Run \`sh tests/release-check.sh\` with both GCC and Clang.
- [ ] Inspect the performance and ranking artifacts. Investigate any threshold failure or unexplained ranking change; do not dismiss a result without evidence.
- [ ] Confirm that shell scripts pass ShellCheck and that \`git diff --check\` is clean.
- [ ] Verify that the public header builds as a standalone implementation and that the standalone CLI builds from a clean temporary directory.
- [ ] Check the release diff for generated files, secrets, local paths, temporary benchmarks, and unreviewed changes.
- [ ] Confirm runtime dependency expectations: a C11 compiler and wide-character ncurses development/runtime libraries for builds and execution, respectively.

## Tag and publish

1. Create the release tag on the exact merged commit after all checks pass.
2. Publish release notes from the reviewed changelog and include source links. Only attach prebuilt binaries if they are built reproducibly for documented targets and tested separately.
3. Do not move or silently replace a published tag. If a release defect is found, publish a corrective release with a new tag and document the issue.

## Stop conditions

Do not release with failing CI, unresolved sanitizer/Valgrind findings, open implementation PRs, undocumented behavior changes, or a known regression hidden by weakened tests. Fix the cause and rerun the affected gates; never remove assertions merely to obtain a green build.
