# Plan

## Current state (factual only)

- Repository: Vulto/mmenu; default branch: master.
- Baseline commit: 8ebed41eed3a742720deaa6efbc7045c951827e0.
- Implementation is a C11 single-header ncursesw library plus a CLI in main.c. The CI currently compiles as C11 with GCC and Clang.
- Public entry points include mmenu, mmenuEx, mmenuMulti, mmenuConfigured, and mmenuMultiConfigured. The public configuration structure provides initial-query, preview callback, and a small set of key bindings.
- Existing CLI options include --fuzzy, --exact, --invert-match, --no-sort, --read0, --print0, --multi, --query, --preview, --filter, --format, -t, and six dedicated single-key binding options.
- Interactive input is currently loaded as a complete list before the menu starts. The CLI batch filter is also a complete-input path.
- The existing CI includes strict GCC/Clang release builds, ASan/UBSan builds, CLI and PTY/API regression suites, static analysis, Valgrind, allocation-failure testing, matcher fuzzing, and performance artifacts.
- Existing measurement scripts: tests/benchmark.sh (batch throughput/RSS at 1k, 10k, 100k, 1M records) and tests/interactive-benchmark.sh (interactive first paint at 1k, 10k, 100k records). These report measurements rather than imposing absolute timing thresholds.
- No implementation or checklist item is considered complete without regression coverage and successful full CI on its commit.
- Authoritative option/action reference inspected for this inventory: https://github.com/junegunn/fzf/blob/master/man/man1/fzf.1 (fzf 0.74.5, checked 2026-10-12). The complete option and action surface below is tracked as individual entries; new upstream options must be added when the reference version changes.

## Complete fzf feature checklist

Status legend: present = behavior exists and is tested; partial = an incomplete or narrower analogue exists; missing = no equivalent verified.

### Search and matching

- [ ] Extended query mode (--extended / -x) — partial — no full extended-query parser exists.
- [ ] Extended query syntax, including whitespace-separated terms — partial — current matching is simpler and does not implement the complete query grammar.
- [ ] Exact matching (--exact / -e) — partial — batch and interactive exact modes exist, but full fzf query semantics are absent.
- [ ] Extended-search disable (--no-extended / +x) — missing — no extended-search parser exists.
- [ ] Case-insensitive matching (--ignore-case / -i) — missing — no selectable case mode.
- [ ] Case-sensitive matching (--no-ignore-case / +i) — missing — no selectable case mode.
- [ ] Smart-case matching (--smart-case) — missing — no query-sensitive case policy.
- [ ] Literal matching and Latin normalization controls (--literal) — missing — no equivalent option.
- [ ] Scoring schemes (--scheme=default|path|history) — missing — no selectable scoring scheme.
- [ ] Fuzzy algorithms (--algo=v1|v2) — missing — only the existing matcher is available.
- [ ] Search field selection (--nth) — missing — all input text is searched.
- [ ] Display field transformation (--with-nth) — missing.
- [ ] Accepted-output field selection/template (--accept-nth) — missing.
- [ ] Unsorted matching (--no-sort / +s) — partial — batch fuzzy mode supports --no-sort; interactive behavior and option compatibility differ.
- [ ] Custom field delimiter (--delimiter / -d) — missing.
- [ ] Tail-limited input (--tail) — missing.
- [ ] Disabled search (--disabled) — missing.
- [ ] Configurable tie-breaking (--tiebreak) — missing.
- [ ] Prefix, suffix, inverse, exact-boundary, quoted exact, and OR query operators — missing — no fzf extended-search grammar.
- [ ] Unicode-aware matching and normalization semantics — partial — wide-character UI support exists, but matching and normalization need a defined/tested policy.
- [ ] Ranking-quality fixtures and stable ranking baseline — partial — a baseline exists; the current matcher still needs comprehensive quality comparison.

### Input, streaming, and output

- [ ] Interactive incremental streaming input; show and update results before stdin closes — missing — the interactive list is currently fully loaded first.
- [ ] Keep arena/slab allocation for streaming input without per-record allocation — partial — the arena loader exists, but it is not incremental into the live UI.
- [ ] NUL-delimited input (--read0) — present — existing CLI option and regression coverage.
- [ ] NUL-delimited output (--print0) — present — existing CLI option and regression coverage.
- [ ] ANSI escape parsing and color preservation (--ansi) — missing.
- [ ] Synchronous input/search mode (--sync) — missing — no asynchronous streaming mode yet.
- [ ] TTY default-input behavior and --no-tty-default — missing.
- [ ] Preserve raw records separately from display/transformed records — partial — current arena/list model needs to be extended for ANSI, field transforms, and multiline presentation.
- [ ] Multi-line items and --no-multi-line — missing.
- [ ] Reverse input order (--tac) — missing.
- [ ] Track the current item across updates (--track) — missing.
- [ ] Stable item identity (--id-nth) — missing.
- [ ] Batch filter (--filter / -f) — present — existing mode; expand its semantics without changing current defaults.
- [ ] Print query (--print-query) — missing.
- [ ] Expected-key output (--expect) — missing.
- [ ] Select the sole match automatically (--select-1 / -1) — missing.
- [ ] Exit immediately on zero matches (--exit-0 / -0) — missing.
- [ ] No-clear terminal behavior (--no-clear) — missing.
- [ ] Standard exit statuses (0 selected, 1 no match, 2 error, 126/127 become failures, 130 interruption) — partial — current CLI status conventions are narrower.
- [ ] Batch output templates and original-index output — present — preserve existing --format and -t behavior while adding compatible conventions.
- [ ] Robust stdout/stderr failure handling and prompt option validation — present — keep regression coverage.

### Global style and color

- [ ] Style presets (--style=default|minimal|full[:border]) — missing.
- [ ] Named color/theme configuration (--color) — missing.
- [ ] Disable color (--no-color) — missing.
- [ ] Disable bold (--no-bold) — missing.
- [ ] Black terminal background preset (--black) — missing.
- [ ] Honor NO_COLOR consistently — missing.
- [ ] ANSI color names, 256-color, RGB color, attributes, and per-component colors — missing.
- [ ] Preserve matching highlights while rendering ANSI-colored input — missing.

### Display size, layout, and chrome

- [ ] Height modes (--height, percentages, auto/inverted sizing) — missing.
- [ ] Minimum height (--min-height) — missing.
- [ ] Popup placement and sizing (--popup) — missing.
- [ ] tmux popup integration (--tmux) — missing.
- [ ] Layout modes (--layout=default|reverse|reverse-list) and --reverse — missing.
- [ ] Margin geometry (--margin) — missing.
- [ ] Padding geometry (--padding) — missing.
- [ ] Border styles and placement (--border, including none, rounded, sharp, bold, block, thinblock, double, dashed, horizontal, vertical, top, bottom, left, right, line, inline) — missing.
- [ ] Global border label and position (--border-label, --border-label-pos) — missing.
- [ ] List border (--list-border) — missing.
- [ ] List label and position (--list-label, --list-label-pos) — missing.
- [ ] Header/footer/input/preview borders and labels — missing.
- [ ] Header label (--header-label) — missing.
- [ ] Header label position (--header-label-pos) — missing.
- [ ] Footer label (--footer-label) — missing.
- [ ] Footer label position (--footer-label-pos) — missing.
- [ ] Header/footer label positioning — missing.
- [ ] Multi-selection limit (--multi[=MAX]) — partial — unlimited multi-select exists; limits do not.
- [ ] Highlight selected/current line (--highlight-line) — missing.
- [ ] Cursor wrapping (--cycle) — missing.
- [ ] Multiline wrapping modes and wrap signs (--wrap, --wrap-sign, --no-multi-line) — missing.
- [ ] Raw mode (--raw) — missing.
- [ ] Gap between list entries (--gap, --gap-line) — missing.
- [ ] Freeze fields on left/right (--freeze-left, --freeze-right) — missing.
- [ ] Keep right side visible (--keep-right) — missing.
- [ ] Scroll offset (--scroll-off) — missing.
- [ ] Horizontal scrolling control and offset (--no-hscroll, --hscroll-off) — missing.
- [ ] Jump labels (--jump-labels) — missing.
- [ ] Gutter and raw gutter (--gutter, --gutter-raw) — missing.
- [ ] Configurable pointer (--pointer) — missing.
- [ ] Configurable multi-select marker (--marker, --marker-multi-line) — partial — basic selection state exists, but no configurable marker parity.
- [ ] Ellipsis (--ellipsis) — missing.
- [ ] Tab width (--tabstop) — missing.
- [ ] Scrollbar glyphs and disabling (--scrollbar, --no-scrollbar) — missing.
- [ ] List position controls (--list) — missing.
- [ ] Input visibility (--no-input) — missing.
- [ ] Prompt text (--prompt) — partial — current positional prompt exists, but not full fzf prompt semantics or runtime action.
- [ ] Info-line styles and disabling (--info, --no-info) — missing.
- [ ] Dynamic info command (--info-command) — missing.
- [ ] Item separator (--separator, --no-separator) — missing.
- [ ] Ghost text (--ghost) — missing.
- [ ] Filepath-aware word movement (--filepath-word) — missing.
- [ ] Input border, label, and label position (--input-border, --input-label, --input-label-pos) — missing.
- [ ] Header text, placement, and borders (--header, --header-lines, --header-first, --header-border, --header-lines-border) — missing.
- [ ] Footer text and border (--footer, --footer-border) — missing.
- [ ] Unicode glyph toggle (--no-unicode) — missing.
- [ ] East Asian ambiguous-width handling (--ambidouble) — partial — wide prompt cursor geometry has regression coverage, but no matching option.

### Preview system

- [ ] Preview command (--preview) — partial — a synchronous preview command exists.
- [ ] Asynchronous preview execution without blocking input/rendering — missing.
- [ ] Preview layout and sizing (--preview-window) — missing.
- [ ] Right, left, top, bottom, center, and next-to-input preview positions — partial — current implementation provides a side pane only.
- [ ] Preview width/height in cells and percentages — missing.
- [ ] Preview border styles and placement (--preview-border) — missing.
- [ ] Preview label and label position (--preview-label, --preview-label-pos) — missing.
- [ ] Preview wrapping, word wrapping, and wrap sign (--preview-window wrap options) — missing.
- [ ] Preview wrap sign (--preview-wrap-sign) — missing.
- [ ] Preview follow and cycle modes — missing.
- [ ] Preview hidden/visible initial state and runtime toggles — partial — no complete fzf-equivalent state/action surface.
- [ ] Preview info display and suppression — missing.
- [ ] Preview scroll offsets, fractional scroll, header lines, default layout, and size-threshold alternative layout — missing.
- [ ] Runtime preview command changes, multiple preview bindings, and refresh — missing.
- [ ] Preview up/down/page/half-page/top/bottom scrolling — missing.
- [ ] Preview placeholders for item, index, query, selected items, fields, and field ranges — partial — only a narrow placeholder set is available.
- [ ] Correct shell quoting for every placeholder and safe handling of embedded quotes/newlines — partial — preserve current quoting tests and expand adversarial coverage.
- [ ] Preview process cancellation/reaping and resize handling — missing.

### Query input, selection, history, and mouse

- [ ] Prefilled query (--query / -q) — partial — exists for interactive mode, but lacks fzf query semantics and history integration.
- [ ] Query editing with Unicode-aware cursor movement — partial — current cursor behavior is tested; complete readline-style editing is absent.
- [ ] Persistent query history (--history) — missing.
- [ ] History size limit (--history-size) — missing.
- [ ] Previous/next history actions — missing.
- [ ] Full multi-selection behavior (--multi, selected-item ordering, limits, deselection) — partial — current multi-select exists but action/limit semantics are incomplete.
- [ ] Mouse support, wheel scrolling, clicks, and double-click acceptance — missing.
- [ ] Mouse disable (--no-mouse) — missing.
- [ ] Keyboard key-name parser and all fzf key aliases/chords — missing — current custom bindings accept a single printable key.
- [ ] Jump-label interaction — missing.
- [ ] Resize events and robust geometry recomputation — partial — geometry helpers and resize handling exist, but no event binding system.

### Scripting, actions, and runtime events

- [ ] General key/action binding grammar (--bind), comma-separated bindings, action arguments, and action composition — missing — only dedicated one-key flags exist.
- [ ] Action argument quoting/alternate delimiters and parser escaping — missing.
- [ ] All fzf action names below — only the basic up/down/accept/cancel/toggle/backspace behaviors have partial equivalents; every other action is missing.
- [ ] Event names below and correct event timing — missing.
- [ ] Environment-driven default options (FZF_DEFAULT_OPTS, FZF_DEFAULT_OPTS_FILE) — missing.
- [ ] Configurable child shell (--with-shell) — missing.
- [ ] Local HTTP/Unix socket action server (--listen) — missing.
- [ ] Explicit unsafe remote execution mode (--listen-unsafe) — missing.
- [ ] Thread-count option (--threads) — missing.
- [ ] Built-in benchmark mode (--bench) — missing — external benchmark scripts exist.
- [ ] Directory walker (--walker, --walker-root, --walker-skip) — missing.
- [ ] Shell integration scripts (--bash, --zsh, --fish, --nushell) — missing.
- [ ] Version/help/man output (--version, --help, --man) — partial — --help exists; version and man output do not.
- [ ] No-TTY default behavior and terminal output clearing control — missing.
- [ ] Remote-listener authentication boundary (FZF_API_KEY) — missing, depends on listener feature.

#### Complete action inventory

- [ ] abort — missing.
- [ ] accept — partial — existing accept key behavior only.
- [ ] accept-non-empty — missing.
- [ ] accept-or-print-query — missing.
- [ ] backward-char — missing.
- [ ] backward-delete-char — partial — basic backspace only.
- [ ] backward-delete-char/eof — missing.
- [ ] backward-kill-subword — missing.
- [ ] backward-kill-word — missing.
- [ ] backward-subword — missing.
- [ ] backward-word — missing.
- [ ] become(...) — missing.
- [ ] beginning-of-line — missing.
- [ ] bell — missing.
- [ ] best — missing.
- [ ] bg-cancel — missing.
- [ ] cancel — partial — basic cancel behavior only.
- [ ] change-border-label(...) — missing.
- [ ] change-ghost(...) — missing.
- [ ] change-header(...) — missing.
- [ ] change-header-lines(...) — missing.
- [ ] change-header-label(...) — missing.
- [ ] change-input-label(...) — missing.
- [ ] change-list-label(...) — missing.
- [ ] change-multi — partial — multi-select is configured at startup only.
- [ ] change-multi(...) — missing.
- [ ] change-nth(...) — missing.
- [ ] change-with-nth(...) — missing.
- [ ] change-pointer(...) — missing.
- [ ] change-preview(...) — missing.
- [ ] change-preview-label(...) — missing.
- [ ] change-preview-window(...) — missing.
- [ ] change-prompt(...) — missing.
- [ ] change-query(...) — missing.
- [ ] clear-screen — missing.
- [ ] clear-multi — missing.
- [ ] close — missing.
- [ ] clear-query — missing.
- [ ] delete-char — missing.
- [ ] delete-char/eof — missing.
- [ ] deselect — missing.
- [ ] deselect-all — missing.
- [ ] disable-raw — missing.
- [ ] disable-search — missing.
- [ ] down — partial — basic movement exists.
- [ ] down-match — missing.
- [ ] down-selected — missing.
- [ ] enable-raw — missing.
- [ ] enable-search — missing.
- [ ] end-of-line — missing.
- [ ] exclude — missing.
- [ ] exclude-multi — missing.
- [ ] execute(...) — missing.
- [ ] execute-silent(...) — missing.
- [ ] first — missing.
- [ ] forward-char — missing.
- [ ] forward-subword — missing.
- [ ] forward-word — missing.
- [ ] half-page-down — missing.
- [ ] half-page-up — missing.
- [ ] hide-header — missing.
- [ ] hide-input — missing.
- [ ] hide-preview — partial — no complete action system.
- [ ] ignore — missing.
- [ ] jump — missing.
- [ ] kill-line — missing.
- [ ] kill-subword — missing.
- [ ] kill-word — missing.
- [ ] last — missing.
- [ ] next-history — missing.
- [ ] next-selected — missing.
- [ ] offset-down — missing.
- [ ] offset-middle — missing.
- [ ] offset-up — missing.
- [ ] page-down — missing.
- [ ] page-up — missing.
- [ ] pos(...) — missing.
- [ ] prev-history — missing.
- [ ] prev-selected — missing.
- [ ] preview(...) — partial — one configured preview command exists, but runtime preview action is missing.
- [ ] preview-bottom — missing.
- [ ] preview-down — missing.
- [ ] preview-half-page-down — missing.
- [ ] preview-half-page-up — missing.
- [ ] preview-page-down — missing.
- [ ] preview-page-up — missing.
- [ ] preview-top — missing.
- [ ] preview-up — missing.
- [ ] print(...) — missing.
- [ ] put — missing.
- [ ] put(...) — missing.
- [ ] rebind(...) — missing.
- [ ] refresh-preview — missing.
- [ ] reload(...) — missing.
- [ ] reload-sync(...) — missing.
- [ ] replace-query — missing.
- [ ] search(...) — missing.
- [ ] select — partial — current toggle-based selection only.
- [ ] select-all — missing.
- [ ] show-header — missing.
- [ ] show-input — missing.
- [ ] show-preview — partial — preview can be configured initially, but runtime action is missing.
- [ ] toggle — partial — selection toggle exists in multi mode.
- [ ] toggle-all — missing.
- [ ] toggle-bind — missing.
- [ ] toggle-header — missing.
- [ ] toggle-hscroll — missing.
- [ ] toggle-in — missing.
- [ ] toggle-input — missing.
- [ ] toggle-multi-line — missing.
- [ ] toggle-out — missing.
- [ ] toggle-preview — partial — no complete runtime binding action.
- [ ] toggle-preview-wrap — missing.
- [ ] toggle-preview-wrap-word — missing.
- [ ] toggle-raw — missing.
- [ ] toggle-search — missing.
- [ ] toggle-sort — missing.
- [ ] toggle-track — missing.
- [ ] toggle-track-current — missing.
- [ ] toggle-wrap — missing.
- [ ] toggle-wrap-word — missing.
- [ ] track-current — missing.
- [ ] transform(...) — missing.
- [ ] transform-border-label(...) — missing.
- [ ] transform-ghost(...) — missing.
- [ ] transform-header(...) — missing.
- [ ] transform-header-lines(...) — missing.
- [ ] transform-header-label(...) — missing.
- [ ] transform-input-label(...) — missing.
- [ ] transform-list-label(...) — missing.
- [ ] transform-nth(...) — missing.
- [ ] transform-with-nth(...) — missing.
- [ ] transform-pointer(...) — missing.
- [ ] transform-preview-label(...) — missing.
- [ ] transform-prompt(...) — missing.
- [ ] transform-query(...) — missing.
- [ ] transform-search(...) — missing.
- [ ] trigger(...) — missing.
- [ ] unbind(...) — missing.
- [ ] unix-line-discard — missing.
- [ ] unix-word-rubout — missing.
- [ ] untrack-current — missing.
- [ ] up — partial — basic movement exists.
- [ ] up-match — missing.
- [ ] up-selected — missing.
- [ ] wait — missing.
- [ ] yank — missing.
- [ ] bg-transform(...) and bg-transform-* variants — missing.

#### Complete event inventory

- [ ] start — missing.
- [ ] load — missing.
- [ ] resize — partial — resize handling exists but is not bindable as an event.
- [ ] result — missing.
- [ ] result-final — missing.
- [ ] change — missing.
- [ ] focus — missing.
- [ ] multi — missing.
- [ ] one — missing.
- [ ] zero — missing.
- [ ] jump — missing.
- [ ] N (numeric event) — missing.
- [ ] Event ordering, re-entrancy, cancellation, and background-process semantics — missing.

#### Search expression and placeholder inventory

- [ ] Unanchored fuzzy query terms — partial — basic subsequence matching exists.
- [ ] Exact query term syntax (single-quoted terms) — missing.
- [ ] Prefix/anchored query terms (caret syntax) — missing.
- [ ] Suffix/ending query terms (dollar syntax) — missing.
- [ ] Exact-boundary terms (both ends anchored) — missing.
- [ ] Inverse/negative terms — missing.
- [ ] OR expression syntax — missing.
- [ ] Field index expressions (positive/negative indices and ranges) — missing.
- [ ] Item placeholder {} and escaped literal placeholder — partial — narrow preview placeholder handling exists.
- [ ] Index placeholder {n} — missing.
- [ ] Query placeholder {q} — missing.
- [ ] Multi-selection placeholder {+} and selected-field variants — missing.
- [ ] Field/range placeholders and delimiter-aware extraction — missing.
- [ ] Placeholder shell-quoting contract for embedded apostrophes, newlines, and empty values — partial.
- [ ] FZF_* runtime variables for dimensions, query, current item, selection, counts, action, and preview context — missing.

### C API compatibility and capability checklist

- [ ] Preserve ABI/source behavior of mmenu, mmenuEx, mmenuMulti, mmenuConfigured, and mmenuMultiConfigured — present — compatibility is a hard invariant.
- [ ] Keep the existing arena loader and batch modes unchanged by default — present — compatibility is a hard invariant.
- [ ] Add new configuration fields only additively and with documented defaults — missing — no broad configuration model yet.
- [ ] Expose new interactive options through configured API without leaking ncurses types — partial — current API has only a small config structure.
- [ ] Provide C API configuration for match mode, case policy, scorer, layout, selection, bindings, preview, streaming, and output — missing.
- [ ] Stable ownership/lifetime rules for selections, callback data, streamed records, and preview jobs — partial — existing multi-select ownership is documented; new facilities need contracts.
- [ ] C23 compiler mode and portable C23 implementation — missing — CI currently builds in C11 mode.
- [ ] Keep binary size and dependency set measured — missing — establish baseline in M0.

## Milestone list (ordered)

- [ ] M0 — Baseline & checklist: complete this inventory; run reproducible batch/interactive baselines and comparative fzf/fzy runs when available; record numbers and environment only in this file.
- [ ] M1 — Streaming input: incremental loader/UI updates before EOF, cancellation, loader/search synchronization, stable arena ownership, and tests for slow/never-ending input.
- [ ] M2 — Matcher parity & speed: complete query grammar and match modes, case policy, scorer/algorithm choices, tie-breaks, field expressions, ranking tests, and measured performance.
- [ ] M3 — Layout & chrome: dimensions, layout modes, borders, margins/padding, info/header/footer/input chrome, colors, scrolling, line wrapping, and presets.
- [ ] M4 — Preview system: nonblocking child lifecycle, full layout/size/wrap/follow/scroll semantics, placeholders, quoting, runtime changes, and C API configuration.
- [ ] M5 — Key bindings, multi-select, history, jump, mouse: general parser, complete action dispatch, events, history, selection behavior, key aliases, jump labels, and mouse input.
- [ ] M6 — Remaining scripting options & polish: expected keys, output/exit conventions, ANSI, multiline, walker/shell integrations, listener if safe and supportable, remaining options, final measurement and complete regression coverage.

## Tasks for the current milestone

### M0 — Baseline & checklist

- [x] Verify this checklist against the entire upstream fzf 0.74.5 man page, including options, inverse options, actions, events, placeholders, environment variables, and exit statuses.
- [ ] Build baseline binaries with GCC and Clang using the existing CI flags; also build in strict C23 mode and record compiler support.
- [x] Run the full existing regression suite unchanged and record the exact commit and result.
- [x] Run tests/benchmark.sh with 5 samples per input size (1k, 10k, 100k, 1M), preserving raw CSV, summary CSV, metadata, elapsed time, and peak RSS.
- [x] Run tests/interactive-benchmark.sh with 3 samples per size (1k, 10k, 100k), preserving first-paint measurements and terminal dimensions.
- [x] Run the newly added reproducible matcher-throughput harness against the same generated records/query for mmenu, fzy, and fzf; retain versions, compiler/runtime, input size, matches, elapsed time, and RSS. Do not make noisy absolute timing a CI gate.
- [x] Record the existing controlled-PTY interactive first-paint baseline at a fixed 80x24 terminal size, including its measurement limitations.
- [x] Record existing batch/interactive benchmark numbers, available tool versions, machine/kernel, terminal, sample count, and known measurement limitations below.
- [ ] Record C23-mode compiler support and baseline binary size from the new smoke-build artifact.
- [x] Confirm the matcher-throughput harness is deterministic in input/query/output and that all existing test assertions remain unchanged.
- [ ] Verify the harness and C23 smoke build on CI, then mark M0 complete only after all four CI jobs pass.

## Measurement notes

- Baseline revision: 8ebed41eed3a742720deaa6efbc7045c951827e0.
- Current CI run on the baseline revision: https://github.com/Vulto/mmenu/actions/runs/38190345009 — GCC release, Clang release, GCC sanitizers, and Clang sanitizers all passed.
- Existing batch benchmark sizes: 1,000; 10,000; 100,000; 1,000,000 records; 5 samples per size.
- Existing interactive first-paint sizes: 1,000; 10,000; 100,000 records; 3 samples per size; terminal 80x24.
- Baseline batch filter (commit 8ebed41eed3a742720deaa6efbc7045c951827e0; GCC 13.3.0; -O2 -g -std=c11 -Wall -Wextra -Wpedantic -Werror -D_DEFAULT_SOURCE; 5 samples per size):
  - 1,000 records: median 2,354,947 ns; max RSS 2,088 KiB; min/max 2,114,366 / 2,464,737 ns.
  - 10,000 records: median 3,015,098 ns; max RSS 2,092 KiB; min/max 2,975,098 / 5,354,650 ns.
  - 100,000 records: median 8,751,763 ns; max RSS 4,828 KiB; min/max 8,651,693 / 9,027,608 ns.
  - 1,000,000 records: median 71,232,287 ns; max RSS 31,004 KiB; min/max 68,015,176 / 103,228,474 ns.
- Baseline interactive first paint (terminal 80x24, TERM=xterm; 3 samples; wall-clock launch-to-prompt marker):
  - 1,000 records: samples 8,447,272 / 8,381,618 / 8,581,067 ns; median 8,447,272 ns.
  - 10,000 records: samples 8,328,363 / 8,368,603 / 8,362,950 ns; median 8,362,950 ns.
  - 100,000 records: samples 14,988,676 / 15,061,079 / 15,011,729 ns; median 15,011,729 ns.
- Ranking-quality fixture versions: fzf 0.44.1 (Debian package), fzy 1.0, skim 5.7.4. On the four small judged query sets, mmenu fuzzy and fzf both reported MRR 1.0000; NDCG@5 values were menu 0.8273, mmh 0.8839, tst 0.9896, cfg 0.8502; Recall@5 values were 0.8333, 0.6667, 1.0000, 1.0000. fzy NDCG@5 was 0.8273, 0.8839, 0.8502, 0.8502 with the same recall values. Candidate counts were 8, 4, 5, and 4 respectively.
- Matcher-throughput run 38191917923 (same GCC release job as the performance artifact; GCC 13.3.0 per that job's compiler metadata; query "ndl"; 3 samples; one in ten records contains "needle"; all tools returned the same exact set):
  - 1,000 records — mmenu 3,000,323 ns / 1,900 KiB; fzf 4,361,185 ns / 11,028 KiB; fzy 2,643,567 ns / 2,052 KiB (median time / maximum RSS).
  - 10,000 records — mmenu 7,397,665 ns / 2,080 KiB; fzf 9,284,398 ns / 15,816 KiB; fzy 3,376,936 ns / 2,308 KiB.
  - 100,000 records — mmenu 50,176,892 ns / 5,344 KiB; fzf 36,425,782 ns / 26,204 KiB; fzy 9,123,756 ns / 5,764 KiB.
  - 1,000,000 records — mmenu 480,676,231 ns / 38,584 KiB; fzf 318,311,756 ns / 132,360 KiB; fzy 59,030,097 ns / 43,252 KiB.
- C23 smoke build passed in both GCC and Clang release jobs using -std=c2x. GCC version was 13.3.0. GCC loaded section totals from `size`: 37,128 bytes for both the existing C11 binary and C2x smoke binary. Exact Clang version and executable file byte counts are pending the next smoke-build artifact.
- The throughput artifact's own compiler field was initially unset; this is being corrected by passing CC=gcc and recording the compiler version directly.


- Batch timing measures end-to-end filter invocation, not isolated matcher throughput.
- Existing interactive first-paint marker measures launch-to-prompt visibility; it does not currently measure time-to-first-record or responsiveness during input streaming.
- Comparative timing is informational unless repeated, controlled measurements justify a stable threshold. Always preserve raw samples as well as summaries.

## Open risks

- Streaming input requires a producer/consumer design that safely coordinates the input reader, live arena growth, matching, UI redraw, cancellation, and stable record references.
- The current C API accepts a completed array of strings; a streaming C API should be additive and must define ownership and callback/lifetime rules.
- Current CI tests C11; C23 support must be established across available GCC/Clang toolchains before choosing mandatory flags.
- The fzf option/action surface includes process execution, shell quoting, reload, remote listener, and background transforms; avoid command injection, unbounded child accumulation, and deadlocks.
- ANSI parsing must keep terminal control sequences out of cursor-width and matching calculations without corrupting original output records.
- Unicode grapheme width and normalization require careful separation between byte offsets, code-point offsets, and terminal display columns.
- Matching 1M records under interactive query updates can expose CPU and memory spikes; optimize only after reproducible baselines and correctness tests exist.
- Keep the project source-first and C-only; do not add C++/Rust/Go/Python runtime implementations.
- Public-facing files other than this plan must remain factual and must not contain roadmap, goal, or comparative claims.
