#!/bin/sh
# Deterministic ranking-quality baseline against fuzzy-finder reference tools.
set -eu
Bin=${1:-./build/mmenu}
Out=${2:-ranking-baseline.csv}
Judgments=tests/fixtures/ranking-judgments.tsv
Expected=tests/fixtures/ranking-mmenu.expected.csv
MetricsAwk=tests/ranking-metrics.awk
TmpDir=$(mktemp -d)
trap 'rm -rf "$TmpDir"' EXIT HUP INT TERM

for Tool in fzf sk fzy; do
    Command=$Tool
    [ "$Tool" != sk ] || Command=sk
    command -v "$Command" >/dev/null 2>&1 || {
        printf 'Missing ranking reference tool: %s\n' "$Command" >&2
        exit 1
    }
done

printf 'tool,query_id,candidates,matched,relevant,mrr,ndcg_at_5,recall_at_5,elapsed_ns,max_rss_kib,version\n' > "$Out"
{
    printf 'revision=%s\n' "${GITHUB_SHA:-unknown}"
    printf 'host=%s\n' "$(uname -a)"
    printf 'metric_definition=MRR;NDCG@5 with gain 2^grade-1;Recall@5 over all judged relevant candidates\n'
    printf 'mmenu_semantics=case-insensitive contiguous substring matching; preserves input order; no fuzzy ranking\n'
    printf 'reference_semantics=fuzzy subsequence matching with tool-specific scoring and ranking; versions recorded per row\n'
} > "$Out.metadata.txt"

for Tool in mmenu fzf skim fzy; do
    case "$Tool" in
        mmenu) Version=${GITHUB_SHA:-unknown} ;;
        fzf) Version=$(fzf --version 2>&1 | head -n 1 || true) ;;
        skim) Version=$(sk --version 2>&1 | head -n 1 || true) ;;
        fzy) Version=$(fzy --version 2>&1 | head -n 1 || true) ;;
    esac
    Version=$(printf '%s' "$Version" | tr ',' ';' | tr '\n' ' ')
    printf 'tool_version_%s=%s\n' "$Tool" "$Version" >> "$Out.metadata.txt"

    for QueryId in menu mmh tst cfg; do
        Query=$(awk -F '\t' -v q="$QueryId" '$1 == q { print $2; exit }' "$Judgments")
        awk -F '\t' -v q="$QueryId" '$1 == q { print $4 }' "$Judgments" > "$TmpDir/input"

        Start=$(date +%s%N)
        if [ "$Tool" = mmenu ]; then
            if /usr/bin/time -f '%M' -o "$TmpDir/rss" "$Bin" --filter "$Query" < "$TmpDir/input" > "$TmpDir/result"; then
                Status=0
            else
                Status=$?
            fi
        elif [ "$Tool" = fzf ]; then
            if /usr/bin/time -f '%M' -o "$TmpDir/rss" fzf --filter="$Query" < "$TmpDir/input" > "$TmpDir/result"; then
                Status=0
            else
                Status=$?
            fi
        elif [ "$Tool" = skim ]; then
            if /usr/bin/time -f '%M' -o "$TmpDir/rss" sk --filter="$Query" < "$TmpDir/input" > "$TmpDir/result"; then
                Status=0
            else
                Status=$?
            fi
        else
            if /usr/bin/time -f '%M' -o "$TmpDir/rss" fzy --show-matches="$Query" < "$TmpDir/input" > "$TmpDir/result"; then
                Status=0
            else
                Status=$?
            fi
        fi
        End=$(date +%s%N)
        [ "$Status" -eq 0 ] || [ "$Status" -eq 1 ] || {
            printf '%s failed for query %s with status %s\n' "$Tool" "$Query" "$Status" >&2
            exit "$Status"
        }

        Elapsed=$((End - Start))
        RSS=$(cat "$TmpDir/rss")
        Metrics=$(awk -v query_id="$QueryId" -v tool="$Tool" -f "$MetricsAwk" "$Judgments" "$TmpDir/result")
        printf '%s,%s,%s,%s\n' "$Metrics" "$Elapsed" "$RSS" "$Version" >> "$Out"
    done
done

awk -F, 'BEGIN { OFS = "," } $1 == "mmenu" { print $2, $4, $5, $6, $7, $8 }' "$Out" > "$TmpDir/mmenu.actual"
if ! cmp -s "$Expected" "$TmpDir/mmenu.actual"; then
    printf 'mmenu ranking baseline changed; inspect metrics before updating the golden file.\n' >&2
    diff -u "$Expected" "$TmpDir/mmenu.actual" >&2 || true
    exit 1
fi

printf 'Ranking-quality baseline recorded in %s\n' "$Out"
cat "$Out"
