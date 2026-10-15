#!/bin/sh
set -eu

Root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TmpDir=$(mktemp -d)
trap 'rm -rf "$TmpDir"' EXIT HUP INT TERM

: "${CC:=cc}"
export CC
"$CC" -std=c2x -Wall -Wextra -Wpedantic -Werror -D_DEFAULT_SOURCE \
    -I"$Root" -o "$TmpDir/nobuild" "$Root/nobuild.c"

mkdir -p "$TmpDir/prefix"
(
    cd "$Root" || exit 1
    PREFIX="$TmpDir/prefix" MMENU_BUILD_OUTPUT="$TmpDir/mmenu" "$TmpDir/nobuild"
)

test -x "$TmpDir/prefix/bin/mmenu"
"$TmpDir/prefix/bin/mmenu" --help > "$TmpDir/help"
grep -Fq -- '--layout MODE' "$TmpDir/help"
cmp "$Root/mmenu.1" "$TmpDir/prefix/share/man/man1/mmenu.1"
groff -man -Tutf8 -Wall -z "$Root/mmenu.1"

printf 'PASS: man page formats and nobuild installs mmenu without privilege escalation\n'
