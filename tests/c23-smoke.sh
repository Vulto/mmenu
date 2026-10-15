#!/bin/sh
set -eu

CC=${CC:-cc}
mkdir -p build
CompilerVersion=$("$CC" --version | sed -n '1p')
Flags='-O2 -g -std=c2x -Wall -Wextra -Wpedantic -Werror -D_DEFAULT_SOURCE'

# shellcheck disable=SC2046 # pkg-config emits multiple linker arguments intentionally.
"$CC" -O2 -g -std=c2x -Wall -Wextra -Wpedantic -Werror -D_DEFAULT_SOURCE -o build/mmenu-c23 main.c $(pkg-config --libs ncursesw)
"$CC" -O2 -g -std=c2x -Wall -Wextra -Wpedantic -Werror -D_DEFAULT_SOURCE -o build/api-test-c23 tests/apiHarness.c $(pkg-config --libs ncursesw)

{
    printf 'compiler=%s\n' "$CompilerVersion"
    printf 'standard=-std=c2x\n'
    printf 'flags=%s\n' "$Flags"
    printf 'binary_size_default:\n'
    size build/mmenu
    printf 'binary_size_c2x:\n'
    size build/mmenu-c23
    printf 'binary_file_bytes_default=%s\n' "$(wc -c < build/mmenu | tr -d '[:space:]')"
    printf 'binary_file_bytes_c2x=%s\n' "$(wc -c < build/mmenu-c23 | tr -d '[:space:]')"
} > build/c23-smoke.txt
cat build/c23-smoke.txt
