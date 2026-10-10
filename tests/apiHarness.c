#define _GNU_SOURCE
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MMENU_IMPLEMENTATION
#include "../mmenu.h"

int main(int argc, char **argv)
{
    const char *options[] = {"alpha", "beta", "needle"};
    const char *null_entry_options[] = {"alpha", NULL, "needle"};
    int count = 3;
    const char *prompt = "test> ";

    if (argc > 1 && strcmp(argv[1], "empty") == 0) {
        count = 0;
    } else if (argc > 1 && strcmp(argv[1], "null-prompt") == 0) {
        prompt = NULL;
    } else if (argc > 1 && strcmp(argv[1], "invalid-null-options") == 0) {
        if (mmenu(NULL, 1, prompt) != -1) return 1;
        puts("MMENU_API_RESULT=invalid-rejected");
        return 0;
    } else if (argc > 1 && strcmp(argv[1], "invalid-negative-count") == 0) {
        if (mmenu(options, -1, prompt) != -1) return 1;
        puts("MMENU_API_RESULT=invalid-rejected");
        return 0;
    } else if (argc > 1 && strcmp(argv[1], "invalid-null-entry") == 0) {
        if (mmenu(null_entry_options, 3, prompt) != -1) return 1;
        puts("MMENU_API_RESULT=invalid-rejected");
        return 0;
    }

    int result = mmenu(options, count, prompt);
    printf("\nMMENU_RESULT=%d\n", result);
    return 0;
}
