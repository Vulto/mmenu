#!/bin/sh
# Pseudo-terminal tests exercise the real ncurses C API without a human.
set -eu

Bin=${1:-./build/api-test}
TmpDir=$(mktemp -d)
trap 'rm -rf "$TmpDir"' EXIT HUP INT TERM
Failures=0

CheckSelection() {
    Name=$1
    Expected=$2
    Keys=$3
    if ! { sleep 0.2; printf '%b' "$Keys"; } | TERM=xterm timeout 5s script -q -e -c "$Bin ${4:-}" /dev/null > "$TmpDir/output" 2> "$TmpDir/error"; then
        printf 'FAIL: %s (process/timeout failure)\n' "$Name" >&2
        cat "$TmpDir/error" >&2
        Failures=$((Failures + 1))
    elif ! grep -aFq "MMENU_RESULT=$Expected" "$TmpDir/output"; then
        printf 'FAIL: %s (expected selection %s)\n' "$Name" "$Expected" >&2
        tail -c 2000 "$TmpDir/output" | od -An -tx1c >&2
        Failures=$((Failures + 1))
    else
        printf 'PASS: %s\n' "$Name"
    fi
}

CheckSelection 'Enter selects the first option' 0 '\n'
CheckSelection 'Down arrow moves selection to the second option' 1 "$(TERM=xterm tput kcud1)\n"
CheckSelection 'typed query filters to the matching option' 2 'needle\n'
CheckSelection 'no matching options returns -1 on Enter' -1 'absent\n'
CheckSelection 'empty option list returns -1 on Enter' -1 '\n' empty
CheckSelection 'Ctrl-C cancels selection' -1 '\003'


CheckApiContract() {
    Mode=$1
    Diagnostic=$2
    if ! "$Bin" "$Mode" > "$TmpDir/api-output" 2> "$TmpDir/api-error"; then
        printf 'FAIL: API contract %s returned nonzero\n' "$Mode" >&2
        cat "$TmpDir/api-error" >&2
        Failures=$((Failures + 1))
    elif ! grep -Fq 'MMENU_API_RESULT=invalid-rejected' "$TmpDir/api-output"; then
        printf 'FAIL: API contract %s was not rejected cleanly\n' "$Mode" >&2
        Failures=$((Failures + 1))
    elif ! grep -Fq "$Diagnostic" "$TmpDir/api-error"; then
        printf 'FAIL: API contract %s omitted diagnostic: %s\n' "$Mode" "$Diagnostic" >&2
        cat "$TmpDir/api-error" >&2
        Failures=$((Failures + 1))
    else
        printf 'PASS: API rejects %s before terminal initialization\n' "$Mode"
    fi
}

CheckApiContract invalid-null-options 'options must not be NULL'
CheckApiContract invalid-negative-count 'option count must not be negative'
CheckApiContract invalid-null-entry 'option 1 must not be NULL'
CheckSelection 'NULL prompt uses the default prompt' 0 '\n' null-prompt
CheckSelection 'SIGWINCH resize does not corrupt selection' 0 '\n' resize

if [ "$Failures" -ne 0 ]; then
    printf '%s interactive regression check(s) failed.\n' "$Failures" >&2
    exit 1
fi
printf 'All interactive regression checks passed.\n'
