#!/bin/sh
set -eu

Root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
TmpDir=$(mktemp -d)
trap 'rm -rf "$TmpDir"' EXIT HUP INT TERM

: "${CC:=cc}"
if ! command -v "$CC" >/dev/null 2>&1; then
    printf 'FAIL: configured compiler not found: %s\n' "$CC" >&2
    exit 1
fi

mkdir -p "$TmpDir/bin" "$TmpDir/install-build" "$TmpDir/prefix"
MMENU_BUILD_OUTPUT="$TmpDir/bin/mmenu" sh "$Root/build.sh" build
test -x "$TmpDir/bin/mmenu"
"$TmpDir/bin/mmenu" --help > "$TmpDir/help"
grep -Fq -- '--layout MODE' "$TmpDir/help"
printf 'PASS: shell build creates a working executable\n'

PREFIX="$TmpDir/prefix" MMENU_BUILD_OUTPUT="$TmpDir/install-build/mmenu" \
    sh "$Root/build.sh" install
test -x "$TmpDir/prefix/bin/mmenu"
"$TmpDir/prefix/bin/mmenu" --help > "$TmpDir/installed-help"
grep -Fq -- '--layout MODE' "$TmpDir/installed-help"
cmp "$Root/mmenu.1" "$TmpDir/prefix/share/man/man1/mmenu.1"
groff -man -Tutf8 -Wall -z "$Root/mmenu.1"
printf 'PASS: shell install installs the executable and formatted man page without privilege escalation\n'
