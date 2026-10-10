/* main.c - standalone mmenu tool (reads options from stdin) */
#define MMENU_IMPLEMENTATION
#include "mmenu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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

static void lines_free(lines_t *l);

static void lines_push(lines_t *l, char *s) {
    if (l->count == l->cap) {
        int new_cap = l->cap ? l->cap * 2 : INITIAL_CAP;
        char **new_lines = realloc(l->lines, (size_t)new_cap * sizeof(char *));
        if (!new_lines) {
            perror("realloc lines");
            lines_free(l);
            exit(EXIT_FAILURE);
        }
        l->lines = new_lines;
        l->cap = new_cap;
    }
    l->lines[l->count++] = s;
}

/* Allocate a new 1MB slab and make it current. */
static void lines_new_slab(lines_t *l) {
    if (l->slabs_count == l->slabs_cap) {
        int new_cap = l->slabs_cap ? l->slabs_cap * 2 : 4;
        char **new_slabs = realloc(l->slabs, (size_t)new_cap * sizeof(char *));
        if (!new_slabs) {
            perror("realloc slabs");
            lines_free(l);
            exit(EXIT_FAILURE);
        }
        l->slabs = new_slabs;
        l->slabs_cap = new_cap;
    }
    char *slab = malloc(CHUNK_CAP);
    if (!slab) {
        perror("malloc slab");
        lines_free(l);
        exit(EXIT_FAILURE);
    }
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
            int new_cap = l->slabs_cap ? l->slabs_cap * 2 : 4;
            char **new_slabs = realloc(l->slabs, (size_t)new_cap * sizeof(char *));
            if (!new_slabs) {
                perror("realloc slabs");
                lines_free(l);
                exit(EXIT_FAILURE);
            }
            l->slabs = new_slabs;
            l->slabs_cap = new_cap;
        }
        char *big = malloc(len + 1);
        if (!big) {
            perror("malloc bigline");
            lines_free(l);
            exit(EXIT_FAILURE);
        }
        memcpy(big, src, len);
        big[len] = '\0';
        l->slabs[l->slabs_count++] = big;
        /* Force the next ordinary record into a fresh CHUNK_CAP slab. */
        l->slab_used = CHUNK_CAP;
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

static void print_usage(FILE *out) {
    fprintf(out,
        "Usage: mmenu [PROMPT] [t|true] [-t]\n"
        "       mmenu [--filter QUERY | --filter=QUERY | -f QUERY] [-t]\n"
        "       mmenu --help\n"
        "\n"
        "Interactive mode reads one option per line. PROMPT is optional. The legacy\n"
        "second positional argument t or true, and the -t flag, print the selected\n"
        "zero-based index. Batch filter mode writes matching records (or indexes with -t).\n");
}

static int argument_error(const char *message) {
    fprintf(stderr, "mmenu: %s\n", message);
    fprintf(stderr, "Try 'mmenu --help' for usage.\n");
    return 2;
}

static int flush_stdout(void) {
    if (fflush(stdout) == EOF) {
        perror("mmenu: writing stdout");
        return EXIT_FAILURE;
    }
    if (ferror(stdout)) {
        errno = EIO;
        perror("mmenu: writing stdout");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
    /* Parse arguments before reading stdin so malformed invocations fail promptly. */
    const char *filter_query = NULL;
    int filter_seen = 0;
    int output_index = 0;
    const char *prompt = "> ";
    int positional_count = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
            print_usage(stdout);
            return flush_stdout();
        }
        if (!strcmp(arg, "--filter") || !strcmp(arg, "-f")) {
            if (filter_seen)
                return argument_error("filter option may only be specified once");
            if (i + 1 >= argc)
                return argument_error("filter requires a query argument");
            filter_query = argv[++i];
            filter_seen = 1;
            continue;
        }
        if (!strncmp(arg, "--filter=", 9)) {
            if (filter_seen)
                return argument_error("filter option may only be specified once");
            filter_query = arg + 9;
            filter_seen = 1;
            continue;
        }
        if (!strcmp(arg, "-t")) {
            output_index = 1;
            continue;
        }
        if (arg[0] == '-' && strcmp(arg, "-") != 0) {
            char message[256];
            snprintf(message, sizeof(message), "unknown option: %s", arg);
            return argument_error(message);
        }
        if (filter_seen)
            return argument_error("positional argument is not valid with batch filtering");
        if (positional_count == 0) {
            prompt = arg;
            positional_count = 1;
        } else if (positional_count == 1 &&
                   (!strcmp(arg, "t") || !strcmp(arg, "true"))) {
            output_index = 1;
            positional_count = 2;
        } else {
            return argument_error("unexpected positional argument");
        }
    }

    if (filter_seen && positional_count > 0)
        return argument_error("positional argument is not valid with batch filtering");

    /* Faster buffered input for huge pipes. */
    setvbuf(stdin, NULL, _IOFBF, 64 * 1024);

    lines_t opts = {0};
    char *buf = NULL;
    size_t buf_cap = 0;
    ssize_t read_len;

    /* getline preserves arbitrarily long records and the final unterminated record. */
    while ((read_len = getline(&buf, &buf_cap, stdin)) != -1) {
        size_t len = (size_t)read_len;
        if (memchr(buf, '\0', len) != NULL) {
            fprintf(stderr, "mmenu: input record contains an embedded NUL byte\n");
            free(buf);
            lines_free(&opts);
            return EXIT_FAILURE;
        }
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
        return flush_stdout();
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
    return flush_stdout();
}
