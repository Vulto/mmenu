#!/bin/sh
# Pseudo-terminal tests exercise the real ncurses C API without a human.
set -eu

Bin=${1:-./build/api-test}
AllocBin=${2:-./build/api-test-allocfail}
TmpDir=$(mktemp -d)
trap 'rm -rf "$TmpDir"' EXIT HUP INT TERM
Failures=0

CheckSelection() {
    Name=$1
    Expected=$2
    Keys=$3
    if ! { sleep 0.2; printf '%b' "$Keys"; } | LC_ALL="${7:-C.UTF-8}" TERM=xterm timeout 5s script -q -e -c "$Bin ${4:-}" /dev/null > "$TmpDir/output" 2> "$TmpDir/error"; then
        printf 'FAIL: %s (process/timeout failure)\n' "$Name" >&2
        cat "$TmpDir/error" >&2
        Failures=$((Failures + 1))
        return
    fi

    # Strip terminal control sequences before asserting visible clipping/text.
    sed -E 's/\x1B\[[0-?]*[ -/]*[@-~]//g; s/\x1B[()][A-Za-z0-9]//g; s/\x1B[=>]//g' "$TmpDir/output" > "$TmpDir/plain-output"
    if ! grep -aFq "MMENU_RESULT=$Expected" "$TmpDir/plain-output"; then
        printf 'FAIL: %s (expected selection %s)\n' "$Name" "$Expected" >&2
        tail -c 2000 "$TmpDir/output" | od -An -tx1c >&2
        Failures=$((Failures + 1))
    elif [ -n "${5:-}" ] && ! grep -aFq "$5" "$TmpDir/plain-output"; then
        printf 'FAIL: %s (expected terminal text %s)\n' "$Name" "$5" >&2
        tail -c 2000 "$TmpDir/output" | od -An -tx1c >&2
        Failures=$((Failures + 1))
    elif [ -n "${6:-}" ] && grep -aFq "$6" "$TmpDir/plain-output"; then
        printf 'FAIL: %s (unexpected terminal text %s)\n' "$Name" "$6" >&2
        tail -c 2000 "$TmpDir/output" | od -An -tx1c >&2
        Failures=$((Failures + 1))
    else
        printf 'PASS: %s\n' "$Name"
    fi
}

CheckSelection 'Enter selects the first option' 0 '\n'
CheckSelection 'initial query is prefilled and filters immediately' 2 '\n' configured-query 'needle'
CheckSelection 'custom navigation and accept keys work as aliases' 1 'jx' configured-keys
CheckSelection 'exact matching remains correct while typing a multi-character query' 0 'alpha\n' exact-typing
CheckMultiSelection() {
    Name=$1
    Expected=$2
    Keys=$3
    if ! { sleep 0.2; printf '%b' "$Keys"; } |
         LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "$Bin multi" /dev/null > "$TmpDir/multi-output" 2> "$TmpDir/multi-error"; then
        printf 'FAIL: %s (process/timeout failure)\n' "$Name" >&2
        cat "$TmpDir/multi-error" >&2
        Failures=$((Failures + 1))
        return
    fi
    sed -E 's/\x1B\[[0-?]*[ -/]*[@-~]//g; s/\x1B[()][A-Za-z0-9]//g; s/\x1B[=>]//g' "$TmpDir/multi-output" > "$TmpDir/multi-plain"
    if ! grep -aFq "MMENU_MULTI_RESULT=$Expected" "$TmpDir/multi-plain"; then
        printf 'FAIL: %s (expected multi selection %s)\n' "$Name" "$Expected" >&2
        tail -c 2000 "$TmpDir/multi-output" | od -An -tx1c >&2
        Failures=$((Failures + 1))
    elif [ "$Expected" = "0,2" ] && ! grep -aFq '[x]' "$TmpDir/multi-plain"; then
        printf 'FAIL: %s (selected options were not marked)\n' "$Name" >&2
        Failures=$((Failures + 1))
    else
        printf 'PASS: %s\n' "$Name"
    fi
}
DownKey=$(TERM=xterm tput kcud1)
CheckMultiSelection 'Tab toggles multiple options and Enter returns indexes in input order' '0,2' "$(printf '\t')${DownKey}${DownKey}$(printf '\t')\n"
CheckMultiSelection 'Enter without toggles selects the current option' '0' '\n'
CheckMultiSelection 'multi-select cancellation returns no selections' '-1' '\003'
CheckSelection 'Down arrow moves selection to the second option' 1 "$(TERM=xterm tput kcud1)\n"
CheckSelection 'typed query filters to the matching option' 2 'needle\n'
CheckSelection 'legacy API keeps contiguous-substring semantics' -1 'ndl\n'
CheckSelection 'fuzzy API matches a subsequence' 2 'ndl\n' fuzzy

CheckFuzzyHighlight() {
    if ! { sleep 0.2; printf 'ndl\n'; } |
         LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "$Bin fuzzy" /dev/null > "$TmpDir/highlight-output" 2> "$TmpDir/highlight-error"; then
        printf 'FAIL: fuzzy highlighting test did not exit cleanly\n' >&2
        cat "$TmpDir/highlight-error" >&2
        Failures=$((Failures + 1))
        return
    fi
    sed -E 's/\x1B\[[0-?]*[ -/]*[@-~]//g; s/\x1B[()][A-Za-z0-9]//g; s/\x1B[=>]//g' "$TmpDir/highlight-output" > "$TmpDir/highlight-plain"
    if ! grep -aFq 'MMENU_RESULT=2' "$TmpDir/highlight-plain"; then
        printf 'FAIL: fuzzy highlighting changed the selected result\n' >&2
        Failures=$((Failures + 1))
    else
        Escape=$(printf '\033')
        if grep -aEq "${Escape}\[[0-9;]*4[0-9;]*m" "$TmpDir/highlight-output"; then
            printf 'PASS: fuzzy matched characters are underlined\n'
        else
            printf 'FAIL: fuzzy matches were not underlined in terminal output\n' >&2
            tail -c 2000 "$TmpDir/highlight-output" | od -An -tx1c >&2
            Failures=$((Failures + 1))
        fi
    fi
}
CheckFuzzyHighlight

CheckFuzzyUnicodeHighlight() {
    if ! { sleep 0.2; printf '日語\n'; } |
         LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "$Bin fuzzy" /dev/null > "$TmpDir/unicode-highlight-output" 2> "$TmpDir/unicode-highlight-error"; then
        printf 'FAIL: fuzzy Unicode highlighting test did not exit cleanly\n' >&2
        cat "$TmpDir/unicode-highlight-error" >&2
        Failures=$((Failures + 1))
        return
    fi
    if ! grep -aFq 'MMENU_RESULT=3' "$TmpDir/unicode-highlight-output"; then
        printf 'FAIL: fuzzy Unicode subsequence selected the wrong option\n' >&2
        Failures=$((Failures + 1))
    else
        Escape=$(printf '\033')
        if grep -aEq "${Escape}\[[0-9;]*4[0-9;]*m" "$TmpDir/unicode-highlight-output"; then
            printf 'PASS: fuzzy Unicode matched characters are underlined\n'
        else
            printf 'FAIL: fuzzy Unicode matches were not underlined\n' >&2
            tail -c 2000 "$TmpDir/unicode-highlight-output" | od -An -tx1c >&2
            Failures=$((Failures + 1))
        fi
    fi
}
CheckFuzzyUnicodeHighlight
CheckSelection 'fuzzy renderer survives narrow terminals with long candidates' 0 'xxx\n' fuzzy-narrow
CheckSelection 'fuzzy renderer survives a one-cell terminal' 0 '\n' fuzzy-tiny

CheckSelection 'incremental edits followed by Backspace preserve the match' 2 'needle\177\n'
CheckSelection 'ASCII case-insensitive matching works in C locale' 2 'NEEDLE\n' ascii '' '' C
CheckSelection 'non-ASCII case matching remains byte-exact' -1 'CAFÉ\n'
CheckSelection 'Backspace clears an unmatched query' 0 'x\177\n'
CheckSelection 'no matching options returns -1 on Enter' -1 'absent\n'
CheckSelection 'empty option list returns -1 on Enter' -1 '\n' empty
CheckSelection 'Ctrl-C cancels selection' -1 '\003'

CheckSelection 'Escape cancels selection' -1 '\033'
CheckSelection 'Ctrl-D cancels selection' -1 '\004'
CheckSelection 'NULL empty options list returns -1' -1 '\n' null-empty
CheckSelection 'UTF-8 query selects matching option' 3 'café-日本語\n'
CheckSelection 'Up at first option remains bounded' 0 "$(TERM=xterm tput kcuu1)\n"
DownKey=$(TERM=xterm tput kcud1)
CheckSelection 'Down beyond last option remains bounded' 3 "${DownKey}${DownKey}${DownKey}${DownKey}\n"


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
CheckApiContract invalid-flags 'unsupported API flags'

# A library call without a controlling terminal must return -1, not exit its host process.
if ! setsid "$Bin" no-tty > "$TmpDir/no-tty-output" 2> "$TmpDir/no-tty-error"; then
    printf 'FAIL: unavailable controlling terminal terminated the API harness\n' >&2
    cat "$TmpDir/no-tty-error" >&2
    Failures=$((Failures + 1))
elif ! grep -Fq 'MMENU_API_RESULT=terminal-unavailable' "$TmpDir/no-tty-output"; then
    printf 'FAIL: unavailable controlling terminal did not return -1 cleanly\n' >&2
    cat "$TmpDir/no-tty-output" >&2
    Failures=$((Failures + 1))
elif ! grep -Fq 'mmenu: opening /dev/tty' "$TmpDir/no-tty-error"; then
    printf 'FAIL: unavailable controlling terminal omitted its diagnostic\n' >&2
    cat "$TmpDir/no-tty-error" >&2
    Failures=$((Failures + 1))
else
    printf 'PASS: unavailable controlling terminal returns -1 without exiting host process\n'
fi

CheckApiAllocationFailure() {
    At=$1
    if ! env MMENU_FAIL_ALLOC_AT="$At" TERM=xterm timeout 5s script -q -e -c "$AllocBin many" /dev/null > "$TmpDir/alloc-output" 2> "$TmpDir/alloc-error"; then
        printf 'FAIL: API allocation failure %s did not exit cleanly\n' "$At" >&2
        cat "$TmpDir/alloc-error" >&2
        tail -c 2000 "$TmpDir/alloc-output" >&2
        Failures=$((Failures + 1))
    elif ! grep -aFq 'MMENU_RESULT=-1' "$TmpDir/alloc-output" ||
         ! grep -aFq 'mmenu:' "$TmpDir/alloc-output"; then
        printf 'FAIL: API allocation failure %s omitted result or diagnostic\n' "$At" >&2
        cat "$TmpDir/alloc-error" >&2
        tail -c 2000 "$TmpDir/alloc-output" >&2
        Failures=$((Failures + 1))
    else
        printf 'PASS: API allocation failure %s returns safely with diagnostic\n' "$At"
    fi
}

CheckApiAllocationFailure 1
CheckApiAllocationFailure 2
CheckApiAllocationFailure 3

CheckApiFilterAllocationFailure() {
    At=$1
    Keys=$2
    Expected=$3
    Diagnostic=$4
    if ! { sleep 0.2; printf '%b' "$Keys" 2>/dev/null || :; } |
         env MMENU_FAIL_ALLOC_AT="$At" TERM=xterm timeout 5s script -q -e -c "$AllocBin" /dev/null > "$TmpDir/filter-alloc-output" 2> "$TmpDir/filter-alloc-error"; then
        printf 'FAIL: API filter allocation failure %s did not exit cleanly\n' "$At" >&2
        cat "$TmpDir/filter-alloc-error" >&2
        tail -c 2000 "$TmpDir/filter-alloc-output" >&2
        Failures=$((Failures + 1))
    elif ! grep -aFq "MMENU_RESULT=$Expected" "$TmpDir/filter-alloc-output"; then
        printf 'FAIL: API filter allocation failure %s expected result %s\n' "$At" "$Expected" >&2
        tail -c 2000 "$TmpDir/filter-alloc-output" >&2
        Failures=$((Failures + 1))
    elif [ -n "$Diagnostic" ] && ! grep -aFq "$Diagnostic" "$TmpDir/filter-alloc-output"; then
        printf 'FAIL: API filter allocation failure %s omitted diagnostic %s\n' "$At" "$Diagnostic" >&2
        tail -c 2000 "$TmpDir/filter-alloc-output" >&2
        Failures=$((Failures + 1))
    else
        printf 'PASS: API filter allocation failure %s handled with result %s\n' "$At" "$Expected"
    fi
}

CheckApiFilterAllocationFailure 6 'n\n' -1 'converting filter'
CheckApiFilterAllocationFailure 7 'needle\n' 2 ''
CheckApiFilterAllocationFailure 8 'ne\n' -1 'rendering option'
CheckApiFilterAllocationFailure 9 'n\177\n' -1 'converting filter'
CheckSelection 'NULL prompt uses the default prompt' 0 '\n' null-prompt
CheckSelection 'SIGWINCH resize redraws after query edits' 0 'x\177\n' resize 'alpha'
CheckSelection 'narrow terminal clips long options and preserves scrolling' 1 "$(TERM=xterm tput kcud1)\n" narrow 'xxxxxxxx' 'xxxxxxxxx'
CheckSelection 'one-cell terminal does not crash on selection' 0 '\n' tiny
CheckSelection 'wide terminal preserves selection' 1 "$(TERM=xterm tput kcud1)\n" wide
CheckSelection 'single-row terminal preserves selection' 1 "$(TERM=xterm tput kcud1)\n" one-row
CheckSelection 'single-column terminal preserves selection' 1 "$(TERM=xterm tput kcud1)\n" one-column
CheckSelection 'small-grid terminal preserves selection' 1 "$(TERM=xterm tput kcud1)\n" small-grid

CheckRepeatedCalls() {
    Name=$1
    FirstKeys=$2
    SecondKeys=$3
    First=$4
    Second=$5
    Mode=$6
    if [ "$Mode" = allocfail ]; then
        if ! { sleep 0.4; printf '%b' "$SecondKeys"; } |
             MMENU_FAIL_ALLOC_AT=1 TERM=xterm timeout 5s script -q -e -c "$AllocBin repeat" /dev/null > "$TmpDir/repeat-output" 2> "$TmpDir/repeat-error"; then
            printf 'FAIL: %s (process/timeout failure)\n' "$Name" >&2
            cat "$TmpDir/repeat-error" >&2
            Failures=$((Failures + 1))
            return
        fi
    else
        if ! { sleep 0.2; printf '%b' "$FirstKeys"; sleep 0.2; printf '%b' "$SecondKeys"; } |
             TERM=xterm timeout 5s script -q -e -c "$Bin repeat" /dev/null > "$TmpDir/repeat-output" 2> "$TmpDir/repeat-error"; then
            printf 'FAIL: %s (process/timeout failure)\n' "$Name" >&2
            cat "$TmpDir/repeat-error" >&2
            Failures=$((Failures + 1))
            return
        fi
    fi
    sed -E 's/\x1B\[[0-?]*[ -/]*[@-~]//g; s/\x1B[()][A-Za-z0-9]//g; s/\x1B[=>]//g' "$TmpDir/repeat-output" > "$TmpDir/repeat-plain"
    if ! grep -aFq "MMENU_FIRST=$First" "$TmpDir/repeat-plain" ||
       ! grep -aFq "MMENU_SECOND=$Second" "$TmpDir/repeat-plain"; then
        printf 'FAIL: %s (expected first=%s second=%s)\n' "$Name" "$First" "$Second" >&2
        tail -c 2000 "$TmpDir/repeat-output" | od -An -tx1c >&2
        Failures=$((Failures + 1))
    else
        printf 'PASS: %s\n' "$Name"
    fi
}

CheckRepeatedCalls 'repeated API calls retain selection and clean up terminal state' '\n' "$(TERM=xterm tput kcud1)\n" 0 1 normal
CheckRepeatedCalls 'API cancellation cleans up before the next call' '\003' "$(TERM=xterm tput kcud1)\n" -1 1 normal
CheckRepeatedCalls 'API allocation failure cleans up before the next call' '' '\n' -1 0 allocfail

CheckInvalidLocaleText() {
    Locale=$1
    Mode=$2
    Diagnostic=$3
    if LC_ALL="$Locale" "$Bin" "$Mode" > "$TmpDir/invalid-text-output" 2> "$TmpDir/invalid-text-error"; then
        if grep -aFq 'MMENU_RESULT=-1' "$TmpDir/invalid-text-output" &&
           grep -qi "$Diagnostic" "$TmpDir/invalid-text-error"; then
            printf 'PASS: %s rejected under locale %s\n' "$Mode" "$Locale"
        else
            printf 'FAIL: %s omitted result/diagnostic under locale %s\n' "$Mode" "$Locale" >&2
            cat "$TmpDir/invalid-text-error" >&2
            Failures=$((Failures + 1))
        fi
    else
        Status=$?
        printf 'FAIL: %s exited process under locale %s (status %s)\n' "$Mode" "$Locale" "$Status" >&2
        cat "$TmpDir/invalid-text-error" >&2
        Failures=$((Failures + 1))
    fi
}
CheckInvalidLocaleText C invalid-utf8 'option 0 is not valid in the current locale'
CheckInvalidLocaleText C.UTF-8 invalid-utf8 'option 0 is not valid in the current locale'
CheckInvalidLocaleText C invalid-prompt 'prompt is not valid in the current locale'
CheckInvalidLocaleText C.UTF-8 invalid-prompt 'prompt is not valid in the current locale'

if [ "$Failures" -ne 0 ]; then
    printf '%s interactive regression check(s) failed.\n' "$Failures" >&2
    exit 1
fi
printf 'All interactive regression checks passed.\n'
