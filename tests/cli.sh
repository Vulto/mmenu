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

# Deterministic property-style corpus with varied lengths, delimiters, and overlaps.
awk 'BEGIN {
    for (i = 0; i < 2500; i++) {
        a = (i * 37 + 11) % 101;
        b = (i * 17 + 3) % 47;
        marker = (i % 13 == 0) ? "needle" : "other";
        printf "Case-%04d|group-%02d|token-%02d|literal.[*]?|%s\n", i, a, b, marker;
    }
    print "café-日本語";
    print "CAFÉ-日本語";
}' > "$TmpDir/input"
for Query in case-001 group-07 token-12 'literal.[*]?' needle other absent-value 'café-日本語'; do
    LC_ALL=C grep -i -F -- "$Query" "$TmpDir/input" > "$TmpDir/expected" || [ "$Query" = absent-value ]
    "$Bin" --filter "$Query" < "$TmpDir/input" > "$TmpDir/actual"
    CheckEqual "generated corpus differential oracle: $Query" "$TmpDir/expected" "$TmpDir/actual"
done

for Flag in --filter -f; do
    if "$Bin" "$Flag" < /dev/null > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
        Fail "missing query after $Flag must be rejected"
    elif ! grep -qi 'filter.*query\|query.*filter' "$TmpDir/stderr"; then
        Fail "missing query after $Flag must explain the argument error"
    else
        Pass "missing query after $Flag is rejected"
    fi
done

if "$Bin" --filter < /dev/null > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
    Fail 'missing --filter query must be rejected with nonzero status'
elif ! grep -qi 'filter.*query\|query.*filter' "$TmpDir/stderr"; then
    Fail 'missing --filter query must report an argument error instead of entering interactive mode'
else
    Pass 'missing --filter query is rejected with an argument diagnostic'
fi


# Option parsing: documented aliases, ordering, and malformed combinations.
printf 'alpha needle\nbeta needle\n' > "$TmpDir/options"
printf '0\n1\n' > "$TmpDir/expected"
"$Bin" -t --filter needle < "$TmpDir/options" > "$TmpDir/actual"
CheckEqual 'index flag before filter flag' "$TmpDir/expected" "$TmpDir/actual"
"$Bin" --filter needle -t < "$TmpDir/options" > "$TmpDir/actual"
CheckEqual 'index flag after filter flag' "$TmpDir/expected" "$TmpDir/actual"
"$Bin" --filter=needle < "$TmpDir/options" > "$TmpDir/actual"
printf 'alpha needle\nbeta needle\n' > "$TmpDir/expected"
CheckEqual 'equals form of long filter option' "$TmpDir/expected" "$TmpDir/actual"
"$Bin" --filter= < "$TmpDir/options" > "$TmpDir/actual"
CheckEqual 'equals form accepts an empty query' "$TmpDir/options" "$TmpDir/actual"

CheckRejectedArgs() {
    Name=$1
    shift
    if "$Bin" "$@" < /dev/null > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
        Fail "$Name must be rejected"
    else
        Status=$?
        if [ "$Status" -ne 2 ] || ! grep -qi 'argument\|option\|usage' "$TmpDir/stderr"; then
            printf 'FAIL: %s expected status 2 and an argument diagnostic, got %s\n' "$Name" "$Status" >&2
            cat "$TmpDir/stderr" >&2
            Fail "$Name"
        else
            Pass "$Name"
        fi
    fi
}
CheckRejectedArgs 'unknown option is rejected' --bogus
CheckRejectedArgs 'duplicate filter options are rejected' --filter needle -f other
CheckRejectedArgs 'positional prompt is rejected in batch mode' --filter needle prompt
CheckRejectedArgs 'extra positional argument is rejected' prompt extra
CheckRejectedArgs 'filter-like unknown option is rejected' --filterx needle

# Legacy interactive positional index mode remains supported by the documented CLI.
CheckInteractiveIndexMode() {
    Name=$1
    shift
    printf 'alpha\nbeta\n' > "$TmpDir/options"
    printf '0\n' > "$TmpDir/expected"
    if ! printf '\n' | TERM=xterm timeout 5s script -q -e -c "$Bin $* < '$TmpDir/options' > '$TmpDir/selected'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/stderr"; then
        printf 'FAIL: %s did not exit cleanly\n' "$Name" >&2
        cat "$TmpDir/stderr" >&2
        Fail "$Name"
    else
        CheckEqual "$Name" "$TmpDir/expected" "$TmpDir/selected"
    fi
}
CheckInteractiveIndexMode 'legacy positional t selects index output' prompt t
CheckInteractiveIndexMode 'legacy positional true selects index output' prompt true
CheckInteractiveIndexMode 'flag index mode works before positional prompt' -t prompt
CheckInteractiveIndexMode 'flag index mode works after positional prompt' prompt -t

# Empty stdin is valid and produces no batch output.
: > "$TmpDir/input"
: > "$TmpDir/expected"
"$Bin" --filter needle < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'empty input produces empty output' "$TmpDir/expected" "$TmpDir/actual"

# Preserve CRLF records exactly and reject embedded NUL bytes explicitly.
printf 'alpha needle\r\nno match\r\n' > "$TmpDir/input"
printf 'alpha needle\r\n' > "$TmpDir/expected"
"$Bin" --filter needle < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'CRLF record endings are preserved' "$TmpDir/expected" "$TmpDir/actual"

printf 'alpha\000needle\n' > "$TmpDir/input"
if "$Bin" --filter needle < "$TmpDir/input" > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
    Fail 'embedded NUL input must be rejected'
else
    Status=$?
    if [ "$Status" -ne 1 ] || ! grep -qi 'NUL' "$TmpDir/stderr"; then
        Fail 'embedded NUL input must report a clear diagnostic and status 1'
    else
        Pass 'embedded NUL input is rejected explicitly'
    fi
fi

# Large standalone slabs must not corrupt the following record.
awk 'BEGIN { for (i = 0; i < 1048577; i++) printf "x"; printf " needle\nshort needle\n"; }' > "$TmpDir/input"
cp "$TmpDir/input" "$TmpDir/expected"
"$Bin" --filter needle < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'over-1MiB record followed by another record remains intact' "$TmpDir/expected" "$TmpDir/actual"

# stdout write errors must be observed before the process returns.
printf 'needle\n' > "$TmpDir/input"
if "$Bin" --filter needle < "$TmpDir/input" > /dev/full 2> "$TmpDir/stderr"; then
    Fail 'stdout write failure must return nonzero status'
else
    Status=$?
    if [ "$Status" -ne 1 ] || ! grep -qi 'stdout\|writing' "$TmpDir/stderr"; then
        Fail 'stdout write failure must report a diagnostic and status 1'
    else
        Pass 'stdout write failures are reported'
    fi
fi

if "$Bin" --filter needle 0<&- > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
    Fail 'stdin read failure must return nonzero status'
else
    Status=$?
    if [ "$Status" -ne 1 ] || ! grep -qi 'reading stdin' "$TmpDir/stderr"; then
        Fail 'stdin read failure must report a diagnostic and status 1'
    else
        Pass 'stdin read failures are reported'
    fi
fi

if [ "$Failures" -ne 0 ]; then
    printf '%s regression check(s) failed.\n' "$Failures" >&2
    exit 1
fi
printf 'All CLI regression checks passed.\n'
