#!/bin/sh
set -eu

Root=$(git -C "$(dirname -- "$0")/.." rev-parse --show-toplevel)
CC=${CC:-cc}
TmpDir=$(mktemp -d)
trap 'rm -rf "$TmpDir"' EXIT HUP INT TERM
Bin="$TmpDir/mmenu-clean"

Fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

Pass() {
    printf 'PASS: %s\n' "$*"
}

git -C "$Root" diff --check HEAD^ HEAD

"$CC" -O2 -g -std=c2x -Wall -Wextra -Wpedantic -Werror \
    -D_DEFAULT_SOURCE -o "$Bin" "$Root/main.c" -lncursesw
Pass 'clean optimized CLI build'

# Compile a standalone consumer using only the public single header.
cat > "$TmpDir/public-api.c" <<'EOF'
#define MMENU_IMPLEMENTATION
#include "mmenu.h"

static int preview_callback(const char *option, int index,
                            const wchar_t *query, char *output,
                            size_t output_size, void *userdata) {
    (void)option;
    (void)index;
    (void)query;
    (void)userdata;
    if (output_size) output[0] = '\0';
    return 0;
}

int main(void) {
    const char *options[] = {"alpha"};
    mmenu_config config = {
        .initial_query = "alpha",
        .preview = preview_callback
    };
    (void)mmenuConfigured(options, 1, NULL, 0, &config);
    int *indices = NULL;
    int count = 0;
    (void)mmenuMultiConfigured(options, 1, NULL, 0, &config, &indices, &count);
    free(indices);
    return 0;
}
EOF
"$CC" -O2 -g -std=c2x -Wall -Wextra -Wpedantic -Werror \
    -D_DEFAULT_SOURCE -I "$Root" -o "$TmpDir/public-api" \
    "$TmpDir/public-api.c" -lncursesw
Pass 'standalone public-header/API consumer build'

"$Bin" --help > "$TmpDir/help"
for Option in --filter --exact --fuzzy --format --read0 --print0 --multi --query --preview; do
    grep -Fq -- "$Option" "$TmpDir/help" || Fail "help omits $Option"
done
Pass 'release help lists the supported CLI surface'

printf 'alpha\nbeta\n' > "$TmpDir/input"
printf 'alpha\n' > "$TmpDir/expected"
"$Bin" --filter alp < "$TmpDir/input" > "$TmpDir/actual"
cmp -s "$TmpDir/expected" "$TmpDir/actual" || Fail 'substring batch smoke test failed'
printf 'beta\n' > "$TmpDir/expected"
"$Bin" --exact --filter beta < "$TmpDir/input" > "$TmpDir/actual"
cmp -s "$TmpDir/expected" "$TmpDir/actual" || Fail 'exact batch smoke test failed'
printf '1:beta:beta\n' > "$TmpDir/expected"
"$Bin" --format '{index}:{query}:{}' --filter beta < "$TmpDir/input" > "$TmpDir/actual"
cmp -s "$TmpDir/expected" "$TmpDir/actual" || Fail 'format batch smoke test failed'
printf '%s\0' alpha beta > "$TmpDir/input0"
printf '%s\0' beta > "$TmpDir/expected0"
"$Bin" --read0 --print0 --filter beta < "$TmpDir/input0" > "$TmpDir/actual0"
cmp -s "$TmpDir/expected0" "$TmpDir/actual0" || Fail 'NUL-delimited batch smoke test failed'
Pass 'substring, exact, format, and NUL-delimited smoke tests'

if "$Bin" --filter < /dev/null > /dev/null 2> "$TmpDir/error"; then
    Fail 'missing --filter argument was accepted'
else
    Status=$?
    [ "$Status" -eq 2 ] || Fail "missing --filter returned status $Status, expected 2"
    grep -Fq 'requires a query argument' "$TmpDir/error" ||
        Fail 'missing --filter argument had no diagnostic'
fi
if "$Bin" --preview 'printf test' --filter test < "$TmpDir/input" > /dev/null 2> "$TmpDir/error"; then
    Fail 'interactive preview was accepted in batch mode'
else
    grep -Fq 'cannot be used with --filter' "$TmpDir/error" ||
        Fail 'preview/batch conflict had no diagnostic'
fi
Pass 'invalid release CLI invocations fail early and clearly'
