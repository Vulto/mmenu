#!/bin/sh
# Deterministic non-interactive throughput measurements. No noisy absolute timing gate.
set -eu
Bin=${1:-./build/mmenu}
Out=${2:-performance.csv}
TmpDir=$(mktemp -d)
trap 'rm -rf "$TmpDir"' EXIT HUP INT TERM
printf 'items,bytes,query,elapsed_seconds,matched_lines,exit_status\n' > "$Out"

for Count in 1000 10000 100000 1000000; do
    awk -v count="$Count" 'BEGIN {
        for (i = 0; i < count; i++) {
            if (i % 10 == 0) printf "src/module-%07d-Needle.c\n", i;
            else printf "src/module-%07d.c\n", i;
        }
    }' > "$TmpDir/input"
    Bytes=$(wc -c < "$TmpDir/input" | tr -d ' ')
    Start=$(date +%s%N)
    "$Bin" --filter needle < "$TmpDir/input" > "$TmpDir/output"
    End=$(date +%s%N)
    Elapsed=$(awk -v start="$Start" -v end="$End" 'BEGIN { printf "%.6f", (end-start)/1000000000 }')
    Matches=$(wc -l < "$TmpDir/output" | tr -d ' ')
    Expected=$((Count / 10))
    [ "$Matches" -eq "$Expected" ] || {
        printf 'Benchmark correctness failure: expected %s matches, got %s\n' "$Expected" "$Matches" >&2
        exit 1
    }
    printf '%s,%s,needle,%s,%s,0\n' "$Count" "$Bytes" "$Elapsed" "$Matches" >> "$Out"
    printf 'items=%s bytes=%s matches=%s elapsed=%ss\n' "$Count" "$Bytes" "$Matches" "$Elapsed"
done
