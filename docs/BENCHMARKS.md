# Benchmark and Ranking Baselines

## Matching contract

`mmenu --filter QUERY` performs ASCII-case-insensitive **contiguous substring** matching and
emits matches in original input order. The default C API behaves the same. Opt-in
`--fuzzy --filter QUERY` and `mmenuEx(..., MMENU_FUZZY)` use ASCII-case-insensitive
subsequence matching, rewarding early, adjacent, and word-boundary matches and sorting by
score with stable input-order ties. The fuzzy score is a practical heuristic, not a learned
relevance model. Interactive fuzzy mode underlines and bolds the greedily matched
subsequence characters.

The ranking fixture in `tests/fixtures/ranking-judgments.tsv` is a small, deterministic,
synthetic path-like corpus with manually assigned relevance grades. It is an initial
regression fixture, not a representative user study. Each row records a query ID, query
string, relevance grade (0–3), and candidate. Query groups deliberately include
exact-substring cases and fuzzy-subsequence cases that the current matcher is expected not
to find. This makes the semantic difference observable rather than conflating missing
matches with poor ordering.

## Ranking metrics

The ranking baseline records:

- **MRR**: reciprocal rank of the first candidate with relevance grade greater than zero.
- **NDCG@5**: normalized discounted cumulative gain at five, using gain (2^grade - 1).
- **Recall@5**: relevant candidates retrieved in the first five results divided by all
  relevant candidates in that query's judgments.
- **Matched count**, candidate count, tool version, elapsed nanoseconds, and peak RSS in KiB.

The golden files `tests/fixtures/ranking-mmenu.expected.csv` and
`tests/fixtures/ranking-mmenu-fuzzy.expected.csv` lock the default and fuzzy `mmenu`
metrics. Changes require reviewing the changed output and relevance judgments; never
update the golden file merely to make a failing test pass. `fzf`, `skim` (`sk`), and
`fzy` are evaluated on the same candidate order and judgments. Their fuzzy matching and
scoring semantics differ, so their metrics are a comparative reference, not an assertion
that the tools implement the same contract.


## Initial CI snapshot

The first successful reference run used `mmenu` from the PR build, fzf 0.44.1,
skim 5.7.4, and fzy 1.0. Mean metrics across the four fixed query groups were:

| Tool | Mean MRR | Mean NDCG@5 | Mean Recall@5 |
|---|---:|---:|---:|
| mmenu (substring, input order) | 0.5000 | 0.2083 | 0.2708 |
| mmenu (fuzzy, stable ties) | 1.0000 | 0.8878 | 0.8750 |
| fzf 0.44.1 | 1.0000 | 0.8878 | 0.8750 |
| skim 5.7.4 | 1.0000 | 0.8878 | 0.8750 |
| fzy 1.0 | 1.0000 | 0.8529 | 0.8750 |

This small, deliberately diagnostic corpus includes queries such as `mmh` and `tst`
that require subsequence matching. Their zero scores for mmenu are expected under its
documented contiguous-substring contract; the table is evidence of a semantic difference,
not a claim that these four queries represent all user searches.

| Query group | Tool | MRR | NDCG@5 | Recall@5 |
|---|---|---:|---:|---:|
| menu | mmenu | 1.0000 | 0.6084 | 0.8333 |
| menu | mmenu-fuzzy / fzf / skim | 1.0000 | 0.8273 | 0.8333 |
| menu | fzy | 1.0000 | 0.8273 | 0.8333 |
| mmh | mmenu | 0.0000 | 0.0000 | 0.0000 |
| mmh | mmenu-fuzzy / fzf / skim / fzy | 1.0000 | 0.8839 | 0.6667 |
| tst | mmenu | 0.0000 | 0.0000 | 0.0000 |
| tst | mmenu-fuzzy / fzf / skim | 1.0000 | 0.9896 | 1.0000 |
| tst | fzy | 1.0000 | 0.8502 | 1.0000 |
| cfg | mmenu | 1.0000 | 0.2248 | 0.2500 |
| cfg | mmenu-fuzzy / fzf / skim / fzy | 1.0000 | 0.8502 | 1.0000 |

The reference versions in that run were fzf 0.44.1, skim 5.7.4, and fzy 1.0. The tiny
ranking benchmark also records elapsed time and RSS per invocation, but those measurements
are dominated by process startup; use the large-corpus throughput harness for performance
comparisons.

In CI run [38059037822](https://github.com/Vulto/mmenu/actions/runs/38059037822), the
base-versus-candidate median latency ratios were 0.832x at 100k records and 0.812x at 1m
records. Peak RSS ratios rounded to 1.000x at both sizes. The detailed run artifact retains
the actual per-sample times, memory values, tool versions, and host metadata. These timing
numbers describe that runner only and are not portable performance guarantees.

## Throughput and memory

`tests/benchmark.sh` measures 1k, 10k, 100k, and 1m records with five samples per size.
It retains every elapsed-time sample, a min/median/max summary, and peak resident set size
(RSS) measured by GNU `time` in KiB. Metadata records the revision, host, compiler, flags,
and sample count.

`tests/performance-regression.sh` compares the pull-request base and candidate on the same
runner, input, compiler flags, and five-sample procedure at 100k and 1m records. It validates
both outputs before measuring and fails only when median latency or peak RSS exceeds twice
the baseline. This intentionally coarse threshold catches severe regressions; it is not an
absolute speed or memory promise.

CI uploads the raw samples, summaries, ranking metrics, and metadata as artifacts. Timing
and RSS values are machine-specific; compare runs only with their recorded environment.
