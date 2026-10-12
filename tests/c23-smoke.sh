#!/bin/sh
set -eu

CC=${CC:-cc}
mkdir -p build
CompilerVersion=$("$CC" --version | sed -n '1p')
Flags='-O2 -g -std=c2x -Wall -Wextra -Wpedantic -Werror -D_DEFAULT_SOURCE'

"$CC" -O2 -g -std=c2x -Wall -Wextra -Wpedantic -Werror -D_DEFAULT_SOURCE -o build/mmenu-c23 main.c -lncursesw
"$CC" -O2 -g -std=c2x -Wall -Wextra -Wpedantic -Werror -D_DEFAULT_SOURCE -o build/api-test-c23 tests/apiHarness.c -lncursesw

{
    printf 'compiler=%s\n' "$CompilerVersion"
    printf 'standard=-std=c2x\n'
    printf 'flags=%s\n' "$Flags"
    printf 'binary_size_c11:\n'
    size build/mmenu
    printf 'binary_size_c2x:\n'
    size build/mmenu-c23
} > build/c23-smoke.txt
