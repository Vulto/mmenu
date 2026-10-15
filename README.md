# mmenu - minimal menu

Small ncurses menu for c programs and shell scripts, similar to suckless' dmenu. 

## For shell scripts

Interactive mode starts before stdin reaches EOF and adds options as records arrive.
Newline-delimited input is the default; `--read0` enables NUL-delimited records.
The interactive menu remains usable while the producer is open. Batch `--filter`
mode remains synchronous and waits for the complete input stream.

```text
mmenu [PROMPT] [t|true] [-t] [--multi] [--query QUERY] [--preview COMMAND] [--bind-ACTION KEY]
```

The selected line is printed by default. `--query QUERY` pre-fills and applies an initial
interactive query. `--height N` limits the interactive option list to a fixed positive number
of visible rows; `--height PCT%` uses that percentage (1–100%) of available option rows. The
input row remains separate. Omitted height uses the available terminal rows; automatic and
inverted height modes are not implemented. `--preview COMMAND` opens a side pane and synchronously runs the command
for the highlighted record whenever the selection or query changes. The command output is
clipped to the pane. `{}`, `{index}`, and `{query}` are replaced by shell-quoted values for
the selected record, its original zero-based input index, and the current query. The command
itself is evaluated by the shell only when `--preview` is explicitly supplied; placeholders
are quoted to prevent record/query contents from becoming shell syntax. The pane is shown
when the terminal is at least 40 columns by 4 rows; smaller terminals keep the menu full-width
and do not run the preview command. Preview is interactive-only and cannot be combined with
`--filter`. `--bind-up`, `--bind-down`, `--bind-accept`, `--bind-cancel`, and
`--bind-backspace` each add a printable ASCII key alias; `--bind-toggle` adds an alias for
Tab and requires `--multi`. Built-in keys remain active. Use `space` to bind the Space key.
`--multi` enables multi-selection: Tab toggles an option and Enter emits all toggled records
in original input order. If no options were toggled, Enter selects the current option. For compatibility, a second positional argument
of `t` or `true`, or the `-t` flag, prints the selected zero-based index instead; with
`--multi -t`, all selected original indexes are printed.

Batch filtering does not open ncurses:

```text
mmenu --filter QUERY [-t] [--read0] [--print0]
mmenu --filter=QUERY [-t]
mmenu -f QUERY [-t]
mmenu --fuzzy [PROMPT] [-t] [--no-sort]
mmenu --exact [PROMPT] [-t]
mmenu --fuzzy --filter QUERY [-t]
mmenu --fuzzy --no-sort --filter QUERY [-t]
mmenu --fuzzy --no-sort [PROMPT] [--query QUERY]
mmenu --exact --filter QUERY [-t]
mmenu --invert-match --filter QUERY [-t] [--read0] [--print0]
```

Default batch mode emits ASCII-case-insensitive contiguous-substring matches in input order. Use `--no-ignore-case` (or `+i`) for ASCII-case-sensitive matching, `--ignore-case` (or `-i`) to restore the default, and `--smart-case` to ignore case unless the query contains an ASCII uppercase letter. These policies also apply to fuzzy and extended matching. `--exact` matches whole records byte-for-byte (case-sensitive), preserving input order and duplicates. An empty
query matches every input record; an empty input is valid. Batch mode is byte-oriented and
preserves invalid UTF-8 bytes, but rejects embedded NUL bytes in newline-delimited mode. The interactive C API
requires valid multibyte options and prompt text in the active locale; matching folds ASCII
case only, while non-ASCII bytes must match exactly. `-h`/`--help` prints usage.
`--invert-match` (or `-v`) emits records that do not match the selected batch matcher, in original input order; it requires `--filter` and also supports `-t` indexes. The optional `--exact` mode matches only records exactly equal to the query, including case and all bytes. An empty exact query matches only empty records. The `--exact` and `--fuzzy` modes cannot be combined. The optional `--fuzzy` mode matches query characters as an ordered subsequence and ranks
results by early, adjacent, and word-boundary matches; ties retain input order by default, and `--tiebreak` can order equal-score matches by length, begin/end position, chunk count, pathname, or input index. In the
interactive menu, matched fuzzy characters are bolded and underlined. In fuzzy
batch mode, use `--fuzzy --filter QUERY` (or `-f QUERY`); `-t` emits original input
indexes in ranked order. `--read0` treats NUL bytes as input record separators, and `--print0` terminates each output record (including indexes) with NUL; newline delimiters remain the default. Fuzzy mode requires valid multibyte query text; invalid multibyte
candidate records are ignored. Without `--fuzzy`, the original substring behavior is
unchanged. Unknown options, duplicate filter options, and unexpected positional arguments exit with
status 2. Embedded NUL bytes and stdin/stdout I/O errors exit with status 1.

Example:

```bash
ls -1 --color=never | mmenu "select file: " | xargs less -R
find . -type f | mmenu --filter=.c
find . -type f | mmenu -f .c -t
find . -type f | mmenu --preview 'file {}' --query .c
```

## For C programs
The `mmenu` function takes 3 inputs, the options (similar to argv), the length of the options (similar to argc),
and the prompt to be asked (a string). The return value is the index of the element chosen, or -1 in the case of none. For multi-selection, `mmenuMulti` returns status 0 and fills an allocated array of selected zero-based indexes; the caller must `free` that array. It supports the same matching flags and returns indexes in original input order. If no options were toggled, Enter selects the current option. `mmenuConfigured` and `mmenuMultiConfigured` additionally accept an `mmenu_config` structure for an initial query, printable key aliases, an optional generic preview callback, stream callback, searchable-text callback, and display-text callback, plus an optional fuzzy tie-break key order through `mmenu_config.tiebreak`. The searchable-text callback supplies the string used for matching; the display-text callback supplies the rendered option. Both can differ from the original option, which remains the selected/returned record. The stream callback can return a record, temporary no-data, EOF, or an error; record storage must remain valid until the menu call returns. The menu invokes it without blocking for more input and filters new records as they arrive. The preview callback receives the selected record, original index, and current wide-character query and fills a caller-provided output buffer. Shell execution remains a CLI callback implementation, not matcher logic; zero-valued key fields preserve built-in keys. Initial queries are limited to 128 wide characters, and conflicting custom aliases are rejected. Invalid API arguments (a negative option count, a NULL options array with a positive count, or a NULL entry) are rejected with a diagnostic and return -1 before terminal initialization. If no controlling terminal is available, the API reports the error and returns -1 rather than terminating the host process. A NULL prompt uses the default prompt. An empty list is valid and returns -1 when cancelled or confirmed with no selection.
For opt-in fuzzy matching, call `mmenuEx(options, count, prompt, MMENU_FUZZY)`; for byte-exact matching, use `mmenuEx(options, count, prompt, MMENU_EXACT)`. Add `MMENU_CASE_SENSITIVE` for ASCII-case-sensitive matching, `MMENU_SMART_CASE` for query-sensitive case behavior (mutually exclusive), or `MMENU_NO_SORT` with `MMENU_FUZZY` to preserve input order instead of ranking, or `MMENU_DISABLED` to keep all options visible. The case-policy flags are mutually exclusive. `MMENU_NO_SORT` requires `MMENU_FUZZY`; the existing `mmenu` function keeps substring semantics. The -lncursesw flag is required at compilation. An example may look like:

```c
#include "mmenu.h"

int main() {
	char* options[5] = {"Zero", "One", "Two", "Three", "Four"};
	int chosen = mmenu(options, 5, "Please choose an option: ");
	if(chosen == -1) exit(1);
	printf("You chose: %s", options[chosen]);
}
```
## Build

Requires a C compiler and the ncursesw development headers.

```sh
cc -std=c2x -Wall -Wextra -Wpedantic -o mmenu main.c -lncursesw
./mmenu --help
```

The build creates a local `mmenu` executable; generated binaries and object files are
not committed. The documented `nobuild.c` driver is not present in this repository, and
the standalone menu does not require `nobuild.h` to compile.

## Performance and ranking baselines

mmenu is now optimized for large piped inputs (hundreds of thousands to millions of lines):

- Chunked arena loader in the CLI (one or two large allocations instead of millions of tiny `malloc`s per line).
- Byte-oriented `strcasestr` matching (case-insensitive) on the original UTF-8 strings — no more per-candidate `mbstowcs` + `wcsstr` + malloc/free in the hot path.
- Incremental refinement: typing more characters only scans the shrinking set of previous matches (O(M) instead of O(N) per keystroke).
- Batch latency and peak RSS are measured by CI. Interactive first-paint latency is recorded separately as an informational metric, not a timing gate, because pseudo-terminal startup and shared-runner timing are noisy.

Options (in addition to the legacy positional prompt and `t`/`true` index argument):

- `--filter QUERY`, `--filter=QUERY`, or `-f QUERY`: non-interactive batch mode. Outputs matching records (or indices with `-t`), without initializing ncurses.
- `--read0`: read NUL-delimited input records instead of newline-delimited records.
- `--print0`: terminate each output record with NUL instead of newline.
- `--exact`: match whole records byte-for-byte, case-sensitively, in interactive or batch mode. It cannot be combined with `--extended`.
- `--ignore-case`/`-i`: ASCII-case-insensitive matching (the default); `--no-ignore-case`/`+i`: ASCII-case-sensitive matching; `--smart-case`: case-insensitive unless the complete query contains an ASCII uppercase letter. Non-ASCII bytes are always matched exactly.
- `--invert-match` or `-v`: invert batch filter results; requires `--filter` and works with substring, exact, and fuzzy modes.
- `--fuzzy`: match subsequences and rank by score; mutually exclusive with `--exact`.
- `--extended` or `-x`: parse AND terms separated by whitespace, OR groups separated by `|`, inverse terms prefixed by `!`, exact contiguous terms prefixed by `'`, prefix terms prefixed by `^`, and suffix terms ending in `$`. `--no-extended` or `+x` keeps legacy literal-query behavior. Extended mode can be combined with `--fuzzy`, but not whole-record `--exact`.
- `--no-sort`: with `--fuzzy`, keep matching records in original input order instead of ranking by score in both interactive and batch modes; requires `--fuzzy`.
- `--tiebreak=KEYS`: order equal-score fuzzy matches using a comma-separated list of `length`, `begin`, `end`, `index`, `chunk`, and `pathname`. Input index is appended as a stable fallback when omitted; requires `--fuzzy`.
- `--disabled`: interactive-only; keep all options visible regardless of the current query.
- `--cycle`: interactive-only; Up/Down wraps between the first and last filtered results instead of stopping at either end.
- `--nth SPEC`: search only the selected 1-based fields or ranges (for example `1,3`, `2..4`, or `1,-1`). Matching uses the selected fields joined by spaces, while display and output preserve the original record.
- `--with-nth SPEC`: display only the selected fields in interactive mode; selection and output still refer to the original record. Batch-filter mode rejects it because there is no interactive display.
- `--accept-nth SPEC`: output only the selected fields from each accepted record. This is independent of `--nth`; field projections share the same `--delimiter`/`-d` regular expression. It cannot be combined with `--index` or `--format.
- `--delimiter REGEX`/`-d REGEX`: split fields using a POSIX extended regular expression; requires `--nth`, `--with-nth`, or `--accept-nth`. Delimiter expressions must be valid and must not match an empty string. Fields selected by `--nth` are joined with spaces for matching; `--with-nth` and `--accept-nth` opt into display/output projections.
- `--format FORMAT`: in batch mode, expand `{}` to the matched record, `{index}` to its original zero-based input index, and `{query}` to the query. It can be combined with `--print0`, but not `-t`; requires `--filter`.
- `--multi`: interactive multi-selection; Tab toggles the highlighted option and Enter emits all selected records (or indexes with `-t`). It cannot be combined with `--filter`.
- `--query QUERY`: prefill the interactive query; cannot be combined with `--filter`.
- `--preview COMMAND`: evaluate a shell command for the highlighted record and show its output in a side pane. `{}`, `{index}`, and `{query}` are shell-quoted placeholders for the record, original input index, and current query. The command runs synchronously on initial display and whenever selection/query changes; output is clipped. Interactive-only; cannot be combined with `--filter`.
- `--bind-up KEY`, `--bind-down KEY`, `--bind-accept KEY`, `--bind-cancel KEY`, `--bind-toggle KEY`, `--bind-backspace KEY`: add one printable ASCII key alias for the action. `space` denotes the Space key; built-in keys remain active, duplicate aliases are rejected, and `--bind-toggle` requires `--multi`.
- `-t`: output zero-based matching indexes in batch mode, or the selected index in interactive mode.
- `-h`/`--help`: print usage.
  ```bash
  cat huge-list.txt | mmenu --filter "foo" | head
  mmenu -f "bar" -t < million-lines.txt
  ```

Example large-list usage:
```bash
find / -type f 2>/dev/null | mmenu "open: " | xargs -d'\n' -n1 less
# or the fast filter form:
locate -r '\.c$' | mmenu --filter "mmenu" 
```

The fuzzy matcher also has a bounded Clang libFuzzer target in `tests/fuzz/matcher.c`,
seeded with subsequence, repeated-character, empty-query, and Unicode cases. CI runs it under
ASan/UBSan and checks ranking invariants in addition to the deterministic regression suite.

Performance is measured by the repeatable harnesses in `tests/benchmark.sh` and `tests/performance-regression.sh`. Ranking quality is measured separately by MRR, NDCG@5, and Recall@5 against a deterministic judged corpus, with `fzf`, `skim`, and `fzy` recorded as fuzzy-matching references. These tools use different matching semantics, so the reports explicitly separate throughput from relevance quality. See [the benchmark methodology](docs/BENCHMARKS.md) for the contract and interpretation.

The original substring behavior remains the default for backward compatibility. Fuzzy matching is explicitly opt-in through `--fuzzy` or the `mmenuEx(..., MMENU_FUZZY)` C API.

## Maintainer documentation

- [Quality gates and test policy](docs/QUALITY.md)
- [Release checklist](docs/RELEASE.md)
- [Security policy](SECURITY.md)
- [Changelog](CHANGELOG.md)

## Notes
- Requires ncursesw (`-lncursesw` when linking the C API).
- For best results with truly enormous inputs, ensure you have enough RAM (the tool buffers everything, as it must present a live menu).
