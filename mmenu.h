/* mmenu.h - single-header fuzzy menu using ncursesw (stb-style) */
/* Public domain or MIT */

#ifndef MMENU_H
#define MMENU_H

/* Enable wide-character functions in ncurses */
#ifndef _XOPEN_SOURCE_EXTENDED
#define _XOPEN_SOURCE_EXTENDED 1
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE   /* for strcasestr on glibc */
#endif

#include <ncurses.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>

/* MMENU_FUZZY enables subsequence matching and score-ranked results unless
   MMENU_NO_SORT is set; MMENU_NO_SORT requires MMENU_FUZZY. MMENU_DISABLED
   keeps all options visible regardless of the query. */
enum { MMENU_FUZZY = 1u, MMENU_EXACT = 2u, MMENU_EXTENDED = 4u, MMENU_CASE_SENSITIVE = 8u, MMENU_SMART_CASE = 16u, MMENU_NO_SORT = 32u, MMENU_DISABLED = 64u };

/* Options and prompt must be valid multibyte strings in the active locale.
   Default matching is ASCII-case-insensitive substring matching; non-ASCII
   characters are matched exactly. MMENU_CASE_SENSITIVE disables ASCII case
   folding; MMENU_SMART_CASE enables case-sensitive matching when the query
   contains an ASCII uppercase letter. These two flags are mutually exclusive.
   MMENU_FUZZY enables ranked subsequence matching and MMENU_EXACT enables
   byte-exact equality matching. MMENU_EXTENDED enables AND/OR terms, inverse
   terms, exact substring terms, and prefix/suffix anchors; it cannot be
   combined with MMENU_EXACT. Invalid multibyte strings return -1 before
   terminal initialization. */
int mmenu(const char *const *options, int n_options, const char *prompt);
int mmenuEx(const char *const *options, int n_options, const char *prompt,
            unsigned int flags);
/* Multi-select returns 0 and allocates selected_indices; caller frees it.
   Enter selects the current option if none were toggled. Cancel/failure: -1. */
int mmenuMulti(const char *const *options, int n_options, const char *prompt,
               unsigned int flags, int **selected_indices, int *selected_count);

/* Preview callbacks fill a text buffer and return 0 on success. Shell execution,
   if desired, belongs in the caller's callback rather than the menu/matcher. */
#define MMENU_PREVIEW_BUFFER_SIZE 8192
typedef int (*mmenu_preview_callback)(const char *option, int index,
                                      const wchar_t *query, char *output,
                                      size_t output_size, void *userdata);

/* Stream callbacks return one record, temporary no-data, EOF, or error.
   For RECORD, *record must point to a NUL-terminated string that remains valid
   until the configured menu call returns. */
enum {
    MMENU_STREAM_ERROR = -1,
    MMENU_STREAM_EOF = 0,
    MMENU_STREAM_RECORD = 1,
    MMENU_STREAM_PENDING = 2
};
typedef int (*mmenu_stream_next_callback)(void *userdata, const char **record);
/* Return stable searchable text for an option; NULL signals an error. The
   returned string must remain valid until the configured menu call returns. */
typedef const char *(*mmenu_search_text_callback)(const char *option, int index,
                                                   void *userdata);
/* Return stable display text for an option; NULL signals an error. The
   returned string must remain valid until the configured menu call returns. */
typedef const char *(*mmenu_display_text_callback)(const char *option, int index,
                                                    void *userdata);

/* Zero-initialize this structure before setting optional fields. Optional
   query, key aliases, preview, input-stream, searchable/display-text callbacks,
   fuzzy tie-breaking, cyclic navigation, and visible row limits are configured here.
   Zero-valued key fields preserve built-in keys; cycle == 0 preserves bounded navigation.
   Each callback userdata pointer is passed through unchanged. */
typedef struct {
    const char *initial_query;
    wint_t key_up;
    wint_t key_down;
    wint_t key_accept;
    wint_t key_cancel;
    wint_t key_toggle;
    wint_t key_backspace;
    mmenu_preview_callback preview;
    void *preview_userdata;
    mmenu_stream_next_callback stream_next;
    void *stream_userdata;
    mmenu_search_text_callback search_text;
    void *search_text_userdata;
    mmenu_display_text_callback display_text;
    void *display_text_userdata;
    const char *tiebreak; /* optional comma-separated fuzzy tie-break keys */
    int cycle; /* wrap up/down navigation at the ends when nonzero */
    int height; /* maximum visible option rows; zero leaves this limit unset */
    int height_percent; /* optional 1..100 percent of available option rows; zero unset */
    int reverse_list; /* render first result at the bottom while keeping query at top */
} mmenu_config;

int mmenuConfigured(const char *const *options, int n_options, const char *prompt,
                    unsigned int flags, const mmenu_config *config);
int mmenuMultiConfigured(const char *const *options, int n_options, const char *prompt,
                         unsigned int flags, const mmenu_config *config,
                         int **selected_indices, int *selected_count);

#endif /* MMENU_H */

#ifdef MMENU_IMPLEMENTATION

#define MAX_INPUT_LEN 128
#define INITIAL_CAP 128

static volatile sig_atomic_t resize_flag = 0;

static void handle_resize(int sig) { (void)sig; resize_flag = 1; }

static int mmenu_visible_rows(int terminal_rows, const mmenu_config *config) {
    int visible = terminal_rows - 1;
    if (visible < 0) visible = 0;
    if (config && config->height_percent > 0 && visible > 0) {
        int percent_rows = (int)(((int64_t)visible * config->height_percent) / 100);
        if (percent_rows < 1) percent_rows = 1;
        if (visible > percent_rows) visible = percent_rows;
    }
    if (config && config->height > 0 && visible > config->height)
        visible = config->height;
    return visible;
}

static void mmenu_refresh_dimensions(FILE *tty, int *rows, int *cols) {
    struct winsize Size;
    if (ioctl(fileno(tty), TIOCGWINSZ, &Size) == 0 &&
        Size.ws_row > 0 && Size.ws_col > 0) {
        resizeterm((int)Size.ws_row, (int)Size.ws_col);
    }
    refresh();
    getmaxyx(stdscr, *rows, *cols);
}

/* Checked size arithmetic for conversion buffers and other byte counts. */
static int mmenu_size_add(size_t left, size_t right, size_t *result) {
    if (!result || left > SIZE_MAX - right) {
        errno = ENOMEM;
        return 0;
    }
    *result = left + right;
    return 1;
}

static int mmenu_size_mul(size_t left, size_t right, size_t *result) {
    if (!result || (right != 0 && left > SIZE_MAX / right)) {
        errno = ENOMEM;
        return 0;
    }
    *result = left * right;
    return 1;
}

static wchar_t *mb_to_wc(const char *s) {
    if (!s) return NULL;
    size_t n = mbstowcs(NULL, s, 0);
    if (n == (size_t)-1) return NULL;
    size_t count, bytes;
    if (!mmenu_size_add(n, 1, &count) ||
        !mmenu_size_mul(count, sizeof(wchar_t), &bytes))
        return NULL;
    wchar_t *w = malloc(bytes);
    if (!w && errno == 0) errno = ENOMEM;
    if (w && mbstowcs(w, s, count) == (size_t)-1) {
        free(w);
        errno = EILSEQ;
        return NULL;
    }
    return w;
}

/* Convert the (small) current search input from wchar to multibyte for fast byte matching.
   Only called once per keystroke, not per candidate. */
static char *wc_to_mb(const wchar_t *w) {
    if (!w || !*w) {
        char *empty = malloc(1);
        if (!empty && errno == 0) errno = ENOMEM;
        if (empty) empty[0] = '\0';
        return empty;
    }
    size_t n = wcstombs(NULL, w, 0);
    if (n == (size_t)-1) return NULL;
    size_t bytes;
    if (!mmenu_size_add(n, 1, &bytes)) return NULL;
    char *m = malloc(bytes);
    if (!m && errno == 0) errno = ENOMEM;
    if (m && wcstombs(m, w, bytes) == (size_t)-1) {
        free(m);
        errno = EILSEQ;
        return NULL;
    }
    return m;
}

/* Shared overflow-safe capacity growth for every dynamic index/result list. */
static int mmenu_next_capacity(int current, int initial, size_t element_size,
                               int *next_capacity) {
    if (!next_capacity || current < 0 || initial <= 0 || element_size == 0) {
        errno = EINVAL;
        return 0;
    }

    int capacity;
    if (current == 0) {
        capacity = initial;
    } else {
        if (current > INT_MAX / 2) {
            errno = ENOMEM;
            return 0;
        }
        capacity = current * 2;
    }
    if ((size_t)capacity > SIZE_MAX / element_size) {
        errno = ENOMEM;
        return 0;
    }
    *next_capacity = capacity;
    return 1;
}

typedef struct { int *indices; int cap; int count; } filt;

typedef enum {
    MMENU_TIEBREAK_LENGTH,
    MMENU_TIEBREAK_BEGIN,
    MMENU_TIEBREAK_END,
    MMENU_TIEBREAK_INDEX,
    MMENU_TIEBREAK_CHUNK,
    MMENU_TIEBREAK_PATHNAME
} mmenu_tiebreak_key;

typedef struct {
    mmenu_tiebreak_key keys[7];
    int count;
} mmenu_tiebreak_spec;

typedef struct {
    size_t length;
    size_t begin;
    size_t end;
    size_t basename_start;
    unsigned int chunks;
    int has_match;
} FuzzyMetrics;

typedef struct {
    int index;
    long long score;
    FuzzyMetrics metrics;
} FuzzyMatch;

static wchar_t FoldAscii(wchar_t ch)
{
    return ch >= L'A' && ch <= L'Z' ? ch + (L'a' - L'A') : ch;
}

static int MmenuTieBreakParse(const char *source, mmenu_tiebreak_spec *spec,
                              char *error, size_t error_size)
{
    if (!spec) return 0;
    spec->count = 0;
    if (source && !*source) {
        if (error && error_size) snprintf(error, error_size, "--tiebreak must not be empty");
        return 0;
    }
    const char *cursor = source ? source : "length,index";
    while (*cursor) {
        const char *end = strchr(cursor, ',');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        if (length == 0) {
            if (error && error_size) snprintf(error, error_size, "--tiebreak contains an empty key");
            return 0;
        }
        mmenu_tiebreak_key key;
        if (length == 6 && memcmp(cursor, "length", 6) == 0) key = MMENU_TIEBREAK_LENGTH;
        else if (length == 5 && memcmp(cursor, "begin", 5) == 0) key = MMENU_TIEBREAK_BEGIN;
        else if (length == 3 && memcmp(cursor, "end", 3) == 0) key = MMENU_TIEBREAK_END;
        else if (length == 5 && memcmp(cursor, "index", 5) == 0) key = MMENU_TIEBREAK_INDEX;
        else if (length == 5 && memcmp(cursor, "chunk", 5) == 0) key = MMENU_TIEBREAK_CHUNK;
        else if (length == 8 && memcmp(cursor, "pathname", 8) == 0) key = MMENU_TIEBREAK_PATHNAME;
        else {
            if (error && error_size)
                snprintf(error, error_size, "invalid --tiebreak key '%.*s' (expected length,begin,end,chunk,pathname,index)",
                         (int)(length > INT_MAX ? INT_MAX : length), cursor);
            return 0;
        }
        for (int i = 0; i < spec->count; i++) {
            if (spec->keys[i] == key) {
                if (error && error_size)
                    snprintf(error, error_size, "--tiebreak key '%.*s' is repeated",
                             (int)(length > INT_MAX ? INT_MAX : length), cursor);
                return 0;
            }
        }
        if (spec->count >= (int)(sizeof(spec->keys) / sizeof(spec->keys[0]))) {
            if (error && error_size) snprintf(error, error_size, "too many --tiebreak keys");
            return 0;
        }
        spec->keys[spec->count++] = key;
        if (!end) break;
        cursor = end + 1;
        if (!*cursor) {
            if (error && error_size) snprintf(error, error_size, "--tiebreak contains an empty key");
            return 0;
        }
    }
    for (int i = 0; i < spec->count; i++) {
        if (spec->keys[i] == MMENU_TIEBREAK_INDEX && i != spec->count - 1) {
            if (error && error_size)
                snprintf(error, error_size, "--tiebreak index must be the final key");
            return 0;
        }
    }
    if (spec->keys[spec->count - 1] != MMENU_TIEBREAK_INDEX)
        spec->keys[spec->count++] = MMENU_TIEBREAK_INDEX;
    return 1;
}

static size_t MmenuByteOffsetToCharacterIndex(const char *text, size_t byte_offset)
{
    size_t count = 0;
    size_t consumed = 0;
    mbstate_t state = {0};
    while (consumed < byte_offset && text[consumed]) {
        wchar_t ch;
        size_t bytes = mbrtowc(&ch, text + consumed, MB_CUR_MAX, &state);
        if (bytes == (size_t)-1 || bytes == (size_t)-2 || bytes == 0) {
            memset(&state, 0, sizeof(state));
            consumed++;
        } else {
            consumed += bytes;
        }
        count++;
    }
    return count;
}

static void MmenuTextMetrics(const char *text, FuzzyMetrics *metrics)
{
    if (!metrics) return;
    memset(metrics, 0, sizeof(*metrics));
    size_t last_separator = SIZE_MAX;
    size_t position = 0;
    mbstate_t state = {0};
    const char *cursor = text;
    while (*cursor) {
        wchar_t ch;
        size_t bytes = mbrtowc(&ch, cursor, MB_CUR_MAX, &state);
        if (bytes == (size_t)-1 || bytes == (size_t)-2 || bytes == 0) {
            memset(&state, 0, sizeof(state));
            ch = (unsigned char)*cursor;
            bytes = 1;
        }
        if (ch == L'/' || ch == L'\\') last_separator = position;
        position++;
        cursor += bytes;
    }
    metrics->length = position;
    metrics->basename_start = last_separator == SIZE_MAX ? 0 : last_separator + 1;
}

static void MmenuSubstringMetrics(const char *candidate, const char *matched_at,
                                  size_t term_length, FuzzyMetrics *metrics)
{
    MmenuTextMetrics(candidate, metrics);
    if (!matched_at || !metrics) return;
    metrics->begin = MmenuByteOffsetToCharacterIndex(candidate,
                                                     (size_t)(matched_at - candidate));
    metrics->end = metrics->begin + (term_length ? term_length - 1 : 0);
    metrics->has_match = term_length != 0;
    metrics->chunks = metrics->has_match ? 1U : 0U;
}


/* Byte-wise ASCII folding keeps matching independent of the active locale.
   UTF-8 bytes outside ASCII remain unchanged, as documented by the API. */
static unsigned char MmenuFoldAsciiByte(unsigned char ch)
{
    return ch >= 'A' && ch <= 'Z' ? (unsigned char)(ch + ('a' - 'A')) : ch;
}

static int MmenuCaseSensitiveBytes(unsigned int flags, const char *query)
{
    if (flags & MMENU_CASE_SENSITIVE) return 1;
    if (!(flags & MMENU_SMART_CASE) || !query) return 0;
    for (const unsigned char *p = (const unsigned char *)query; *p; p++)
        if (*p >= 'A' && *p <= 'Z') return 1;
    return 0;
}

static int MmenuCaseSensitiveWide(unsigned int flags, const wchar_t *query)
{
    if (flags & MMENU_CASE_SENSITIVE) return 1;
    if (!(flags & MMENU_SMART_CASE) || !query) return 0;
    for (const wchar_t *p = query; *p; p++)
        if (*p >= L'A' && *p <= L'Z') return 1;
    return 0;
}

static int MmenuAsciiNcmpPolicy(const char *left, const char *right,
                                size_t length, int case_sensitive)
{
    for (size_t i = 0; i < length; i++) {
        unsigned char a = (unsigned char)left[i];
        unsigned char b = (unsigned char)right[i];
        if (!case_sensitive) {
            a = MmenuFoldAsciiByte(a);
            b = MmenuFoldAsciiByte(b);
        }
        if (a != b) return a < b ? -1 : 1;
        if (a == '\0') return 0;
    }
    return 0;
}

static const char *MmenuAsciiStrcasestr(const char *haystack, const char *needle)
{
    if (!*needle) return haystack;
    size_t needleLength = strlen(needle);
    for (const char *cursor = haystack; *cursor; cursor++) {
        if (MmenuAsciiNcmpPolicy(cursor, needle, needleLength, 0) == 0)
            return cursor;
    }
    return NULL;
}

static const char *MmenuStrstrPolicy(const char *haystack, const char *needle,
                                     unsigned int flags)
{
    if (MmenuCaseSensitiveBytes(flags, needle))
        return strstr(haystack, needle);
    return MmenuAsciiStrcasestr(haystack, needle);
}

static int IsFuzzyBoundary(wchar_t previous, wchar_t current)
{
    if (previous == L'/' || previous == L'\\' || previous == L'_' ||
        previous == L'-' || previous == L'.' || previous == L' ' ||
        previous == L':' || previous == L'\t')
        return 1;
    return previous >= L'a' && previous <= L'z' &&
           current >= L'A' && current <= L'Z';
}

/* Greedy subsequence score: reward early, word-boundary, and adjacent matches;
   penalize leading and internal gaps. Invalid multibyte candidates do not match. */
static int FuzzyScoreLengthMetrics(const char *candidate, const wchar_t *query,
                                   size_t query_len, unsigned int flags,
                                   long long *result, FuzzyMetrics *metrics)
{
    size_t query_pos = 0;
    size_t candidate_pos = 0;
    size_t last_match = SIZE_MAX;
    size_t first_match = SIZE_MAX;
    size_t last_separator = SIZE_MAX;
    unsigned int chunks = 0;
    int case_sensitive = MmenuCaseSensitiveWide(flags, query);
    wchar_t previous = 0;
    long long score = 0;
    mbstate_t state = {0};
    const char *cursor = candidate;

    while (*cursor) {
        wchar_t current;
        size_t bytes = mbrtowc(&current, cursor, MB_CUR_MAX, &state);
        if (bytes == (size_t)-1 || bytes == (size_t)-2 || bytes == 0)
            return 0;

        if (current == L'/' || current == L'\\') last_separator = candidate_pos;
        if (query_pos < query_len &&
            (case_sensitive ? current == query[query_pos] : FoldAscii(current) == FoldAscii(query[query_pos]))) {
            score += 10;
            if (candidate_pos == 0 || IsFuzzyBoundary(previous, current))
                score += 8;
            if (last_match == SIZE_MAX) {
                first_match = candidate_pos;
                chunks = 1;
                if (candidate_pos > (size_t)LLONG_MAX)
                    score = LLONG_MIN / 2;
                else
                    score -= (long long)candidate_pos;
            } else {
                size_t gap = candidate_pos - last_match - 1;
                if (gap == 0) score += 15;
                else {
                    if (chunks < UINT_MAX) chunks++;
                    if (gap > (size_t)LLONG_MAX / 4)
                        score = LLONG_MIN / 2;
                    else score -= (long long)gap;
                }
            }
            last_match = candidate_pos;
            query_pos++;
        }

        previous = current;
        candidate_pos++;
        cursor += bytes;
    }

    if (query_pos != query_len) return 0;
    *result = score;
    if (metrics) {
        memset(metrics, 0, sizeof(*metrics));
        metrics->length = candidate_pos;
        metrics->begin = first_match == SIZE_MAX ? 0 : first_match;
        metrics->end = last_match == SIZE_MAX ? 0 : last_match;
        metrics->basename_start = last_separator == SIZE_MAX ? 0 : last_separator + 1;
        metrics->chunks = chunks;
        metrics->has_match = first_match != SIZE_MAX;
    }
    return 1;
}

static int FuzzyScoreMetrics(const char *candidate, const wchar_t *query,
                             unsigned int flags, long long *result, FuzzyMetrics *metrics)
{
    return FuzzyScoreLengthMetrics(candidate, query, wcslen(query), flags, result, metrics);
}

/* Highlight the same greedy subsequence used by FuzzyScore, clipping by
   terminal display cells so wide Unicode characters do not overrun the row. */
static void RenderFuzzyOption(const wchar_t *option, int columns,
                              const wchar_t *query, unsigned int flags)
{
    size_t query_pos = 0;
    int case_sensitive = MmenuCaseSensitiveWide(flags, query);
    int used_columns = 0;

    for (size_t i = 0; option[i] && used_columns < columns; i++) {
        wchar_t ch = option[i];
        int width = wcwidth(ch);
        if (width < 0) {
            ch = L' ';
            width = 1;
        }
        if (width > 0 && used_columns + width > columns) break;

        int matched = query[query_pos] != L'\0' &&
                      (case_sensitive ? ch == query[query_pos] : FoldAscii(ch) == FoldAscii(query[query_pos]));
        if (matched) {
            query_pos++;
            attron(A_BOLD | A_UNDERLINE);
        }
        addnwstr(&ch, 1);
        if (matched) attroff(A_BOLD | A_UNDERLINE);
        used_columns += width;
    }
}

static int MmenuCompareFuzzyMatches(const FuzzyMatch *a, const FuzzyMatch *b,
                                    const mmenu_tiebreak_spec *spec)
{
    if (a->score > b->score) return -1;
    if (a->score < b->score) return 1;
    if (spec) {
        for (int i = 0; i < spec->count; i++) {
            int comparison = 0;
            switch (spec->keys[i]) {
            case MMENU_TIEBREAK_LENGTH:
                comparison = (a->metrics.length > b->metrics.length) -
                             (a->metrics.length < b->metrics.length);
                break;
            case MMENU_TIEBREAK_BEGIN:
                comparison = (a->metrics.begin > b->metrics.begin) -
                             (a->metrics.begin < b->metrics.begin);
                break;
            case MMENU_TIEBREAK_END:
                comparison = (a->metrics.end > b->metrics.end) -
                             (a->metrics.end < b->metrics.end);
                break;
            case MMENU_TIEBREAK_INDEX:
                comparison = (a->index > b->index) - (a->index < b->index);
                break;
            case MMENU_TIEBREAK_CHUNK:
                comparison = (a->metrics.chunks > b->metrics.chunks) -
                             (a->metrics.chunks < b->metrics.chunks);
                break;
            case MMENU_TIEBREAK_PATHNAME: {
                int a_basename = a->metrics.has_match && a->metrics.begin >= a->metrics.basename_start;
                int b_basename = b->metrics.has_match && b->metrics.begin >= b->metrics.basename_start;
                comparison = b_basename - a_basename;
                break;
            }
            }
            if (comparison) return comparison;
        }
    }
    return (a->index > b->index) - (a->index < b->index);
}

static void MmenuFuzzySiftDown(FuzzyMatch *matches, size_t root, size_t end,
                               const mmenu_tiebreak_spec *spec)
{
    while (root < end / 2) {
        size_t child = root * 2 + 1;
        if (child + 1 < end &&
            MmenuCompareFuzzyMatches(&matches[child], &matches[child + 1], spec) < 0)
            child++;
        if (MmenuCompareFuzzyMatches(&matches[root], &matches[child], spec) >= 0)
            return;
        FuzzyMatch temporary = matches[root];
        matches[root] = matches[child];
        matches[child] = temporary;
        root = child;
    }
}

static void MmenuFuzzySort(FuzzyMatch *matches, int count,
                           const mmenu_tiebreak_spec *spec)
{
    if (!matches || count < 2) return;
    size_t length = (size_t)count;
    for (size_t start = length / 2; start > 0; start--)
        MmenuFuzzySiftDown(matches, start - 1, length, spec);
    for (size_t end = length; end > 1; end--) {
        FuzzyMatch temporary = matches[0];
        matches[0] = matches[end - 1];
        matches[end - 1] = temporary;
        MmenuFuzzySiftDown(matches, 0, end - 1, spec);
    }
}
/* Returns matching indices; MMENU_NO_SORT preserves input order, otherwise
   score ranking is stable for equal scores. */
static int FuzzyCollect(const char *const *options, int n_options,
                        const wchar_t *query, unsigned int flags,
                        const mmenu_tiebreak_spec *tiebreak,
                        FuzzyMatch **result, int *result_count)
{
    FuzzyMatch *matches = NULL;
    int count = 0;
    int capacity = 0;

    *result = NULL;
    *result_count = 0;

    for (int i = 0; i < n_options; i++) {
        long long score;
        FuzzyMetrics metrics = {0};
        if (!FuzzyScoreMetrics(options[i], query, flags, &score, &metrics)) continue;

        if (count == capacity) {
            int new_capacity;
            if (!mmenu_next_capacity(capacity, INITIAL_CAP, sizeof(*matches),
                                     &new_capacity)) {
                free(matches);
                perror("mmenu: fuzzy result capacity");
                return 0;
            }
            FuzzyMatch *new_matches =
                realloc(matches, (size_t)new_capacity * sizeof(*matches));
            if (!new_matches) {
                if (errno == 0) errno = ENOMEM;
                perror("mmenu: growing fuzzy results");
                free(matches);
                return 0;
            }
            matches = new_matches;
            capacity = new_capacity;
        }
        matches[count++] = (FuzzyMatch){.index = i, .score = score, .metrics = metrics};
    }

    /* An empty query means "show everything"; preserve source order rather than
       applying match-position tie-breaks to candidates that matched nothing. */
    if (count > 1 && query[0] != L'\0' && !(flags & MMENU_NO_SORT))
        MmenuFuzzySort(matches, count, tiebreak);
    *result = matches;
    *result_count = count;
    return 1;
}

typedef enum {
    MMENU_QUERY_NORMAL,
    MMENU_QUERY_EXACT,
    MMENU_QUERY_PREFIX,
    MMENU_QUERY_SUFFIX,
    MMENU_QUERY_EQUAL
} mmenu_query_kind;

typedef struct {
    const wchar_t *wide;
    size_t wide_len;
    const char *bytes;
    size_t bytes_len;
    int group;
    int inverse;
    mmenu_query_kind kind;
} mmenu_query_term;

typedef struct {
    mmenu_query_term *terms;
    int count;
    int groups;
    wchar_t *wide_storage;
    char *byte_storage;
} mmenu_query;

static void mmenu_query_free(mmenu_query *query)
{
    if (!query) return;
    free(query->terms);
    free(query->wide_storage);
    free(query->byte_storage);
    memset(query, 0, sizeof(*query));
}

/* Compile the extended expression once per filtering pass. Terms within a
   group are ANDed; groups separated by | are ORed. */
static int mmenu_query_compile(const wchar_t *source, mmenu_query *query)
{
    if (!source || !query) {
        errno = EINVAL;
        return 0;
    }
    memset(query, 0, sizeof(*query));

    size_t length = wcslen(source);
    size_t term_capacity, term_bytes, wide_capacity, wide_bytes;
    size_t byte_capacity, per_character;
    if (!mmenu_size_add(length, 1, &term_capacity) ||
        !mmenu_size_mul(term_capacity, sizeof(*query->terms), &term_bytes) ||
        !mmenu_size_mul(length, 2, &wide_capacity) ||
        !mmenu_size_add(wide_capacity, 1, &wide_capacity) ||
        !mmenu_size_mul(wide_capacity, sizeof(*query->wide_storage), &wide_bytes) ||
        !mmenu_size_add((size_t)MB_LEN_MAX, 1, &per_character) ||
        !mmenu_size_mul(length, per_character, &byte_capacity) ||
        !mmenu_size_add(byte_capacity, 1, &byte_capacity)) {
        errno = ENOMEM;
        return 0;
    }

    query->terms = malloc(term_bytes);
    query->wide_storage = malloc(wide_bytes);
    query->byte_storage = malloc(byte_capacity);
    if (!query->terms || !query->wide_storage || !query->byte_storage) {
        if (errno == 0) errno = ENOMEM;
        mmenu_query_free(query);
        return 0;
    }

    size_t wide_used = 0;
    size_t byte_used = 0;
    size_t pos = 0;
    int group = 0;
    int group_has_terms = 0;

    while (pos < length) {
        if (iswspace(source[pos])) {
            pos++;
            continue;
        }
        if (source[pos] == L'|') {
            if (group_has_terms) {
                group++;
                group_has_terms = 0;
            }
            pos++;
            continue;
        }

        int inverse = 0;
        mmenu_query_kind kind = MMENU_QUERY_NORMAL;
        if (source[pos] == L'!') {
            inverse = 1;
            pos++;
        }
        if (pos < length && source[pos] == L'\'') {
            kind = MMENU_QUERY_EXACT;
            pos++;
        } else if (pos < length && source[pos] == L'^') {
            kind = MMENU_QUERY_PREFIX;
            pos++;
        }

        size_t start = pos;
        while (pos < length && !iswspace(source[pos]) && source[pos] != L'|')
            pos++;
        size_t end = pos;
        if (end > start && source[end - 1] == L'$') {
            end--;
            if (kind == MMENU_QUERY_PREFIX) kind = MMENU_QUERY_EQUAL;
            else if (kind == MMENU_QUERY_NORMAL) kind = MMENU_QUERY_SUFFIX;
        }
        if (end == start) continue;

        size_t term_len = end - start;
        if (term_len + 1 > wide_capacity - wide_used) {
            errno = ENOMEM;
            mmenu_query_free(query);
            return 0;
        }
        wchar_t *wide_term = query->wide_storage + wide_used;
        memcpy(wide_term, source + start, term_len * sizeof(*wide_term));
        wide_term[term_len] = L'\0';
        wide_used += term_len + 1;

        errno = 0;
        size_t bytes_len = wcstombs(NULL, wide_term, 0);
        if (bytes_len == (size_t)-1) {
            errno = EILSEQ;
            mmenu_query_free(query);
            return 0;
        }
        if (bytes_len >= byte_capacity - byte_used) {
            errno = ENOMEM;
            mmenu_query_free(query);
            return 0;
        }
        size_t converted = wcstombs(query->byte_storage + byte_used, wide_term,
                                    byte_capacity - byte_used);
        if (converted == (size_t)-1 || converted != bytes_len) {
            errno = EILSEQ;
            mmenu_query_free(query);
            return 0;
        }

        query->terms[query->count++] = (mmenu_query_term){
            .wide = wide_term,
            .wide_len = term_len,
            .bytes = query->byte_storage + byte_used,
            .bytes_len = bytes_len,
            .group = group,
            .inverse = inverse,
            .kind = kind
        };
        byte_used += bytes_len + 1;
        if (query->groups < group + 1) query->groups = group + 1;
        group_has_terms = 1;
    }
    return 1;
}

static int mmenu_query_term_match(const char *candidate,
                                  const mmenu_query_term *term,
                                  unsigned int flags, long long *score,
                                  FuzzyMetrics *metrics)
{
    *score = 0;
    if (metrics) MmenuTextMetrics(candidate, metrics);
    size_t candidate_len = strlen(candidate);
    int matched = 0;
    const char *match_at = NULL;

    switch (term->kind) {
    case MMENU_QUERY_EXACT:
        match_at = MmenuStrstrPolicy(candidate, term->bytes, flags);
        matched = match_at != NULL;
        if (matched && (flags & MMENU_FUZZY)) *score = 10;
        if (matched && metrics)
            MmenuSubstringMetrics(candidate, match_at, term->wide_len, metrics);
        break;
    case MMENU_QUERY_PREFIX:
        matched = candidate_len >= term->bytes_len &&
                  MmenuAsciiNcmpPolicy(candidate, term->bytes, term->bytes_len, MmenuCaseSensitiveBytes(flags, term->bytes)) == 0;
        if (matched && (flags & MMENU_FUZZY)) *score = 20;
        if (matched && metrics) MmenuSubstringMetrics(candidate, candidate, term->wide_len, metrics);
        break;
    case MMENU_QUERY_SUFFIX:
        matched = candidate_len >= term->bytes_len &&
                  MmenuAsciiNcmpPolicy(candidate + candidate_len - term->bytes_len, term->bytes, term->bytes_len, MmenuCaseSensitiveBytes(flags, term->bytes)) == 0;
        if (matched && (flags & MMENU_FUZZY)) *score = 15;
        if (matched && metrics)
            MmenuSubstringMetrics(candidate, candidate + candidate_len - term->bytes_len, term->wide_len, metrics);
        break;
    case MMENU_QUERY_EQUAL:
        matched = candidate_len == term->bytes_len &&
                  MmenuAsciiNcmpPolicy(candidate, term->bytes, term->bytes_len, MmenuCaseSensitiveBytes(flags, term->bytes)) == 0;
        if (matched && (flags & MMENU_FUZZY)) *score = 30;
        if (matched && metrics) MmenuSubstringMetrics(candidate, candidate, term->wide_len, metrics);
        break;
    case MMENU_QUERY_NORMAL:
        if (flags & MMENU_FUZZY)
            matched = FuzzyScoreLengthMetrics(candidate, term->wide, term->wide_len, flags, score, metrics);
        else {
            match_at = MmenuStrstrPolicy(candidate, term->bytes, flags);
            matched = match_at != NULL;
            if (matched && metrics)
                MmenuSubstringMetrics(candidate, match_at, term->wide_len, metrics);
        }
        break;
    }
    return matched;
}

static int mmenu_query_match(const mmenu_query *query, const char *candidate,
                             unsigned int flags, long long *result_score,
                             FuzzyMetrics *result_metrics)
{
    if (!query || !candidate || !result_score) return 0;
    if (query->count == 0) {
        *result_score = 0;
        if (result_metrics) MmenuTextMetrics(candidate, result_metrics);
        return 1;
    }

    int found = 0;
    long long best_score = LLONG_MIN;
    FuzzyMetrics best_metrics = {0};
    unsigned int effective_flags = flags;
    if (flags & MMENU_SMART_CASE) {
        int has_uppercase = 0;
        for (int i = 0; i < query->count && !has_uppercase; i++)
            for (const wchar_t *p = query->terms[i].wide; *p; p++)
                if (*p >= L'A' && *p <= L'Z') { has_uppercase = 1; break; }
        effective_flags &= ~MMENU_SMART_CASE;
        if (has_uppercase) effective_flags |= MMENU_CASE_SENSITIVE;
    }
    for (int group = 0; group < query->groups; group++) {
        int has_terms = 0;
        int group_matches = 1;
        long long group_score = 0;
        FuzzyMetrics group_metrics;
        MmenuTextMetrics(candidate, &group_metrics);
        group_metrics.begin = SIZE_MAX;
        group_metrics.end = 0;
        group_metrics.chunks = 0;
        group_metrics.has_match = 0;
        for (int i = 0; i < query->count; i++) {
            const mmenu_query_term *term = &query->terms[i];
            if (term->group != group) continue;
            has_terms = 1;
            long long term_score = 0;
            FuzzyMetrics term_metrics = {0};
            int matched = mmenu_query_term_match(candidate, term, effective_flags,
                                                  &term_score, &term_metrics);
            if (term->inverse) {
                if (matched) group_matches = 0;
            } else if (!matched) {
                group_matches = 0;
            } else {
                if (term_metrics.has_match) {
                    if (!group_metrics.has_match || term_metrics.begin < group_metrics.begin)
                        group_metrics.begin = term_metrics.begin;
                    if (!group_metrics.has_match || term_metrics.end > group_metrics.end)
                        group_metrics.end = term_metrics.end;
                    if (UINT_MAX - group_metrics.chunks < term_metrics.chunks)
                        group_metrics.chunks = UINT_MAX;
                    else group_metrics.chunks += term_metrics.chunks;
                    group_metrics.has_match = 1;
                }
                if (flags & MMENU_FUZZY) {
                    if (term_score > 0 && group_score > LLONG_MAX - term_score)
                        group_score = LLONG_MAX;
                    else if (term_score < 0 && group_score < LLONG_MIN - term_score)
                        group_score = LLONG_MIN;
                    else
                        group_score += term_score;
                }
            }
        }
        if (has_terms && group_matches &&
            (!found || group_score > best_score)) {
            found = 1;
            best_score = group_score;
            best_metrics = group_metrics;
            if (!best_metrics.has_match) best_metrics.begin = 0;
        }
    }
    *result_score = found ? best_score : 0;
    if (found && result_metrics) *result_metrics = best_metrics;
    return found;
}

static int mmenu_extended_collect(const char *const *options, int start, int end,
                                  const wchar_t *source, unsigned int flags,
                                  const mmenu_tiebreak_spec *tiebreak,
                                  FuzzyMatch **result, int *result_count)
{
    FuzzyMatch *matches = NULL;
    int count = 0;
    int capacity = 0;
    mmenu_query query;

    *result = NULL;
    *result_count = 0;
    if (!mmenu_query_compile(source, &query)) {
        perror("mmenu: compiling extended query");
        return 0;
    }

    for (int i = start; i < end; i++) {
        long long score = 0;
        FuzzyMetrics metrics = {0};
        if (!mmenu_query_match(&query, options[i], flags, &score, &metrics)) continue;
        if (count == capacity) {
            int new_capacity;
            if (!mmenu_next_capacity(capacity, INITIAL_CAP, sizeof(*matches),
                                     &new_capacity)) {
                perror("mmenu: extended result capacity");
                free(matches);
                mmenu_query_free(&query);
                return 0;
            }
            FuzzyMatch *grown = realloc(matches,
                                        (size_t)new_capacity * sizeof(*matches));
            if (!grown) {
                if (errno == 0) errno = ENOMEM;
                perror("mmenu: growing extended results");
                free(matches);
                mmenu_query_free(&query);
                return 0;
            }
            matches = grown;
            capacity = new_capacity;
        }
        matches[count++] = (FuzzyMatch){.index = i, .score = score, .metrics = metrics};
    }

    if ((flags & MMENU_FUZZY) && !(flags & MMENU_NO_SORT) && count > 1)
        MmenuFuzzySort(matches, count, tiebreak);
    mmenu_query_free(&query);
    *result = matches;
    *result_count = count;
    return 1;
}
static int filt_init(filt *f) {
    f->cap = INITIAL_CAP;
    f->count = 0;
    f->indices = malloc((size_t)f->cap * sizeof(int));
    if (!f->indices) {
        perror("mmenu: allocating filter");
        f->cap = 0;
        return 0;
    }
    return 1;
}

static int filt_push(filt *f, int idx) {
    if (f->count == f->cap) {
        int new_cap;
        if (!mmenu_next_capacity(f->cap, INITIAL_CAP, sizeof(*f->indices),
                                 &new_cap)) {
            perror("mmenu: filter capacity");
            return 0;
        }
        int *new_indices = realloc(f->indices, (size_t)new_cap * sizeof(int));
        if (!new_indices) {
            perror("mmenu: growing filter");
            return 0;
        }
        f->indices = new_indices;
        f->cap = new_cap;
    }
    f->indices[f->count++] = idx;
    return 1;
}

static void filt_clear(filt *f) { f->count = 0; }

static void mmenu_draw_preview(const char *text, int rows, int cols, int menu_cols) {
    int start = menu_cols + 1;
    int width = cols - start;
    if (width <= 0) return;
    for (int row = 0; row < rows; row++)
        mvaddch(row, menu_cols, ACS_VLINE);
    move(0, start);
    attron(A_BOLD);
    addnstr("Preview", width);
    attroff(A_BOLD);
    clrtoeol();

    const char *line = text ? text : "";
    int row = 1;
    while (row < rows && *line) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        move(row++, start);
        mbstate_t state = {0};
        size_t pos = 0;
        int used_columns = 0;
        while (pos < len && used_columns < width) {
            wchar_t ch;
            size_t bytes = mbrtowc(&ch, line + pos, len - pos, &state);
            if (bytes == (size_t)-1 || bytes == (size_t)-2 || bytes == 0) {
                ch = L'?';
                bytes = 1;
                memset(&state, 0, sizeof(state));
            }
            pos += bytes;
            if (ch == L'\t' || iswcntrl(ch)) ch = ch == L'\t' ? L' ' : L'?';
            int char_width = wcwidth(ch);
            if (char_width < 0) {
                ch = L'?';
                char_width = 1;
            }
            if (used_columns + char_width > width) break;
            addnwstr(&ch, 1);
            used_columns += char_width;
        }
        clrtoeol();
        if (!end) break;
        line = end + 1;
    }
    while (row < rows) {
        move(row++, start);
        clrtoeol();
    }
}

static int mmenu_prompt_display_width(const char *prompt, int max_columns) {
    if (!prompt || max_columns <= 0) return 0;

    size_t length = strlen(prompt);
    size_t offset = 0;
    int columns = 0;
    mbstate_t state = {0};
    while (offset < length && columns < max_columns) {
        wchar_t ch;
        size_t bytes = mbrtowc(&ch, prompt + offset, length - offset, &state);
        if (bytes == (size_t)-1 || bytes == (size_t)-2 || bytes == 0) {
            ch = L'?';
            bytes = 1;
            memset(&state, 0, sizeof(state));
        }
        offset += bytes;

        int width = wcwidth(ch);
        if (width < 0) width = 1;
        if (width > max_columns - columns) return max_columns;
        columns += width;
    }
    return columns;
}

static int mmenu_query_cursor_column(size_t prompt_len, int input_len,
                                       int menu_cols) {
    if (menu_cols <= 0) return 0;
    if (input_len < 0) input_len = 0;

    size_t max_column = (size_t)(menu_cols - 1);
    if (prompt_len > max_column ||
        (size_t)input_len > max_column - prompt_len)
        return menu_cols - 1;

    return (int)(prompt_len + (size_t)input_len);
}

static void mmenu_position_query_cursor(size_t prompt_len, int input_len, int menu_cols) {
    move(0, mmenu_query_cursor_column(prompt_len, input_len, menu_cols));
}



static void mmenu_update_preview(const mmenu_config *config,
                                 const char *const *options, int n_options,
                                 const filt *filtered, int selection,
                                 const wchar_t *query, char *output,
                                 size_t output_size) {
    if (!output || output_size == 0) return;
    output[0] = '\0';
    if (!config || !config->preview) return;
    if (selection < 0 || selection >= filtered->count ||
        filtered->count <= 0 || filtered->indices[selection] < 0 ||
        filtered->indices[selection] >= n_options) {
        snprintf(output, output_size, "No matching option");
        return;
    }
    int index = filtered->indices[selection];
    int status = config->preview(options[index], index, query, output,
                                 output_size, config->preview_userdata);
    output[output_size - 1] = '\0';
    if (status != 0 && output[0] == '\0')
        snprintf(output, output_size, "Preview command failed");
}

/* Pull a bounded batch of available records into the stable pointer vector.
   The caller owns record storage; this helper owns only the pointer vector. */
static int mmenu_stream_append_available(const mmenu_config *config,
                                         const char ***options,
                                         int *n_options, int *options_cap,
                                         unsigned char **selected_options,
                                         int *selected_options_cap,
                                         int multi, int *finished, int *added) {
    int budget = *n_options < 1024 ? 256 : *n_options / 4;
    if (budget < 256) budget = 256;
    if (budget > 65536) budget = 65536;
    *added = 0;
    while (!*finished && budget-- > 0) {
        const char *record = NULL;
        errno = 0;
        int status = config->stream_next(config->stream_userdata, &record);
        if (status == MMENU_STREAM_PENDING) return 1;
        if (status == MMENU_STREAM_EOF) {
            *finished = 1;
            return 1;
        }
        if (status == MMENU_STREAM_ERROR) {
            if (errno == 0) errno = EIO;
            perror("mmenu: reading streamed input");
            return 0;
        }
        if (status != MMENU_STREAM_RECORD || !record) {
            errno = EINVAL;
            perror("mmenu: invalid stream callback result");
            return 0;
        }
        if (mbstowcs(NULL, record, 0) == (size_t)-1) {
            fprintf(stderr, "mmenu: streamed option %d is not valid in the current locale\n",
                    *n_options);
            errno = EILSEQ;
            return 0;
        }
        if (*n_options == INT_MAX) {
            errno = ENOMEM;
            perror("mmenu: too many streamed options");
            return 0;
        }
        if (*n_options == *options_cap) {
            int new_capacity;
            if (!mmenu_next_capacity(*options_cap, INITIAL_CAP,
                                     sizeof(**options), &new_capacity)) {
                perror("mmenu: streamed option capacity");
                return 0;
            }
            size_t bytes;
            if (!mmenu_size_mul((size_t)new_capacity, sizeof(**options), &bytes)) {
                perror("mmenu: streamed option capacity");
                return 0;
            }
            const char **grown = realloc((void *)*options, bytes);
            if (!grown) {
                if (errno == 0) errno = ENOMEM;
                perror("mmenu: growing streamed options");
                return 0;
            }
            *options = grown;
            if (multi && *selected_options_cap < new_capacity) {
                unsigned char *grown_selected = realloc(*selected_options,
                                                        (size_t)new_capacity);
                if (!grown_selected) {
                    if (errno == 0) errno = ENOMEM;
                    perror("mmenu: growing streamed multi-select state");
                    return 0;
                }
                memset(grown_selected + *selected_options_cap, 0,
                       (size_t)(new_capacity - *selected_options_cap));
                *selected_options = grown_selected;
                *selected_options_cap = new_capacity;
            }
            *options_cap = new_capacity;
        }
        (*options)[*n_options] = record;
        (*n_options)++;
        *added = 1;
    }
    return 1;
}

static int mmenu_scores_reserve(long long **scores, int *capacity, int needed) {
    if (needed <= *capacity) return 1;
    int next;
    if (!mmenu_next_capacity(*capacity, INITIAL_CAP, sizeof(**scores), &next)) {
        perror("mmenu: streamed score capacity");
        return 0;
    }
    while (next < needed) {
        if (next > INT_MAX / 2) {
            errno = ENOMEM;
            perror("mmenu: streamed score capacity");
            return 0;
        }
        next *= 2;
    }
    size_t bytes;
    if (!mmenu_size_mul((size_t)next, sizeof(**scores), &bytes)) {
        perror("mmenu: streamed score capacity");
        return 0;
    }
    long long *grown = realloc(*scores, bytes);
    if (!grown) {
        if (errno == 0) errno = ENOMEM;
        perror("mmenu: growing streamed scores");
        return 0;
    }
    *scores = grown;
    *capacity = next;
    return 1;
}

/* Filter only newly arrived records. Fuzzy results are inserted by score while
   preserving input order for ties; substring/exact results naturally append. */
static int mmenu_metrics_reserve(FuzzyMetrics **metrics, int *capacity, int needed) {
    if (needed <= *capacity) return 1;
    int next;
    if (!mmenu_next_capacity(*capacity, INITIAL_CAP, sizeof(**metrics), &next)) {
        perror("mmenu: streamed match metrics capacity");
        return 0;
    }
    while (next < needed) {
        if (next > INT_MAX / 2) {
            errno = ENOMEM;
            perror("mmenu: streamed match metrics capacity");
            return 0;
        }
        next *= 2;
    }
    size_t bytes;
    if (!mmenu_size_mul((size_t)next, sizeof(**metrics), &bytes)) {
        perror("mmenu: streamed match metrics capacity");
        return 0;
    }
    FuzzyMetrics *grown = realloc(*metrics, bytes);
    if (!grown) {
        if (errno == 0) errno = ENOMEM;
        perror("mmenu: growing streamed match metrics");
        return 0;
    }
    *metrics = grown;
    *capacity = next;
    return 1;
}

static int mmenu_filter_stream_batch(const char *const *options,
                                     int start, int end,
                                     const wchar_t *query, unsigned int flags,
                                     const mmenu_tiebreak_spec *tiebreak,
                                     filt *filtered, long long **scores,
                                     int *scores_cap, FuzzyMetrics **metrics,
                                     int *metrics_cap) {
    if ((flags & MMENU_DISABLED) || !query[0]) {
        for (int i = start; i < end; i++)
            if (!filt_push(filtered, i)) return 0;
        return 1;
    }
    if ((flags & MMENU_EXTENDED) && !(flags & MMENU_FUZZY)) {
        FuzzyMatch *batch = NULL;
        int batch_count = 0;
        if (!mmenu_extended_collect(options, start, end, query, flags,
                                    tiebreak, &batch, &batch_count))
            return 0;
        for (int i = 0; i < batch_count; i++) {
            if (!filt_push(filtered, batch[i].index)) {
                free(batch);
                return 0;
            }
        }
        free(batch);
        return 1;
    }

    if (flags & MMENU_FUZZY) {
        FuzzyMatch *batch = NULL;
        int batch_count = 0;
        int batch_cap = 0;
        if (flags & MMENU_EXTENDED) {
            if (!mmenu_extended_collect(options, start, end, query, flags,
                                        tiebreak, &batch, &batch_count))
                return 0;
            batch_cap = batch_count;
        } else {
            for (int i = start; i < end; i++) {
                long long score;
                FuzzyMetrics metrics = {0};
                if (!FuzzyScoreMetrics(options[i], query, flags, &score, &metrics)) continue;
            if (batch_count == batch_cap) {
                int new_cap;
                if (!mmenu_next_capacity(batch_cap, INITIAL_CAP,
                                         sizeof(*batch), &new_cap)) {
                    free(batch);
                    perror("mmenu: streamed fuzzy batch capacity");
                    return 0;
                }
                FuzzyMatch *grown = realloc(batch, (size_t)new_cap * sizeof(*batch));
                if (!grown) {
                    if (errno == 0) errno = ENOMEM;
                    free(batch);
                    perror("mmenu: growing streamed fuzzy batch");
                    return 0;
                }
                batch = grown;
                batch_cap = new_cap;
            }
                batch[batch_count++] = (FuzzyMatch){.index = i, .score = score, .metrics = metrics};
            }
            if (batch_count > 1 && !(flags & MMENU_NO_SORT))
                MmenuFuzzySort(batch, batch_count, tiebreak);
        }
        if (batch_count == 0) {
            free(batch);
            return 1;
        }

        int old_count = filtered->count;
        if (batch_count > INT_MAX - old_count) {
            free(batch);
            errno = ENOMEM;
            perror("mmenu: streamed fuzzy result count");
            return 0;
        }
        int total = old_count + batch_count;
        if (total > filtered->cap) {
            int new_cap = filtered->cap;
            while (new_cap < total) {
                int next;
                if (!mmenu_next_capacity(new_cap, INITIAL_CAP,
                                         sizeof(*filtered->indices), &next)) {
                    free(batch);
                    perror("mmenu: streamed fuzzy result capacity");
                    return 0;
                }
                new_cap = next;
            }
            size_t bytes;
            if (!mmenu_size_mul((size_t)new_cap, sizeof(*filtered->indices), &bytes)) {
                free(batch);
                perror("mmenu: streamed fuzzy result capacity");
                return 0;
            }
            int *grown = realloc(filtered->indices, bytes);
            if (!grown) {
                if (errno == 0) errno = ENOMEM;
                free(batch);
                perror("mmenu: growing streamed fuzzy results");
                return 0;
            }
            filtered->indices = grown;
            filtered->cap = new_cap;
        }
        if (!mmenu_scores_reserve(scores, scores_cap, total) ||
            !mmenu_metrics_reserve(metrics, metrics_cap, total)) {
            free(batch);
            return 0;
        }

        if (flags & MMENU_NO_SORT) {
            for (int i = 0; i < batch_count; i++) {
                filtered->indices[old_count + i] = batch[i].index;
                (*scores)[old_count + i] = batch[i].score;
                (*metrics)[old_count + i] = batch[i].metrics;
            }
            filtered->count = total;
            free(batch);
            return 1;
        }

        /* Merge backward in-place using the same full ordering as batch mode. */
        int old_pos = old_count - 1;
        int batch_pos = batch_count - 1;
        int out_pos = total - 1;
        while (old_pos >= 0 && batch_pos >= 0) {
            FuzzyMatch old_match = {
                .index = filtered->indices[old_pos],
                .score = (*scores)[old_pos],
                .metrics = (*metrics)[old_pos]
            };
            if (MmenuCompareFuzzyMatches(&old_match, &batch[batch_pos], tiebreak) < 0) {
                filtered->indices[out_pos] = batch[batch_pos].index;
                (*scores)[out_pos] = batch[batch_pos].score;
                (*metrics)[out_pos] = batch[batch_pos].metrics;
                batch_pos--;
            } else {
                filtered->indices[out_pos] = filtered->indices[old_pos];
                (*scores)[out_pos] = (*scores)[old_pos];
                (*metrics)[out_pos] = (*metrics)[old_pos];
                old_pos--;
            }
            out_pos--;
        }
        while (batch_pos >= 0) {
            filtered->indices[out_pos] = batch[batch_pos].index;
            (*scores)[out_pos] = batch[batch_pos].score;
            (*metrics)[out_pos] = batch[batch_pos].metrics;
            batch_pos--;
            out_pos--;
        }
        filtered->count = total;
        free(batch);
        return 1;
    }

    errno = 0;
    char *q = wc_to_mb(query);
    if (!q) {
        if (errno == 0) errno = EILSEQ;
        perror("mmenu: converting streamed filter");
        return 0;
    }
    for (int i = start; i < end; i++) {
        int matches = (flags & MMENU_EXACT)
                    ? strcmp(options[i], q) == 0
                    : MmenuStrstrPolicy(options[i], q, flags) != NULL;
        if (matches && !filt_push(filtered, i)) {
            free(q);
            return 0;
        }
    }
    free(q);
    return 1;
}

static int mmenu_internal(const char *const *options, int n_options,
                          const char *prompt, unsigned int flags, int multi,
                          const mmenu_config *config,
                          int **selected_indices, int *selected_count) {
    mmenu_tiebreak_spec tiebreak;
    char tiebreak_error[256];
    const char *const *current_options = options;
    const char *const *current_match_options = options;
    const char *const *current_display_options = options;
    const char **match_options = NULL;
    const char **display_options = NULL;
    int match_options_cap = 0;
    int display_options_cap = 0;
    int use_search_text = config && config->search_text;
    int use_display_text = config && config->display_text;
    const char **stream_options = NULL;
    int stream_options_cap = 0;
    int stream_finished = 0;
    long long *filtered_scores = NULL;
    int filtered_scores_cap = 0;
    FuzzyMetrics *filtered_metrics = NULL;
    int filtered_metrics_cap = 0;
    if (multi) {
        if (!selected_indices || !selected_count) {
            fprintf(stderr, "mmenu: multi-select output pointers must not be NULL\n");
            return -1;
        }
        *selected_indices = NULL;
        *selected_count = 0;
    }
    /* Validate the public API before touching the terminal or initializing ncurses. */
    if ((flags & ~(MMENU_FUZZY | MMENU_EXACT | MMENU_EXTENDED | MMENU_CASE_SENSITIVE | MMENU_SMART_CASE | MMENU_NO_SORT | MMENU_DISABLED)) ||
        ((flags & MMENU_CASE_SENSITIVE) && (flags & MMENU_SMART_CASE)) ||
        ((flags & MMENU_NO_SORT) && !(flags & MMENU_FUZZY)) ||
        ((flags & MMENU_FUZZY) && (flags & MMENU_EXACT)) ||
        ((flags & MMENU_EXTENDED) && (flags & MMENU_EXACT))) {
        fprintf(stderr, "mmenu: unsupported API flags or conflicting matching modes\n");
        return -1;
    }
    if (config && config->tiebreak && !(flags & MMENU_FUZZY)) {
        fprintf(stderr, "mmenu: tiebreak requires fuzzy matching\n");
        return -1;
    }
    if (!MmenuTieBreakParse(config ? config->tiebreak : NULL, &tiebreak,
                            tiebreak_error, sizeof(tiebreak_error))) {
        fprintf(stderr, "mmenu: %s\n", tiebreak_error);
        return -1;
    }
    if (n_options < 0) {
        fprintf(stderr, "mmenu: option count must not be negative\\n");
        return -1;
    }
    if (n_options > 0 && !options) {
        fprintf(stderr, "mmenu: options must not be NULL when option count is positive\\n");
        return -1;
    }
    for (int i = 0; i < n_options; i++) {
        if (!current_options[i]) {
            fprintf(stderr, "mmenu: option %d must not be NULL\\n", i);
            return -1;
        }
    }

    setlocale(LC_ALL, "");

    if (config && (config->height < 0 || config->height_percent < 0 ||
                   config->height_percent > 100 ||
                   (config->height > 0 && config->height_percent > 0))) {
        fprintf(stderr, "mmenu: invalid height configuration; use rows or a percentage from 1 to 100, not both\n");
        return -1;
    }
    if (config) {
        const wint_t keys[] = {config->key_up, config->key_down,
                               config->key_accept, config->key_cancel,
                               config->key_toggle, config->key_backspace};
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
            if (keys[i] && (keys[i] > (wint_t)WCHAR_MAX || !iswprint(keys[i]))) {
                fprintf(stderr, "mmenu: custom keys must be printable characters\n");
                return -1;
            }
            for (size_t j = 0; keys[i] && j < i; j++) {
                if (keys[i] == keys[j]) {
                    fprintf(stderr, "mmenu: a custom key cannot be assigned to multiple actions\n");
                    return -1;
                }
            }
        }
    }

    for (int i = 0; i < n_options; i++) {
        if (mbstowcs(NULL, current_options[i], 0) == (size_t)-1) {
            fprintf(stderr, "mmenu: option %d is not valid in the current locale\n", i);
            return -1;
        }
    }
    size_t initial_query_len = 0;
    if (config && config->initial_query) {
        initial_query_len = mbstowcs(NULL, config->initial_query, 0);
        if (initial_query_len == (size_t)-1) {
            fprintf(stderr, "mmenu: initial query is not valid in the current locale\n");
            return -1;
        }
        if (initial_query_len > MAX_INPUT_LEN) {
            fprintf(stderr, "mmenu: initial query exceeds %d characters\n", MAX_INPUT_LEN);
            return -1;
        }
    }
    if (prompt && mbstowcs(NULL, prompt, 0) == (size_t)-1) {
        fprintf(stderr, "mmenu: prompt is not valid in the current locale\n");
        return -1;
    }

    if (config && config->stream_next) {
        stream_options_cap = n_options > INITIAL_CAP ? n_options : INITIAL_CAP;
        size_t bytes;
        if (!mmenu_size_mul((size_t)stream_options_cap, sizeof(*stream_options), &bytes)) {
            perror("mmenu: streamed option vector size");
            return -1;
        }
        stream_options = malloc(bytes);
        if (!stream_options) {
            if (errno == 0) errno = ENOMEM;
            perror("mmenu: allocating streamed option vector");
            return -1;
        }
        for (int i = 0; i < n_options; i++) stream_options[i] = options[i];
        current_options = (const char *const *)stream_options;
    }

    FILE *tty = fopen("/dev/tty", "r+");
    if (!tty) { free(stream_options); perror("mmenu: opening /dev/tty"); return -1; }

    SCREEN *scr = newterm(NULL, tty, tty);
    if (!scr) { free(stream_options); fclose(tty); fprintf(stderr, "Failed to init ncurses\n"); return -1; }
    set_term(scr);

    /* raw() disables terminal-generated SIGINT so Ctrl-C reaches wget_wch and can cancel cleanly. */
    raw(); noecho(); keypad(stdscr, TRUE);
    /* Bound idle input waits so a resize arriving just before wget_wch cannot be missed. */
    wtimeout(stdscr, 100);
    struct sigaction resize_action = {0};
    struct sigaction previous_resize_action;
    resize_action.sa_handler = handle_resize;
    resize_action.sa_flags = 0; /* Interrupt blocking input so the loop can redraw immediately. */
    sigemptyset(&resize_action.sa_mask);
    resize_flag = 0;
    if (sigaction(SIGWINCH, &resize_action, &previous_resize_action) != 0) {
        perror("mmenu: installing resize handler");
        endwin();
        delscreen(scr);
        fclose(tty);
        free(stream_options);
        return -1;
    }
    int restore_resize_action = 1;

    int rows, cols; getmaxyx(stdscr, rows, cols);
    int has_preview = config && config->preview;
    int preview_visible = has_preview && cols >= 40 && rows >= 4;
    int menu_cols = preview_visible ? cols / 2 : cols;

    int ret = -1;
    unsigned char *selected_options = NULL;
    int selected_options_cap = 0;
    filt filtered = {0};
    wchar_t input[MAX_INPUT_LEN + 1] = {0};
    int input_len = 0;
    if (config && config->initial_query) {
        size_t converted = mbstowcs(input, config->initial_query, MAX_INPUT_LEN + 1);
        if (converted == (size_t)-1 || converted != initial_query_len) {
            fprintf(stderr, "mmenu: converting initial query failed\n");
            goto cleanup;
        }
        input_len = (int)converted;
    }
    wint_t key_up = KEY_UP;
    wint_t key_down = KEY_DOWN;
    wint_t key_accept = config ? config->key_accept : 0;
    wint_t key_cancel = config ? config->key_cancel : 0;
    wint_t key_toggle = config ? config->key_toggle : 0;
    wint_t key_backspace = config ? config->key_backspace : 0;

    if (!filt_init(&filtered)) goto cleanup;
    if (use_search_text) {
        match_options_cap = n_options > INITIAL_CAP ? n_options : INITIAL_CAP;
        size_t match_bytes;
        if (!mmenu_size_mul((size_t)match_options_cap, sizeof(*match_options),
                            &match_bytes)) {
            perror("mmenu: searchable option vector size");
            goto cleanup;
        }
        match_options = malloc(match_bytes);
        if (!match_options) {
            if (errno == 0) errno = ENOMEM;
            perror("mmenu: allocating searchable option vector");
            goto cleanup;
        }
        for (int i = 0; i < n_options; i++) {
            match_options[i] = config->search_text(current_options[i], i,
                                                    config->search_text_userdata);
            if (!match_options[i]) {
                fprintf(stderr, "mmenu: searchable text callback failed for option %d\n", i);
                goto cleanup;
            }
            if (mbstowcs(NULL, match_options[i], 0) == (size_t)-1) {
                fprintf(stderr, "mmenu: searchable text for option %d is not valid in the current locale\n", i);
                goto cleanup;
            }
        }
        current_match_options = (const char *const *)match_options;
    } else {
        current_match_options = current_options;
    }
    if (use_display_text) {
        display_options_cap = n_options > INITIAL_CAP ? n_options : INITIAL_CAP;
        size_t display_bytes;
        if (!mmenu_size_mul((size_t)display_options_cap, sizeof(*display_options),
                            &display_bytes)) {
            perror("mmenu: display option vector size");
            goto cleanup;
        }
        display_options = malloc(display_bytes);
        if (!display_options) {
            if (errno == 0) errno = ENOMEM;
            perror("mmenu: allocating display option vector");
            goto cleanup;
        }
        for (int i = 0; i < n_options; i++) {
            display_options[i] = config->display_text(current_options[i], i,
                                                       config->display_text_userdata);
            if (!display_options[i]) {
                fprintf(stderr, "mmenu: display text callback failed for option %d\n", i);
                goto cleanup;
            }
            if (mbstowcs(NULL, display_options[i], 0) == (size_t)-1) {
                fprintf(stderr, "mmenu: display text for option %d is not valid in the current locale\n", i);
                goto cleanup;
            }
        }
        current_display_options = (const char *const *)display_options;
    } else {
        current_display_options = current_options;
    }
    if (multi) {
        size_t selected_capacity = config && config->stream_next
                                 ? (size_t)stream_options_cap : (size_t)n_options;
        if (selected_capacity > 0) {
            selected_options = malloc(selected_capacity);
            if (selected_options) memset(selected_options, 0, selected_capacity);
            if (!selected_options) {
                if (errno == 0) errno = ENOMEM;
                perror("mmenu: allocating multi-select state");
                goto cleanup;
            }
            selected_options_cap = (int)selected_capacity;
        }
    }

    int selection = 0;
    int top = 0;
    int need_filter = 0;
    int preview_dirty = 1;
    char preview_output[MMENU_PREVIEW_BUFFER_SIZE] = {0};
    int prev_input_len = input_len;   /* for incremental filter optimization */

    const char *prompt_str = prompt ? prompt : "> ";
    size_t prompt_columns = (size_t)mmenu_prompt_display_width(prompt_str, INT_MAX);

    /* Initial filter (show all for empty query - fast path) */
    filt_clear(&filtered);
    if ((flags & MMENU_DISABLED) || input[0] == L'\0') {
        for (int i = 0; i < n_options; i++) {
            if (!filt_push(&filtered, i)) goto cleanup;
        }
    } else if (flags & MMENU_EXTENDED) {
        FuzzyMatch *matches = NULL;
        int match_count = 0;
        if (!mmenu_extended_collect(current_match_options, 0, n_options, input, flags,
                                    &tiebreak, &matches, &match_count))
            goto cleanup;
        if (config && config->stream_next && (flags & MMENU_FUZZY)) {
            if (!mmenu_scores_reserve(&filtered_scores, &filtered_scores_cap, match_count) ||
                !mmenu_metrics_reserve(&filtered_metrics, &filtered_metrics_cap, match_count)) {
                free(matches);
                goto cleanup;
            }
            for (int i = 0; i < match_count; i++) {
                filtered_scores[i] = matches[i].score;
                filtered_metrics[i] = matches[i].metrics;
            }
        }
        for (int i = 0; i < match_count; i++) {
            if (!filt_push(&filtered, matches[i].index)) {
                free(matches);
                goto cleanup;
            }
        }
        free(matches);
    } else if (flags & MMENU_FUZZY) {
        FuzzyMatch *matches = NULL;
        int match_count = 0;
        if (!FuzzyCollect(current_match_options, n_options, input, flags, &tiebreak, &matches, &match_count))
            goto cleanup;
        if (config && config->stream_next) {
            if (!mmenu_scores_reserve(&filtered_scores, &filtered_scores_cap, match_count) ||
                !mmenu_metrics_reserve(&filtered_metrics, &filtered_metrics_cap, match_count)) {
                free(matches);
                goto cleanup;
            }
            for (int i = 0; i < match_count; i++) {
                filtered_scores[i] = matches[i].score;
                filtered_metrics[i] = matches[i].metrics;
            }
        }
        for (int i = 0; i < match_count; i++) {
            if (!filt_push(&filtered, matches[i].index)) {
                free(matches);
                goto cleanup;
            }
        }
        free(matches);
    } else {
        char *q = wc_to_mb(input);
        if (!q && errno == ENOMEM) {
            perror("mmenu: converting filter");
            goto cleanup;
        }
        if (q) {
            for (int i = 0; i < n_options; i++) {
                if (((flags & MMENU_EXACT) ? strcmp(current_match_options[i], q) == 0 :
                     MmenuStrstrPolicy(current_match_options[i], q, flags) != NULL) &&
                    !filt_push(&filtered, i)) {
                    free(q);
                    goto cleanup;
                }
            }
            free(q);
        }
    }
    if (filtered.count > 0) selection = 0;
    if (preview_visible) {
        mmenu_update_preview(config, current_options, n_options, &filtered, selection,
                             input, preview_output, sizeof(preview_output));
        preview_dirty = 0;
    }

    /* Initial draw */
    clear();
    printw("%s%ls", prompt_str, input); clrtoeol();
    mmenu_position_query_cursor(prompt_columns, input_len, menu_cols);
    int visible = mmenu_visible_rows(rows, config);
    for (int v = 0; v < visible && top + v < filtered.count; v++) {
        int fidx = top + v;
        int oidx = filtered.indices[fidx];
        errno = 0;
        wchar_t *w = mb_to_wc(current_display_options[oidx]);
        if (!w && errno == ENOMEM) {
            perror("mmenu: rendering option");
            goto cleanup;
        }
        if (w) {
            move(config && config->reverse_list ? visible - v : v + 1, 0);
            if (fidx == selection) attron(A_STANDOUT);
            int option_cols = menu_cols;
            if (multi && menu_cols >= 4) {
                printw("%s", selected_options[oidx] ? "[x] " : "[ ] ");
                option_cols = menu_cols - 4;
            }
            if ((flags & MMENU_FUZZY) && !(flags & MMENU_DISABLED) && !use_search_text && !use_display_text)
            RenderFuzzyOption(w, option_cols, input, flags);
            else printw("%.*ls", option_cols, w);
            if (fidx == selection) attroff(A_STANDOUT);
            clrtoeol();
            free(w);
        }
    }
    if (preview_visible) mmenu_draw_preview(preview_output, rows, cols, menu_cols);
    refresh();

    while (1) {
        int resized = 0;
        if (resize_flag) {
            resize_flag = 0;
            endwin();
            mmenu_refresh_dimensions(tty, &rows, &cols);
            int was_preview_visible = preview_visible;
            preview_visible = has_preview && cols >= 40 && rows >= 4;
            menu_cols = preview_visible ? cols / 2 : cols;
            if (preview_visible != was_preview_visible) preview_dirty = 1;
            visible = rows - 1;
            resized = 1;
        }

        wint_t ch = 0;
        int kc = resized ? ERR : wget_wch(stdscr, &ch);
        int stream_changed = 0;
        int stream_start_count = n_options;
        if (config && config->stream_next && !stream_finished) {
            if (!mmenu_stream_append_available(config, &stream_options, &n_options,
                                               &stream_options_cap, &selected_options,
                                               &selected_options_cap, multi,
                                               &stream_finished, &stream_changed))
                goto cleanup;
            current_options = (const char *const *)stream_options;
            if (use_search_text) {
                if (n_options > match_options_cap) {
                    int new_cap;
                    if (!mmenu_next_capacity(match_options_cap, INITIAL_CAP,
                                             sizeof(*match_options), &new_cap)) {
                        perror("mmenu: searchable option capacity");
                        goto cleanup;
                    }
                    size_t match_bytes;
                    if (!mmenu_size_mul((size_t)new_cap, sizeof(*match_options),
                                        &match_bytes)) {
                        perror("mmenu: searchable option capacity");
                        goto cleanup;
                    }
                    const char **grown = realloc(match_options, match_bytes);
                    if (!grown) {
                        if (errno == 0) errno = ENOMEM;
                        perror("mmenu: growing searchable options");
                        goto cleanup;
                    }
                    match_options = grown;
                    match_options_cap = new_cap;
                }
                for (int i = stream_start_count; i < n_options; i++) {
                    match_options[i] = config->search_text(current_options[i], i,
                                                            config->search_text_userdata);
                    if (!match_options[i]) {
                        fprintf(stderr, "mmenu: searchable text callback failed for option %d\n", i);
                        goto cleanup;
                    }
                    if (mbstowcs(NULL, match_options[i], 0) == (size_t)-1) {
                        fprintf(stderr, "mmenu: searchable text for option %d is not valid in the current locale\n", i);
                        goto cleanup;
                    }
                }
                current_match_options = (const char *const *)match_options;
            } else {
                current_match_options = current_options;
            }
            if (use_display_text) {
                if (n_options > display_options_cap) {
                    int new_cap;
                    if (!mmenu_next_capacity(display_options_cap, INITIAL_CAP,
                                             sizeof(*display_options), &new_cap)) {
                        perror("mmenu: display option capacity");
                        goto cleanup;
                    }
                    size_t display_bytes;
                    if (!mmenu_size_mul((size_t)new_cap, sizeof(*display_options),
                                        &display_bytes)) {
                        perror("mmenu: display option capacity");
                        goto cleanup;
                    }
                    const char **grown = realloc(display_options, display_bytes);
                    if (!grown) {
                        if (errno == 0) errno = ENOMEM;
                        perror("mmenu: growing display options");
                        goto cleanup;
                    }
                    display_options = grown;
                    display_options_cap = new_cap;
                }
                for (int i = stream_start_count; i < n_options; i++) {
                    display_options[i] = config->display_text(current_options[i], i,
                                                               config->display_text_userdata);
                    if (!display_options[i]) {
                        fprintf(stderr, "mmenu: display text callback failed for option %d\n", i);
                        goto cleanup;
                    }
                    if (mbstowcs(NULL, display_options[i], 0) == (size_t)-1) {
                        fprintf(stderr, "mmenu: display text for option %d is not valid in the current locale\n", i);
                        goto cleanup;
                    }
                }
                current_display_options = (const char *const *)display_options;
            } else {
                current_display_options = current_options;
            }
        }
        if (kc == ERR && !resized && !stream_changed) continue;
        if (resized) kc = OK;

        need_filter = stream_changed;
        int force_full_filter = stream_changed;
        int preserve_stream_selection = 0;
        int stream_preserve_index = -1;
        int query_edited = 0;
        int accept_pending = 0;
        int toggle_pending = 0;

        int is_key_code = kc == KEY_CODE_YES;
        int is_up = ch == key_up || (config && config->key_up && ch == config->key_up);
        int is_down = ch == key_down || (config && config->key_down && ch == config->key_down);
        int is_accept = (!is_key_code &&
                         (ch == '\n' || ch == '\r' || ch == KEY_ENTER)) ||
                        (key_accept && ch == key_accept);
        int is_cancel = (!is_key_code && (ch == 27 || ch == 3 || ch == 4)) ||
                        (key_cancel && ch == key_cancel);
        int is_toggle = multi && ((!is_key_code && ch == '\t') ||
                                  (key_toggle && ch == key_toggle));
        int is_backspace = (ch == KEY_BACKSPACE || (!is_key_code &&
                             (ch == 127 || ch == 8))) ||
                           (key_backspace && ch == key_backspace);
        if (is_cancel) { ret = -1; goto cleanup; }
        if (is_up) {
            if (selection > 0) {
                selection--;
                preview_dirty = 1;
            } else if (config && config->cycle && filtered.count > 0 &&
                       selection != filtered.count - 1) {
                selection = filtered.count - 1;
                preview_dirty = 1;
            }
        } else if (is_down) {
            if (selection < filtered.count - 1) {
                selection++;
                preview_dirty = 1;
            } else if (config && config->cycle && filtered.count > 0 && selection != 0) {
                selection = 0;
                preview_dirty = 1;
            }
        } else if (is_backspace) {
            if (input_len > 0) { input[--input_len] = L'\0'; need_filter = 1; query_edited = 1; }
        } else if (is_toggle) {
            if (filtered.count > 0) {
                selected_options[filtered.indices[selection]] =
                    !selected_options[filtered.indices[selection]];
            } else if (stream_changed) {
                /* Apply a queued toggle after the first streamed batch is filtered. */
                toggle_pending = 1;
            }
        } else if (is_accept) {
            accept_pending = 1;
        } else if (!is_key_code && iswprint(ch) && input_len < MAX_INPUT_LEN) {
            input[input_len++] = ch;
            input[input_len] = L'\0';
            need_filter = 1;
            query_edited = 1;
        }

        if (stream_changed && !query_edited && filtered.count > 0 &&
            selection >= 0 && selection < filtered.count) {
            stream_preserve_index = filtered.indices[selection];
            preserve_stream_selection = 1;
        }

        if (need_filter) {
            if (flags & MMENU_DISABLED) {
                filt_clear(&filtered);
                for (int i = 0; i < n_options; i++)
                    if (!filt_push(&filtered, i)) goto cleanup;
            } else if (stream_changed && !query_edited) {
                if (!mmenu_filter_stream_batch(current_match_options, stream_start_count,
                                               n_options, input, flags, &tiebreak, &filtered,
                                               &filtered_scores, &filtered_scores_cap,
                                               &filtered_metrics, &filtered_metrics_cap))
                    goto cleanup;
            } else if (flags & MMENU_EXTENDED) {
                FuzzyMatch *matches = NULL;
                int match_count = 0;
                if (!mmenu_extended_collect(current_match_options, 0, n_options, input,
                                            flags, &tiebreak, &matches, &match_count))
                    goto cleanup;
                if (config && config->stream_next && (flags & MMENU_FUZZY)) {
                    if (!mmenu_scores_reserve(&filtered_scores, &filtered_scores_cap,
                                              match_count) ||
                        !mmenu_metrics_reserve(&filtered_metrics, &filtered_metrics_cap,
                                               match_count)) {
                        free(matches);
                        goto cleanup;
                    }
                    for (int i = 0; i < match_count; i++) {
                        filtered_scores[i] = matches[i].score;
                        filtered_metrics[i] = matches[i].metrics;
                    }
                }
                filt_clear(&filtered);
                for (int i = 0; i < match_count; i++) {
                    if (!filt_push(&filtered, matches[i].index)) {
                        free(matches);
                        goto cleanup;
                    }
                }
                free(matches);
            } else if (flags & MMENU_FUZZY) {
                FuzzyMatch *matches = NULL;
                int match_count = 0;
                if (!FuzzyCollect(current_match_options, n_options, input, flags, &tiebreak, &matches, &match_count))
                    goto cleanup;
                if (config && config->stream_next) {
                    if (!mmenu_scores_reserve(&filtered_scores, &filtered_scores_cap,
                                              match_count) ||
                        !mmenu_metrics_reserve(&filtered_metrics, &filtered_metrics_cap,
                                               match_count)) {
                        free(matches);
                        goto cleanup;
                    }
                    for (int i = 0; i < match_count; i++) {
                        filtered_scores[i] = matches[i].score;
                        filtered_metrics[i] = matches[i].metrics;
                    }
                }
                filt_clear(&filtered);
                for (int i = 0; i < match_count; i++) {
                    if (!filt_push(&filtered, matches[i].index)) {
                        free(matches);
                        goto cleanup;
                    }
                }
                free(matches);
            } else {
            errno = 0;
            char *q = wc_to_mb(input);
            if (!q && errno == ENOMEM) {
                perror("mmenu: converting filter");
                goto cleanup;
            }
            int do_full = 1;
            if (q && !force_full_filter && !(flags & MMENU_EXACT) &&
                input_len > prev_input_len && filtered.count > 0) {
                /* Common case: user typed another char. Refine only the previous matches. */
                filt newf;
                newf.cap = filtered.count < INITIAL_CAP ? INITIAL_CAP : filtered.count;
                int checked_cap;
                if (mmenu_next_capacity(0, newf.cap, sizeof(*newf.indices), &checked_cap)) {
                    newf.cap = checked_cap;
                    newf.indices = malloc((size_t)newf.cap * sizeof(*newf.indices));
                } else {
                    newf.indices = NULL;
                }
                if (newf.indices) {
                    newf.count = 0;
                    for (int k = 0; k < filtered.count; k++) {
                        int oidx = filtered.indices[k];
                        if ((flags & MMENU_EXACT) ? strcmp(current_match_options[oidx], q) == 0 :
                            MmenuStrstrPolicy(current_match_options[oidx], q, flags) != NULL) {
                            if (newf.count == newf.cap) {
                                int new_cap;
                                if (!mmenu_next_capacity(newf.cap, INITIAL_CAP,
                                                         sizeof(*newf.indices), &new_cap)) {
                                    free(newf.indices);
                                    newf.indices = NULL;
                                    break;
                                }
                                int *new_indices = realloc(newf.indices,
                                                           (size_t)new_cap * sizeof(*newf.indices));
                                if (!new_indices) {
                                    free(newf.indices);
                                    newf.indices = NULL;
                                    break;
                                }
                                newf.indices = new_indices;
                                newf.cap = new_cap;
                            }
                            newf.indices[newf.count++] = oidx;
                        }
                    }
                    if (newf.indices) {
                        /* Swap in the smaller refined list only after successful allocation. */
                        free(filtered.indices);
                        filtered = newf;
                        do_full = 0;
                    }
                }
            }
            if (do_full) {
                filt_clear(&filtered);
                if (!q || q[0] == '\0') {
                    for (int i = 0; i < n_options; i++) {
                        if (!filt_push(&filtered, i)) {
                            free(q);
                            goto cleanup;
                        }
                    }
                } else {
                    for (int i = 0; i < n_options; i++) {
                        if (((flags & MMENU_EXACT) ? strcmp(current_match_options[i], q) == 0 :
                         MmenuStrstrPolicy(current_match_options[i], q, flags) != NULL) &&
                        !filt_push(&filtered, i)) {
                            free(q);
                            goto cleanup;
                        }
                    }
                }
            }
            if (q) free(q);

            }
            prev_input_len = input_len;
            selection = 0;
            if (preserve_stream_selection && stream_preserve_index >= 0) {
                for (int i = 0; i < filtered.count; i++) {
                    if (filtered.indices[i] == stream_preserve_index) {
                        selection = i;
                        break;
                    }
                }
            }
            top = 0;
            preview_dirty = 1;
        }

        if (toggle_pending && filtered.count > 0) {
            selected_options[filtered.indices[selection]] =
                !selected_options[filtered.indices[selection]];
        }

        if (accept_pending) {
            if (multi) {
                int count = 0;
                for (int i = 0; i < n_options; i++)
                    if (selected_options[i]) count++;
                if (count == 0 && filtered.count > 0) count = 1;
                if ((size_t)count > SIZE_MAX / sizeof(int)) {
                    errno = ENOMEM;
                    perror("mmenu: multi-select result is too large");
                    goto cleanup;
                }
                int *indices = count ? malloc((size_t)count * sizeof(*indices)) : NULL;
                if (count && !indices) {
                    if (errno == 0) errno = ENOMEM;
                    perror("mmenu: allocating multi-select result");
                    goto cleanup;
                }
                int out = 0;
                for (int i = 0; i < n_options; i++)
                    if (selected_options[i]) indices[out++] = i;
                if (out == 0 && count == 1)
                    indices[out++] = filtered.indices[selection];
                *selected_indices = indices;
                *selected_count = out;
                ret = 0;
            } else if (filtered.count > 0) {
                ret = filtered.indices[selection];
            }
            goto cleanup;
        }

        if (preview_visible && preview_dirty) {
            mmenu_update_preview(config, current_options, n_options, &filtered, selection,
                                 input, preview_output, sizeof(preview_output));
            preview_dirty = 0;
        }

        /* Scroll */
        visible = mmenu_visible_rows(rows, config);
        if (filtered.count > 0) {
            if (selection < top) top = selection;
            else if (selection >= top + visible) top = selection - visible + 1;
            if (top < 0) top = 0;
            int max_top = filtered.count - visible;
            if (max_top < 0) max_top = 0;
            if (top > max_top) top = max_top;
        } else {
            top = 0;
        }

        /* Redraw */
        clear();
        printw("%s%ls", prompt_str, input); clrtoeol();
        mmenu_position_query_cursor(prompt_columns, input_len, menu_cols);
        for (int v = 0; v < visible && top + v < filtered.count; v++) {
            int fidx = top + v;
            int oidx = filtered.indices[fidx];
            errno = 0;
            wchar_t *w = mb_to_wc(current_display_options[oidx]);
            if (!w && errno == ENOMEM) {
                perror("mmenu: rendering option");
                goto cleanup;
            }
            if (w) {
                move(config && config->reverse_list ? visible - v : v + 1, 0);
                if (fidx == selection) attron(A_STANDOUT);
                int option_cols = menu_cols;
                if (multi && menu_cols >= 4) {
                    printw("%s", selected_options[oidx] ? "[x] " : "[ ] ");
                    option_cols = menu_cols - 4;
                }
                if ((flags & MMENU_FUZZY) && !(flags & MMENU_DISABLED) && !use_search_text && !use_display_text)
            RenderFuzzyOption(w, option_cols, input, flags);
                else printw("%.*ls", option_cols, w);
                if (fidx == selection) attroff(A_STANDOUT);
                clrtoeol();
                free(w);
            }
        }
        if (preview_visible) mmenu_draw_preview(preview_output, rows, cols, menu_cols);
        refresh();
    }

cleanup:
    free(display_options);
    free(match_options);
    free(filtered_scores);
    free(filtered_metrics);
    free(stream_options);
    free(selected_options);
    free(filtered.indices);
    if (restore_resize_action)
        sigaction(SIGWINCH, &previous_resize_action, NULL);
    endwin();
    delscreen(scr);
    fclose(tty);
    return ret;
}

int mmenuConfigured(const char *const *options, int n_options, const char *prompt,
                    unsigned int flags, const mmenu_config *config)
{
    return mmenu_internal(options, n_options, prompt, flags, 0, config, NULL, NULL);
}

int mmenuEx(const char *const *options, int n_options, const char *prompt,
            unsigned int flags)
{
    return mmenuConfigured(options, n_options, prompt, flags, NULL);
}

int mmenuMultiConfigured(const char *const *options, int n_options, const char *prompt,
                         unsigned int flags, const mmenu_config *config,
                         int **selected_indices, int *selected_count)
{
    if (!selected_indices || !selected_count) {
        fprintf(stderr, "mmenu: multi-select output pointers must not be NULL\n");
        return -1;
    }
    *selected_indices = NULL;
    *selected_count = 0;
    return mmenu_internal(options, n_options, prompt, flags, 1, config,
                          selected_indices, selected_count);
}

int mmenuMulti(const char *const *options, int n_options, const char *prompt,
               unsigned int flags, int **selected_indices, int *selected_count)
{
    return mmenuMultiConfigured(options, n_options, prompt, flags, NULL,
                                selected_indices, selected_count);
}

int mmenu(const char *const *options, int n_options, const char *prompt)
{
    return mmenuEx(options, n_options, prompt, 0);
}

#endif /* MMENU_IMPLEMENTATION */
