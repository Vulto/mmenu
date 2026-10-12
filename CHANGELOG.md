# Changelog

## Unreleased

### Added
- Exact and fuzzy matching modes, inverse filtering, and unsorted fuzzy batch output.
- NUL-delimited batch input/output and configurable batch output templates using `{}`, `{index}`, and `{query}`.
- Interactive multi-selection through Tab toggles and a public C API that returns selected original indexes.
- Prefilled interactive queries and configurable key aliases, with configured C API entry points.
- Interactive preview panes with record/index/query placeholders and a generic C API preview callback.

### Fixed
- Long input records and records following records larger than the input arena slab are preserved.
- CLI argument combinations are validated before reading input; embedded NUL records and stdout write failures are handled explicitly.
- Interactive C API failures without a controlling terminal return an error instead of terminating the host process.
- Allocation failures in interactive filtering are handled safely, including incremental-filter fallback.
- Exact-mode interactive query edits recompute results correctly for multi-character queries.
- Interactive query cursor positioning clamps safely at terminal boundaries and accounts for wide Unicode prompt display columns.

### Quality
- Expanded GCC/Clang strict-build and sanitizer CI, static analysis, Valgrind, allocation-failure sweeps, matcher fuzzing, performance comparisons, and ranking-quality baselines.
