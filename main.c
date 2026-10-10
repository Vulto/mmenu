/* main.c - standalone mmenu tool (reads options from stdin) */
#define MMENU_IMPLEMENTATION
#include "mmenu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        l->lines = realloc(l->lines, (size_t)l->cap * sizeof(char *));
        if (!l->lines) { perror("realloc"); exit(EXIT_FAILURE); }
    }
    l->lines[l->count++] = s;
}

/* Allocate a new 1MB slab and make it current. */
static void lines_new_slab(lines_t *l) {
    if (l->slabs_count == l->slabs_cap) {
        l->slabs_cap = l->slabs_cap ? l->slabs_cap * 2 : 4;
        l->slabs = realloc(l->slabs, (size_t)l->slabs_cap * sizeof(char *));
        if (!l->slabs) { perror("realloc slabs"); exit(EXIT_FAILURE); }
    }
    char *slab = malloc(CHUNK_CAP);
    if (!slab) { perror("malloc slab"); exit(EXIT_FAILURE); }
    l->slabs[l->slabs_count++] = slab;
    l->slab_used = 0;
}

/* Append a nul-terminated string into the arena and return pointer inside it.
   Never invalidates previous pointers (we only append to current slab or start new). */
static char *lines_arena_dup(lines_t *l, const char *src, size_t len) {
    if (len == SIZE_MAX) {
        fprintf(stderr, "mmenu: input record is too large\n");
        exit(EXIT_FAILURE);
    }
    if (len + 1 > CHUNK_CAP) {
        /* Rare: huge single line >1MB. Give it its own slab. */
        if (l->slabs_count == l->slabs_cap) {
            l->slabs_cap = l->slabs_cap ? l->slabs_cap * 2 : 4;
            l->slabs = realloc(l->slabs, (size_t)l->slabs_cap * sizeof(char *));
            if (!l->slabs) { perror("realloc slabs"); exit(EXIT_FAILURE); }
        }
        char *big = malloc(len + 1);
        if (!big) { perror("malloc bigline"); exit(EXIT_FAILURE); }
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

static void lines_free(lines_t *l) {
    free(l->lines);
    for (int i = 0; i < l->slabs_count; i++) free(l->slabs[i]);
    free(l->slabs);
}

int main(int argc, char **argv) {
    /* Parse arguments before reading stdin so malformed invocations fail promptly. */
    const char *filter_query = NULL;
    int output_index = 0;
    const char *prompt = "> ";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--filter") || !strcmp(argv[i], "-f")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "mmenu: --filter requires a query argument\n");
                return 2;
            }
            filter_query = argv[++i];
        } else if (!strcmp(argv[i], "-t")) {
            output_index = 1;
        } else if (i == 1 && !filter_query) {
            prompt = argv[i];   /* backward compat for positional prompt */
        }
    }

    /* Faster buffered input for huge pipes. */
    setvbuf(stdin, NULL, _IOFBF, 64 * 1024);

    lines_t opts = {0};
    char *buf = NULL;
    size_t buf_cap = 0;
    ssize_t read_len;

    /* getline preserves arbitrarily long records and the final unterminated record. */
    while ((read_len = getline(&buf, &buf_cap, stdin)) != -1) {
        size_t len = (size_t)read_len;
        if (len > 0 && buf[len - 1] == '\n') len--;
        char *stored = lines_arena_dup(&opts, buf, len);
        lines_push(&opts, stored);
    }
    free(buf);
    if (ferror(stdin)) {
        perror("mmenu: reading stdin");
        lines_free(&opts);
        return EXIT_FAILURE;
    }

    /* Non-interactive fast path: outputs matching lines or original indexes. */
    if (filter_query != NULL) {
        for (int i = 0; i < opts.count; i++) {
            if (strcasestr(opts.lines[i], filter_query)) {
                if (output_index) printf("%d\n", i);
                else printf("%s\n", opts.lines[i]);
            }
        }
        lines_free(&opts);
        return ferror(stdout) ? EXIT_FAILURE : 0;
    }

    int chosen = mmenu((const char *const *)opts.lines, opts.count, prompt);

    if (chosen == -1) {
        printf("\n");
    } else if (output_index) {
        printf("%d\n", chosen);
    } else {
        printf("%s\n", opts.lines[chosen]);
    }

    lines_free(&opts);
    return ferror(stdout) ? EXIT_FAILURE : 0;
}
