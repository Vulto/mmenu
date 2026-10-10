#!/bin/sh
# Deterministic throughput and peak-RSS measurements; no noisy absolute timing gate.
set -eu
Bin=${1:-./build/mmenu}
Out=${2:-performance.csv}
TmpDir=$(mktemp -d)
trap 'rm -rf "$TmpDir"' EXIT HUP INT TERM
printf 'items,bytes,query,sample,elapsed_ns,max_rss_kib,matched_lines,exit_status\n' > "$Out"
printf 'items,samples,min_elapsed_ns,median_elapsed_ns,max_elapsed_ns,max_rss_kib,matched_lines\n' > "$Out.summary.csv"
{
    printf 'revision=%s\n' "${GITHUB_SHA:-unknown}"
    printf 'host=%s\n' "$(uname -a)"
    Compiler=${CC:-cc}
    printf 'compiler=%s\n' "$("$Compiler" --version | sed -n '1p')"
    printf 'flags=%s\n' "${BENCHMARK_CFLAGS:-unspecified}"
    printf 'samples_per_size=5\n'
    printf 'rss_unit=KiB from GNU time %M\n'
} > "$Out.metadata.txt"

for Count in 1000 10000 100000 1000000; do
    awk -v count="$Count" 'BEGIN {
        for (i = 0; i < count; i++) {
            if (i % 10 == 0) printf "src/module-%07d-Needle.c\n", i;
            else printf "src/module-%07d.c\n", i;
        }
    }' > "$TmpDir/input"
    Bytes=$(wc -c < "$TmpDir/input" | tr -d ' ')
    Expected=$((Count / 10))
    : > "$TmpDir/times"
    : > "$TmpDir/rss-values"

    Run=1
    while [ "$Run" -le 5 ]; do
        Start=$(date +%s%N)
        /usr/bin/time -f '%M' -o "$TmpDir/rss-current" "$Bin" --filter needle < "$TmpDir/input" > "$TmpDir/output"
        End=$(date +%s%N)
        Elapsed=$((End - Start))
        Matches=$(wc -l < "$TmpDir/output" | tr -d ' ')
        [ "$Matches" -eq "$Expected" ] || {
            printf 'Benchmark correctness failure: expected %s matches, got %s\n' "$Expected" "$Matches" >&2
            exit 1
        }
        RSS=$(cat "$TmpDir/rss-current")
        printf '%s,%s,needle,%s,%s,%s,%s,0\n' "$Count" "$Bytes" "$Run" "$Elapsed" "$RSS" "$Matches" >> "$Out"
        printf '%s\n' "$Elapsed" >> "$TmpDir/times"
        printf '%s\n' "$RSS" >> "$TmpDir/rss-values"
        Run=$((Run + 1))
    done

    MinElapsed=$(sort -n "$TmpDir/times" | sed -n '1p')
    MedianElapsed=$(sort -n "$TmpDir/times" | sed -n '3p')
    MaxElapsed=$(sort -n "$TmpDir/times" | sed -n '5p')
    MaxRSS=$(sort -n "$TmpDir/rss-values" | tail -n 1)
    printf '%s,5,%s,%s,%s,%s,%s\n' "$Count" "$MinElapsed" "$MedianElapsed" "$MaxElapsed" "$MaxRSS" "$Expected" >> "$Out.summary.csv"
    printf 'items=%s bytes=%s matches=%s latency_ns[min/median/max]=%s/%s/%s max_rss_kib=%s\n' "$Count" "$Bytes" "$Expected" "$MinElapsed" "$MedianElapsed" "$MaxElapsed" "$MaxRSS"
done
