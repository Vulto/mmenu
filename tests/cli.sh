#!/bin/sh
# Black-box contract tests for the documented non-interactive CLI.
set -eu

Bin=${1:-./build/mmenu}
TmpDir=$(mktemp -d)
trap 'rm -rf "$TmpDir"' EXIT HUP INT TERM
Failures=0
Fail() { printf 'FAIL: %s\n' "$*" >&2; Failures=$((Failures + 1)); }
Pass() { printf 'PASS: %s\n' "$*"; }
CheckEqual() {
    Name=$1
    Expected=$2
    Actual=$3
    if ! cmp -s "$Expected" "$Actual"; then
        printf 'FAIL: %s\n--- expected (visible) ---\n' "$Name" >&2
        od -An -tx1c "$Expected" >&2
        printf '%s\n--- actual (visible) ---\n' "$Name" >&2
        od -An -tx1c "$Actual" >&2
        Fail "$Name"
    else
        Pass "$Name"
    fi
}

printf 'alpha needle omega\nNeedle uppercase\nnothing here\nneedle\nneedle\n\nfinal needle' > "$TmpDir/input"
printf 'alpha needle omega\nNeedle uppercase\nneedle\nneedle\nfinal needle\n' > "$TmpDir/expected"
"$Bin" --filter needle < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'case-insensitive substring; duplicates; final unterminated line' "$TmpDir/expected" "$TmpDir/actual"

printf '0\n1\n3\n4\n6\n' > "$TmpDir/expected"
"$Bin" -f needle -t < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'short filter alias and original zero-based line indexes' "$TmpDir/expected" "$TmpDir/actual"

printf 'alpha needle omega\nNeedle uppercase\nneedle\nneedle\nfinal needle\n' > "$TmpDir/expected"
"$Bin" --filter NEEDLE < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'ASCII case-insensitive matching' "$TmpDir/expected" "$TmpDir/actual"

: > "$TmpDir/expected"
"$Bin" --filter absent < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'query with zero matches returns empty stdout' "$TmpDir/expected" "$TmpDir/actual"

printf 'alpha needle omega\nNeedle uppercase\nnothing here\nneedle\nneedle\n\nfinal needle\n' > "$TmpDir/expected"
"$Bin" --filter '' < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'empty query returns all input records in order' "$TmpDir/expected" "$TmpDir/actual"

printf '\n\n' > "$TmpDir/input"
printf '\n\n' > "$TmpDir/expected"
"$Bin" --filter '' < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'empty records are preserved' "$TmpDir/expected" "$TmpDir/actual"

printf 'prefix-needle-suffix\n' > "$TmpDir/input"
printf '0\n' > "$TmpDir/expected"
"$Bin" --filter needle -t < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'index output is zero based' "$TmpDir/expected" "$TmpDir/actual"

# Long-record behavior is a correctness gate, not an implementation preference.
awk 'BEGIN { for (i = 0; i < 6000; i++) printf "x"; printf "needle\n" }' > "$TmpDir/input"
cp "$TmpDir/input" "$TmpDir/expected"
"$Bin" --filter needle < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'long records are preserved without truncation' "$TmpDir/expected" "$TmpDir/actual"

awk 'BEGIN { for (i = 0; i < 120000; i++) printf "item-%06d needle\n", i }' > "$TmpDir/input"
"$Bin" --filter needle -t < "$TmpDir/input" > "$TmpDir/actual"
awk 'BEGIN { for (i = 0; i < 120000; i++) printf "%d\n", i }' > "$TmpDir/expected"
CheckEqual 'large input returns all matching indexes in stable order' "$TmpDir/expected" "$TmpDir/actual"

# Differential checks against grep's fixed-string, case-insensitive matcher.
# The generated corpus and query set are deterministic for reproducible failures.
awk 'BEGIN {
    for (i = 0; i < 5000; i++) {
        printf "row-%05d alpha-%d Needle-%d punctuation.[x] Z9\n", i, i % 17, i % 31;
    }
}' > "$TmpDir/input"
for Query in alpha-3 needle 'punctuation.[x]' Z9 missing; do
    grep -i -F -- "$Query" "$TmpDir/input" > "$TmpDir/expected" || [ "$Query" = missing ]
    "$Bin" --filter "$Query" < "$TmpDir/input" > "$TmpDir/actual"
    CheckEqual "differential matching oracle for query: $Query" "$TmpDir/expected" "$TmpDir/actual"
done

if "$Bin" --filter < /dev/null > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
    Fail 'missing --filter query must be rejected with nonzero status'
elif ! grep -qi 'filter.*query\|query.*filter' "$TmpDir/stderr"; then
    Fail 'missing --filter query must report an argument error instead of entering interactive mode'
else
    Pass 'missing --filter query is rejected with an argument diagnostic'
fi

if [ "$Failures" -ne 0 ]; then
    printf '%s regression check(s) failed.\n' "$Failures" >&2
    exit 1
fi
printf 'All CLI regression checks passed.\n'
