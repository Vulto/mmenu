/* mmenu.h - single-header fuzzy menu using ncursesw (stb-style) */
/* Public domain or MIT */

#ifndef MMENU_H
#define MMENU_H

/* Enable wide-character functions in ncurses */
#define _XOPEN_SOURCE_EXTENDED

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

    const char *prompt_str = prompt ? prompt : "> ";
    size_t prompt_len = strlen(prompt_str);

    int ret = -1;

    /* Initial filter (show all) */
    filt_clear(&filtered);
    for (int i = 0; i < n_options; i++) {
        wchar_t *w = mb_to_wc(options[i]);
        if (w && wcsstr(w, input) != NULL) filt_push(&filtered, i);
        free(w);
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
            filt_clear(&filtered);
            for (int i = 0; i < n_options; i++) {
                wchar_t *w = mb_to_wc(options[i]);
                if (w && wcsstr(w, input) != NULL) filt_push(&filtered, i);
                free(w);
            }
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
