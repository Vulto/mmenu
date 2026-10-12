#!/bin/sh
# Informational first-paint measurements for the real interactive CLI.
# Timing is reported, not used as a pass/fail performance threshold.
set -eu

Bin=${1:-./build/mmenu}
Out=${2:-interactive-first-paint.csv}
TmpDir=$(mktemp -d)
trap 'exec 3>&- 2>/dev/null || :; rm -rf "$TmpDir"' EXIT HUP INT TERM
Prompt='MMENU_FIRST_PAINT_MARKER> '

printf 'items,sample,first_paint_ns,exit_status\n' > "$Out"
{
    printf 'revision=%s\n' "${GITHUB_SHA:-unknown}"
    printf 'host=%s\n' "$(uname -a)"
    printf 'terminal=80x24 TERM=xterm\n'
    printf 'samples_per_size=3\n'
    printf 'measurement=wall-clock from launch until prompt marker is observed\n'
} > "$Out.metadata.txt"

for Count in 1000 10000 100000; do
    awk -v count="$Count" 'BEGIN {
        for (i = 0; i < count; i++)
            printf "src/module-%07d.c\n", i;
    }' > "$TmpDir/input"

    Sample=1
    while [ "$Sample" -le 3 ]; do
        mkfifo "$TmpDir/keys"
        exec 3<> "$TmpDir/keys"
        : > "$TmpDir/typescript"
        : > "$TmpDir/error"
        Start=$(date +%s%N)
        timeout 20s script -q -f -e -c "stty rows 24 cols 80; TERM=xterm '$Bin' '$Prompt' < '$TmpDir/input'" "$TmpDir/typescript" <&3 > /dev/null 2> "$TmpDir/error" &
        Pid=$!

        Found=0
        Attempts=0
        while [ "$Attempts" -lt 3000 ]; do
            if grep -aFq "$Prompt" "$TmpDir/typescript"; then
                Found=1
                break
            fi
            if ! kill -0 "$Pid" 2>/dev/null; then
                break
            fi
            sleep 0.005
            Attempts=$((Attempts + 1))
        done

        if [ "$Found" -ne 1 ]; then
            printf '\n' >&3
            wait "$Pid" || :
            cat "$TmpDir/error" >&2
            tail -c 1000 "$TmpDir/typescript" | od -An -tx1c >&2
            printf 'FAIL: interactive prompt did not render for %s options (sample %s)\n' "$Count" "$Sample" >&2
            exit 1
        fi

        End=$(date +%s%N)
        Elapsed=$((End - Start))
        printf '\n' >&3
        if ! wait "$Pid"; then
            cat "$TmpDir/error" >&2
            printf 'FAIL: interactive menu did not exit for %s options (sample %s)\n' "$Count" "$Sample" >&2
            exit 1
        fi
        exec 3>&-
        rm -f "$TmpDir/keys"

        printf '%s,%s,%s,0\n' "$Count" "$Sample" "$Elapsed" >> "$Out"
        printf 'interactive_first_paint items=%s sample=%s elapsed_ns=%s\n' "$Count" "$Sample" "$Elapsed"
        Sample=$((Sample + 1))
    done
done
