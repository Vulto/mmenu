/* main.c - standalone mmenu tool (reads options from stdin) */
#define MMENU_IMPLEMENTATION
#include "mmenu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_BUF_SIZE 4096
#define INITIAL_CAP   128

typedef struct {
    char **lines;
    int cap;
    int count;
} lines_t;

static void lines_push(lines_t *l, char *s) {
    if (l->count == l->cap) {
        l->cap = l->cap ? l->cap * 2 : INITIAL_CAP;
        l->lines = realloc(l->lines, l->cap * sizeof(char*));
        if (!l->lines) { perror("realloc"); exit(1); }
    }
    l->lines[l->count++] = s;
}

int main(int argc, char **argv) {
    lines_t opts = {0};
    char buf[LINE_BUF_SIZE];

    while (fgets(buf, sizeof(buf), stdin)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[--len] = '\0';
        else if (len == sizeof(buf)-1) {
            int c;
            while ((c = getchar()) != EOF && c != '\n');
        }
        char *dup = malloc(len + 1);
        if (!dup) continue; /* skip on oom */
        memcpy(dup, buf, len);
        dup[len] = '\0';
        lines_push(&opts, dup);
    }

    const char *prompt = (argc > 1) ? argv[1] : "> ";
    int chosen = mmenu((const char *const *)opts.lines, opts.count, prompt);

    if (chosen == -1) {
        printf("\n");
    } else {
        if (argc > 2 && argv[2] && argv[2][0] == 't') {
            printf("%d\n", chosen);
        } else {
            printf("%s\n", opts.lines[chosen]);
        }
    }

    for (int i = 0; i < opts.count; i++) free(opts.lines[i]);
    free(opts.lines);
    return 0;
}
