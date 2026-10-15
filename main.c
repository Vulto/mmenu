/* main.c - standalone mmenu tool (reads options from stdin) */
#define MMENU_IMPLEMENTATION
#include "mmenu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <regex.h>
#include <sys/wait.h>
#include <poll.h>
#include <limits.h>

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

static int parse_nth_number(const char *text, long *value) {
    if (!text || !*text || isspace((unsigned char)*text)) return 0;
    errno = 0;
    char *end = NULL;
    long parsed = strtol(text, &end, 10);
    if (errno || !end || *end || parsed == 0) return 0;
    *value = parsed;
    return 1;
}

static int parse_nth_token(char *token, int *open_start, long *start,
                           int *open_end, long *end) {
    char *dots = strstr(token, "..");
    *open_start = 0;
    *open_end = 0;
    if (!dots) {
        if (!parse_nth_number(token, start)) return 0;
        *end = *start;
        return 1;
    }
    if (strstr(dots + 2, "..")) return 0;
    *dots = '\0';
    const char *right = dots + 2;
    if (!*token && !*right) return 0;
    if (!*token) *open_start = 1;
    else if (!parse_nth_number(token, start)) return 0;
    if (!*right) *open_end = 1;
    else if (!parse_nth_number(right, end)) return 0;
    return 1;
}

static int nth_spec_valid(const char *spec) {
    if (!spec || !*spec) return 0;
    const char *cursor = spec;
    while (*cursor) {
        const char *comma = strchr(cursor, ',');
        size_t length = comma ? (size_t)(comma - cursor) : strlen(cursor);
        if (length == 0 || length >= 128) return 0;
        char token[128];
        memcpy(token, cursor, length);
        token[length] = '\0';
        int open_start, open_end;
        long start = 0, end = 0;
        if (!parse_nth_token(token, &open_start, &start, &open_end, &end))
            return 0;
        if (comma && !comma[1]) return 0;
        if (!comma) break;
        cursor = comma + 1;
    }
    return 1;
}

static long resolve_nth_index(long index, int field_count) {
    return index < 0 ? (long)field_count + index + 1 : index;
}

static int nth_selects_field(const char *spec, int field_index, int field_count) {
    const char *cursor = spec;
    while (*cursor) {
        const char *comma = strchr(cursor, ',');
        size_t length = comma ? (size_t)(comma - cursor) : strlen(cursor);
        char token[128];
        memcpy(token, cursor, length);
        token[length] = '\0';
        int open_start, open_end;
        long start = 0, end = 0;
        if (!parse_nth_token(token, &open_start, &start, &open_end, &end))
            return 0;
        start = open_start ? 1 : resolve_nth_index(start, field_count);
        end = open_end ? field_count : resolve_nth_index(end, field_count);
        if (start < 1) start = 1;
        if (end > field_count) end = field_count;
        if (start <= field_count && end >= 1) {
            if (start > end) {
                long temporary = start;
                start = end;
                end = temporary;
            }
            if (field_index >= start && field_index <= end) return 1;
        }
        if (!comma) break;
        cursor = comma + 1;
    }
    return 0;
}

static regex_t nth_delimiter_regex;
static int nth_delimiter_regex_ready;

static void nth_delimiter_regex_cleanup(void) {
    if (nth_delimiter_regex_ready) {
        regfree(&nth_delimiter_regex);
        nth_delimiter_regex_ready = 0;
    }
}

static int nth_delimiter_regex_prepare(const char *pattern) {
    setlocale(LC_ALL, "");
    int code = regcomp(&nth_delimiter_regex, pattern, REG_EXTENDED);
    if (code != 0) {
        char message[256];
        regerror(code, NULL, message, sizeof(message));
        fprintf(stderr, "mmenu: invalid --delimiter regex '%s': %s\n",
                pattern, message);
        return 0;
    }
    nth_delimiter_regex_ready = 1;

    /* Empty-width delimiters make splitting ambiguous and can prevent progress. */
    regmatch_t match;
    code = regexec(&nth_delimiter_regex, "", 1, &match, 0);
    if (code == 0 && match.rm_so == match.rm_eo) {
        fprintf(stderr, "mmenu: --delimiter regex must not match an empty string\n");
        nth_delimiter_regex_cleanup();
        return 0;
    }
    if (code != 0 && code != REG_NOMATCH) {
        char message[256];
        regerror(code, &nth_delimiter_regex, message, sizeof(message));
        fprintf(stderr, "mmenu: could not validate --delimiter regex: %s\n", message);
        nth_delimiter_regex_cleanup();
        return 0;
    }
    if (atexit(nth_delimiter_regex_cleanup) != 0) {
        fprintf(stderr, "mmenu: could not register delimiter cleanup\n");
        nth_delimiter_regex_cleanup();
        return 0;
    }
    return 1;
}

/* Find the next delimiter while preserving ^/$ as anchors to the full record. */
static int nth_find_delimiter(const char *record, const char *cursor,
                              const char **end, size_t *length) {
    int flags = cursor != record ? REG_NOTBOL : 0;
    regmatch_t match;
    int code = regexec(&nth_delimiter_regex, cursor, 1, &match, flags);
    if (code == REG_NOMATCH) return 0;
    if (code != 0) {
        errno = code == REG_ESPACE ? ENOMEM : EINVAL;
        return -1;
    }
    if (match.rm_so < 0 || match.rm_eo <= match.rm_so) {
        errno = EINVAL;
        return -1;
    }
    *end = cursor + match.rm_so;
    *length = (size_t)(match.rm_eo - match.rm_so);
    return 1;
}

static int nth_field_count(const char *record, const char *delimiter) {
    if (delimiter && *delimiter) {
        int count = 1;
        const char *cursor = record;
        for (;;) {
            const char *end = NULL;
            size_t delimiter_len = 0;
            int found = nth_find_delimiter(record, cursor, &end, &delimiter_len);
            if (found < 0) return -1;
            if (!found) break;
            if (count == INT_MAX) {
                errno = EOVERFLOW;
                return -1;
            }
            count++;
            cursor = end + delimiter_len;
        }
        return count;
    }
    int count = 0;
    const unsigned char *cursor = (const unsigned char *)record;
    while (*cursor) {
        while (*cursor && isspace(*cursor)) cursor++;
        if (!*cursor) break;
        if (count == INT_MAX) {
            errno = EOVERFLOW;
            return -1;
        }
        count++;
        while (*cursor && !isspace(*cursor)) cursor++;
    }
    return count;
}

static char *select_nth_fields(const char *record, const char *spec,
                               const char *delimiter) {
    size_t record_len = strlen(record);
    if (record_len > SIZE_MAX - 2) {
        errno = ENOMEM;
        return NULL;
    }
    int field_count = nth_field_count(record, delimiter);
    if (field_count < 0) {
        if (errno == 0) errno = ENOMEM;
        return NULL;
    }
    char *selected = malloc(record_len + 2);
    if (!selected) return NULL;
    size_t output_len = 0;
    int output_fields = 0;
    if (delimiter && *delimiter) {
        const char *start = record;
        int field = 1;
        for (;;) {
            const char *end = NULL;
            size_t delimiter_len = 0;
            int found = nth_find_delimiter(record, start, &end, &delimiter_len);
            if (found < 0) {
                free(selected);
                return NULL;
            }
            size_t length = found ? (size_t)(end - start) : strlen(start);
            if (nth_selects_field(spec, field, field_count)) {
                if (output_fields++) selected[output_len++] = ' ';
                if (length) memcpy(selected + output_len, start, length);
                output_len += length;
            }
            if (!found) break;
            start = end + delimiter_len;
            field++;
        }
    } else {
        const unsigned char *cursor = (const unsigned char *)record;
        int field = 1;
        while (*cursor) {
            while (*cursor && isspace(*cursor)) cursor++;
            if (!*cursor) break;
            const unsigned char *start = cursor;
            while (*cursor && !isspace(*cursor)) cursor++;
            size_t length = (size_t)(cursor - start);
            if (nth_selects_field(spec, field, field_count)) {
                if (output_fields++) selected[output_len++] = ' ';
                memcpy(selected + output_len, start, length);
                output_len += length;
            }
            field++;
        }
    }
    selected[output_len] = '\0';
    return selected;
}

static void print_nth_fields(const char *record, const char *spec,
                            const char *delimiter, int nul_delimiter) {
    char *selected = select_nth_fields(record, spec, delimiter);
    if (!selected) {
        perror("mmenu: selecting accepted fields");
        exit(EXIT_FAILURE);
    }
    size_t length = strlen(selected);
    if (length) fwrite(selected, 1, length, stdout);
    fputc(nul_delimiter ? '\0' : '\n', stdout);
    free(selected);
}

/* Nonblocking stdin source. The reusable record buffer grows only for unusually
   long records; completed records are copied into the existing arena. */
typedef struct {
    lines_t *lines;
    lines_t *search_lines;
    lines_t *display_lines;
    const char *nth_spec;
    const char *with_nth_spec;
    const char *field_delimiter;
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

static const char *input_stream_search_text(const char *option, int index,
                                            void *userdata) {
    (void)option;
    input_stream_t *stream = userdata;
    if (!stream || !stream->search_lines || index < 0 ||
        index >= stream->search_lines->count) return NULL;
    return stream->search_lines->lines[index];
}

static const char *input_stream_display_text(const char *option, int index,
                                             void *userdata) {
    (void)option;
    input_stream_t *stream = userdata;
    if (!stream || !stream->display_lines || index < 0 ||
        index >= stream->display_lines->count) return NULL;
    return stream->display_lines->lines[index];
}

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
    if (stream->nth_spec) {
        char *selected = select_nth_fields(stored, stream->nth_spec,
                                           stream->field_delimiter);
        if (!selected) {
            stream->error = 1;
            return MMENU_STREAM_ERROR;
        }
        char *search_record = lines_arena_dup(stream->search_lines, selected,
                                              strlen(selected));
        free(selected);
        lines_push(stream->search_lines, search_record);
    }
    if (stream->with_nth_spec) {
        char *selected = select_nth_fields(stored, stream->with_nth_spec,
                                           stream->field_delimiter);
        if (!selected) {
            stream->error = 1;
            return MMENU_STREAM_ERROR;
        }
        char *display_record = lines_arena_dup(stream->display_lines, selected,
                                               strlen(selected));
        free(selected);
        lines_push(stream->display_lines, display_record);
    }
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
        "Use --multi for Tab toggling; --cycle wraps navigation at either end.\n"
        "--height N limits the visible option list to N rows (interactive only).\n"
        "Enter emits selected records (or indexes with -t).\n"
        "The legacy second positional argument t or true, and -t, print zero-based indexes.\n"
        "Batch filter mode writes matching records (or indexes with -t).\n"
        "--read0 reads NUL-delimited records; --print0 terminates output records with NUL.\n"
        "--fuzzy enables ranked subsequence matching; --exact matches whole records.\n"
        "--tiebreak=KEYS orders equal-score fuzzy matches (length,begin,end,chunk,pathname,index).\n"
        "--disabled keeps all options visible without filtering as the query changes.\n"
        "--nth SPEC searches selected 1-based fields/ranges; --with-nth SPEC changes interactive display fields.\n"
        "--delimiter/-d sets a POSIX extended regular expression for field options.\n"
        "--accept-nth SPEC outputs selected fields from each accepted record.\n"
        "--no-sort with --fuzzy preserves input order in batch and interactive modes.\n"
        "--ignore-case/-i folds ASCII case (default); --no-ignore-case/+i is case-sensitive.\n"
        "--smart-case is case-insensitive unless the query contains ASCII uppercase.\n"
        "--extended enables AND/OR, inverse, exact-substring, prefix (^), and suffix ($) terms.\n"
        "--invert-match or -v emits records that do not match the batch query.\n"
        "--format FORMAT expands {}, {index}, and {query} for each batch result.\n");
}

static int parse_positive_int(const char *value, int *result) {
    if (!value || !*value || !result) return 0;
    errno = 0;
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (errno || end == value || *end != '\0' || parsed < 1 || parsed > INT_MAX)
        return 0;
    *result = (int)parsed;
    return 1;
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
                        int nul_delimiter, const char *accept_nth,
                        const char *field_delimiter) {
    if (output_index) {
        char index_text[32];
        snprintf(index_text, sizeof(index_text), "%d", index);
        print_record(index_text, nul_delimiter);
    } else if (accept_nth) {
        print_nth_fields(record, accept_nth, field_delimiter, nul_delimiter);
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
                              const char *query, const char *accept_nth,
                              const char *field_delimiter) {
    if (format) print_formatted_match(record, index, format, query, nul_delimiter);
    else print_match(record, index, output_index, nul_delimiter,
                     accept_nth, field_delimiter);
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
    const char *tiebreak_spec_string = NULL;
    int tiebreak_seen = 0;
    mmenu_tiebreak_spec tiebreak;
    char tiebreak_error[256];
    int exact_mode = 0;
    int extended_mode = 0;
    int disabled_mode = 0;
    unsigned int case_mode = 0;
    int invert_mode = 0;
    int no_sort = 0;
    int input_nul = 0;
    int output_nul = 0;
    int output_index = 0;
    int multi_mode = 0;
    int cycle_mode = 0;
    int height_seen = 0;
    const char *initial_query = NULL;
    int initial_query_seen = 0;
    const char *preview_command = NULL;
    int preview_seen = 0;
    mmenu_config menu_config = {0};
    int bind_up_seen = 0, bind_down_seen = 0, bind_accept_seen = 0;
    int bind_cancel_seen = 0, bind_toggle_seen = 0, bind_backspace_seen = 0;
    const char *output_format = NULL;
    const char *nth_spec = NULL;
    const char *with_nth_spec = NULL;
    const char *accept_nth = NULL;
    const char *field_delimiter = NULL;
    int nth_seen = 0;
    int with_nth_seen = 0;
    int accept_nth_seen = 0;
    int delimiter_seen = 0;
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
        if (!strcmp(arg, "--tiebreak")) {
            if (tiebreak_seen) return argument_error("--tiebreak may only be specified once");
            if (i + 1 >= argc) return argument_error("--tiebreak requires a comma-separated key list");
            tiebreak_spec_string = argv[++i];
            tiebreak_seen = 1;
            continue;
        }
        if (!strncmp(arg, "--tiebreak=", 11)) {
            if (tiebreak_seen) return argument_error("--tiebreak may only be specified once");
            tiebreak_spec_string = arg + 11;
            tiebreak_seen = 1;
            continue;
        }
        if (!strcmp(arg, "--disabled")) {
            disabled_mode = 1;
            continue;
        }
        if (!strcmp(arg, "--ignore-case") || !strcmp(arg, "-i")) {
            case_mode = 0;
            continue;
        }
        if (!strcmp(arg, "--no-ignore-case") || !strcmp(arg, "+i")) {
            case_mode = MMENU_CASE_SENSITIVE;
            continue;
        }
        if (!strcmp(arg, "--smart-case")) {
            case_mode = MMENU_SMART_CASE;
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
        if (!strcmp(arg, "--cycle")) {
            cycle_mode = 1;
            continue;
        }
        if (!strcmp(arg, "--height")) {
            if (height_seen) return argument_error("--height may only be specified once");
            if (i + 1 >= argc) return argument_error("--height requires a positive row count");
            if (!parse_positive_int(argv[++i], &menu_config.height))
                return argument_error("--height requires a positive integer row count");
            height_seen = 1;
            continue;
        }
        if (!strncmp(arg, "--height=", 9)) {
            if (height_seen) return argument_error("--height may only be specified once");
            if (!parse_positive_int(arg + 9, &menu_config.height))
                return argument_error("--height requires a positive integer row count");
            height_seen = 1;
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
        if (!strcmp(arg, "--nth")) {
            if (nth_seen || i + 1 >= argc) return argument_error("--nth requires one field expression");
            nth_spec = argv[++i];
            nth_seen = 1;
            continue;
        }
        if (!strncmp(arg, "--nth=", 6)) {
            if (nth_seen) return argument_error("--nth may only be specified once");
            nth_spec = arg + 6;
            nth_seen = 1;
            continue;
        }
        if (!strcmp(arg, "--with-nth")) {
            if (with_nth_seen || i + 1 >= argc) return argument_error("--with-nth requires one field expression");
            with_nth_spec = argv[++i];
            with_nth_seen = 1;
            continue;
        }
        if (!strncmp(arg, "--with-nth=", 11)) {
            if (with_nth_seen) return argument_error("--with-nth may only be specified once");
            with_nth_spec = arg + 11;
            with_nth_seen = 1;
            continue;
        }
        if (!strcmp(arg, "--accept-nth")) {
            if (accept_nth_seen || i + 1 >= argc) return argument_error("--accept-nth requires one field expression");
            accept_nth = argv[++i];
            accept_nth_seen = 1;
            continue;
        }
        if (!strncmp(arg, "--accept-nth=", 13)) {
            if (accept_nth_seen) return argument_error("--accept-nth may only be specified once");
            accept_nth = arg + 13;
            accept_nth_seen = 1;
            continue;
        }
        if (!strcmp(arg, "--delimiter") || !strcmp(arg, "-d")) {
            if (delimiter_seen || i + 1 >= argc) return argument_error("--delimiter requires one regular expression");
            field_delimiter = argv[++i];
            delimiter_seen = 1;
            continue;
        }
        if (!strncmp(arg, "--delimiter=", 11)) {
            if (delimiter_seen) return argument_error("--delimiter may only be specified once");
            field_delimiter = arg + 11;
            delimiter_seen = 1;
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
    if (disabled_mode && filter_seen)
        return argument_error("--disabled is only available in interactive mode");
    if (height_seen && filter_seen)
        return argument_error("--height is only available in interactive mode");
    if (nth_seen && !nth_spec_valid(nth_spec))
        return argument_error("--nth must be a comma-separated list of fields or ranges");
    if (with_nth_seen && !nth_spec_valid(with_nth_spec))
        return argument_error("--with-nth must be a comma-separated list of fields or ranges");
    if (with_nth_seen && filter_seen)
        return argument_error("--with-nth is only available in interactive mode");
    if (delimiter_seen && (!field_delimiter || !*field_delimiter))
        return argument_error("--delimiter must not be empty");
    if (accept_nth_seen && !nth_spec_valid(accept_nth))
        return argument_error("--accept-nth must be a comma-separated list of fields or ranges");
    if (delimiter_seen && !nth_seen && !with_nth_seen && !accept_nth_seen)
        return argument_error("--delimiter requires --nth, --with-nth, or --accept-nth");
    if (accept_nth_seen && (output_index || output_format))
        return argument_error("--accept-nth cannot be combined with --index or --format");
    if (no_sort && !fuzzy_mode)
        return argument_error("--no-sort requires --fuzzy");
    if (tiebreak_seen && !fuzzy_mode)
        return argument_error("--tiebreak requires --fuzzy");
    if (!MmenuTieBreakParse(tiebreak_spec_string, &tiebreak,
                            tiebreak_error, sizeof(tiebreak_error)))
        return argument_error(tiebreak_error);

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
    if (delimiter_seen && !nth_delimiter_regex_prepare(field_delimiter))
        return EXIT_FAILURE;
    unsigned int match_flags = (fuzzy_mode ? MMENU_FUZZY : 0u) |
                               (exact_mode ? MMENU_EXACT : 0u) |
                               (extended_mode ? MMENU_EXTENDED : 0u) |
                               (no_sort ? MMENU_NO_SORT : 0u) |
                               (disabled_mode ? MMENU_DISABLED : 0u) | case_mode;
    menu_config.initial_query = initial_query_seen ? initial_query : NULL;
    menu_config.cycle = cycle_mode;
    menu_config.tiebreak = tiebreak_seen ? tiebreak_spec_string : NULL;
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
    if (cycle_mode && filter_seen)
        return argument_error("--cycle is only available in interactive mode");
    if (output_format && !filter_seen)
        return argument_error("--format requires --filter");
    if (output_format && output_index)
        return argument_error("--format cannot be combined with -t");

    /* Faster buffered input for huge pipes. */
    setvbuf(stdin, NULL, _IOFBF, 64 * 1024);

    lines_t opts = {0};
    lines_t search_opts = {0};
    lines_t display_opts = {0};
    input_stream_t input_stream = {
        .lines = &opts,
        .search_lines = nth_seen ? &search_opts : NULL,
        .display_lines = with_nth_seen ? &display_opts : NULL,
        .nth_spec = nth_seen ? nth_spec : NULL,
        .with_nth_spec = with_nth_seen ? with_nth_spec : NULL,
        .field_delimiter = delimiter_seen ? field_delimiter : NULL,
        .fd = STDIN_FILENO,
        .delimiter = input_nul ? '\0' : '\n'
    };
    menu_config.search_text = nth_seen ? input_stream_search_text : NULL;
    menu_config.search_text_userdata = &input_stream;
    menu_config.display_text = with_nth_seen ? input_stream_display_text : NULL;
    menu_config.display_text_userdata = &input_stream;

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
                lines_free(&display_opts);
                lines_free(&search_opts);
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
            lines_free(&display_opts);
                lines_free(&search_opts);
                lines_free(&opts);
            return EXIT_FAILURE;
        }
    } else {
        menu_config.stream_next = lines_stream_next;
        menu_config.stream_userdata = &input_stream;
    }

    const char *const *match_lines = (const char *const *)opts.lines;
    if (filter_query != NULL && nth_seen) {
        setlocale(LC_ALL, "");
        for (int i = 0; i < opts.count; i++) {
            char *selected = select_nth_fields(opts.lines[i], nth_spec,
                                               delimiter_seen ? field_delimiter : NULL);
            if (!selected) {
                perror("mmenu: selecting searchable fields");
                lines_free(&display_opts);
                lines_free(&search_opts);
                lines_free(&opts);
                return EXIT_FAILURE;
            }
            char *stored = lines_arena_dup(&search_opts, selected, strlen(selected));
            free(selected);
            lines_push(&search_opts, stored);
        }
        match_lines = (const char *const *)search_opts.lines;
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
                lines_free(&display_opts);
                lines_free(&search_opts);
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
                    lines_free(&display_opts);
                lines_free(&search_opts);
                lines_free(&opts);
                    return EXIT_FAILURE;
                }
                for (int i = 0; i < opts.count; i++) {
                    long long score = 0;
                    int matched = mmenu_query_match(&query, match_lines[i],
                                                    match_flags, &score, NULL);
                    if (invert_mode ? !matched : matched)
                        print_batch_match(opts.lines[i], i, output_index, output_nul,
                                          output_format, filter_query, accept_nth, delimiter_seen ? field_delimiter : NULL);
                }
                mmenu_query_free(&query);
            } else {
                FuzzyMatch *matches = NULL;
                int match_count = 0;
                if (!mmenu_extended_collect(match_lines, 0,
                                            opts.count, wide_query, match_flags,
                                            &tiebreak, &matches, &match_count)) {
                    free(wide_query);
                    lines_free(&display_opts);
                lines_free(&search_opts);
                lines_free(&opts);
                    return EXIT_FAILURE;
                }
                for (int i = 0; i < match_count; i++) {
                    int index = matches[i].index;
                    print_batch_match(opts.lines[index], index, output_index,
                                      output_nul, output_format, filter_query, accept_nth, delimiter_seen ? field_delimiter : NULL);
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
                    lines_free(&display_opts);
                lines_free(&search_opts);
                lines_free(&opts);
                    return EXIT_FAILURE;
                }
            }
            for (int i = 0; i < opts.count; i++) {
                int matched;
                if (fuzzy_mode) {
                    long long score;
                    matched = FuzzyScoreMetrics(match_lines[i], wide_query, match_flags, &score, NULL);
                } else if (exact_mode) {
                    matched = strcmp(match_lines[i], filter_query) == 0;
                } else {
                    matched = MmenuStrstrPolicy(match_lines[i], filter_query, match_flags) != NULL;
                }
                if (!matched) {
                    print_batch_match(opts.lines[i], i, output_index, output_nul, output_format, filter_query, accept_nth, delimiter_seen ? field_delimiter : NULL);
                }
            }
            free(wide_query);
        } else if (exact_mode) {
            for (int i = 0; i < opts.count; i++) {
                if (strcmp(match_lines[i], filter_query) == 0) {
                    print_batch_match(opts.lines[i], i, output_index, output_nul, output_format, filter_query, accept_nth, delimiter_seen ? field_delimiter : NULL);
                }
            }
        } else if (fuzzy_mode && no_sort) {
            setlocale(LC_ALL, "");
            errno = 0;
            wchar_t *query = mb_to_wc(filter_query);
            if (!query) {
                if (errno == ENOMEM) perror("mmenu: converting fuzzy query");
                else fprintf(stderr, "mmenu: fuzzy query is not valid in the current locale\n");
                lines_free(&display_opts);
                lines_free(&search_opts);
                lines_free(&opts);
                return EXIT_FAILURE;
            }
            for (int i = 0; i < opts.count; i++) {
                long long score;
                if (FuzzyScoreMetrics(match_lines[i], query, match_flags, &score, NULL)) {
                    print_batch_match(opts.lines[i], i, output_index, output_nul, output_format, filter_query, accept_nth, delimiter_seen ? field_delimiter : NULL);
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
                lines_free(&display_opts);
                lines_free(&search_opts);
                lines_free(&opts);
                return EXIT_FAILURE;
            }
            FuzzyMatch *matches = NULL;
            int match_count = 0;
            if (!FuzzyCollect(match_lines, opts.count, query, match_flags, &tiebreak, &matches, &match_count)) {
                free(query);
                lines_free(&display_opts);
                lines_free(&search_opts);
                lines_free(&opts);
                return EXIT_FAILURE;
            }
            for (int i = 0; i < match_count; i++) {
                int index = matches[i].index;
                print_batch_match(opts.lines[index], index, output_index, output_nul, output_format, filter_query, accept_nth, delimiter_seen ? field_delimiter : NULL);
            }
            free(matches);
            free(query);
        } else {
            for (int i = 0; i < opts.count; i++) {
                if (MmenuStrstrPolicy(match_lines[i], filter_query, match_flags)) {
                    print_batch_match(opts.lines[i], i, output_index, output_nul, output_format, filter_query, accept_nth, delimiter_seen ? field_delimiter : NULL);
                }
            }
        }
        lines_free(&display_opts);
                lines_free(&search_opts);
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
            lines_free(&display_opts);
                lines_free(&search_opts);
                lines_free(&opts);
            return EXIT_FAILURE;
        }
        if (status != 0) {
            print_record("", output_nul);
        } else {
            for (int i = 0; i < selected_count; i++)
                print_match(opts.lines[selected[i]], selected[i], output_index, output_nul, accept_nth, delimiter_seen ? field_delimiter : NULL);
        }
        free(selected);
    } else {
        int chosen = mmenuConfigured((const char *const *)opts.lines, opts.count, prompt,
                                     match_flags, &menu_config);
        if (input_stream.error) {
            free(input_stream.record);
            lines_free(&display_opts);
                lines_free(&search_opts);
                lines_free(&opts);
            return EXIT_FAILURE;
        }
        if (chosen == -1) {
            print_record("", output_nul);
        } else {
            print_match(opts.lines[chosen], chosen, output_index, output_nul, accept_nth, delimiter_seen ? field_delimiter : NULL);
        }
    }

    free(input_stream.record);
    lines_free(&display_opts);
                lines_free(&search_opts);
                lines_free(&opts);
    return flush_stdout();
}
