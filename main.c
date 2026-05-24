/* main.c - standalone mmenu tool (reads options from stdin) */
#define MMENU_IMPLEMENTATION
#include "mmenu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_BUF_SIZE 4096
#define CHUNK_CAP (1024 * 1024)   /* 1 MiB slabs for string data - huge reduction in mallocs */

typedef struct {
    char **lines;     /* pointers into slabs (or final compact) */
    int cap;
    int count;

    /* Chunked string arena: O(total/CHUNK) mallocs instead of O(N) */
    char **slabs;     /* the raw data blocks */
    int slabs_cap;
    int slabs_count;
    size_t slab_used; /* used bytes in the current (last) slab */
} lines_t;

static void lines_push(lines_t *l, char *s) {
    if (l->count == l->cap) {
        l->cap = l->cap ? l->cap * 2 : INITIAL_CAP;
        l->lines = realloc(l->lines, l->cap * sizeof(char*));
        if (!l->lines) { perror("realloc"); exit(1); }
    }
    l->lines[l->count++] = s;
}

/* Allocate a new 1MB slab and make it current. */
static void lines_new_slab(lines_t *l) {
    if (l->slabs_count == l->slabs_cap) {
        l->slabs_cap = l->slabs_cap ? l->slabs_cap * 2 : 4;
        l->slabs = realloc(l->slabs, l->slabs_cap * sizeof(char*));
        if (!l->slabs) { perror("realloc slabs"); exit(1); }
    }
    char *slab = malloc(CHUNK_CAP);
    if (!slab) { perror("malloc slab"); exit(1); }
    l->slabs[l->slabs_count++] = slab;
    l->slab_used = 0;
}

/* Append a nul-terminated string into the arena and return pointer inside it.
   Never invalidates previous pointers (we only append to current slab or start new). */
static char *lines_arena_dup(lines_t *l, const char *src, size_t len) {
    if (len + 1 > CHUNK_CAP) {
        /* Rare: huge single line >1MB. Give it its own slab. */
        if (l->slabs_count == l->slabs_cap) {
            l->slabs_cap = l->slabs_cap ? l->slabs_cap * 2 : 4;
            l->slabs = realloc(l->slabs, l->slabs_cap * sizeof(char*));
            if (!l->slabs) { perror("realloc slabs"); exit(1); }
        }
        char *big = malloc(len + 1);
        if (!big) { perror("malloc bigline"); exit(1); }
        memcpy(big, src, len);
        big[len] = '\0';
        l->slabs[l->slabs_count++] = big;
        return big;
    }

    if (!l->slabs_count || l->slab_used + len + 1 > CHUNK_CAP) {
        lines_new_slab(l);
    }
    char *dst = l->slabs[l->slabs_count - 1] + l->slab_used;
    memcpy(dst, src, len);
    dst[len] = '\0';
    l->slab_used += len + 1;
    return dst;
}

int main(int argc, char **argv) {
    /* Faster buffered input for huge pipes */
    setvbuf(stdin, NULL, _IOFBF, 64 * 1024);

    lines_t opts = {0};
    char buf[LINE_BUF_SIZE];

    while (fgets(buf, sizeof(buf), stdin)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[--len] = '\0';
        else if (len == sizeof(buf)-1) {
            int c;
            while ((c = getchar()) != EOF && c != '\n');
        }
        /* Store in chunked arena instead of per-line malloc */
        char *stored = lines_arena_dup(&opts, buf, len);
        lines_push(&opts, stored);
    }

    /* Non-interactive fast path: mmenu --filter "query"  (or -f)
       Outputs matching lines (or indices with -t). Perfect for scripting
       and for isolated matcher benchmarks. No ncurses, very fast. */
    const char *filter_query = NULL;
    int output_index = 0;
    const char *prompt = "> ";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--filter") || !strcmp(argv[i], "-f")) {
            if (i + 1 < argc) filter_query = argv[++i];
        } else if (!strcmp(argv[i], "-t")) {
            output_index = 1;
        } else if (i == 1 && !filter_query) {
            prompt = argv[i];   /* backward compat for positional prompt */
        }
    }

    if (filter_query) {
        for (int i = 0; i < opts.count; i++) {
            if (strcasestr(opts.lines[i], filter_query)) {
                if (output_index) printf("%d\n", i);
                else printf("%s\n", opts.lines[i]);
            }
        }
        /* cleanup and exit */
        free(opts.lines);
        for (int i = 0; i < opts.slabs_count; i++) free(opts.slabs[i]);
        free(opts.slabs);
        return 0;
    }

    /* Interactive path (prompt already set by the arg loop above) */
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

    /* Free the O(1) pointer array + the O(total_size / CHUNK) slabs.
       Massively fewer frees than before (N individual string frees). */
    free(opts.lines);
    for (int i = 0; i < opts.slabs_count; i++) free(opts.slabs[i]);
    free(opts.slabs);
    return 0;
}
