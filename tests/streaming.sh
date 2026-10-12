#!/bin/sh
set -eu

Bin=${1:-./build/mmenu}
case "$Bin" in
    /*) ;;
    *) Bin=$(cd "$(dirname "$Bin")" && pwd)/$(basename "$Bin") ;;
esac
TmpDir=$(mktemp -d)
trap 'rm -rf "$TmpDir"' EXIT HUP INT TERM

RunStreamingAccept() {
    Mode=$1
    Expected=$2
    rm -f "$TmpDir/input" "$TmpDir/eof" "$TmpDir/result" "$TmpDir/result.tmp" "$TmpDir/terminal"
    export MMENU_BIN="$Bin"
    export MMENU_FIFO="$TmpDir/input"
    export MMENU_EOF="$TmpDir/eof"
    export MMENU_RESULT="$TmpDir/result"
    export MMENU_RESULT_TMP="$TmpDir/result.tmp"

    if [ "$Mode" = nul ]; then
        # shellcheck disable=SC2016
        Command='mkfifo "$MMENU_FIFO"; ( { printf "first\000"; sleep 1; printf "second\000"; : > "$MMENU_EOF"; } > "$MMENU_FIFO" ) & Producer=$!; "$MMENU_BIN" --read0 < "$MMENU_FIFO" > "$MMENU_RESULT_TMP"; Status=$?; if [ "$Status" -eq 0 ]; then mv "$MMENU_RESULT_TMP" "$MMENU_RESULT"; fi; wait "$Producer"; exit "$Status"'
    elif [ "$Mode" = empty ]; then
        # shellcheck disable=SC2016
        Command='mkfifo "$MMENU_FIFO"; ( { printf "\nfirst\n"; sleep 1; printf "second\n"; : > "$MMENU_EOF"; } > "$MMENU_FIFO" ) & Producer=$!; "$MMENU_BIN" -t < "$MMENU_FIFO" > "$MMENU_RESULT_TMP"; Status=$?; if [ "$Status" -eq 0 ]; then mv "$MMENU_RESULT_TMP" "$MMENU_RESULT"; fi; wait "$Producer"; exit "$Status"'
    else
        # shellcheck disable=SC2016
        Command='mkfifo "$MMENU_FIFO"; ( { printf "first\n"; sleep 1; printf "second\n"; : > "$MMENU_EOF"; } > "$MMENU_FIFO" ) & Producer=$!; "$MMENU_BIN" < "$MMENU_FIFO" > "$MMENU_RESULT_TMP"; Status=$?; if [ "$Status" -eq 0 ]; then mv "$MMENU_RESULT_TMP" "$MMENU_RESULT"; fi; wait "$Producer"; exit "$Status"'
    fi

    { sleep 0.35; printf '\n'; } |
        LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "sh -c '$Command'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/script-error" &
    SessionPid=$!
    sleep 0.65
    if [ -e "$TmpDir/eof" ]; then
        printf 'FAIL: %s producer reached EOF before the menu accepted the first record\n' "$Mode" >&2
        kill "$SessionPid" 2>/dev/null || :
        wait "$SessionPid" 2>/dev/null || :
        return 1
    fi
    if [ ! -f "$TmpDir/result" ]; then
        printf 'FAIL: %s menu did not accept the first record before input EOF\n' "$Mode" >&2
        kill "$SessionPid" 2>/dev/null || :
        wait "$SessionPid" 2>/dev/null || :
        cat "$TmpDir/script-error" >&2
        return 1
    fi
    if ! grep -aFq 'first' "$TmpDir/terminal"; then
        printf 'FAIL: %s first record was not rendered while the producer remained open\n' "$Mode" >&2
        kill "$SessionPid" 2>/dev/null || :
        wait "$SessionPid" 2>/dev/null || :
        return 1
    fi
    wait "$SessionPid"
    if ! grep -aFxq "$Expected" "$TmpDir/result"; then
        printf 'FAIL: %s selected output did not equal %s\n' "$Mode" "$Expected" >&2
        od -An -tx1c "$TmpDir/result" >&2
        return 1
    fi
    printf 'PASS: %s input is interactive before EOF\n' "$Mode"
}

RunStreamingAccept newline first
RunStreamingAccept nul first
RunStreamingAccept empty 0

RunStreamingQuery() {
    rm -f "$TmpDir/input" "$TmpDir/eof" "$TmpDir/result" "$TmpDir/result.tmp" "$TmpDir/terminal"
    export MMENU_BIN="$Bin" MMENU_FIFO="$TmpDir/input" MMENU_EOF="$TmpDir/eof"
    export MMENU_RESULT="$TmpDir/result" MMENU_RESULT_TMP="$TmpDir/result.tmp"
    # shellcheck disable=SC2016
    Command='mkfifo "$MMENU_FIFO"; ( { printf "alpha\n"; sleep 0.3; printf "second\n"; sleep 1; printf "third\n"; : > "$MMENU_EOF"; } > "$MMENU_FIFO" ) & Producer=$!; "$MMENU_BIN" --query second < "$MMENU_FIFO" > "$MMENU_RESULT_TMP"; Status=$?; if [ "$Status" -eq 0 ]; then mv "$MMENU_RESULT_TMP" "$MMENU_RESULT"; fi; wait "$Producer"; exit "$Status"'
    { sleep 0.65; printf '\n'; } |
        LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "sh -c '$Command'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/script-error" &
    SessionPid=$!
    sleep 0.9
    if [ -e "$TmpDir/eof" ] || [ ! -f "$TmpDir/result" ]; then
        printf 'FAIL: query filtering did not select a later record before EOF\n' >&2
        kill "$SessionPid" 2>/dev/null || :
        wait "$SessionPid" 2>/dev/null || :
        return 1
    fi
    if ! grep -aFq 'second' "$TmpDir/terminal"; then
        printf 'FAIL: later matching record was not rendered while input remained open\n' >&2
        kill "$SessionPid" 2>/dev/null || :
        wait "$SessionPid" 2>/dev/null || :
        return 1
    fi
    wait "$SessionPid"
    grep -aFxq second "$TmpDir/result" || {
        printf 'FAIL: query stream selected the wrong record\n' >&2
        return 1
    }
    printf 'PASS: query filtering updates as records arrive before EOF\n'
}

RunFinalUnterminated() {
    rm -f "$TmpDir/input" "$TmpDir/eof" "$TmpDir/result" "$TmpDir/result.tmp" "$TmpDir/terminal"
    export MMENU_BIN="$Bin" MMENU_FIFO="$TmpDir/input" MMENU_EOF="$TmpDir/eof"
    export MMENU_RESULT="$TmpDir/result" MMENU_RESULT_TMP="$TmpDir/result.tmp"
    # shellcheck disable=SC2016
    Command='mkfifo "$MMENU_FIFO"; ( { printf "first\n"; sleep 0.3; printf "last"; : > "$MMENU_EOF"; } > "$MMENU_FIFO" ) & Producer=$!; "$MMENU_BIN" --query last < "$MMENU_FIFO" > "$MMENU_RESULT_TMP"; Status=$?; if [ "$Status" -eq 0 ]; then mv "$MMENU_RESULT_TMP" "$MMENU_RESULT"; fi; wait "$Producer"; exit "$Status"'
    { sleep 0.65; printf '\n'; } |
        LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "sh -c '$Command'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/script-error"
    grep -aFxq last "$TmpDir/result" || {
        printf 'FAIL: final unterminated stream record was not selectable\n' >&2
        return 1
    }
    printf 'PASS: final unterminated record is retained at EOF\n'
}

RunLongStream() {
    rm -f "$TmpDir/input" "$TmpDir/eof" "$TmpDir/result" "$TmpDir/result.tmp" "$TmpDir/terminal" "$TmpDir/expected" "$TmpDir/long-record"
    export MMENU_BIN="$Bin" MMENU_FIFO="$TmpDir/input" MMENU_EOF="$TmpDir/eof"
    export MMENU_RESULT="$TmpDir/result" MMENU_RESULT_TMP="$TmpDir/result.tmp"
    export MMENU_LONG_FILE="$TmpDir/long-record"
    awk 'BEGIN { for (i = 0; i < 20000; i++) printf "x"; printf "needle\n"; }' > "$TmpDir/expected"
    cp "$TmpDir/expected" "$MMENU_LONG_FILE"
    # shellcheck disable=SC2016
    Command='mkfifo "$MMENU_FIFO"; ( { cat "$MMENU_LONG_FILE"; sleep 1; printf "second\n"; : > "$MMENU_EOF"; } > "$MMENU_FIFO" ) & Producer=$!; "$MMENU_BIN" --query needle < "$MMENU_FIFO" > "$MMENU_RESULT_TMP"; Status=$?; if [ "$Status" -eq 0 ]; then mv "$MMENU_RESULT_TMP" "$MMENU_RESULT"; fi; wait "$Producer"; exit "$Status"'
    { sleep 0.35; printf '\n'; } |
        LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "sh -c '$Command'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/script-error" &
    SessionPid=$!
    sleep 0.65
    if [ -e "$TmpDir/eof" ] || [ ! -f "$TmpDir/result" ]; then
        printf 'FAIL: long streamed record was not selected before EOF\n' >&2
        kill "$SessionPid" 2>/dev/null || :
        wait "$SessionPid" 2>/dev/null || :
        return 1
    fi
    wait "$SessionPid"
    cmp -s "$TmpDir/expected" "$TmpDir/result" || {
        printf 'FAIL: long streamed record contents changed\n' >&2
        return 1
    }
    printf 'PASS: long streamed records retain exact bytes\n'
}

RunStreamingCancel() {
    rm -f "$TmpDir/input" "$TmpDir/eof" "$TmpDir/result" "$TmpDir/result.tmp" "$TmpDir/terminal"
    export MMENU_BIN="$Bin" MMENU_FIFO="$TmpDir/input" MMENU_EOF="$TmpDir/eof"
    export MMENU_RESULT="$TmpDir/result" MMENU_RESULT_TMP="$TmpDir/result.tmp"
    # shellcheck disable=SC2016
    Command='mkfifo "$MMENU_FIFO"; ( { printf "first\n"; sleep 1; printf "second\n"; : > "$MMENU_EOF"; } > "$MMENU_FIFO" ) & Producer=$!; "$MMENU_BIN" < "$MMENU_FIFO" > "$MMENU_RESULT_TMP"; Status=$?; if [ "$Status" -eq 0 ]; then mv "$MMENU_RESULT_TMP" "$MMENU_RESULT"; fi; wait "$Producer"; exit "$Status"'
    { sleep 0.35; printf '\003'; } |
        LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "sh -c '$Command'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/script-error" &
    SessionPid=$!
    sleep 0.65
    if [ -e "$TmpDir/eof" ] || [ ! -f "$TmpDir/result" ]; then
        printf 'FAIL: cancellation did not return while producer remained open\n' >&2
        kill "$SessionPid" 2>/dev/null || :
        wait "$SessionPid" 2>/dev/null || :
        return 1
    fi
    wait "$SessionPid"
    printf '\n' > "$TmpDir/expected"
    cmp -s "$TmpDir/expected" "$TmpDir/result" || {
        printf 'FAIL: streaming cancellation output was not empty\n' >&2
        return 1
    }
    printf 'PASS: cancellation returns while the producer remains open\n'
}

RunEmptyProducerEOF() {
    rm -f "$TmpDir/input" "$TmpDir/eof" "$TmpDir/result" "$TmpDir/result.tmp" "$TmpDir/terminal"
    export MMENU_BIN="$Bin" MMENU_FIFO="$TmpDir/input" MMENU_EOF="$TmpDir/eof"
    export MMENU_RESULT="$TmpDir/result" MMENU_RESULT_TMP="$TmpDir/result.tmp"
    # shellcheck disable=SC2016
    Command='mkfifo "$MMENU_FIFO"; ( { sleep 0.25; : > "$MMENU_EOF"; } > "$MMENU_FIFO" ) & Producer=$!; "$MMENU_BIN" < "$MMENU_FIFO" > "$MMENU_RESULT_TMP"; Status=$?; if [ "$Status" -eq 0 ]; then mv "$MMENU_RESULT_TMP" "$MMENU_RESULT"; fi; wait "$Producer"; exit "$Status"'
    { sleep 0.55; printf '\003'; } |
        LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "sh -c '$Command'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/script-error"
    printf '\n' > "$TmpDir/expected"
    cmp -s "$TmpDir/expected" "$TmpDir/result" || {
        printf 'FAIL: empty producer EOF did not remain safely cancellable\n' >&2
        return 1
    }
    printf 'PASS: EOF before the first record remains cancellable\n'
}

RunInvalidStreamRecord() {
    rm -f "$TmpDir/invalid-input" "$TmpDir/result" "$TmpDir/error" "$TmpDir/terminal"
    printf 'bad\000record\n' > "$TmpDir/invalid-input"
    export MMENU_BIN="$Bin" MMENU_INVALID_INPUT="$TmpDir/invalid-input"
    export MMENU_RESULT="$TmpDir/result" MMENU_ERROR="$TmpDir/error"
    # shellcheck disable=SC2016
    Command='"$MMENU_BIN" < "$MMENU_INVALID_INPUT" > "$MMENU_RESULT" 2> "$MMENU_ERROR"'
    if LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "sh -c '$Command'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/script-error"; then
        printf 'FAIL: embedded NUL in newline-delimited stream was accepted\n' >&2
        return 1
    fi
    grep -aFq 'mmenu: reading streamed input' "$TmpDir/error" || {
        printf 'FAIL: invalid stream record omitted its diagnostic\n' >&2
        return 1
    }
    printf 'PASS: invalid streamed record fails with a diagnostic\n'
}

RunStreamingEditedQuery() {
    rm -f "$TmpDir/input" "$TmpDir/eof" "$TmpDir/result" "$TmpDir/result.tmp" "$TmpDir/terminal"
    export MMENU_BIN="$Bin" MMENU_FIFO="$TmpDir/input" MMENU_EOF="$TmpDir/eof"
    export MMENU_RESULT="$TmpDir/result" MMENU_RESULT_TMP="$TmpDir/result.tmp"
    # shellcheck disable=SC2016
    Command='mkfifo "$MMENU_FIFO"; ( { printf "alpha\n"; sleep 0.5; printf "second\n"; sleep 1; printf "third\n"; : > "$MMENU_EOF"; } > "$MMENU_FIFO" ) & Producer=$!; "$MMENU_BIN" < "$MMENU_FIFO" > "$MMENU_RESULT_TMP"; Status=$?; if [ "$Status" -eq 0 ]; then mv "$MMENU_RESULT_TMP" "$MMENU_RESULT"; fi; wait "$Producer"; exit "$Status"'
    { sleep 0.25; printf 'second'; sleep 0.4; printf '\n'; } |
        LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "sh -c '$Command'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/script-error" &
    SessionPid=$!
    sleep 0.9
    if [ -e "$TmpDir/eof" ] || [ ! -f "$TmpDir/result" ]; then
        printf 'FAIL: query edited during streaming did not accept before EOF\n' >&2
        kill "$SessionPid" 2>/dev/null || :
        wait "$SessionPid" 2>/dev/null || :
        return 1
    fi
    wait "$SessionPid"
    grep -aFxq second "$TmpDir/result" || {
        printf 'FAIL: edited streaming query selected the wrong record\n' >&2
        return 1
    }
    printf 'PASS: an edited query continues matching incoming records\n'
}

RunStreamingMultiSelect() {
    rm -f "$TmpDir/input" "$TmpDir/eof" "$TmpDir/result" "$TmpDir/result.tmp" "$TmpDir/terminal"
    export MMENU_BIN="$Bin" MMENU_FIFO="$TmpDir/input" MMENU_EOF="$TmpDir/eof"
    export MMENU_RESULT="$TmpDir/result" MMENU_RESULT_TMP="$TmpDir/result.tmp"
    # shellcheck disable=SC2016
    Command='mkfifo "$MMENU_FIFO"; ( { printf "first\n"; sleep 0.3; printf "second\n"; sleep 1; : > "$MMENU_EOF"; } > "$MMENU_FIFO" ) & Producer=$!; "$MMENU_BIN" --multi < "$MMENU_FIFO" > "$MMENU_RESULT_TMP"; Status=$?; if [ "$Status" -eq 0 ]; then mv "$MMENU_RESULT_TMP" "$MMENU_RESULT"; fi; wait "$Producer"; exit "$Status"'
    { sleep 0.25; printf '\t'; sleep 0.4; printf '\n'; } |
        LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "sh -c '$Command'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/script-error" &
    SessionPid=$!
    sleep 0.9
    if [ -e "$TmpDir/eof" ] || [ ! -f "$TmpDir/result" ]; then
        printf 'FAIL: multi-select did not accept while the producer remained open\n' >&2
        kill "$SessionPid" 2>/dev/null || :
        wait "$SessionPid" 2>/dev/null || :
        return 1
    fi
    wait "$SessionPid"
    grep -aFxq first "$TmpDir/result" || {
        printf 'FAIL: selected state was lost when a new record arrived\n' >&2
        od -An -tx1c "$TmpDir/result" >&2
        return 1
    }
    if grep -aFxq second "$TmpDir/result"; then
        printf 'FAIL: newly arrived record was selected without user input\n' >&2
        return 1
    fi
    printf 'PASS: multi-select state survives incoming records\n'
}

RunInvalidMultibyteStreamRecord() {
    rm -f "$TmpDir/invalid-input" "$TmpDir/result" "$TmpDir/error" "$TmpDir/terminal"
    printf '\377\n' > "$TmpDir/invalid-input"
    export MMENU_BIN="$Bin" MMENU_INVALID_INPUT="$TmpDir/invalid-input"
    export MMENU_RESULT="$TmpDir/result" MMENU_ERROR="$TmpDir/error"
    # shellcheck disable=SC2016
    Command='"$MMENU_BIN" < "$MMENU_INVALID_INPUT" > "$MMENU_RESULT" 2> "$MMENU_ERROR"'
    if LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "sh -c '$Command'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/script-error"; then
        printf 'FAIL: invalid multibyte streamed record was accepted\n' >&2
        return 1
    fi
    grep -aFq 'mmenu: reading streamed input' "$TmpDir/error" || {
        printf 'FAIL: invalid multibyte record omitted its diagnostic\n' >&2
        return 1
    }
    printf 'PASS: invalid multibyte streamed record fails with a diagnostic\n'
}

RunStreamingPreview() {
    rm -f "$TmpDir/input" "$TmpDir/eof" "$TmpDir/result" "$TmpDir/result.tmp" "$TmpDir/terminal" "$TmpDir/preview-log"
    export MMENU_BIN="$Bin" MMENU_FIFO="$TmpDir/input" MMENU_EOF="$TmpDir/eof"
    export MMENU_RESULT="$TmpDir/result" MMENU_RESULT_TMP="$TmpDir/result.tmp"
    export MMENU_PREVIEW_LOG="$TmpDir/preview-log"
    # shellcheck disable=SC2016
    Command='mkfifo "$MMENU_FIFO"; ( { printf "first\n"; sleep 0.3; printf "second\n"; sleep 1; : > "$MMENU_EOF"; } > "$MMENU_FIFO" ) & Producer=$!; "$MMENU_BIN" --preview "printf x >> \"$MMENU_PREVIEW_LOG\"; printf preview-state" < "$MMENU_FIFO" > "$MMENU_RESULT_TMP"; Status=$?; if [ "$Status" -eq 0 ]; then mv "$MMENU_RESULT_TMP" "$MMENU_RESULT"; fi; wait "$Producer"; exit "$Status"'
    { sleep 0.65; printf '\n'; } |
        LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "sh -c '$Command'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/script-error" &
    SessionPid=$!
    sleep 0.9
    if [ -e "$TmpDir/eof" ] || [ ! -f "$TmpDir/result" ]; then
        printf 'FAIL: preview-enabled streaming menu did not accept before EOF\n' >&2
        kill "$SessionPid" 2>/dev/null || :
        wait "$SessionPid" 2>/dev/null || :
        return 1
    fi
    if ! grep -aFq 'preview-state' "$TmpDir/terminal"; then
        printf 'FAIL: preview pane did not render while streaming\n' >&2
        kill "$SessionPid" 2>/dev/null || :
        wait "$SessionPid" 2>/dev/null || :
        return 1
    fi
    wait "$SessionPid"
    grep -aFxq first "$TmpDir/result" || {
        printf 'FAIL: preview-enabled stream selected the wrong record\n' >&2
        return 1
    }
    if [ ! -f "$TmpDir/preview-log" ] || [ "$(wc -c < "$TmpDir/preview-log")" -lt 2 ]; then
        printf 'FAIL: preview callback was not refreshed after a later record arrived\n' >&2
        cat "$TmpDir/preview-log" >&2 2>/dev/null || :
        return 1
    fi
    printf 'PASS: preview remains active and refreshes as records arrive\n'
}

RunStreamingEarlyToggle() {
    rm -f "$TmpDir/input" "$TmpDir/eof" "$TmpDir/result" "$TmpDir/result.tmp" "$TmpDir/terminal"
    export MMENU_BIN="$Bin" MMENU_FIFO="$TmpDir/input" MMENU_EOF="$TmpDir/eof"
    export MMENU_RESULT="$TmpDir/result" MMENU_RESULT_TMP="$TmpDir/result.tmp"
    # shellcheck disable=SC2016
    Command='mkfifo "$MMENU_FIFO"; ( { printf "first\n"; sleep 0.3; printf "second\n"; sleep 1; : > "$MMENU_EOF"; } > "$MMENU_FIFO" ) & Producer=$!; "$MMENU_BIN" --multi < "$MMENU_FIFO" > "$MMENU_RESULT_TMP"; Status=$?; if [ "$Status" -eq 0 ]; then mv "$MMENU_RESULT_TMP" "$MMENU_RESULT"; fi; wait "$Producer"; exit "$Status"'
    { sleep 0.05; printf '\t'; sleep 0.35; printf '\n'; } |
        LC_ALL=C.UTF-8 TERM=xterm timeout 5s script -q -e -c "sh -c '$Command'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/script-error" &
    SessionPid=$!
    sleep 0.65
    if [ -e "$TmpDir/eof" ] || [ ! -f "$TmpDir/result" ]; then
        printf 'FAIL: early multi-select toggle did not accept before EOF\n' >&2
        kill "$SessionPid" 2>/dev/null || :
        wait "$SessionPid" 2>/dev/null || :
        return 1
    fi
    wait "$SessionPid"
    grep -aFxq first "$TmpDir/result" || {
        printf 'FAIL: toggle received before the first streamed batch was lost\n' >&2
        od -An -tx1c "$TmpDir/result" >&2
        return 1
    }
    if grep -aFxq second "$TmpDir/result"; then
        printf 'FAIL: second streamed record was selected without a toggle\n' >&2
        return 1
    fi
    printf 'PASS: early multi-select toggle applies to the first streamed record\n'
}

RunStreamingQuery
RunFinalUnterminated
RunLongStream
RunStreamingCancel
RunEmptyProducerEOF
RunInvalidStreamRecord
RunStreamingEditedQuery
RunStreamingMultiSelect
RunInvalidMultibyteStreamRecord
RunStreamingPreview
RunStreamingEarlyToggle
