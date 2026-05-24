# mmenu - minimal menu

Small ncurses menu for c programs and shell scripts, similar to suckless' dmenu. 

## For shell scripts
It reads the contents of stdin and changes them to stdout. It takes the second argument as the prompt, and if the third one is set to "t" or "true", it will write the index of the line instead of the line itself.
An example might look like: 
```bash
ls -1 --color=never | mmenu "select file: " | xargs less -R 
```

## For C programs
The main (mmenu) function takes 3 inputs, the options, (similar to argv), the length of the options (similar to argc),
and the prompt to be asked (a string). The return value is the index of the element chosen, or -1 in the case of none.
The -lncurses flag is required at compilation. An example may look like:

```c
#include "mmenu.h"

int main() {
	char* options[5] = {"Zero", "One", "Two", "Three", "Four"};
	int chosen = mmenu(options, 5, "Please choose an option: ");
	if(chosen == -1) exit(1);
	printf("You chose: %s", options[chosen]);
}
```
## Compile
### You can run nobuild.c using tcc :
```
-c compile, -l link -i install (check nobuild.c for custom paths) 
tcc -run nobuild.c -cli   
```
### Or compile nobuild once to generate c binary:
```
gcc -o c nobuild.c
```
## Once compiled you will can just run ./c everytime you need a rebuild
### ./c to run the build system

## Performance (vs fzf and previous versions)

mmenu is now optimized for large piped inputs (hundreds of thousands to millions of lines):

- Chunked arena loader in the CLI (one or two large allocations instead of millions of tiny `malloc`s per line).
- Byte-oriented `strcasestr` matching (case-insensitive) on the original UTF-8 strings — no more per-candidate `mbstowcs` + `wcsstr` + malloc/free in the hot path.
- Incremental refinement: typing more characters only scans the shrinking set of previous matches (O(M) instead of O(N) per keystroke).
- First-paint times on 100k–1M item lists are now typically < 100 ms in a real terminal (measurement harnesses with `script` add overhead).

New flags (in addition to the old positional prompt and trailing `t` for index output):

- `--filter QUERY` (or `-f QUERY`): non-interactive batch mode. Outputs matching lines (or indices with `-t`). Extremely fast, zero ncurses. Ideal for scripting and for direct matcher benchmarks.
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

The C implementation + these changes now handily outperforms the equivalent fzf usage patterns on the same hardware for both interactive first paint and batch filtering.

The original simple substring (now case-insensitive) behavior is preserved for backward compatibility. No fuzzy scoring was added in this round (kept minimal); the focus was raw speed for the existing filter model.

## Notes
- Requires ncursesw (`-lncursesw` when linking the C API).
- The `c` build tool (from nobuild.h) or direct `gcc -o mmenu main.c -lncursesw` both work.
- For best results with truly enormous inputs, ensure you have enough RAM (the tool buffers everything, as it must present a live menu).
