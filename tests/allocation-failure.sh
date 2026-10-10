#!/bin/sh
# Deterministically fail each allocation in the CLI's initial arena/list setup.
set -eu

Bin=${1:-./build/mmenu-allocfail}
TmpDir=$(mktemp -d)
trap 'rm -rf "$TmpDir"' EXIT HUP INT TERM
printf 'needle\\nsecond\\n' > "$TmpDir/input"

for Allocation in 1 2 3; do
    Status=0
    if MMENU_FAIL_ALLOC_AT="$Allocation" "$Bin" --filter needle < "$TmpDir/input" > "$TmpDir/output" 2> "$TmpDir/error"; then
        printf 'FAIL: allocation failure %s was ignored\\n' "$Allocation" >&2
        exit 1
    else
        Status=$?
    fi
    if [ "$Status" -eq 139 ] || [ "$Status" -eq 134 ]; then
        printf 'FAIL: allocation failure %s caused a crash (status %s)\\n' "$Allocation" "$Status" >&2
        cat "$TmpDir/error" >&2
        exit 1
    fi
    if [ ! -s "$TmpDir/error" ]; then
        printf 'FAIL: allocation failure %s produced no diagnostic\\n' "$Allocation" >&2
        exit 1
    fi
    printf 'PASS: allocation failure %s exits safely with diagnostics (status %s)\\n' "$Allocation" "$Status"
done
printf 'All allocation-failure checks passed.\\n'
