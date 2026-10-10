# mmenu Quality Gates and Test Policy

## Purpose

This document defines evidence required before changing the matcher, memory layout,
CLI behavior, or interactive UI. Tests are a compatibility contract and an independent
measurement system, not a hurdle to be weakened when an implementation fails.

## Required CI gates

- GCC and Clang compile with C11, -Wall, -Wextra, -Wpedantic, and -Werror.
- CLI contract tests verify output bytes, status, ordering, case handling, indexes,
  legacy positional arguments, `--filter`/`--filter=`/`-f` argument combinations,
  unknown/duplicate options, empty records, CRLF, final records without newline, empty
  input, embedded-NUL rejection, records over 1 MiB followed by another record, closed-stdin
  errors, stdout write errors, generated deterministic corpora, and differential matching
  against grep's fixed-string oracle.
- Public C API tests reject invalid counts, NULL option arrays, and NULL entries before
  opening a terminal; pseudo-terminal tests cover NULL prompts and SIGWINCH resize events.
- Allocation-failure injection deterministically fails the first three CLI-owned
  arena/list allocations and requires a diagnostic and a non-crashing nonzero exit.
- The public C API is fault-injected at initial filter allocation, filter-list growth
  beyond 128 options, initial and post-filter rendering, non-empty and empty-query
  conversion, and incremental-filter allocation. Fatal allocation failures must return
  `-1`, emit a diagnostic, and clean up ncurses rather than terminating the host process;
  failure to allocate the incremental filter must safely fall back to a full filter.
- Repeated C API calls are tested after normal selection, cancellation, and an injected
  allocation failure to verify terminal/resource cleanup and state reset.
- AddressSanitizer and UndefinedBehaviorSanitizer run the regression suite.
- Valgrind checks the batch path for invalid accesses and leaks.
- Throughput/RSS benchmarks record five samples per size at 1k, 10k, 100k, and 1m records,
  retain min/median/max latency and peak RSS, and validate match counts.
- Ranking baseline records MRR, NDCG@5, Recall@5, match counts, tool versions, latency, and RSS
  for mmenu, fzf, skim, and fzy on a deterministic judged corpus.

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

The suite covers CLI batch filtering, a deterministic generated differential corpus,
ncurses interaction through a pseudo-terminal (including navigation bounds, cancellation,
repeated query edits, Backspace, Unicode input, resize-triggered redraws, 2x8 clipping, and
a 1x1 terminal), compiler/runtime health, and memory checks
for batch and interactive paths. Allocation fault injection covers each explicit allocation
path in the C API, including its incremental-filter fallback; it does not intercept
allocations internal to ncurses or the C library. More exhaustive terminal-width/redraw-geometry assertions across additional terminal types
and broader fuzzing beyond the deterministic property corpus remain open. Comparative ranking metrics are now recorded against a fixed
judged corpus; ranking improvements still require explicit relevance judgments and review.

The long-record test intentionally rejects silent truncation. If it fails on the current
implementation, preserve the failure as evidence and fix the input reader; do not change
the test to fit the fixed-size input buffer.

## Defects exposed by the baseline

The original regression run reproduced these defects; fixes are now present on the
quality branch and are guarded by the original assertions:

- Long records were truncated at the fixed input-buffer boundary. Input now uses getline
  and retains the complete record, including a final record without a newline.
- A record larger than the 1 MiB arena slab could leave the next record writing into the
  large record's allocation. Large records now force the following ordinary record into
  a fresh slab; a >1 MiB followed-by-small-record regression test guards this.
- Unknown options and extra positional arguments were silently ignored, legacy `t`/`true`
  index arguments were not honored, and `--filter=QUERY` was not parsed. The parser now
  validates argument combinations, documents `--help`/`-h`, and preserves the legacy form.
- Embedded NUL bytes were silently treated as C-string terminators, and buffered stdout
  errors could be missed before process exit. NUL records are rejected explicitly and
  stdout is flushed and checked before returning.
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

The benchmarks use deterministic data and validate output counts before timing. The
throughput sweep retains five latency samples per size plus min/median/max summaries and
peak RSS. The CI comparison builds the PR base revision and candidate with the same GCC
flags on the same runner, then compares five-sample medians at 100k and 1m records. A
candidate more than 2x slower or using more than 2x peak RSS fails the gate. These are
deliberately coarse guards against severe regressions, not guarantees against smaller
regressions; artifacts retain raw samples, summaries, ranking metrics, and environment
metadata. Never claim a regression from one noisy sample.
