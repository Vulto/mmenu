#!/bin/sh
set -eu

Bin=${1:-./build/mmenu}
Out=${2:-build/matcher-throughput.csv}
Query=ndl
Samples=${MMENU_THROUGHPUT_SAMPLES:-3}
Revision=${GITHUB_SHA:-unknown}
TmpDir=$(mktemp -d)
trap 'rm -rf "$TmpDir"' EXIT HUP INT TERM
LC_ALL=C
export LC_ALL

Fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

[ -x "$Bin" ] || Fail "mmenu binary is not executable: $Bin"
case "$Samples" in
    ''|*[!0-9]*) Fail "sample count must be a positive odd integer" ;;
esac
if [ "$Samples" -le 0 ] || [ $((Samples % 2)) -eq 0 ]; then
    Fail "sample count must be a positive odd integer"
fi
command -v fzf >/dev/null 2>&1 || Fail "fzf is required for the measurement run"
command -v fzy >/dev/null 2>&1 || Fail "fzy is required for the measurement run"
[ -x /usr/bin/time ] || Fail "GNU time is required for RSS measurements"
mkdir -p "$(dirname "$Out")"

MmenuVersion=$Revision
CompilerVersion=unknown
if [ -n "${CC:-}" ]; then
    CompilerVersion=$("$CC" --version | sed -n '1p')
fi
FzfVersion=$(fzf --version 2>&1 | head -n 1 || true)
FzyVersion=$(fzy --version 2>&1 | head -n 1 || true)
FzfVersion=$(printf '%s' "$FzfVersion" | tr ',' ';' | tr '\n' ' ')
FzyVersion=$(printf '%s' "$FzyVersion" | tr ',' ';' | tr '\n' ' ')

{
    printf 'revision=%s\n' "$Revision"
    printf 'host=%s\n' "$(uname -a)"
    printf 'compiler=%s\n' "$CompilerVersion"
    printf 'flags=-O2 -g -std=c11 -Wall -Wextra -Wpedantic -Werror -D_DEFAULT_SOURCE\n'
    printf 'query=%s\n' "$Query"
    printf 'samples_per_size=%s\n' "$Samples"
    printf 'input_pattern=one in ten records contain needle; other records contain noise\n'
    printf 'timing=wall clock from process launch to exit\n'
    printf 'rss_unit=KiB from GNU time %%M\n'
    printf 'tool_version_mmenu_fuzzy=%s\n' "$MmenuVersion"
    printf 'tool_version_fzf=%s\n' "$FzfVersion"
    printf 'tool_version_fzy=%s\n' "$FzyVersion"
} > "$Out.metadata.txt"

printf 'tool,items,sample,elapsed_ns,max_rss_kib,matched_lines,exit_status,version\n' > "$Out"

RunOne() {
    Tool=$1
    Count=$2
    Sample=$3
    case "$Tool" in
        mmenu-fuzzy) Version=$MmenuVersion ;;
        fzf) Version=$FzfVersion ;;
        fzy) Version=$FzyVersion ;;
        *) Fail "unknown tool: $Tool" ;;
    esac

    Start=$(date +%s%N)
    case "$Tool" in
        mmenu-fuzzy)
            if /usr/bin/time -f '%M' -o "$TmpDir/rss" "$Bin" --fuzzy --filter "$Query" < "$TmpDir/input" > "$TmpDir/result"; then
                Status=0
            else
                Status=$?
            fi
            ;;
        fzf)
            if /usr/bin/time -f '%M' -o "$TmpDir/rss" fzf --filter="$Query" < "$TmpDir/input" > "$TmpDir/result"; then
                Status=0
            else
                Status=$?
            fi
            ;;
        fzy)
            if /usr/bin/time -f '%M' -o "$TmpDir/rss" fzy --show-matches="$Query" < "$TmpDir/input" > "$TmpDir/result"; then
                Status=0
            else
                Status=$?
            fi
            ;;
    esac
    End=$(date +%s%N)
    [ "$Status" -eq 0 ] || Fail "$Tool failed at $Count records (status $Status)"
    Elapsed=$((End - Start))
    RSS=$(cat "$TmpDir/rss")
    Matched=$(awk 'END { print NR + 0 }' "$TmpDir/result")
    Expected=$((Count / 10))
    [ "$Matched" -eq "$Expected" ] || Fail "$Tool matched $Matched rows at $Count records; expected $Expected"
    sort "$TmpDir/result" > "$TmpDir/result.sorted"
    cmp -s "$TmpDir/expected.sorted" "$TmpDir/result.sorted" || Fail "$Tool returned a different result set at $Count records"
    printf '%s,%s,%s,%s,%s,%s,%s,%s\n' "$Tool" "$Count" "$Sample" "$Elapsed" "$RSS" "$Matched" "$Status" "$Version" >> "$Out"
}

for Count in 1000 10000 100000 1000000; do
    awk -v count="$Count" 'BEGIN {
        for (i = 0; i < count; i++) {
            if (i % 10 == 0) printf "src/module-%07d-needle.c\n", i
            else printf "src/module-%07d-noise.c\n", i
        }
    }' > "$TmpDir/input"
    awk 'NR % 10 == 1 { print }' "$TmpDir/input" | sort > "$TmpDir/expected.sorted"

    Sample=1
    while [ "$Sample" -le "$Samples" ]; do
        RunOne mmenu-fuzzy "$Count" "$Sample"
        RunOne fzf "$Count" "$Sample"
        RunOne fzy "$Count" "$Sample"
        Sample=$((Sample + 1))
    done
done

Summary="$Out.summary.csv"
printf 'tool,items,samples,min_elapsed_ns,median_elapsed_ns,max_elapsed_ns,max_rss_kib,matched_lines\n' > "$Summary"
for Tool in mmenu-fuzzy fzf fzy; do
    for Count in 1000 10000 100000 1000000; do
        awk -F, -v tool="$Tool" -v items="$Count" '$1 == tool && $2 == items { print $4 }' "$Out" | sort -n > "$TmpDir/times"
        Min=$(sed -n '1p' "$TmpDir/times")
        MedianLine=$(((Samples + 1) / 2))
        Median=$(sed -n "${MedianLine}p" "$TmpDir/times")
        Max=$(tail -n 1 "$TmpDir/times")
        MaxRss=$(awk -F, -v tool="$Tool" -v items="$Count" '$1 == tool && $2 == items { if ($5 > max) max = $5 } END { print max + 0 }' "$Out")
        Matched=$(awk -F, -v tool="$Tool" -v items="$Count" '$1 == tool && $2 == items { matched = $6 } END { print matched + 0 }' "$Out")
        printf '%s,%s,%s,%s,%s,%s,%s,%s\n' "$Tool" "$Count" "$Samples" "$Min" "$Median" "$Max" "$MaxRss" "$Matched" >> "$Summary"
    done
done

printf 'Matcher throughput measurements written to %s and %s\n' "$Out" "$Summary"
