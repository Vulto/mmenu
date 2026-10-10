#!/bin/sh
# Compare baseline and candidate on identical inputs on the same runner.
# Five samples and median reduce timing noise; 2x latency/RSS limits are coarse regression guards.
set -eu
BaselineBin=$1
CandidateBin=$2
Out=${3:-performance-regression.csv}
TmpDir=$(mktemp -d)
trap 'rm -rf "$TmpDir"' EXIT HUP INT TERM
printf 'items,baseline_median_ns,candidate_median_ns,baseline_max_rss_kib,candidate_max_rss_kib,slowdown_ratio,memory_ratio,latency_limit_ratio,memory_limit_ratio\n' > "$Out"

for Count in 100000 1000000; do
    awk -v count="$Count" 'BEGIN {
        for (i = 0; i < count; i++) {
            if (i % 10 == 0) printf "src/module-%07d-Needle.c\n", i;
            else printf "src/module-%07d.c\n", i;
        }
    }' > "$TmpDir/input"
    Expected=$((Count / 10))

    for Kind in baseline candidate; do
        if [ "$Kind" = baseline ]; then Bin=$BaselineBin; else Bin=$CandidateBin; fi
        "$Bin" --filter needle < "$TmpDir/input" > "$TmpDir/output"
        Matches=$(wc -l < "$TmpDir/output" | tr -d ' ')
        [ "$Matches" -eq "$Expected" ] || {
            printf '%s correctness mismatch at %s items: expected %s, got %s\n' "$Kind" "$Count" "$Expected" "$Matches" >&2
            exit 1
        }
        : > "$TmpDir/$Kind.times"
        : > "$TmpDir/$Kind.rss"
        Run=1
        while [ "$Run" -le 5 ]; do
            Start=$(date +%s%N)
            /usr/bin/time -f '%M' -o "$TmpDir/$Kind.rss-current" "$Bin" --filter needle < "$TmpDir/input" > /dev/null
            End=$(date +%s%N)
            printf '%s\n' "$((End - Start))" >> "$TmpDir/$Kind.times"
            cat "$TmpDir/$Kind.rss-current" >> "$TmpDir/$Kind.rss"
            Run=$((Run + 1))
        done
    done

    BaselineMedian=$(sort -n "$TmpDir/baseline.times" | sed -n '3p')
    CandidateMedian=$(sort -n "$TmpDir/candidate.times" | sed -n '3p')
    BaselineRSS=$(sort -n "$TmpDir/baseline.rss" | tail -n 1)
    CandidateRSS=$(sort -n "$TmpDir/candidate.rss" | tail -n 1)
    Ratio=$(awk -v base="$BaselineMedian" -v candidate="$CandidateMedian" 'BEGIN {
        if (base <= 0) exit 2;
        printf "%.3f", candidate / base;
    }')
    MemoryRatio=$(awk -v base="$BaselineRSS" -v candidate="$CandidateRSS" 'BEGIN {
        if (base <= 0) exit 2;
        printf "%.3f", candidate / base;
    }')
    printf '%s,%s,%s,%s,%s,%s,%s,2.000,2.000\n' "$Count" "$BaselineMedian" "$CandidateMedian" "$BaselineRSS" "$CandidateRSS" "$Ratio" "$MemoryRatio" >> "$Out"
    printf 'items=%s latency_baseline_median=%sns candidate_median=%sns slowdown=%sx baseline_max_rss=%sKiB candidate_max_rss=%sKiB memory_ratio=%sx\n' "$Count" "$BaselineMedian" "$CandidateMedian" "$Ratio" "$BaselineRSS" "$CandidateRSS" "$MemoryRatio"

    if awk -v ratio="$Ratio" 'BEGIN { exit !(ratio > 2.0) }'; then
        printf 'Performance regression: candidate is more than 2x slower at %s items\n' "$Count" >&2
        exit 1
    fi
    if awk -v ratio="$MemoryRatio" 'BEGIN { exit !(ratio > 2.0) }'; then
        printf 'Memory regression: candidate peak RSS is more than 2x baseline at %s items\n' "$Count" >&2
        exit 1
    fi
done
