# Benchmark and Ranking Baselines

## Matching contract

`mmenu --filter QUERY` performs case-insensitive **contiguous substring** matching and
emits matches in original input order. It does not perform subsequence fuzzy matching or
score/rank candidates. The C API uses the same matcher while narrowing the visible list.
These are compatibility semantics, not a claim that input order is a relevance ranking.

The ranking fixture in `tests/fixtures/ranking-judgments.tsv` is a small, deterministic,
human-judged corpus. Each row records a query ID, query string, relevance grade (0–3), and
candidate. Query groups deliberately include exact-substring cases and fuzzy-subsequence
cases that the current matcher is expected not to find. This makes the semantic difference
observable rather than conflating missing matches with poor ordering.

## Ranking metrics

The ranking baseline records:

- **MRR**: reciprocal rank of the first candidate with relevance grade greater than zero.
- **NDCG@5**: normalized discounted cumulative gain at five, using gain (2^grade - 1).
- **Recall@5**: relevant candidates retrieved in the first five results divided by all
  relevant candidates in that query's judgments.
- **Matched count**, candidate count, tool version, elapsed nanoseconds, and peak RSS in KiB.

The golden file `tests/fixtures/ranking-mmenu.expected.csv` locks the current `mmenu`
metrics. Changes require reviewing the changed output and relevance judgments; never
update the golden file merely to make a failing test pass. `fzf`, `skim` (`sk`), and
`fzy` are evaluated on the same candidate order and judgments. Their fuzzy matching and
scoring semantics differ, so their metrics are a comparative reference, not an assertion
that the tools implement the same contract.

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
