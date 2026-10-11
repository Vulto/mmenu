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

/* MMENU_FUZZY enables subsequence matching and score-ranked results. */
enum { MMENU_FUZZY = 1u, MMENU_EXACT = 2u };

/* Options and prompt must be valid multibyte strings in the active locale.
   Default matching is ASCII-case-insensitive substring matching; non-ASCII
   characters are matched exactly. MMENU_FUZZY enables ranked subsequence
   matching and MMENU_EXACT enables byte-exact equality matching. These modes
   are mutually exclusive. Invalid multibyte strings return -1 before terminal initialization. */
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

/* Optional interactive query, key aliases, and preview callback.
   A zero key keeps its default. */
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

static void mmenu_refresh_dimensions(FILE *tty, int *rows, int *cols) {
    struct winsize Size;
    if (ioctl(fileno(tty), TIOCGWINSZ, &Size) == 0 &&
        Size.ws_row > 0 && Size.ws_col > 0) {
        resizeterm((int)Size.ws_row, (int)Size.ws_col);
    }
    refresh();
    getmaxyx(stdscr, *rows, *cols);
}

static wchar_t *mb_to_wc(const char *s) {
    if (!s) return NULL;
    size_t n = mbstowcs(NULL, s, 0);
    if (n == (size_t)-1) return NULL;
    wchar_t *w = malloc((n + 1) * sizeof(wchar_t));
    if (!w && errno == 0) errno = ENOMEM;
    if (w && mbstowcs(w, s, n + 1) == (size_t)-1) {
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
    char *m = malloc(n + 1);
    if (!m && errno == 0) errno = ENOMEM;
    if (m && wcstombs(m, w, n + 1) == (size_t)-1) {
        free(m);
        errno = EILSEQ;
        return NULL;
    }
    return m;
}

typedef struct { int *indices; int cap; int count; } filt;

typedef struct {
    int index;
    long long score;
} FuzzyMatch;

static wchar_t FoldAscii(wchar_t ch)
{
    return ch >= L'A' && ch <= L'Z' ? ch + (L'a' - L'A') : ch;
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
static int FuzzyScore(const char *candidate, const wchar_t *query,
                      long long *result)
{
    size_t query_len = wcslen(query);
    size_t query_pos = 0;
    size_t candidate_pos = 0;
    size_t last_match = SIZE_MAX;
    wchar_t previous = 0;
    long long score = 0;
    mbstate_t state = {0};
    const char *cursor = candidate;

    while (*cursor) {
        wchar_t current;
        size_t bytes = mbrtowc(&current, cursor, MB_CUR_MAX, &state);
        if (bytes == (size_t)-1 || bytes == (size_t)-2 || bytes == 0)
            return 0;

        if (query_pos < query_len &&
            FoldAscii(current) == FoldAscii(query[query_pos])) {
            score += 10;
            if (candidate_pos == 0 || IsFuzzyBoundary(previous, current))
                score += 8;
            if (last_match == SIZE_MAX) {
                if (candidate_pos > (size_t)LLONG_MAX)
                    score = LLONG_MIN / 2;
                else
                    score -= (long long)candidate_pos;
            } else {
                size_t gap = candidate_pos - last_match - 1;
                if (gap == 0) score += 15;
                else if (gap > (size_t)LLONG_MAX / 4)
                    score = LLONG_MIN / 2;
                else score -= (long long)gap;
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
    return 1;
}

/* Highlight the same greedy subsequence used by FuzzyScore, clipping by
   terminal display cells so wide Unicode characters do not overrun the row. */
static void RenderFuzzyOption(const wchar_t *option, int columns,
                              const wchar_t *query)
{
    size_t query_pos = 0;
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
                      FoldAscii(ch) == FoldAscii(query[query_pos]);
        if (matched) {
            query_pos++;
            attron(A_BOLD | A_UNDERLINE);
        }
        addnwstr(&ch, 1);
        if (matched) attroff(A_BOLD | A_UNDERLINE);
        used_columns += width;
    }
}

static int CompareFuzzyMatches(const void *left, const void *right)
{
    const FuzzyMatch *a = left;
    const FuzzyMatch *b = right;
    if (a->score > b->score) return -1;
    if (a->score < b->score) return 1;
    return (a->index > b->index) - (a->index < b->index);
}

/* Returns score-ranked matching indices; ties retain original input order. */
static int FuzzyCollect(const char *const *options, int n_options,
                        const wchar_t *query, FuzzyMatch **result,
                        int *result_count)
{
    FuzzyMatch *matches = NULL;
    int count = 0;
    int capacity = 0;

    *result = NULL;
    *result_count = 0;

    for (int i = 0; i < n_options; i++) {
        long long score;
        if (!FuzzyScore(options[i], query, &score)) continue;

        if (count == capacity) {
            if (capacity > INT_MAX / 2) {
                free(matches);
                errno = ENOMEM;
                perror("mmenu: fuzzy result capacity");
                return 0;
            }
            int new_capacity = capacity ? capacity * 2 : INITIAL_CAP;
            if ((size_t)new_capacity > SIZE_MAX / sizeof(*matches)) {
                free(matches);
                errno = ENOMEM;
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
        matches[count++] = (FuzzyMatch){.index = i, .score = score};
    }

    if (count > 1)
        qsort(matches, (size_t)count, sizeof(*matches), CompareFuzzyMatches);
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
        if (f->cap > 0 && f->cap > INT_MAX / 2) {
            errno = ENOMEM;
            perror("mmenu: filter capacity");
            return 0;
        }
        int new_cap = f->cap <= 0 ? INITIAL_CAP : f->cap * 2;
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

static void mmenu_position_query_cursor(size_t prompt_len, int input_len, int menu_cols) {
    int cursor = (int)prompt_len + input_len;
    if (menu_cols > 0 && cursor >= menu_cols) cursor = menu_cols - 1;
    if (cursor < 0) cursor = 0;
    move(0, cursor);
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

static int mmenu_internal(const char *const *options, int n_options,
                          const char *prompt, unsigned int flags, int multi,
                          const mmenu_config *config,
                          int **selected_indices, int *selected_count) {
    if (multi) {
        if (!selected_indices || !selected_count) {
            fprintf(stderr, "mmenu: multi-select output pointers must not be NULL\n");
            return -1;
        }
        *selected_indices = NULL;
        *selected_count = 0;
    }
    /* Validate the public API before touching the terminal or initializing ncurses. */
    if ((flags & ~(MMENU_FUZZY | MMENU_EXACT)) ||
        ((flags & MMENU_FUZZY) && (flags & MMENU_EXACT))) {
        fprintf(stderr, "mmenu: unsupported API flags or conflicting matching modes\n");
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
        if (!options[i]) {
            fprintf(stderr, "mmenu: option %d must not be NULL\\n", i);
            return -1;
        }
    }

    setlocale(LC_ALL, "");

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
        if (mbstowcs(NULL, options[i], 0) == (size_t)-1) {
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

    FILE *tty = fopen("/dev/tty", "r+");
    if (!tty) { perror("mmenu: opening /dev/tty"); return -1; }

    SCREEN *scr = newterm(NULL, tty, tty);
    if (!scr) { fclose(tty); fprintf(stderr, "Failed to init ncurses\n"); return -1; }
    set_term(scr);

    /* raw() disables terminal-generated SIGINT so Ctrl-C reaches wget_wch and can cancel cleanly. */
    raw(); noecho(); keypad(stdscr, TRUE);
    signal(SIGWINCH, handle_resize);

    int rows, cols; getmaxyx(stdscr, rows, cols);
    int has_preview = config && config->preview;
    int preview_visible = has_preview && cols >= 40 && rows >= 4;
    int menu_cols = preview_visible ? cols / 2 : cols;

    int ret = -1;
    unsigned char *selected_options = NULL;
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
    if (multi && n_options > 0) {
        errno = 0;
        selected_options = malloc((size_t)n_options * sizeof(*selected_options));
        if (selected_options) memset(selected_options, 0, (size_t)n_options * sizeof(*selected_options));
        if (!selected_options) {
            if (errno == 0) errno = ENOMEM;
            perror("mmenu: allocating multi-select state");
            goto cleanup;
        }
    }

    int selection = 0;
    int top = 0;
    int need_filter = 0;
    int preview_dirty = 1;
    char preview_output[MMENU_PREVIEW_BUFFER_SIZE] = {0};
    int prev_input_len = input_len;   /* for incremental filter optimization */

    const char *prompt_str = prompt ? prompt : "> ";
    size_t prompt_len = strlen(prompt_str);

    /* Initial filter (show all for empty query - fast path) */
    filt_clear(&filtered);
    if (input[0] == L'\0') {
        for (int i = 0; i < n_options; i++) {
            if (!filt_push(&filtered, i)) goto cleanup;
        }
    } else if (flags & MMENU_FUZZY) {
        FuzzyMatch *matches = NULL;
        int match_count = 0;
        if (!FuzzyCollect(options, n_options, input, &matches, &match_count))
            goto cleanup;
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
                if (((flags & MMENU_EXACT) ? strcmp(options[i], q) == 0 :
                     strcasestr(options[i], q) != NULL) &&
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
        mmenu_update_preview(config, options, n_options, &filtered, selection,
                             input, preview_output, sizeof(preview_output));
        preview_dirty = 0;
    }

    /* Initial draw */
    clear();
    printw("%s%ls", prompt_str, input); clrtoeol();
    mmenu_position_query_cursor(prompt_len, input_len, menu_cols);
    int visible = rows - 1;
    for (int v = 0; v < visible && top + v < filtered.count; v++) {
        int fidx = top + v;
        int oidx = filtered.indices[fidx];
        errno = 0;
        wchar_t *w = mb_to_wc(options[oidx]);
        if (!w && errno == ENOMEM) {
            perror("mmenu: rendering option");
            goto cleanup;
        }
        if (w) {
            move(v + 1, 0);
            if (fidx == selection) attron(A_STANDOUT);
            int option_cols = menu_cols;
            if (multi && menu_cols >= 4) {
                printw("%s", selected_options[oidx] ? "[x] " : "[ ] ");
                option_cols = menu_cols - 4;
            }
            if (flags & MMENU_FUZZY) RenderFuzzyOption(w, option_cols, input);
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
        if (kc == ERR && !resized) continue;
        if (resized) kc = OK;

        need_filter = 0;

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
            if (selection > 0) { selection--; preview_dirty = 1; }
        } else if (is_down) {
            if (selection < filtered.count - 1) { selection++; preview_dirty = 1; }
        } else if (is_backspace) {
            if (input_len > 0) { input[--input_len] = L'\0'; need_filter = 1; }
        } else if (is_toggle) {
            if (filtered.count > 0)
                selected_options[filtered.indices[selection]] =
                    !selected_options[filtered.indices[selection]];
        } else if (is_accept) {
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
        } else if (!is_key_code && iswprint(ch) && input_len < MAX_INPUT_LEN) {
            input[input_len++] = ch;
            input[input_len] = L'\0';
            need_filter = 1;
        }

        if (need_filter) {
            if (flags & MMENU_FUZZY) {
                FuzzyMatch *matches = NULL;
                int match_count = 0;
                if (!FuzzyCollect(options, n_options, input, &matches, &match_count))
                    goto cleanup;
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
            if (q && !(flags & MMENU_EXACT) && input_len > prev_input_len && filtered.count > 0) {
                /* Common case: user typed another char. Refine only the previous matches. */
                filt newf;
                newf.cap = filtered.count < INITIAL_CAP ? INITIAL_CAP : filtered.count;
                newf.indices = malloc(newf.cap * sizeof(int));
                if (newf.indices) {
                    newf.count = 0;
                    for (int k = 0; k < filtered.count; k++) {
                        int oidx = filtered.indices[k];
                        if ((flags & MMENU_EXACT) ? strcmp(options[oidx], q) == 0 :
                            strcasestr(options[oidx], q) != NULL) {
                            if (newf.count == newf.cap) {
                                int new_cap = newf.cap <= 0 ? INITIAL_CAP : newf.cap * 2;
                                int *new_indices = realloc(newf.indices, (size_t)new_cap * sizeof(int));
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
                        if (((flags & MMENU_EXACT) ? strcmp(options[i], q) == 0 :
                         strcasestr(options[i], q) != NULL) &&
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
            top = 0;
            preview_dirty = 1;
        }

        if (preview_visible && preview_dirty) {
            mmenu_update_preview(config, options, n_options, &filtered, selection,
                                 input, preview_output, sizeof(preview_output));
            preview_dirty = 0;
        }

        /* Scroll */
        visible = rows - 1;
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
        mmenu_position_query_cursor(prompt_len, input_len, menu_cols);
        for (int v = 0; v < visible && top + v < filtered.count; v++) {
            int fidx = top + v;
            int oidx = filtered.indices[fidx];
            errno = 0;
            wchar_t *w = mb_to_wc(options[oidx]);
            if (!w && errno == ENOMEM) {
                perror("mmenu: rendering option");
                goto cleanup;
            }
            if (w) {
                move(v + 1, 0);
                if (fidx == selection) attron(A_STANDOUT);
                int option_cols = menu_cols;
                if (multi && menu_cols >= 4) {
                    printw("%s", selected_options[oidx] ? "[x] " : "[ ] ");
                    option_cols = menu_cols - 4;
                }
                if (flags & MMENU_FUZZY) RenderFuzzyOption(w, option_cols, input);
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
    free(selected_options);
    free(filtered.indices);
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
