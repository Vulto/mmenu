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
