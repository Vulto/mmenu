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
    SecondKey=${8:-}
    if [ -n "$SecondKey" ]; then
        if { sleep 0.5; printf '%b' "$Keys"; sleep 0.1; printf '%b' "$SecondKey"; sleep 0.1; printf '\n'; } | LC_ALL="${7:-C.UTF-8}" TERM=xterm timeout 5s script -q -e -c "$Bin ${4:-}" /dev/null > "$TmpDir/output" 2> "$TmpDir/error"; then
            InputStatus=0
        else
            InputStatus=$?
        fi
    else
        if { sleep 0.2; printf '%b' "$Keys"; } | LC_ALL="${7:-C.UTF-8}" TERM=xterm timeout 5s script -q -e -c "$Bin ${4:-}" /dev/null > "$TmpDir/output" 2> "$TmpDir/error"; then
            InputStatus=0
        else
            InputStatus=$?
        fi
    fi
    if [ "$InputStatus" -ne 0 ]; then
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

CheckStreamApi() {
    Name=$1
    Mode=$2
    Expected=$3
    if ! { sleep 0.5; printf '\n'; } |
         LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "$Bin $Mode" /dev/null > "$TmpDir/stream-output" 2> "$TmpDir/stream-error"; then
        printf 'FAIL: %s (streaming C API did not exit cleanly)\n' "$Name" >&2
        cat "$TmpDir/stream-error" >&2
        Failures=$((Failures + 1))
        return
    fi
    sed -E 's/\x1B\[[0-?]*[ -/]*[@-~]//g; s/\x1B[()][A-Za-z0-9]//g; s/\x1B[=>]//g' "$TmpDir/stream-output" > "$TmpDir/stream-plain"
    if grep -aFq "MMENU_RESULT=$Expected" "$TmpDir/stream-plain"; then
        printf 'PASS: %s\n' "$Name"
    else
        printf 'FAIL: %s (expected selection %s)\n' "$Name" "$Expected" >&2
        tail -c 2000 "$TmpDir/stream-output" | od -An -tx1c >&2
        Failures=$((Failures + 1))
    fi
}

CheckPreviewClipping() {
    if ! { sleep 0.2; printf '\n'; } |
         LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "$Bin preview-long" /dev/null > "$TmpDir/preview-long-output" 2> "$TmpDir/preview-long-error"; then
        printf 'FAIL: long preview clipping test did not exit cleanly\n' >&2
        cat "$TmpDir/preview-long-error" >&2
        Failures=$((Failures + 1))
        return
    fi
    sed -E 's/\x1B\[[0-?]*[ -/]*[@-~]//g; s/\x1B[()][A-Za-z0-9]//g; s/\x1B[=>]//g' "$TmpDir/preview-long-output" > "$TmpDir/preview-long-plain"
    if grep -aEq 'x{20}' "$TmpDir/preview-long-output"; then
        printf 'FAIL: preview text overflowed its pane width\n' >&2
        Failures=$((Failures + 1))
    elif ! grep -aFq 'MMENU_RESULT=0' "$TmpDir/preview-long-plain"; then
        printf 'FAIL: long preview changed selection or did not exit\n' >&2
        Failures=$((Failures + 1))
    else
        printf 'PASS: preview text is clipped to pane display width\n'
    fi
}

CheckSelection 'Enter selects the first option' 0 '\n'
CheckSelection 'configured height limits visible option rows' 0 '\n' height-two '' 'café-日本語' C.UTF-8
CheckSelection 'configured percentage height limits visible option rows' 0 '\n' height-percent '' 'café-日本語' C.UTF-8
CheckSelection 'configured reverse-list preserves logical Down navigation' 1 "$(TERM=xterm tput kcud1)\\n" reverse-list 'beta'
CheckSelection 'configured reverse-prompt preserves logical Down navigation' 1 "$(TERM=xterm tput kcud1)\\n" reverse-prompt 'beta'
CheckSelection 'reverse-prompt preserves navigation with fixed height' 1 "$(TERM=xterm tput kcud1)\\n" reverse-prompt-height 'beta'
CheckSelection '1 percent height rounds up to one visible row' 0 '\n' height-percent-min '' 'beta' C.UTF-8
CheckSelection '100 percent height retains all available option rows' 0 '\n' height-percent-full 'café-日本語' '' C.UTF-8
CheckSelection 'initial query is prefilled and filters immediately' 2 '\n' configured-query 'needle'
CheckSelection 'C API preview callback receives selected option, index, and query' 2 '\n' preview-callback 'api-preview:needle:2:needle'
CheckPreviewClipping


CheckResizeRedrawWithoutInput() {
    rm -f "$TmpDir/preview-marker" "$TmpDir/resize-keys"
    mkfifo "$TmpDir/resize-keys"
    exec 3<> "$TmpDir/resize-keys"
    MMENU_PREVIEW_MARKER="$TmpDir/preview-marker" LC_ALL=C.UTF-8 TERM=xterm \
        timeout 5s script -q -e -c "$Bin resize-preview" /dev/null \
        < "$TmpDir/resize-keys" > "$TmpDir/resize-preview-output" 2> "$TmpDir/resize-preview-error" &
    ResizePid=$!
    Attempts=0
    while [ "$Attempts" -lt 20 ] && [ ! -s "$TmpDir/preview-marker" ]; do
        sleep 0.05
        Attempts=$((Attempts + 1))
    done
    if [ -s "$TmpDir/preview-marker" ]; then
        printf 'PASS: terminal resize redraws without waiting for user input\n'
    else
        printf 'FAIL: terminal resize did not redraw before the next key\n' >&2
        Failures=$((Failures + 1))
    fi
    printf '\n' >&3
    if wait "$ResizePid"; then
        sed -E 's/\x1B\[[0-?]*[ -/]*[@-~]//g; s/\x1B[()][A-Za-z0-9]//g; s/\x1B[=>]//g' "$TmpDir/resize-preview-output" > "$TmpDir/resize-preview-plain"
        if ! grep -aFq 'MMENU_RESULT=0' "$TmpDir/resize-preview-plain"; then
            printf 'FAIL: resize preview menu did not accept the current option\n' >&2
            Failures=$((Failures + 1))
        fi
    else
        printf 'FAIL: resize preview menu did not exit cleanly\n' >&2
        cat "$TmpDir/resize-preview-error" >&2
        Failures=$((Failures + 1))
    fi
    exec 3>&-
}

CheckUnicodePreviewClipping() {
    if ! { sleep 0.2; printf '\n'; } |
         LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "$Bin preview-unicode" /dev/null > "$TmpDir/preview-unicode-output" 2> "$TmpDir/preview-unicode-error"; then
        printf 'FAIL: Unicode preview clipping test did not exit cleanly\n' >&2
        cat "$TmpDir/preview-unicode-error" >&2
        Failures=$((Failures + 1))
        return
    fi
    if grep -aFq '日日日日日日日日日日' "$TmpDir/preview-unicode-output"; then
        printf 'FAIL: wide Unicode preview exceeded its display-cell width\n' >&2
        Failures=$((Failures + 1))
    elif ! grep -aFq 'MMENU_RESULT=0' "$TmpDir/preview-unicode-output"; then
        printf 'FAIL: Unicode preview changed selection or did not exit\n' >&2
        Failures=$((Failures + 1))
    else
        printf 'PASS: wide Unicode preview is clipped by display cells\n'
    fi
}
CheckUnicodePreviewClipping

CheckSelection 'backspacing an initial query broadens the match set' 1 '\177' configured-query-edit '' '' C.UTF-8 "$(TERM=xterm tput kcud1)"
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
UpKey=$(TERM=xterm tput kcuu1)
CheckSelection 'configured C API cycle wraps Down across filtered results' 0 "$(printf '%s%s%s' "$DownKey" "$DownKey" "$DownKey")\\n" cycle
CheckSelection 'single-result cyclic navigation does not rerun preview' 2 "$(printf '%s%s' "$UpKey" "$DownKey")\\n" cycle-preview-single 'MMENU_PREVIEW_CALLS=1'
CheckSelection 'configured C API cycle wraps Up across filtered results' 3 "$UpKey\\n" cycle
CheckSelection 'configured C API default does not wrap Up at the first result' 0 "$UpKey\\n" cycle-default
CheckSelection 'configured C API cycle handles an empty filtered result safely' -1 "$(printf '%s%s' "$UpKey" "$DownKey")\\n" cycle-empty
CheckSelection 'typed query filters to the matching option' 2 'needle\n'
CheckSelection 'legacy API keeps contiguous-substring semantics' -1 'ndl\n'
CheckSelection 'fuzzy API matches a subsequence' 2 'ndl\n' fuzzy
CheckSelection 'interactive fuzzy no-sort preserves input order' 0 '\n' fuzzy-no-sort
CheckSelection 'disabled search keeps all options visible for a non-matching query' 0 '\n' disabled
CheckSelection 'custom searchable text callback filters without changing displayed records' 1 '\n' nth-api
CheckDisplayText() {
    if ! { sleep 0.2; printf '\n'; } |
         LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "$Bin nth-api" /dev/null > "$TmpDir/display-output" 2> "$TmpDir/display-error"; then
        printf 'FAIL: display-text callback test did not exit cleanly\n' >&2
        cat "$TmpDir/display-error" >&2
        Failures=$((Failures + 1))
        return
    fi
    sed -E 's/\x1B\[[0-?]*[ -/]*[@-~]//g; s/\x1B[()][A-Za-z0-9]//g; s/\x1B[=>]//g' "$TmpDir/display-output" > "$TmpDir/display-plain"
    if ! grep -aFq 'DISPLAY_NEEDLE' "$TmpDir/display-plain" ||
       ! grep -aFq 'MMENU_RESULT=1' "$TmpDir/display-plain"; then
        printf 'FAIL: custom display text was not rendered\n' >&2
        cat "$TmpDir/display-plain" >&2
        Failures=$((Failures + 1))
    else
        printf 'PASS: custom display text is rendered while matching and return indexes stay stable\n'
    fi
}
CheckDisplayText


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
CheckSelection 'C API tiebreak length orders equal-score matches' 1 '\n' tiebreak

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
    elif ! grep -Fq -- "$Diagnostic" "$TmpDir/api-error"; then
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
CheckApiContract invalid-no-sort 'unsupported API flags'
CheckApiContract invalid-case-flags 'unsupported API flags'
CheckApiContract invalid-config-keys 'a custom key cannot be assigned to multiple actions'
CheckApiContract invalid-config-height 'invalid height configuration'
CheckApiContract invalid-config-height-percent 'invalid height configuration'
CheckApiContract invalid-config-height-percent-negative 'invalid height configuration'
CheckApiContract invalid-config-height-conflict 'invalid height configuration'
CheckApiContract invalid-config-layout-conflict 'reverse_list and reverse_prompt layouts are mutually exclusive'
CheckApiContract invalid-config-query 'initial query exceeds'
CheckApiContract invalid-tiebreak "--tiebreak key 'length' is repeated"

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
CheckResizeRedrawWithoutInput
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

CheckSelection 'case-sensitive matching is exposed through the C API' 1 '\n' case-sensitive
CheckSelection 'extended query AND terms work in the interactive C API' 1 '\n' extended
CheckSelection 'escaped spaces stay inside one interactive extended term' 1 '\n' extended-escaped-space
CheckSelection 'escaped-space OR groups work in the interactive C API' 1 '\n' extended-escaped-space-or
CheckSelection 'escaped-space terms compose with AND in the interactive C API' 1 '\n' extended-escaped-space-and
CheckSelection 'escaped-space prefix terms work in the interactive C API' 1 '\n' extended-escaped-space-prefix
CheckSelection 'escaped-space suffix terms work in the interactive C API' 1 '\n' extended-escaped-space-suffix
CheckSelection 'escaped-space inverse terms work in the interactive C API' 1 '\n' extended-escaped-space-inverse
CheckSelection 'escaped-space quoted terms match contiguous text in the interactive C API' 0 '\n' extended-escaped-space-quoted
CheckSelection 'escaped-space term preserves whitespace in fuzzy C API matching' 1 '\n' extended-escaped-space-fuzzy

CheckStreamApi 'configured C API accepts asynchronously supplied records' stream 1
CheckStreamApi 'escaped-space AND query filters streamed C API records' extended-escaped-space-stream 1

if [ "$Failures" -ne 0 ]; then
    printf '%s interactive regression check(s) failed.\n' "$Failures" >&2
    exit 1
fi
printf 'All interactive regression checks passed.\n'
