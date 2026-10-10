BEGIN { FS = "\t" }
FILENAME == ARGV[1] {
    if ($1 == query_id) {
        option = $4
        grade[option] = $3 + 0
        candidates++
        if (($3 + 0) > 0) {
            relevant++
            gradeCount[$3 + 0]++
        }
    }
    next
}
FILENAME == ARGV[2] {
    position++
    score = grade[$0] + 0
    if (score > 0) {
        if (firstRelevant == 0) firstRelevant = position
        if (position <= 5) relevantAt5++
    }
    if (position <= 5 && score > 0)
        dcg += ((2 ^ score) - 1) / (log(position + 1) / log(2))
    next
}
END {
    idealPosition = 1
    for (score = 3; score >= 1; score--) {
        for (i = 0; i < gradeCount[score] && idealPosition <= 5; i++) {
            idcg += ((2 ^ score) - 1) / (log(idealPosition + 1) / log(2))
            idealPosition++
        }
    }
    mrr = firstRelevant ? 1 / firstRelevant : 0
    ndcg = idcg > 0 ? dcg / idcg : 0
    recall = relevant > 0 ? relevantAt5 / relevant : 0
    printf "%s,%s,%d,%d,%d,%.4f,%.4f,%.4f\n", tool, query_id, candidates, position, relevant, mrr, ndcg, recall
}
