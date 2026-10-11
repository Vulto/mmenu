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

# Batch output templates substitute records, original indexes, and the query.
printf 'alpha needle\nbeta needle\nneedle\n' > "$TmpDir/input"
printf '0:alpha needle [needle]\n1:beta needle [needle]\n2:needle [needle]\n' > "$TmpDir/expected"
"$Bin" --filter needle --format '{index}:{} [{query}]' < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'format expands record, original index, and query placeholders' "$TmpDir/expected" "$TmpDir/actual"
printf '1|beta needle\n' > "$TmpDir/expected"
"$Bin" --filter needle --format '{index}|{}' < "$TmpDir/input" | sed -n '2p' > "$TmpDir/actual"
CheckEqual 'format indexes remain original input indexes after filtering' "$TmpDir/expected" "$TmpDir/actual"
printf '0:alpha needle\0001:beta needle\0002:needle\000' > "$TmpDir/expected0"
"$Bin" --filter needle --format '{index}:{}' --print0 < "$TmpDir/input" > "$TmpDir/actual0"
CheckEqual 'formatted records honor NUL output delimiters' "$TmpDir/expected0" "$TmpDir/actual0"
printf 'beta needle\nneedle\n' > "$TmpDir/expected"
"$Bin" --filter alpha --invert-match --format '{}' < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'format works with inverse filtering' "$TmpDir/expected" "$TmpDir/actual"
printf 'alpha needle\nbeta needle\nneedle\n' > "$TmpDir/input"
printf '1:beta needle\n' > "$TmpDir/expected"
"$Bin" --fuzzy --filter bn --format '{index}:{}' < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'format works with fuzzy output and original indexes' "$TmpDir/expected" "$TmpDir/actual"

# Inverse filtering preserves original input order and supports all matcher modes.
printf 'alpha\nbeta\nAlpha\nalpha beta\n' > "$TmpDir/input"
printf 'beta\n' > "$TmpDir/expected"
"$Bin" --invert-match --filter alpha < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'inverse substring filter excludes matching records' "$TmpDir/expected" "$TmpDir/actual"
printf '1\n' > "$TmpDir/expected"
"$Bin" -v -f alpha -t < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'inverse substring filter emits original indexes' "$TmpDir/expected" "$TmpDir/actual"
printf 'beta\nAlpha\nalpha beta\n' > "$TmpDir/expected"
"$Bin" --exact --invert-match --filter alpha < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'inverse exact filter uses whole-record equality' "$TmpDir/expected" "$TmpDir/actual"
printf 'alpha\nbeta\nAlpha\nalpha beta\n' > "$TmpDir/expected"
"$Bin" --fuzzy --invert-match --filter zzz < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'inverse fuzzy filter preserves records unmatched by subsequence' "$TmpDir/expected" "$TmpDir/actual"
if "$Bin" --invert-match < "$TmpDir/input" > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
    Fail 'inverse filter without a query must fail'
elif ! grep -Fq 'requires --filter' "$TmpDir/stderr"; then
    Fail 'inverse filter without a query must explain the error'
else
    Pass 'inverse filter requires a batch query'
fi

# Fuzzy --no-sort keeps matched records in original input order.
printf 'zzab\nabzz\nacb\nbbb\n' > "$TmpDir/input"
printf 'zzab\nabzz\nacb\n' > "$TmpDir/expected"
"$Bin" --fuzzy --no-sort --filter ab < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'fuzzy no-sort preserves input order while filtering subsequences' "$TmpDir/expected" "$TmpDir/actual"
printf '0\n1\n2\n' > "$TmpDir/expected"
"$Bin" --fuzzy --no-sort --filter ab -t < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'fuzzy no-sort preserves original indexes' "$TmpDir/expected" "$TmpDir/actual"
if "$Bin" --no-sort --filter ab < "$TmpDir/input" > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
    Fail 'no-sort without fuzzy mode must fail'
elif ! grep -Fq 'requires --fuzzy and --filter' "$TmpDir/stderr"; then
    Fail 'no-sort without fuzzy mode must explain the error'
else
    Pass 'no-sort requires fuzzy batch mode'
fi

# NUL-delimited records support both terminated and final unterminated input.
printf 'alpha\000beta\000alpha beta' > "$TmpDir/input0"
printf 'alpha\000alpha beta\000' > "$TmpDir/expected0"
"$Bin" --read0 --print0 --filter alpha < "$TmpDir/input0" > "$TmpDir/actual0"
CheckEqual 'read0 and print0 preserve NUL-delimited records including final unterminated record' "$TmpDir/expected0" "$TmpDir/actual0"
printf 'alpha\nalpha beta\n' > "$TmpDir/expected"
"$Bin" --read0 --filter alpha < "$TmpDir/input0" > "$TmpDir/actual"
CheckEqual 'read0 accepts NUL-delimited input with newline output' "$TmpDir/expected" "$TmpDir/actual"
printf 'alpha\nbeta\nalpha beta\n' > "$TmpDir/input"
printf 'alpha\000alpha beta\000' > "$TmpDir/expected0"
"$Bin" --print0 --filter alpha < "$TmpDir/input" > "$TmpDir/actual0"
CheckEqual 'print0 emits NUL-delimited output for line-delimited input' "$TmpDir/expected0" "$TmpDir/actual0"
printf '0\0002\000' > "$TmpDir/expected0"
"$Bin" --read0 --print0 --filter alpha -t < "$TmpDir/input0" > "$TmpDir/actual0"
CheckEqual 'print0 also terminates index records with NUL' "$TmpDir/expected0" "$TmpDir/actual0"

# Exact mode uses byte-exact whole-record equality and preserves duplicate order.
printf 'alpha\nAlpha\nalpha beta\nalpha\n\n' > "$TmpDir/input"
printf 'alpha\nalpha\n' > "$TmpDir/expected"
"$Bin" --exact --filter alpha < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'exact mode matches whole records and preserves duplicates' "$TmpDir/expected" "$TmpDir/actual"
printf '0\n3\n' > "$TmpDir/expected"
"$Bin" --exact -f alpha -t < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'exact mode index output uses original input indexes' "$TmpDir/expected" "$TmpDir/actual"
printf 'Alpha\n' > "$TmpDir/expected"
"$Bin" --exact --filter Alpha < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'exact mode is case-sensitive' "$TmpDir/expected" "$TmpDir/actual"
printf '\n' > "$TmpDir/expected"
"$Bin" --exact --filter '' < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'exact empty query matches only empty records' "$TmpDir/expected" "$TmpDir/actual"
if "$Bin" --exact --fuzzy --filter alpha < "$TmpDir/input" > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
    Fail 'conflicting exact and fuzzy modes must fail'
elif ! grep -Fq 'mutually exclusive' "$TmpDir/stderr"; then
    Fail 'conflicting matching modes must explain the error'
else
    Pass 'conflicting matching modes are rejected'
fi

# Fuzzy mode is opt-in; legacy substring mode must keep its existing contract.
printf 'src/other.c\nsrc/mmenu.h\nsrc/m-m-h.c\nsrc/mmh.c\n' > "$TmpDir/input"
printf 'src/mmh.c\nsrc/mmenu.h\nsrc/m-m-h.c\n' > "$TmpDir/expected"
"$Bin" --fuzzy --filter mmh < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'fuzzy batch mode finds subsequences and ranks by score' "$TmpDir/expected" "$TmpDir/actual"
printf 'abc-first\nabc-second\n' > "$TmpDir/input"
printf 'abc-first\nabc-second\n' > "$TmpDir/expected"
"$Bin" --fuzzy --filter abc < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'fuzzy equal-score ties preserve input order' "$TmpDir/expected" "$TmpDir/actual"
printf 'src/other.c\nsrc/mmenu.h\nsrc/m-m-h.c\nsrc/mmh.c\n' > "$TmpDir/input"
printf '3\n1\n2\n' > "$TmpDir/expected"
"$Bin" --fuzzy -f mmh -t < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'fuzzy index mode emits original input indexes in ranked order' "$TmpDir/expected" "$TmpDir/actual"
printf 'alpha\nbeta\n' > "$TmpDir/input"
printf 'alpha\nbeta\n' > "$TmpDir/expected"
"$Bin" --fuzzy --filter '' < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'empty fuzzy query preserves input order' "$TmpDir/expected" "$TmpDir/actual"
"$Bin" --fuzzy --filter zzz < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'unmatched fuzzy query emits no records' /dev/null "$TmpDir/actual"
InvalidQuery=$(printf '\377bad')
if "$Bin" --fuzzy --filter "$InvalidQuery" < /dev/null > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
    Fail 'invalid multibyte fuzzy query must be rejected'
elif ! grep -qi 'fuzzy query is not valid' "$TmpDir/stderr"; then
    Fail 'invalid multibyte fuzzy query must explain the error'
else
    Pass 'invalid multibyte fuzzy query is rejected'
fi
printf '\377bad\nalpha\n' > "$TmpDir/input"
printf 'alpha\n' > "$TmpDir/expected"
"$Bin" --fuzzy --filter a < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'invalid multibyte candidates are ignored in fuzzy mode' "$TmpDir/expected" "$TmpDir/actual"

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

# Seeded fuzz-style corpus: randomized record lengths and punctuation, plus queries
# sampled from the generated data. Every result is compared byte-for-byte with grep.
awk 'BEGIN {
    srand(20261010);
    chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./:-[]*? ";
    for (i = 0; i < 5000; i++) {
        n = 8 + int(rand() * 96);
        line = sprintf("fuzz-%05d|", i);
        for (j = 0; j < n; j++) line = line substr(chars, 1 + int(rand() * length(chars)), 1);
        if (i % 19 == 0) line = line "Needle";
        print line;
    }
}' > "$TmpDir/input"
awk 'BEGIN { srand(20261011) }
     NR % 43 == 0 {
         n = 1 + int(rand() * 9);
         start = 1 + int(rand() * (length($0) - n + 1));
         print substr($0, start, n);
     }' "$TmpDir/input" > "$TmpDir/queries"
while IFS= read -r Query; do
    if LC_ALL=C grep -i -F -- "$Query" "$TmpDir/input" > "$TmpDir/expected"; then
        :
    else
        Status=$?
        [ "$Status" -eq 1 ] || Fail "grep oracle failed for seeded query: $Query"
    fi
    "$Bin" --filter "$Query" < "$TmpDir/input" > "$TmpDir/actual"
    CheckEqual "seeded fuzz corpus differential oracle: $Query" "$TmpDir/expected" "$TmpDir/actual"
done < "$TmpDir/queries"

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
"$Bin" --help > "$TmpDir/actual"
if grep -qi '^Usage:' "$TmpDir/actual"; then
    Pass 'help option prints usage'
else
    Fail 'help option must print usage'
fi
CheckRejectedArgs 'unknown option is rejected' --bogus
CheckRejectedArgs 'duplicate filter options are rejected' --filter needle -f other
CheckRejectedArgs 'positional prompt is rejected in batch mode' --filter needle prompt
CheckRejectedArgs 'extra positional argument is rejected' prompt extra
CheckRejectedArgs 'filter-like unknown option is rejected' --filterx needle
CheckRejectedArgs 'format without batch filter is rejected' --format '{}'
CheckRejectedArgs 'format without its argument is rejected' --filter needle --format
CheckRejectedArgs 'format cannot be combined with index output' --filter needle --format '{}' -t
CheckRejectedArgs 'duplicate format options are rejected' --filter needle --format '{}' --format '{}'

# Interactive multi-select emits every selected record in original input order.
printf 'alpha\nbeta\nneedle\ncafé-日本語\n' > "$TmpDir/options"
printf 'alpha\nneedle\n' > "$TmpDir/expected"
if ! { sleep 0.2; printf '\t'; printf '%s' "$(TERM=xterm tput kcud1)"; printf '%s' "$(TERM=xterm tput kcud1)"; printf '\t\n'; } |
     TERM=xterm timeout 5s script -q -e -c "$Bin --multi < '$TmpDir/options' > '$TmpDir/selected'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/stderr"; then
    Fail 'multi-select CLI should exit cleanly'
    cat "$TmpDir/stderr" >&2
else
    CheckEqual 'multi-select CLI emits all toggled records in input order' "$TmpDir/expected" "$TmpDir/selected"
fi
if "$Bin" --multi --filter needle < "$TmpDir/options" > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
    Fail 'multi-select must be rejected with batch filtering'
elif ! grep -Fq 'only available in interactive mode' "$TmpDir/stderr"; then
    Fail 'multi-select/batch conflict must explain the error'
else
    Pass 'multi-select is restricted to interactive mode'
fi
# Initial query is prefilled and custom key aliases can drive interactive selection.
printf 'alpha\nbeta\nneedle\n' > "$TmpDir/options"
printf 'needle\n' > "$TmpDir/expected"
if ! { sleep 0.2; printf '\n'; } |
     TERM=xterm timeout 5s script -q -e -c "$Bin --query needle < '$TmpDir/options' > '$TmpDir/selected'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/stderr"; then
    Fail 'initial-query CLI should exit cleanly'
else
    CheckEqual 'initial query filters and selects the matching record' "$TmpDir/expected" "$TmpDir/selected"
fi
printf 'beta\n' > "$TmpDir/expected"
if ! { sleep 0.2; printf 'jx'; } |
     TERM=xterm timeout 5s script -q -e -c "$Bin --bind-down j --bind-accept x < '$TmpDir/options' > '$TmpDir/selected'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/stderr"; then
    Fail 'custom-key CLI should exit cleanly'
else
    CheckEqual 'custom navigation and accept aliases select the second record' "$TmpDir/expected" "$TmpDir/selected"
fi
printf 'alpha\n' > "$TmpDir/expected"
if ! { sleep 0.2; printf 'v\n'; } |
     TERM=xterm timeout 5s script -q -e -c "$Bin --multi --bind-toggle v < '$TmpDir/options' > '$TmpDir/selected'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/stderr"; then
    Fail 'custom multi-toggle CLI should exit cleanly'
else
    CheckEqual 'custom toggle alias works with multi-selection' "$TmpDir/expected" "$TmpDir/selected"
fi
# Prefilled queries also work in multi-selection mode.
printf 'needle\n' > "$TmpDir/expected"
if ! { sleep 0.2; printf '\t\n'; } |
     TERM=xterm timeout 5s script -q -e -c "$Bin --multi --query needle < '$TmpDir/options' > '$TmpDir/selected'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/stderr"; then
    Fail 'initial-query multi-select CLI should exit cleanly'
else
    CheckEqual 'initial query filters multi-select options' "$TmpDir/expected" "$TmpDir/selected"
fi
LongQuery=$(awk 'BEGIN { for (i = 0; i < 129; i++) printf "x" }')
if "$Bin" --query "$LongQuery" < /dev/null > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
    Fail 'oversized initial query must fail'
elif ! grep -Fq 'exceeds 128 characters' "$TmpDir/stderr"; then
    Fail 'oversized initial query must explain the error'
else
    Pass 'oversized initial query is rejected before input reading'
fi
if "$Bin" --query needle --filter needle < "$TmpDir/options" > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
    Fail 'initial query must be rejected with batch filtering'
elif ! grep -Fq 'cannot be used with --filter' "$TmpDir/stderr"; then
    Fail 'query/batch conflict must explain the error'
else
    Pass 'initial query is restricted to interactive mode'
fi
if "$Bin" --query a --query b < "$TmpDir/options" > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
    Fail 'duplicate initial queries must fail'
elif ! grep -Fq 'may only be specified once' "$TmpDir/stderr"; then
    Fail 'duplicate initial query must explain the error'
else
    Pass 'duplicate initial query is rejected'
fi
if "$Bin" --bind-up j --bind-down j < "$TmpDir/options" > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
    Fail 'a key cannot be assigned to two actions'
elif ! grep -Fq 'assigned to multiple actions' "$TmpDir/stderr"; then
    Fail 'duplicate key assignment must explain the error'
else
    Pass 'conflicting key bindings are rejected'
fi
if "$Bin" --bind-toggle v < "$TmpDir/options" > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
    Fail 'toggle binding must require multi-selection'
elif ! grep -Fq 'requires --multi' "$TmpDir/stderr"; then
    Fail 'toggle binding mode conflict must explain the error'
else
    Pass 'toggle binding requires multi-selection'
fi

# Preview commands receive safely quoted record/index/query placeholders and refresh on navigation.
printf 'alpha\nbeta\n' > "$TmpDir/options"
printf 'beta\n' > "$TmpDir/expected"
if ! { sleep 0.2; printf '%s\n' "$(TERM=xterm tput kcud1)"; } |
     TERM=xterm timeout 5s script -q -e -c "$Bin --preview 'printf \"preview:%s:%s:%s\" {} {index} {query}' --query a < '$TmpDir/options' > '$TmpDir/selected'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/stderr"; then
    Fail 'preview CLI should exit cleanly'
else
    CheckEqual 'preview mode preserves selected-record output' "$TmpDir/expected" "$TmpDir/selected"
    sed -E 's/\x1B\[[0-?]*[ -/]*[@-~]//g; s/\x1B[()][A-Za-z0-9]//g; s/\x1B[=>]//g' "$TmpDir/terminal" > "$TmpDir/terminal-plain"
    if ! grep -aFq 'preview:beta:1:a' "$TmpDir/terminal-plain"; then
        Fail 'preview should show the selected record, original index, and query'
    else
        Pass 'preview refreshes on navigation and expands all placeholders'
    fi
fi
# Data placeholders must be shell-quoted, not evaluated as shell syntax.
InjectionOption=$(printf "quote'\$(touch %s/pwned)" "$TmpDir")
printf '%s\n' "$InjectionOption" > "$TmpDir/options"
if ! { sleep 0.2; printf '\n'; } |
     TERM=xterm timeout 5s script -q -e -c "$Bin --preview 'printf \"record:%s\" {}' < '$TmpDir/options' > '$TmpDir/selected'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/stderr"; then
    Fail 'preview placeholder quoting test should exit cleanly'
elif [ -e "$TmpDir/pwned" ]; then
    Fail 'preview record placeholder must not execute shell syntax'
else
    Pass 'preview record placeholder is shell-quoted'
fi
if "$Bin" --preview 'printf test' --filter test < "$TmpDir/options" > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
    Fail 'preview must be rejected with batch filtering'
elif ! grep -Fq 'cannot be used with --filter' "$TmpDir/stderr"; then
    Fail 'preview/batch conflict must explain the error'
else
    Pass 'preview is restricted to interactive mode'
fi
if "$Bin" --preview one --preview two < "$TmpDir/options" > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
    Fail 'duplicate preview commands must fail'
elif ! grep -Fq 'may only be specified once' "$TmpDir/stderr"; then
    Fail 'duplicate preview command must explain the error'
else
    Pass 'duplicate preview command is rejected'
fi
if "$Bin" --preview '' < "$TmpDir/options" > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
    Fail 'empty preview command must fail'
elif ! grep -Fq 'must not be empty' "$TmpDir/stderr"; then
    Fail 'empty preview command must explain the error'
else
    Pass 'empty preview command is rejected'
fi
if "$Bin" --preview < "$TmpDir/options" > "$TmpDir/actual" 2> "$TmpDir/stderr"; then
    Fail 'missing preview command must fail'
elif ! grep -Fq 'requires a command argument' "$TmpDir/stderr"; then
    Fail 'missing preview command must explain the error'
else
    Pass 'missing preview command is rejected'
fi

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
        if grep -aFq 'prompt' "$TmpDir/terminal"; then
            Pass "$Name preserves the positional prompt"
        else
            Fail "$Name did not display the positional prompt"
        fi
    fi
}
CheckInteractiveIndexMode 'legacy positional t selects index output' prompt t
CheckInteractiveIndexMode 'legacy positional true selects index output' prompt true
CheckInteractiveIndexMode 'flag index mode works before positional prompt' -t prompt
CheckInteractiveIndexMode 'flag index mode works after positional prompt' prompt -t

# CLI cancellation is successful and emits only the documented empty result line.
printf 'alpha\nbeta\n' > "$TmpDir/options"
printf '\n' > "$TmpDir/expected"
if { sleep 0.2; printf '\003'; } | TERM=xterm timeout 5s script -q -e -c "$Bin prompt < '$TmpDir/options' > '$TmpDir/selected'" /dev/null > "$TmpDir/terminal" 2> "$TmpDir/stderr"; then
    CheckEqual 'CLI Ctrl-C cancellation emits an empty result line' "$TmpDir/expected" "$TmpDir/selected"
else
    Fail 'CLI Ctrl-C cancellation should exit successfully'
    cat "$TmpDir/stderr" >&2
fi

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

printf '\377needle\n' > "$TmpDir/input"
cp "$TmpDir/input" "$TmpDir/expected"
"$Bin" --filter needle < "$TmpDir/input" > "$TmpDir/actual"
CheckEqual 'batch mode preserves invalid UTF-8 bytes' "$TmpDir/expected" "$TmpDir/actual"

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
