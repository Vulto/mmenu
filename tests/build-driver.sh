#!/bin/sh
set -eu

Root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
TmpDir=$(mktemp -d)
trap 'rm -rf "$TmpDir"' EXIT HUP INT TERM

: "${CC:=cc}"
CompilerPath=$(command -v "$CC")
if [ -z "$CompilerPath" ]; then
    printf 'FAIL: configured compiler not found: %s\n' "$CC" >&2
    exit 1
fi
CC=$CompilerPath
export CC

mkdir -p "$TmpDir/source" "$TmpDir/bin" "$TmpDir/prefix"
cp "$Root/nobuild.c" "$Root/nobuild.h" "$TmpDir/source/"
"$CC" -std=c2x -Wall -Wextra -Wpedantic -Werror -D_DEFAULT_SOURCE \
    -I"$TmpDir/source" -o "$TmpDir/nobuild" "$TmpDir/source/nobuild.c"

FalsePath=$(command -v false)
ln -s "$FalsePath" "$TmpDir/bin/cc"
sleep 1
touch "$TmpDir/source/nobuild.c"

(
    cd "$Root" || exit 1
    PATH="$TmpDir/bin:$PATH" CC="$CC" PREFIX="$TmpDir/prefix" \
        MMENU_BUILD_OUTPUT="$TmpDir/mmenu" "$TmpDir/nobuild"
)

test -x "$TmpDir/prefix/bin/mmenu"
"$TmpDir/prefix/bin/mmenu" --help > "$TmpDir/help"
grep -Fq -- '--layout MODE' "$TmpDir/help"
cmp "$Root/mmenu.1" "$TmpDir/prefix/share/man/man1/mmenu.1"
groff -man -Tutf8 -Wall -z "$Root/mmenu.1"

printf 'PASS: man page formats and nobuild installs mmenu without privilege escalation\n'
