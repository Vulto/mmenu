# mmenu Quality Gates and Test Policy

## Purpose

This document defines evidence required before changing the matcher, memory layout,
CLI behavior, or interactive UI. Tests are a compatibility contract and an independent
measurement system, not a hurdle to be weakened when an implementation fails.

## Required CI gates

- GCC and Clang compile with C11, -Wall, -Wextra, -Wpedantic, and -Werror.
- CLI contract tests verify output bytes, status, ordering, case handling, indexes,
  empty records, final records without newline, and large inputs.
- AddressSanitizer and UndefinedBehaviorSanitizer run the regression suite.
- Valgrind checks the batch path for invalid accesses and leaks.
- A deterministic benchmark records input bytes, item count, match count, and elapsed
  time at 1k, 10k, 100k, and 1m records. It validates match counts and retains its CSV.

## Non-negotiable test policy

1. Never remove, relax, skip, or rewrite a failing assertion merely to get a green build.
2. A behavior change requires an explicit contract change, rationale, and regression tests.
3. Tests verify externally observable behavior, not implementation-specific internals.
4. Performance comparisons use identical generated inputs, validate identical results,
   and record compiler, flags, host, and revision when comparisons are introduced.
5. Sanitizer or Valgrind findings are failures. Do not suppress them without documented,
   evidence-based explanation demonstrating a false positive.
6. Keep correctness gates deterministic. Timing data is collected separately from
   correctness; noisy shared-runner timings are not hard absolute guarantees.
7. Every discovered defect becomes a regression test before or with its fix.
8. Passing CI is evidence for tested behavior, not proof that all defects are absent.

## Current scope and known gaps

The initial suite covers CLI batch filtering, deterministic differential matching, basic
ncurses interaction through a pseudo-terminal, compiler/runtime health, and memory checks
for both batch and interactive paths. Resize handling, terminal-width boundaries, full
keyboard-binding coverage, allocation-failure injection, broader fuzz/property testing,
and comparative fuzzy-ranking quality are not yet comprehensively covered. Do not claim
complete coverage until those areas have explicit tests.

The long-record test intentionally rejects silent truncation. If it fails on the current
implementation, preserve the failure as evidence and fix the input reader; do not change
the test to fit the fixed-size input buffer.

## Defects exposed by the baseline

The original regression run reproduced these defects; fixes are now present on the
quality branch and are guarded by the original assertions:

- Long records were truncated at the fixed input-buffer boundary. Input now uses getline
  and retains the complete record, including a final record without a newline.
- A missing argument after --filter fell through toward interactive initialization.
  Argument validation now emits a diagnostic and exits with status 2 before reading stdin.
- Ctrl-C could be consumed by terminal signal handling before wget_wch received it.
  The interactive terminal is configured to deliver Ctrl-C to the cancellation handler.
- GCC's static analyzer identified unsafe direct assignment of realloc results in the
  interactive filter. Buffer growth now retains the old allocation until reallocation
  succeeds, and incremental filtering falls back to a full filter if growth fails.

Do not remove or weaken the regression assertions that originally exposed these defects.
New findings must be added only when supported by a reproducible CI failure or another
recorded reproduction.

## Performance interpretation

The benchmark uses deterministic data and validates output counts. The CI comparison
builds the PR base revision and candidate with the same GCC flags on the same runner, then
takes five samples per implementation at 100k and 1m records and compares medians. A
candidate more than 2x slower at either size fails the gate. This is a deliberately coarse
guard against severe regressions, not a guarantee against smaller regressions; artifacts
retain the raw single-run sweep and comparison medians. Never claim a regression from one
noisy sample.
