/* main.c - standalone mmenu tool (reads options from stdin) */
#define MMENU_IMPLEMENTATION
#include "mmenu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include <poll.h>

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
        int new_cap;
        if (!mmenu_next_capacity(l->cap, INITIAL_CAP, sizeof(*l->lines), &new_cap)) {
            perror("mmenu: input line capacity");
            lines_free(l);
            exit(EXIT_FAILURE);
        }
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
        int new_cap;
        if (!mmenu_next_capacity(l->slabs_cap, 4, sizeof(*l->slabs), &new_cap)) {
            perror("mmenu: input slab capacity");
            lines_free(l);
            exit(EXIT_FAILURE);
        }
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
            int new_cap;
            if (!mmenu_next_capacity(l->slabs_cap, 4, sizeof(*l->slabs), &new_cap)) {
                perror("mmenu: input slab capacity");
                lines_free(l);
                exit(EXIT_FAILURE);
            }
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

/* Nonblocking stdin source. The reusable record buffer grows only for unusually
   long records; completed records are copied into the existing arena. */
typedef struct {
    lines_t *lines;
    int fd;
    int delimiter;
    unsigned char input[8192];
    size_t input_pos;
    size_t input_len;
    char *record;
    size_t record_len;
    size_t record_cap;
    int eof;
    int error;
} input_stream_t;

static int stream_record_reserve(input_stream_t *stream, size_t needed) {
    if (needed <= stream->record_cap) return 1;
    size_t capacity = stream->record_cap ? stream->record_cap : 4096;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) {
            errno = ENOMEM;
            return 0;
        }
        capacity *= 2;
    }
    char *record = realloc(stream->record, capacity);
    if (!record) {
        if (errno == 0) errno = ENOMEM;
        return 0;
    }
    stream->record = record;
    stream->record_cap = capacity;
    return 1;
}

static int stream_record_append(input_stream_t *stream,
                                const unsigned char *bytes, size_t length) {
    if (stream->delimiter != '\0' && length && memchr(bytes, '\0', length)) {
        errno = EILSEQ;
        return 0;
    }
    if (length > SIZE_MAX - stream->record_len) {
        errno = ENOMEM;
        return 0;
    }
    size_t needed = stream->record_len + length;
    if (!stream_record_reserve(stream, needed)) return 0;
    if (length) memcpy(stream->record + stream->record_len, bytes, length);
    stream->record_len = needed;
    return 1;
}

static int stream_emit_record(input_stream_t *stream, const char **record) {
    if (stream->record_len == SIZE_MAX ||
        !stream_record_reserve(stream, stream->record_len + 1)) {
        stream->error = 1;
        return MMENU_STREAM_ERROR;
    }
    stream->record[stream->record_len] = '\0';
    if (mbstowcs(NULL, stream->record, 0) == (size_t)-1) {
        stream->error = 1;
        errno = EILSEQ;
        return MMENU_STREAM_ERROR;
    }
    char *stored = lines_arena_dup(stream->lines, stream->record, stream->record_len);
    lines_push(stream->lines, stored);
    stream->record_len = 0;
    *record = stored;
    return MMENU_STREAM_RECORD;
}

static int lines_stream_next(void *userdata, const char **record) {
    input_stream_t *stream = userdata;
    if (!stream || !record) {
        errno = EINVAL;
        return MMENU_STREAM_ERROR;
    }
    *record = NULL;
    for (;;) {
        if (stream->input_pos < stream->input_len) {
            const unsigned char *begin = stream->input + stream->input_pos;
            size_t available = stream->input_len - stream->input_pos;
            const unsigned char *delimiter =
                memchr(begin, stream->delimiter, available);
            size_t amount = delimiter ? (size_t)(delimiter - begin) : available;
            if (!stream_record_append(stream, begin, amount)) {
                stream->error = 1;
                return MMENU_STREAM_ERROR;
            }
            stream->input_pos += amount;
            if (delimiter) {
                stream->input_pos++;
                return stream_emit_record(stream, record);
            }
            continue;
        }
        if (stream->eof) {
            if (stream->record_len > 0)
                return stream_emit_record(stream, record);
            return MMENU_STREAM_EOF;
        }
        struct pollfd descriptor = {.fd = stream->fd, .events = POLLIN};
        int ready = poll(&descriptor, 1, 0);
        if (ready == 0) return MMENU_STREAM_PENDING;
        if (ready < 0) {
            if (errno == EINTR) return MMENU_STREAM_PENDING;
            stream->error = 1;
            return MMENU_STREAM_ERROR;
        }
        if (descriptor.revents & POLLNVAL) {
            errno = EBADF;
            stream->error = 1;
            return MMENU_STREAM_ERROR;
        }
        ssize_t amount = read(stream->fd, stream->input, sizeof(stream->input));
        if (amount > 0) {
            stream->input_pos = 0;
            stream->input_len = (size_t)amount;
        } else if (amount == 0) {
            stream->eof = 1;
        } else if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return MMENU_STREAM_PENDING;
        } else {
            stream->error = 1;
            return MMENU_STREAM_ERROR;
        }
    }
}


static int parse_binding_key(const char *value, wint_t *key) {
    if (!strcmp(value, "space")) {
        *key = L' ';
        return 1;
    }
    if (!value[0] || value[1] || (unsigned char)value[0] < 0x20 ||
        (unsigned char)value[0] > 0x7e) return 0;
    *key = (unsigned char)value[0];
    return 1;
}

static int parse_binding_option(int argc, char **argv, int *arg_index,
                                const char *arg, const char *name,
                                wint_t *key, int *seen) {
    size_t name_len = strlen(name);
    const char *value;
    if (!strcmp(arg, name)) {
        if (*arg_index + 1 >= argc) return -1;
        value = argv[++(*arg_index)];
    } else if (!strncmp(arg, name, name_len) && arg[name_len] == '=') {
        value = arg + name_len + 1;
    } else return 0;
    if (*seen || !parse_binding_key(value, key)) return -1;
    *seen = 1;
    return 1;
}

static size_t preview_quoted_length(const char *value) {
    size_t length = 2;
    for (const char *p = value; *p; p++) {
        size_t extra = *p == '\'' ? 4u : 1u;
        if (length > SIZE_MAX - extra) return SIZE_MAX;
        length += extra;
    }
    return length;
}

static int preview_add_size(size_t *total, size_t amount) {
    if (*total > SIZE_MAX - amount) return 0;
    *total += amount;
    return 1;
}

static int preview_template_size(const char *format, const char *record,
                                 const char *index, const char *query,
                                 size_t *required) {
    size_t total = 0;
    for (const char *p = format; *p;) {
        const char *value = NULL;
        size_t token_len = 0;
        if (p[0] == '{' && p[1] == '}') {
            value = record;
            token_len = 2;
        } else if (!strncmp(p, "{index}", 7)) {
            value = index;
            token_len = 7;
        } else if (!strncmp(p, "{query}", 7)) {
            value = query;
            token_len = 7;
        }
        size_t amount = value ? preview_quoted_length(value) : 1;
        if (amount == SIZE_MAX || !preview_add_size(&total, amount)) return 0;
        p += value ? token_len : 1;
    }
    *required = total;
    return 1;
}

/* Every data placeholder is shell-quoted; only the explicit command is shell code. */
static char *preview_expand_template(const char *format, const char *record,
                                     const char *index, const char *query) {
    size_t required;
    if (!preview_template_size(format, record, index, query, &required) ||
        required == SIZE_MAX) {
        errno = ENOMEM;
        return NULL;
    }
    char *expanded = malloc(required + 1);
    if (!expanded) return NULL;
    char *out = expanded;
    for (const char *p = format; *p;) {
        const char *value = NULL;
        size_t token_len = 0;
        if (p[0] == '{' && p[1] == '}') {
            value = record;
            token_len = 2;
        } else if (!strncmp(p, "{index}", 7)) {
            value = index;
            token_len = 7;
        } else if (!strncmp(p, "{query}", 7)) {
            value = query;
            token_len = 7;
        }
        if (!value) {
            *out++ = *p++;
            continue;
        }
        *out++ = '\'';
        for (const char *v = value; *v; v++) {
            if (*v == '\'') {
                *out++ = '\'';
                *out++ = '\\';
                *out++ = '\'';
                *out++ = '\'';
            } else {
                *out++ = *v;
            }
        }
        *out++ = '\'';
        p += token_len;
    }
    *out = '\0';
    return expanded;
}

static int run_preview_command(const char *record, int index,
                               const wchar_t *query, char *output,
                               size_t output_size, void *userdata) {
    const char *format = userdata;
    if (!output || output_size == 0 || !format) return -1;
    output[0] = '\0';

    errno = 0;
    char *query_mb = wc_to_mb(query ? query : L"");
    if (!query_mb) {
        snprintf(output, output_size, "Preview: could not convert query");
        return -1;
    }
    char index_text[32];
    snprintf(index_text, sizeof(index_text), "%d", index);
    errno = 0;
    char *expanded = preview_expand_template(format, record, index_text, query_mb);
    free(query_mb);
    if (!expanded) {
        snprintf(output, output_size, "Preview: out of memory");
        return -1;
    }

    size_t command_len = strlen(expanded);
    if (command_len > SIZE_MAX - 11) {
        free(expanded);
        snprintf(output, output_size, "Preview: command is too large");
        return -1;
    }
    char *shell_command = malloc(command_len + 11);
    if (!shell_command) {
        free(expanded);
        snprintf(output, output_size, "Preview: out of memory");
        return -1;
    }
    snprintf(shell_command, command_len + 11, "exec 2>&1\n%s", expanded);
    free(expanded);

    FILE *pipe = popen(shell_command, "r");
    free(shell_command);
    if (!pipe) {
        snprintf(output, output_size, "Preview: could not start command: %s", strerror(errno));
        return -1;
    }

    size_t used = 0;
    char chunk[512];
    size_t nread;
    while ((nread = fread(chunk, 1, sizeof(chunk), pipe)) > 0) {
        for (size_t i = 0; i < nread && used + 1 < output_size; i++)
            output[used++] = chunk[i] == '\0' ? '?' : chunk[i];
        /* Continue draining after the display buffer fills to avoid blocking the child. */
    }
    output[used] = '\0';
    int status = pclose(pipe);
    if (used == 0) {
        if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            snprintf(output, output_size, "Preview command failed");
        else
            snprintf(output, output_size, "(no output)");
    } else if ((status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) &&
               used + 1 < output_size) {
        const char *failure = "\n[preview command exited unsuccessfully]";
        size_t remaining = output_size - used - 1;
        strncat(output, failure, remaining);
    }
    return status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0 ? -1 : 0;
}

static void print_usage(FILE *out) {
    fprintf(out,
        "Usage: mmenu [PROMPT] [t|true] [-t] [--multi] [--query QUERY] [--preview COMMAND]\n"
        "       mmenu [--fuzzy | --exact] [--extended] [PROMPT] [-t] [--multi] [--query QUERY] [--preview COMMAND]\n"
        "       mmenu [--bind-ACTION KEY] ...\n"
        "       mmenu [--fuzzy | --exact] [--invert-match] --filter QUERY [-t] [--print0]\n"
        "       mmenu [--filter QUERY | --filter=QUERY | -f QUERY] [-t | --format FORMAT] [--read0] [--print0]\n"
        "       mmenu --help | -h\n"
        "\n"
        "Interactive mode reads one option per line. --query pre-fills the query.\n"
        "--preview COMMAND displays command output for the highlighted option; {}, {index},\n"
        "and {query} are shell-quoted placeholders. Preview commands execute in the shell.\n"
        "--bind-up/down/accept/cancel/toggle/backspace KEY adds a printable ASCII key alias.\n"
        "Use --multi for Tab toggling; Enter emits selected records (or indexes with -t).\n"
        "The legacy second positional argument t or true, and -t, print zero-based indexes.\n"
        "Batch filter mode writes matching records (or indexes with -t).\n"
        "--read0 reads NUL-delimited records; --print0 terminates output records with NUL.\n"
        "--fuzzy enables ranked subsequence matching; --exact matches whole records.\n"
        "--extended enables AND/OR, inverse, exact-substring, prefix (^), and suffix ($) terms.\n"
        "--invert-match or -v emits records that do not match the batch query.\n"
        "--format FORMAT expands {}, {index}, and {query} for each batch result.\n");
}

static int argument_error(const char *message) {
    fprintf(stderr, "mmenu: %s\n", message);
    fprintf(stderr, "Try 'mmenu --help' for usage.\n");
    return 2;
}

static void print_record(const char *record, int nul_delimiter) {
    if (record) fwrite(record, 1, strlen(record), stdout);
    fputc(nul_delimiter ? '\0' : '\n', stdout);
}

static void print_match(const char *record, int index, int output_index,
                        int nul_delimiter) {
    if (output_index) {
        char index_text[32];
        snprintf(index_text, sizeof(index_text), "%d", index);
        print_record(index_text, nul_delimiter);
    } else {
        print_record(record, nul_delimiter);
    }
}

/* Batch templates support {}, {index}, and {query}; unknown tokens stay literal. */
static void print_formatted_match(const char *record, int index,
                                 const char *format, const char *query,
                                 int nul_delimiter) {
    const char *p = format;
    char index_text[32];
    snprintf(index_text, sizeof(index_text), "%d", index);
    while (*p) {
        const char *value = NULL;
        size_t token_len = 0;
        if (p[0] == '{' && p[1] == '}') {
            value = record;
            token_len = 2;
        } else if (!strncmp(p, "{index}", 7)) {
            value = index_text;
            token_len = 7;
        } else if (!strncmp(p, "{query}", 7)) {
            value = query;
            token_len = 7;
        }
        if (value) {
            size_t len = strlen(value);
            if (fwrite(value, 1, len, stdout) != len) return;
            p += token_len;
        } else {
            if (fputc((unsigned char)*p, stdout) == EOF) return;
            p++;
        }
    }
    fputc(nul_delimiter ? '\0' : '\n', stdout);
}

static void print_batch_match(const char *record, int index, int output_index,
                              int nul_delimiter, const char *format,
                              const char *query) {
    if (format) print_formatted_match(record, index, format, query, nul_delimiter);
    else print_match(record, index, output_index, nul_delimiter);
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
    int fuzzy_mode = 0;
    int exact_mode = 0;
    int extended_mode = 0;
    int invert_mode = 0;
    int no_sort = 0;
    int input_nul = 0;
    int output_nul = 0;
    int output_index = 0;
    int multi_mode = 0;
    const char *initial_query = NULL;
    int initial_query_seen = 0;
    const char *preview_command = NULL;
    int preview_seen = 0;
    mmenu_config menu_config = {0};
    int bind_up_seen = 0, bind_down_seen = 0, bind_accept_seen = 0;
    int bind_cancel_seen = 0, bind_toggle_seen = 0, bind_backspace_seen = 0;
    const char *output_format = NULL;
    const char *prompt = "> ";
    int positional_count = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
            print_usage(stdout);
            return flush_stdout();
        }
        if (!strcmp(arg, "--fuzzy")) {
            fuzzy_mode = 1;
            continue;
        }
        if (!strcmp(arg, "--exact")) {
            exact_mode = 1;
            continue;
        }
        if (!strcmp(arg, "--extended") || !strcmp(arg, "-x")) {
            extended_mode = 1;
            continue;
        }
        if (!strcmp(arg, "--no-extended") || !strcmp(arg, "+x")) {
            extended_mode = 0;
            continue;
        }
        if (!strcmp(arg, "--invert-match") || !strcmp(arg, "-v")) {
            invert_mode = 1;
            continue;
        }
        if (!strcmp(arg, "--no-sort")) {
            no_sort = 1;
            continue;
        }
        if (!strcmp(arg, "--read0")) {
            input_nul = 1;
            continue;
        }
        if (!strcmp(arg, "--print0")) {
            output_nul = 1;
            continue;
        }
        if (!strcmp(arg, "--multi")) {
            multi_mode = 1;
            continue;
        }
        if (!strcmp(arg, "--query")) {
            if (initial_query_seen) return argument_error("--query may only be specified once");
            if (i + 1 >= argc) return argument_error("--query requires a query argument");
            initial_query = argv[++i];
            initial_query_seen = 1;
            continue;
        }
        if (!strncmp(arg, "--query=", 8)) {
            if (initial_query_seen) return argument_error("--query may only be specified once");
            initial_query = arg + 8;
            initial_query_seen = 1;
            continue;
        }
        if (!strcmp(arg, "--preview")) {
            if (preview_seen) return argument_error("--preview may only be specified once");
            if (i + 1 >= argc) return argument_error("--preview requires a command argument");
            preview_command = argv[++i];
            preview_seen = 1;
            continue;
        }
        if (!strncmp(arg, "--preview=", 10)) {
            if (preview_seen) return argument_error("--preview may only be specified once");
            preview_command = arg + 10;
            preview_seen = 1;
            continue;
        }
        int binding_result;
        binding_result = parse_binding_option(argc, argv, &i, arg, "--bind-up", &menu_config.key_up, &bind_up_seen);
        if (binding_result) {
            if (binding_result < 0) return argument_error("invalid or duplicate --bind-up (use one printable ASCII character or space)");
            continue;
        }
        binding_result = parse_binding_option(argc, argv, &i, arg, "--bind-down", &menu_config.key_down, &bind_down_seen);
        if (binding_result) {
            if (binding_result < 0) return argument_error("invalid or duplicate --bind-down (use one printable ASCII character or space)");
            continue;
        }
        binding_result = parse_binding_option(argc, argv, &i, arg, "--bind-accept", &menu_config.key_accept, &bind_accept_seen);
        if (binding_result) {
            if (binding_result < 0) return argument_error("invalid or duplicate --bind-accept (use one printable ASCII character or space)");
            continue;
        }
        binding_result = parse_binding_option(argc, argv, &i, arg, "--bind-cancel", &menu_config.key_cancel, &bind_cancel_seen);
        if (binding_result) {
            if (binding_result < 0) return argument_error("invalid or duplicate --bind-cancel (use one printable ASCII character or space)");
            continue;
        }
        binding_result = parse_binding_option(argc, argv, &i, arg, "--bind-toggle", &menu_config.key_toggle, &bind_toggle_seen);
        if (binding_result) {
            if (binding_result < 0) return argument_error("invalid or duplicate --bind-toggle (use one printable ASCII character or space)");
            continue;
        }
        binding_result = parse_binding_option(argc, argv, &i, arg, "--bind-backspace", &menu_config.key_backspace, &bind_backspace_seen);
        if (binding_result) {
            if (binding_result < 0) return argument_error("invalid or duplicate --bind-backspace (use one printable ASCII character or space)");
            continue;
        }
        if (!strcmp(arg, "--format")) {
            if (output_format)
                return argument_error("--format may only be specified once");
            if (i + 1 >= argc)
                return argument_error("--format requires a format argument");
            output_format = argv[++i];
            continue;
        }
        if (!strncmp(arg, "--format=", 9)) {
            if (output_format)
                return argument_error("--format may only be specified once");
            output_format = arg + 9;
            continue;
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

    if (fuzzy_mode && exact_mode)
        return argument_error("--fuzzy and --exact are mutually exclusive");
    if (extended_mode && exact_mode)
        return argument_error("--extended cannot be combined with whole-record --exact");
    if (invert_mode && !filter_seen)
        return argument_error("--invert-match requires --filter");
    if (no_sort && (!fuzzy_mode || !filter_seen))
        return argument_error("--no-sort requires --fuzzy and --filter");

    if (filter_seen && positional_count > 0)
        return argument_error("positional argument is not valid with batch filtering");
    if ((initial_query_seen || preview_seen || bind_up_seen || bind_down_seen || bind_accept_seen ||
         bind_cancel_seen || bind_toggle_seen || bind_backspace_seen) && filter_seen)
        return argument_error("interactive query, preview, and key bindings cannot be used with --filter");
    if (bind_toggle_seen && !multi_mode)
        return argument_error("--bind-toggle requires --multi");
    wint_t custom_keys[] = {menu_config.key_up, menu_config.key_down,
                            menu_config.key_accept, menu_config.key_cancel,
                            menu_config.key_toggle, menu_config.key_backspace};
    for (size_t key_i = 0; key_i < sizeof(custom_keys) / sizeof(custom_keys[0]); key_i++)
        for (size_t key_j = 0; custom_keys[key_i] && key_j < key_i; key_j++)
            if (custom_keys[key_i] == custom_keys[key_j])
                return argument_error("a custom key cannot be assigned to multiple actions");
    if (preview_seen && (!preview_command || !*preview_command))
        return argument_error("--preview command must not be empty");
    unsigned int match_flags = (fuzzy_mode ? MMENU_FUZZY : 0u) |
                               (exact_mode ? MMENU_EXACT : 0u) |
                               (extended_mode ? MMENU_EXTENDED : 0u);
    menu_config.initial_query = initial_query_seen ? initial_query : NULL;
    menu_config.preview = preview_seen ? run_preview_command : NULL;
    menu_config.preview_userdata = (void *)preview_command;
    if (initial_query_seen) {
        setlocale(LC_ALL, "");
        size_t query_len = mbstowcs(NULL, initial_query, 0);
        if (query_len == (size_t)-1)
            return argument_error("--query is not valid in the current locale");
        if (query_len > 128)
            return argument_error("--query exceeds 128 characters");
    }
    if (multi_mode && filter_seen)
        return argument_error("--multi is only available in interactive mode");
    if (output_format && !filter_seen)
        return argument_error("--format requires --filter");
    if (output_format && output_index)
        return argument_error("--format cannot be combined with -t");

    /* Faster buffered input for huge pipes. */
    setvbuf(stdin, NULL, _IOFBF, 64 * 1024);

    lines_t opts = {0};
    input_stream_t input_stream = {
        .lines = &opts,
        .fd = STDIN_FILENO,
        .delimiter = input_nul ? '\0' : '\n'
    };

    if (filter_query != NULL) {
        char *buf = NULL;
        size_t buf_cap = 0;
        ssize_t read_len;
        /* Batch mode remains synchronous and preserves the existing getdelim path. */
        while ((read_len = getdelim(&buf, &buf_cap, input_stream.delimiter, stdin)) != -1) {
            size_t len = (size_t)read_len;
            if (!input_nul && memchr(buf, '\0', len) != NULL) {
                fprintf(stderr, "mmenu: input record contains an embedded NUL byte\n");
                free(buf);
                lines_free(&opts);
                return EXIT_FAILURE;
            }
            if (len > 0 && buf[len - 1] == input_stream.delimiter) len--;
            char *stored = lines_arena_dup(&opts, buf, len);
            lines_push(&opts, stored);
        }
        free(buf);
        if (ferror(stdin)) {
            perror("mmenu: reading stdin");
            lines_free(&opts);
            return EXIT_FAILURE;
        }
    } else {
        menu_config.stream_next = lines_stream_next;
        menu_config.stream_userdata = &input_stream;
    }

    /* Non-interactive fast path: outputs matching lines or original indexes. */
    if (filter_query != NULL) {
        if (extended_mode) {
            setlocale(LC_ALL, "");
            errno = 0;
            wchar_t *wide_query = mb_to_wc(filter_query);
            if (!wide_query) {
                if (errno == ENOMEM) perror("mmenu: converting extended query");
                else fprintf(stderr, "mmenu: extended query is not valid in the current locale\n");
                lines_free(&opts);
                return EXIT_FAILURE;
            }

            if (invert_mode || no_sort || !fuzzy_mode) {
                mmenu_query query;
                if (!mmenu_query_compile(wide_query, &query)) {
                    if (errno == EILSEQ)
                        fprintf(stderr, "mmenu: extended query is not valid in the current locale\n");
                    else
                        perror("mmenu: compiling extended query");
                    free(wide_query);
                    lines_free(&opts);
                    return EXIT_FAILURE;
                }
                for (int i = 0; i < opts.count; i++) {
                    long long score = 0;
                    int matched = mmenu_query_match(&query, opts.lines[i],
                                                    match_flags, &score);
                    if (invert_mode ? !matched : matched)
                        print_batch_match(opts.lines[i], i, output_index, output_nul,
                                          output_format, filter_query);
                }
                mmenu_query_free(&query);
            } else {
                FuzzyMatch *matches = NULL;
                int match_count = 0;
                if (!mmenu_extended_collect((const char *const *)opts.lines, 0,
                                            opts.count, wide_query, match_flags,
                                            &matches, &match_count)) {
                    free(wide_query);
                    lines_free(&opts);
                    return EXIT_FAILURE;
                }
                for (int i = 0; i < match_count; i++) {
                    int index = matches[i].index;
                    print_batch_match(opts.lines[index], index, output_index,
                                      output_nul, output_format, filter_query);
                }
                free(matches);
            }
            free(wide_query);
        } else if (invert_mode) {
            wchar_t *wide_query = NULL;
            if (fuzzy_mode) {
                setlocale(LC_ALL, "");
                errno = 0;
                wide_query = mb_to_wc(filter_query);
                if (!wide_query) {
                    if (errno == ENOMEM) perror("mmenu: converting fuzzy query");
                    else fprintf(stderr, "mmenu: fuzzy query is not valid in the current locale\n");
                    lines_free(&opts);
                    return EXIT_FAILURE;
                }
            }
            for (int i = 0; i < opts.count; i++) {
                int matched;
                if (fuzzy_mode) {
                    long long score;
                    matched = FuzzyScore(opts.lines[i], wide_query, &score);
                } else if (exact_mode) {
                    matched = strcmp(opts.lines[i], filter_query) == 0;
                } else {
                    matched = MmenuAsciiStrcasestr(opts.lines[i], filter_query) != NULL;
                }
                if (!matched) {
                    print_batch_match(opts.lines[i], i, output_index, output_nul, output_format, filter_query);
                }
            }
            free(wide_query);
        } else if (exact_mode) {
            for (int i = 0; i < opts.count; i++) {
                if (strcmp(opts.lines[i], filter_query) == 0) {
                    print_batch_match(opts.lines[i], i, output_index, output_nul, output_format, filter_query);
                }
            }
        } else if (fuzzy_mode && no_sort) {
            setlocale(LC_ALL, "");
            errno = 0;
            wchar_t *query = mb_to_wc(filter_query);
            if (!query) {
                if (errno == ENOMEM) perror("mmenu: converting fuzzy query");
                else fprintf(stderr, "mmenu: fuzzy query is not valid in the current locale\n");
                lines_free(&opts);
                return EXIT_FAILURE;
            }
            for (int i = 0; i < opts.count; i++) {
                long long score;
                if (FuzzyScore(opts.lines[i], query, &score)) {
                    print_batch_match(opts.lines[i], i, output_index, output_nul, output_format, filter_query);
                }
            }
            free(query);
        } else if (fuzzy_mode) {
            setlocale(LC_ALL, "");
            errno = 0;
            wchar_t *query = mb_to_wc(filter_query);
            if (!query) {
                if (errno == ENOMEM) perror("mmenu: converting fuzzy query");
                else fprintf(stderr, "mmenu: fuzzy query is not valid in the current locale\n");
                lines_free(&opts);
                return EXIT_FAILURE;
            }
            FuzzyMatch *matches = NULL;
            int match_count = 0;
            if (!FuzzyCollect((const char *const *)opts.lines, opts.count,
                              query, &matches, &match_count)) {
                free(query);
                lines_free(&opts);
                return EXIT_FAILURE;
            }
            for (int i = 0; i < match_count; i++) {
                int index = matches[i].index;
                print_batch_match(opts.lines[index], index, output_index, output_nul, output_format, filter_query);
            }
            free(matches);
            free(query);
        } else {
            for (int i = 0; i < opts.count; i++) {
                if (MmenuAsciiStrcasestr(opts.lines[i], filter_query)) {
                    print_batch_match(opts.lines[i], i, output_index, output_nul, output_format, filter_query);
                }
            }
        }
        lines_free(&opts);
        return flush_stdout();
    }

    if (multi_mode) {
        int *selected = NULL;
        int selected_count = 0;
        int status = mmenuMultiConfigured((const char *const *)opts.lines, opts.count, prompt,
                                          match_flags, &menu_config, &selected, &selected_count);
        if (input_stream.error) {
            free(selected);
            free(input_stream.record);
            lines_free(&opts);
            return EXIT_FAILURE;
        }
        if (status != 0) {
            print_record("", output_nul);
        } else {
            for (int i = 0; i < selected_count; i++)
                print_match(opts.lines[selected[i]], selected[i], output_index, output_nul);
        }
        free(selected);
    } else {
        int chosen = mmenuConfigured((const char *const *)opts.lines, opts.count, prompt,
                                     match_flags, &menu_config);
        if (input_stream.error) {
            free(input_stream.record);
            lines_free(&opts);
            return EXIT_FAILURE;
        }
        if (chosen == -1) {
            print_record("", output_nul);
        } else {
            print_match(opts.lines[chosen], chosen, output_index, output_nul);
        }
    }

    free(input_stream.record);
    lines_free(&opts);
    return flush_stdout();
}
