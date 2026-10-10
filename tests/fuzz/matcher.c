#define _DEFAULT_SOURCE
#define MMENU_IMPLEMENTATION
#include "../../mmenu.h"

#include <stdint.h>

#define FUZZ_INPUT_LIMIT 8192
#define FUZZ_OPTION_LIMIT 64

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;
    if (!setlocale(LC_ALL, "C.UTF-8"))
        setlocale(LC_ALL, "");
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char storage[FUZZ_INPUT_LIMIT + 1];
    wchar_t query[FUZZ_INPUT_LIMIT + 1];
    const char *options[FUZZ_OPTION_LIMIT];
    size_t length = size < FUZZ_INPUT_LIMIT ? size : FUZZ_INPUT_LIMIT;

    if (length) memcpy(storage, data, length);
    storage[length] = '\0';

    char *query_text = storage;
    char *separator = memchr(storage, '\n', length);
    char *candidate_text;

    if (separator) {
        *separator = '\0';
        candidate_text = separator + 1;
    } else {
        size_t split = length / 2;
        storage[split] = '\0';
        candidate_text = storage + split + 1;
    }

    size_t query_length = mbstowcs(query, query_text,
                                   sizeof(query) / sizeof(query[0]));
    if (query_length == (size_t)-1 ||
        query_length >= sizeof(query) / sizeof(query[0]))
        return 0;
    query[query_length] = L'\0';

    int option_count = 0;
    char *cursor = candidate_text;
    char *end = storage + length;
    while (cursor <= end && option_count < FUZZ_OPTION_LIMIT) {
        options[option_count++] = cursor;
        char *newline = memchr(cursor, '\n', (size_t)(end - cursor));
        if (!newline) break;
        *newline = '\0';
        cursor = newline + 1;
    }
    if (option_count == 0) options[option_count++] = "";

    FuzzyMatch *matches = NULL;
    int match_count = 0;
    if (!FuzzyCollect(options, option_count, query, &matches, &match_count))
        abort();

    for (int i = 0; i < match_count; i++) {
        if (matches[i].index < 0 || matches[i].index >= option_count)
            abort();

        long long verified_score = 0;
        if (!FuzzyScore(options[matches[i].index], query, &verified_score) ||
            verified_score != matches[i].score)
            abort();

        if (i > 0 &&
            (matches[i - 1].score < matches[i].score ||
             (matches[i - 1].score == matches[i].score &&
              matches[i - 1].index >= matches[i].index)))
            abort();

        for (int j = 0; j < i; j++)
            if (matches[j].index == matches[i].index)
                abort();
    }

    free(matches);
    return 0;
}
