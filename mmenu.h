/* mmenu.h - single-header fuzzy menu using ncursesw (stb-style) */
/* Public domain or MIT */

#ifndef MMENU_H
#define MMENU_H

/* Enable wide-character functions in ncurses */
#define _XOPEN_SOURCE_EXTENDED
#define _GNU_SOURCE   /* for strcasestr on glibc */

#include <ncurses.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

int mmenu(const char *const *options, int n_options, const char *prompt);

#endif /* MMENU_H */

#ifdef MMENU_IMPLEMENTATION

#define MAX_INPUT_LEN 128
#define INITIAL_CAP 128

static volatile sig_atomic_t resize_flag = 0;

static void handle_resize(int sig) { (void)sig; resize_flag = 1; }

static wchar_t *mb_to_wc(const char *s) {
    if (!s) return NULL;
    size_t n = mbstowcs(NULL, s, 0);
    if (n == (size_t)-1) return NULL;
    wchar_t *w = malloc((n + 1) * sizeof(wchar_t));
    if (w) mbstowcs(w, s, n + 1);
    return w;
}

/* Convert the (small) current search input from wchar to multibyte for fast byte matching.
   Only called once per keystroke, not per candidate. */
static char *wc_to_mb(const wchar_t *w) {
    if (!w || !*w) return strdup("");
    size_t n = wcstombs(NULL, w, 0);
    if (n == (size_t)-1) return NULL;
    char *m = malloc(n + 1);
    if (m) wcstombs(m, w, n + 1);
    return m;
}

typedef struct { int *indices; int cap; int count; } filt;

static void filt_init(filt *f) {
    f->cap = INITIAL_CAP;
    f->indices = malloc(f->cap * sizeof(int));
    if (!f->indices) { perror("malloc"); exit(EXIT_FAILURE); }
    f->count = 0;
}

static void filt_push(filt *f, int idx) {
    if (f->count == f->cap) {
        f->cap *= 2;
        f->indices = realloc(f->indices, f->cap * sizeof(int));
        if (!f->indices) { perror("realloc"); exit(EXIT_FAILURE); }
    }
    f->indices[f->count++] = idx;
}

static void filt_clear(filt *f) { f->count = 0; }

int mmenu(const char *const *options, int n_options, const char *prompt) {
    setlocale(LC_ALL, "");

    FILE *tty = fopen("/dev/tty", "r+");
    if (!tty) { perror("fopen /dev/tty"); exit(EXIT_FAILURE); }

    SCREEN *scr = newterm(NULL, tty, tty);
    if (!scr) { fclose(tty); fprintf(stderr, "Failed to init ncurses\n"); return -1; }
    set_term(scr);

    cbreak(); noecho(); keypad(stdscr, TRUE);
    signal(SIGWINCH, handle_resize);

    int rows, cols; getmaxyx(stdscr, rows, cols);

    wchar_t input[MAX_INPUT_LEN + 1] = {0};
    int input_len = 0;

    filt filtered; filt_init(&filtered);

    int selection = 0;
    int top = 0;
    int need_filter = 0;
    int prev_input_len = 0;   /* for incremental filter optimization */

    const char *prompt_str = prompt ? prompt : "> ";
    size_t prompt_len = strlen(prompt_str);

    int ret = -1;

    /* Initial filter (show all for empty query - fast path) */
    filt_clear(&filtered);
    if (input[0] == L'\0') {
        for (int i = 0; i < n_options; i++) filt_push(&filtered, i);
    } else {
        char *q = wc_to_mb(input);
        if (q) {
            for (int i = 0; i < n_options; i++) {
                if (strcasestr(options[i], q)) filt_push(&filtered, i);
            }
            free(q);
        }
    }
    if (filtered.count > 0) selection = 0;

    /* Initial draw */
    clear();
    printw("%s%ls", prompt_str, input); clrtoeol();
    move(0, (int)(prompt_len + input_len));
    int visible = rows - 1;
    for (int v = 0; v < visible && top + v < filtered.count; v++) {
        int fidx = top + v;
        int oidx = filtered.indices[fidx];
        wchar_t *w = mb_to_wc(options[oidx]);
        if (w) {
            move(v + 1, 0);
            if (fidx == selection) attron(A_STANDOUT);
            printw("%.*ls", cols, w);
            if (fidx == selection) attroff(A_STANDOUT);
            clrtoeol();
            free(w);
        }
    }
    refresh();

    while (1) {
        if (resize_flag) {
            resize_flag = 0;
            endwin();
            refresh();
            getmaxyx(stdscr, rows, cols);
            visible = rows - 1;
        }

        wint_t ch;
        int kc = wget_wch(stdscr, &ch);
        if (kc == ERR) continue;

        need_filter = 0;

        if (kc == KEY_CODE_YES) {
            if (ch == KEY_UP && selection > 0) selection--;
            else if (ch == KEY_DOWN && selection < filtered.count - 1) selection++;
            else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                if (input_len > 0) { input[--input_len] = L'\0'; need_filter = 1; }
            }
        } else {
            if (ch == 27 || ch == 3 || ch == 4) { ret = -1; goto cleanup; }
            if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                if (filtered.count > 0) ret = filtered.indices[selection];
                goto cleanup;
            }
            if (iswprint(ch) && input_len < MAX_INPUT_LEN) {
                input[input_len++] = ch;
                input[input_len] = L'\0';
                need_filter = 1;
            }
        }

        if (need_filter) {
            char *q = wc_to_mb(input);
            int do_full = 1;
            if (q && input_len > prev_input_len && filtered.count > 0) {
                /* Common case: user typed another char. Refine only the previous matches. */
                filt newf;
                newf.cap = filtered.count < INITIAL_CAP ? INITIAL_CAP : filtered.count;
                newf.indices = malloc(newf.cap * sizeof(int));
                if (newf.indices) {
                    newf.count = 0;
                    for (int k = 0; k < filtered.count; k++) {
                        int oidx = filtered.indices[k];
                        if (strcasestr(options[oidx], q)) {
                            if (newf.count == newf.cap) {
                                newf.cap *= 2;
                                newf.indices = realloc(newf.indices, newf.cap * sizeof(int));
                                if (!newf.indices) break;
                            }
                            newf.indices[newf.count++] = oidx;
                        }
                    }
                    /* Swap in the smaller refined list */
                    free(filtered.indices);
                    filtered = newf;
                    do_full = 0;
                }
            }
            if (do_full) {
                filt_clear(&filtered);
                if (!q || q[0] == '\0') {
                    for (int i = 0; i < n_options; i++) filt_push(&filtered, i);
                } else {
                    for (int i = 0; i < n_options; i++) {
                        if (strcasestr(options[i], q)) filt_push(&filtered, i);
                    }
                }
            }
            if (q) free(q);
            prev_input_len = input_len;
            selection = filtered.count > 0 ? 0 : 0;
            top = 0;
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
        move(0, (int)(prompt_len + input_len));
        for (int v = 0; v < visible && top + v < filtered.count; v++) {
            int fidx = top + v;
            int oidx = filtered.indices[fidx];
            wchar_t *w = mb_to_wc(options[oidx]);
            if (w) {
                move(v + 1, 0);
                if (fidx == selection) attron(A_STANDOUT);
                printw("%.*ls", cols, w);
                if (fidx == selection) attroff(A_STANDOUT);
                clrtoeol();
                free(w);
            }
        }
        refresh();
    }

cleanup:
    free(filtered.indices);
    endwin();
    delscreen(scr);
    fclose(tty);
    return ret;
}

#endif /* MMENU_IMPLEMENTATION */
