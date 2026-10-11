#!/bin/sh
# Exhaustively fail every wrapped allocation reached by representative CLI/API scenarios.
set -eu
CliBin=${1:-./build/mmenu-allocfail}
ApiBin=${2:-./build/api-test-allocfail}
TmpDir=$(mktemp -d)
trap 'rm -rf "$TmpDir"' EXIT HUP INT TERM

StripTerminalCodes() {
    sed -E 's/\x1B\[[0-?]*[ -/]*[@-~]//g; s/\x1B[()][A-Za-z0-9]//g; s/\x1B[=>]//g' "$1" > "$2"
}

GetAllocationCount() {
    sed -n 's/.*MMENU_ALLOC_CALLS=\([0-9][0-9]*\).*/\1/p' "$1" | tail -n 1
}

Fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

ProbeCli() {
    Name=$1
    Input=$2
    Query=${3:-needle}
    Mode=${4:-substring}
    if [ "$Mode" = fuzzy ]; then
        if ! MMENU_FAIL_ALLOC_AT=999999 MMENU_REPORT_ALLOC_COUNT=1 "$CliBin" --fuzzy --filter "$Query" < "$Input" > /dev/null 2> "$TmpDir/cli-probe.err"; then
            Fail "CLI probe failed for $Name"
        fi
    elif ! MMENU_FAIL_ALLOC_AT=999999 MMENU_REPORT_ALLOC_COUNT=1 "$CliBin" --filter "$Query" < "$Input" > /dev/null 2> "$TmpDir/cli-probe.err"; then
        Fail "CLI probe failed for $Name"
    fi
    Count=$(GetAllocationCount "$TmpDir/cli-probe.err")
    [ -n "$Count" ] || Fail "CLI allocation count missing for $Name"
    printf '%s\n' "$Count"
}

SweepCli() {
    Name=$1
    Input=$2
    Query=${3:-needle}
    Mode=${4:-substring}
    Total=$(ProbeCli "$Name" "$Input" "$Query" "$Mode")
    [ "$Total" -gt 0 ] || Fail "CLI scenario $Name did not allocate"
    At=1
    while [ "$At" -le "$Total" ]; do
        if [ "$Mode" = fuzzy ]; then
            if MMENU_FAIL_ALLOC_AT="$At" MMENU_REPORT_ALLOC_COUNT=1 "$CliBin" --fuzzy --filter "$Query" < "$Input" > /dev/null 2> "$TmpDir/cli-error"; then
                Status=0
            else
                Status=$?
            fi
        elif MMENU_FAIL_ALLOC_AT="$At" MMENU_REPORT_ALLOC_COUNT=1 "$CliBin" --filter "$Query" < "$Input" > /dev/null 2> "$TmpDir/cli-error"; then
            Status=0
        else
            Status=$?
        fi
        Count=$(GetAllocationCount "$TmpDir/cli-error")
        [ -n "$Count" ] || Fail "CLI allocation count missing for $Name at $At"
        [ "$Count" -ge "$At" ] || Fail "CLI allocation $At was not reached in $Name"
        [ "$Status" -ne 0 ] || Fail "CLI ignored allocation failure $At in $Name"
        if ! grep -Ev 'MMENU_ALLOC_CALLS=[0-9]+' "$TmpDir/cli-error" | grep -q '[^[:space:]]'; then
            Fail "CLI allocation failure $At in $Name produced no diagnostic"
        fi
        At=$((At + 1))
    done
    printf 'PASS: CLI %s swept all %s allocation attempts\n' "$Name" "$Total"
}

ProbeApi() {
    Name=$1
    Mode=$2
    Keys=$3
    if ! { sleep 0.2; printf '%b' "$Keys" 2>/dev/null || :; } |
         MMENU_FAIL_ALLOC_AT=999999 MMENU_REPORT_ALLOC_COUNT=1 MMENU_SWEEP_API=1 LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "$ApiBin $Mode" /dev/null > "$TmpDir/api-probe.raw" 2> "$TmpDir/api-probe.err"; then
        Fail "API probe failed for $Name"
    fi
    StripTerminalCodes "$TmpDir/api-probe.raw" "$TmpDir/api-probe.txt"
    Count=$(GetAllocationCount "$TmpDir/api-probe.txt")
    [ -n "$Count" ] || Fail "API allocation count missing for $Name"
    printf '%s\n' "$Count"
}

ProbePreview() {
    Input=$1
    if ! { sleep 0.2; printf '\n'; } |
         MMENU_FAIL_ALLOC_AT=999999 MMENU_REPORT_ALLOC_COUNT=1 LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "$CliBin --preview 'printf preview:{}' < '$Input' > '$TmpDir/preview-selected'" /dev/null > "$TmpDir/preview-probe.raw" 2> "$TmpDir/preview-probe.err"; then
        Fail "preview allocation probe failed"
    fi
    StripTerminalCodes "$TmpDir/preview-probe.raw" "$TmpDir/preview-probe.txt"
    Count=$(GetAllocationCount "$TmpDir/preview-probe.txt")
    [ -n "$Count" ] || Fail "preview allocation count missing"
    printf '%s\n' "$Count"
}

SweepPreview() {
    Input=$1
    Total=$(ProbePreview "$Input")
    [ "$Total" -gt 0 ] || Fail "preview scenario did not allocate"
    At=1
    while [ "$At" -le "$Total" ]; do
        if { sleep 0.2; printf '\n'; } |
             MMENU_FAIL_ALLOC_AT="$At" MMENU_REPORT_ALLOC_COUNT=1 LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "$CliBin --preview 'printf preview:{}' < '$Input' > '$TmpDir/preview-selected'" /dev/null > "$TmpDir/preview-raw" 2> "$TmpDir/preview-error"; then
            Status=0
        else
            Status=$?
        fi
        StripTerminalCodes "$TmpDir/preview-raw" "$TmpDir/preview.txt"
        Count=$(GetAllocationCount "$TmpDir/preview.txt")
        [ -n "$Count" ] || Fail "preview allocation count missing at $At"
        [ "$Count" -ge "$At" ] || Fail "preview allocation $At was not reached"
        [ "$Status" -lt 128 ] || Fail "preview allocation failure $At was terminated by a signal or timeout"
        if grep -aEq 'AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:|DEADLYSIGNAL|Segmentation fault' "$TmpDir/preview.txt" "$TmpDir/preview-error"; then
            Fail "preview allocation failure $At triggered a sanitizer/runtime error"
        fi
        At=$((At + 1))
    done
    printf 'PASS: preview mode survived all %s allocation-failure points\n' "$Total"
}

SweepApi() {
    Name=$1
    Mode=$2
    Keys=$3
    Total=$(ProbeApi "$Name" "$Mode" "$Keys")
    [ "$Total" -gt 0 ] || Fail "API scenario $Name did not allocate"
    At=1
    while [ "$At" -le "$Total" ]; do
        if ! { sleep 0.2; printf '%b' "$Keys" 2>/dev/null || :; } |
             MMENU_FAIL_ALLOC_AT="$At" MMENU_REPORT_ALLOC_COUNT=1 MMENU_SWEEP_API=1 LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "$ApiBin $Mode" /dev/null > "$TmpDir/api-raw" 2> "$TmpDir/api-error"; then
            Fail "API allocation failure $At in $Name terminated the harness"
        fi
        StripTerminalCodes "$TmpDir/api-raw" "$TmpDir/api.txt"
        Count=$(GetAllocationCount "$TmpDir/api.txt")
        [ -n "$Count" ] || Fail "API allocation count missing for $Name at $At"
        [ "$Count" -ge "$At" ] || Fail "API allocation $At was not reached in $Name"
        if ! grep -aEq 'MMENU_(MULTI_)?RESULT=' "$TmpDir/api.txt"; then
            Fail "API allocation failure $At in $Name omitted a result"
        fi
        if grep -aEq 'MMENU_(MULTI_)?RESULT=-1' "$TmpDir/api.txt" &&
           ! grep -qi 'mmenu:' "$TmpDir/api.txt" &&
           ! grep -qi 'mmenu:' "$TmpDir/api-error"; then
            Fail "API allocation failure $At in $Name returned -1 without a diagnostic"
        fi
        At=$((At + 1))
    done
    printf 'PASS: API %s swept all %s allocation attempts\n' "$Name" "$Total"
}

printf 'needle\nsecond\n' > "$TmpDir/small.input"
awk 'BEGIN { for (i = 0; i < 1048577; i++) printf "x"; printf " needle\nshort needle\n"; }' > "$TmpDir/large-record.input"
awk 'BEGIN { for (i = 0; i < 230000; i++) printf "src/module-%06d-needle-padding-padding.c\n", i; }' > "$TmpDir/arena-growth.input"

SweepCli small "$TmpDir/small.input"
SweepCli fuzzy "$TmpDir/small.input" ndl fuzzy
SweepCli large-record "$TmpDir/large-record.input"
SweepCli arena-and-list-growth "$TmpDir/arena-growth.input"
SweepPreview "$TmpDir/small.input"

SweepApi many many '\n'
SweepApi multi multi '\t\n'
SweepApi query-edit '' 'needle\n'
SweepApi incremental-fallback '' 'n\177\n'
SweepApi incremental-realloc many-edit 'a\177\n'
SweepApi unicode-query '' 'café\n'
SweepApi fuzzy fuzzy 'ndl\n'
SweepApi fuzzy-result-growth fuzzy-many 'a\n'

printf 'All exhaustive allocation-failure sweeps passed.\n'
