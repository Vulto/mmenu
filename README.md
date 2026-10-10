# mmenu - minimal menu

Small ncurses menu for c programs and shell scripts, similar to suckless' dmenu. 

## For shell scripts

Interactive mode reads one option per line from stdin:

```text
mmenu [PROMPT] [t|true] [-t]
```

The selected line is printed by default. For compatibility, a second positional argument
of `t` or `true`, or the `-t` flag, prints the selected zero-based index instead.

Batch filtering does not open ncurses:

```text
mmenu --filter QUERY [-t]
mmenu --filter=QUERY [-t]
mmenu -f QUERY [-t]
mmenu --fuzzy [PROMPT] [-t]
mmenu --exact [PROMPT] [-t]
mmenu --fuzzy --filter QUERY [-t]
mmenu --exact --filter QUERY [-t]
mmenu --invert-match --filter QUERY [-t]
```

Default batch mode emits ASCII-case-insensitive contiguous-substring matches in input order. `--exact` matches whole records byte-for-byte (case-sensitive), preserving input order and duplicates. An empty
query matches every input record; an empty input is valid. Batch mode is byte-oriented and
preserves invalid UTF-8 bytes, but rejects embedded NUL records. The interactive C API
requires valid multibyte options and prompt text in the active locale; matching folds ASCII
case only, while non-ASCII bytes must match exactly. `-h`/`--help` prints usage.
`--invert-match` (or `-v`) emits records that do not match the selected batch matcher, in original input order; it requires `--filter` and also supports `-t` indexes. The optional `--exact` mode matches only records exactly equal to the query, including case and all bytes. An empty exact query matches only empty records. The `--exact` and `--fuzzy` modes cannot be combined. The optional `--fuzzy` mode matches query characters as an ordered subsequence and ranks
results by early, adjacent, and word-boundary matches; ties retain input order. In the
interactive menu, matched fuzzy characters are bolded and underlined. In fuzzy
batch mode, use `--fuzzy --filter QUERY` (or `-f QUERY`); `-t` emits original input
indexes in ranked order. Fuzzy mode requires valid multibyte query text; invalid multibyte
candidate records are ignored. Without `--fuzzy`, the original substring behavior is
unchanged. Unknown options, duplicate filter options, and unexpected positional arguments exit with
status 2. Embedded NUL bytes and stdin/stdout I/O errors exit with status 1.

Example:

```bash
ls -1 --color=never | mmenu "select file: " | xargs less -R
find . -type f | mmenu --filter=.c
find . -type f | mmenu -f .c -t
```

## For C programs
The `mmenu` function takes 3 inputs, the options (similar to argv), the length of the options (similar to argc),
and the prompt to be asked (a string). The return value is the index of the element chosen, or -1 in the case of none. Invalid API arguments (a negative option count, a NULL options array with a positive count, or a NULL entry) are rejected with a diagnostic and return -1 before terminal initialization. A NULL prompt uses the default prompt. An empty list is valid and returns -1 when cancelled or confirmed with no selection.
For opt-in fuzzy matching, call `mmenuEx(options, count, prompt, MMENU_FUZZY)`; for byte-exact matching, use `mmenuEx(options, count, prompt, MMENU_EXACT)`. These flags are mutually exclusive; the existing `mmenu` function keeps substring semantics. The -lncursesw flag is required at compilation. An example may look like:

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
cc -std=c11 -Wall -Wextra -Wpedantic -o mmenu main.c -lncursesw
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
- Batch latency and peak RSS are measured by CI. Interactive first-paint latency is not yet included in the automated timing gate.

Options (in addition to the legacy positional prompt and `t`/`true` index argument):

- `--filter QUERY`, `--filter=QUERY`, or `-f QUERY`: non-interactive batch mode. Outputs matching lines (or indices with `-t`), without initializing ncurses.
- `--exact`: match whole records byte-for-byte, case-sensitively, in interactive or batch mode.
- `--invert-match` or `-v`: invert batch filter results; requires `--filter` and works with substring, exact, and fuzzy modes.
- `--fuzzy`: match subsequences and rank by score; mutually exclusive with `--exact`.
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

## Notes
- Requires ncursesw (`-lncursesw` when linking the C API).
- The `c` build tool (from nobuild.h) or direct `gcc -o mmenu main.c -lncursesw` both work.
- For best results with truly enormous inputs, ensure you have enough RAM (the tool buffers everything, as it must present a live menu).
