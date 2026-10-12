# Changelog

## Unreleased

### Added
- Explicit ASCII case-sensitive and smart-case matching modes for CLI and C API; default matching remains ASCII-case-insensitive.
- Interactive fuzzy `--no-sort` mode to preserve input order, including while streaming new records.
- Interactive `--disabled` mode keeps all options visible while queries are edited.
- `--nth` field selection for batch and interactive matching, with POSIX extended-regex delimiters, empty-width delimiter validation, and raw-record output preservation.
- `--accept-nth` output-field selection for batch and interactive accepted records.
- `--with-nth` interactive display-field transformation while retaining original records for matching/output unless `--nth` is also set.
- Configurable fuzzy tie-breaking with `--tiebreak` and the C API `mmenu_config.tiebreak`.
- Interactive input streaming: the menu now shows and filters records before stdin reaches EOF.
- Opt-in extended query grammar for AND/OR expressions, inverse terms, exact contiguous terms, and prefix/suffix anchors in batch and interactive matching.

## 1.0.0 - 2026-10-11

### Added
- Exact and fuzzy matching modes, inverse filtering, and unsorted fuzzy batch output.
- NUL-delimited batch input/output and configurable batch output templates using `{}`, `{index}`, and `{query}`.
- Interactive multi-selection through Tab toggles and a public C API that returns selected original indexes.
- Prefilled interactive queries and configurable key aliases, with configured C API entry points.
- Interactive preview panes with record/index/query placeholders and a generic C API preview callback.
- Informational interactive first-paint measurements alongside batch performance and ranking-quality baselines.

### Fixed
- Long input records and records following records larger than the input arena slab are preserved.
- CLI argument combinations are validated before reading input; embedded NUL records and stdout write failures are handled explicitly.
- Interactive C API failures without a controlling terminal return an error instead of terminating the host process.
- Allocation failures in interactive filtering are handled safely, including incremental-filter fallback.
- Exact-mode interactive query edits recompute results correctly for multi-character queries.
- Interactive query cursor positioning clamps safely at terminal boundaries and accounts for wide Unicode prompt display columns.
- Checked size arithmetic prevents overflow in conversion-buffer allocations and input-capacity growth.

### Quality
- GCC and Clang strict-build and sanitizer CI, GCC static analysis, CLI and interactive API regression suites, Valgrind, allocation-failure sweeps, matcher fuzzing, performance comparisons, and ranking-quality baselines.
