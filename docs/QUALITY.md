# mmenu Quality Gates and Test Policy

## Purpose

This document defines evidence required before changing the matcher, memory layout,
CLI behavior, or interactive UI. Tests are a compatibility contract and an independent
measurement system, not a hurdle to be weakened when an implementation fails.

## Required CI gates

- GCC and Clang compile with C11, -Wall, -Wextra, -Wpedantic, and -Werror.
- The clean release check compiles an optimized CLI from source, builds a standalone public-header consumer, checks CLI help, and runs substring/exact/format/NUL-delimited smoke tests.
- CLI contract tests verify output bytes, status, ordering, substring, exact, fuzzy, inverse, unsorted fuzzy, and NUL-delimited input/output matching, case handling, indexes,
  legacy positional arguments, `--filter`/`--filter=`/`-f` argument combinations,
  unknown/duplicate options, batch `{}`, `{index}`, and `{query}` output templates, invalid
  format combinations, empty records, CRLF, final records without newline, empty
  input, embedded-NUL rejection, records over 1 MiB followed by another record, closed-stdin
  errors, stdout write errors, generated deterministic corpora, a seeded 5,000-record fuzz-style corpus with sampled
  queries, and differential matching against grep's fixed-string oracle.
- Interactive multi-selection tests verify Tab toggling, original-input ordering, Enter fallback to the current option, cancellation, and CLI record output.
- Interactive query/key tests verify prefilled query filtering, printable key aliases for navigation/accept/toggle, invalid or conflicting bindings, query argument validation, and exact-mode multi-character typing.
- Preview CLI tests verify placeholder expansion for record/index/query, preview refresh on selection changes, shell-quoting of untrusted record text, and rejection of duplicate/missing preview options and batch-mode use.
- Preview rendering clips by terminal display cells, including wide Unicode characters, and replaces malformed multibyte sequences and control characters before drawing.
- The generic preview callback runs outside the matcher; only the CLI callback evaluates shell commands, with all record/index/query placeholders shell-quoted.
- Configurable C API entry points support an initial query and key aliases while the original `mmenu`/`mmenuEx`/`mmenuMulti` signatures preserve default behavior.
- Public C API tests reject invalid counts, NULL option arrays, and NULL entries before
  opening a terminal; a detached-session regression verifies that a missing controlling terminal
  returns `-1` with a diagnostic instead of terminating the host process. Pseudo-terminal tests
  cover NULL prompts and SIGWINCH resize events.
- Allocation-failure injection sweeps every wrapped CLI allocation reached by small input,
  a >1 MiB record, a multi-megabyte corpus that forces arena/list growth, and preview command
  expansion/capture. Batch failures must produce a diagnostic and nonzero exit; preview callback
  allocation failures must not crash or trigger sanitizer/runtime errors.
- The public C API allocation sweep first counts allocations in representative scenarios,
  then fails each allocation index in turn under ASan/UBSan. It covers a 600-option filter-list growth path, rendering, multi-select state/result allocation, ASCII/Unicode substring conversion,
  fuzzy result-array growth past 128/256 entries, and incremental filtering that reallocates a
  large filtered index list before exercising fallback. Fatal failures must return `-1`, emit diagnostics, and clean up ncurses rather than
  terminating the host process; incremental-filter allocation failure must safely fall back to
  a full filter.
- Repeated C API calls are tested after normal selection, cancellation, and an injected
  allocation failure to verify terminal/resource cleanup and state reset.
- Locale tests cover ASCII case-insensitive matching under `C` and `C.UTF-8`, exact-byte
  behavior for non-ASCII case differences, valid UTF-8 interaction, and invalid multibyte
  API options/prompts under both locales. Batch mode remains byte-oriented, preserves invalid
  UTF-8 bytes, and rejects embedded NUL records.
- AddressSanitizer and UndefinedBehaviorSanitizer run the regression suite.
- Clang libFuzzer runs a 15-second coverage-guided campaign against the fuzzy matcher
  with checked-in seeds for subsequences, repeated characters, empty queries, and Unicode.
  The harness verifies result membership, score consistency, unique indexes, descending
  scores, and stable input-order ties for equal scores.
- Valgrind checks the batch path for invalid accesses and leaks.
- Release policy, security boundaries, and user-visible changes are maintained in `docs/RELEASE.md`, `SECURITY.md`, and `CHANGELOG.md`.
- Throughput/RSS benchmarks record five samples per size at 1k, 10k, 100k, and 1m records,
  retain min/median/max latency and peak RSS, and validate match counts.
- Ranking baseline records MRR, NDCG@5, Recall@5, match counts, tool versions, latency, and RSS
  for default mmenu, opt-in fuzzy mmenu, fzf, skim, and fzy on a deterministic judged corpus.

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
repeated query edits, Backspace, Unicode input, fuzzy-match highlighting (including Unicode subsequences), fuzzy rendering at 1x1 and 2x8,
resize-triggered redraws both after input and while idle (including bounded idle input waits), wide-Unicode preview clipping, and 1x1, 1x40,
2x8, 3x20, 8x1, and 24x80 terminal geometries), compiler/runtime health, and memory checks
for batch and interactive paths. Allocation fault injection sweeps all wrapped allocation
calls reached by the representative CLI and C API scenarios; it does not intercept
allocations internal to ncurses or the C library. The deterministic regression corpus is reproducible, and a bounded coverage-guided matcher
fuzzing campaign runs in CI. Exhaustive terminal-emulator compatibility testing remains
outside the current CI contract. Comparative ranking metrics are recorded for both default and fuzzy mmenu against a fixed
judged corpus; ranking changes still require explicit relevance judgments and review.
The fuzzer is bounded to keep CI predictable; it supplements but does not replace the
deterministic regression tests or allocation-failure sweeps.

Input-record, arena-slab, fuzzy-result, and interactive filter capacity growth share overflow-safe signed-doubling and allocation-size checks. Dedicated regression tests exercise the boundaries without requiring enormous allocations. Multibyte/wide-character conversion buffers use checked addition and multiplication before allocation, with boundary tests for overflow. Interactive query-cursor positioning uses terminal display columns for Unicode prompts, clamps before narrowing, and has wide-character and `SIZE_MAX` regressions.

The long-record regression test intentionally rejects silent truncation. Any future failure
must be preserved as evidence and fixed in the input reader; do not change the test to fit a
fixed-size input buffer.

## Defects exposed by the baseline

The original regression run reproduced these defects; the fixes are present on `master`
and remain guarded by the original assertions:

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
