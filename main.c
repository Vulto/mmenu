#include <stdio.h>    // Standard I/O functions
#include <stdlib.h>   // Memory allocation, exit
#include <ncurses.h>  // Terminal UI library (link with -lncursesw for wide char support)
#include <locale.h>   // Locale settings for UTF-8
#include <string.h>   // String functions like strlen, strstr, strcpy
#include <wchar.h>    // Wide character support
#include <wctype.h>   // For iswprint and wide char classification
#include <signal.h>   // Signal handling for resize and interrupt

#define MAX_STR_SIZE 128
#define D_SIZE 128

volatile sig_atomic_t resize_flag = 0;

/**
 * Signal handler for terminal resize (SIGWINCH).
 */
void handle_resize(int sig) {
    resize_flag = 1;
}

typedef struct darr {
    char** strs;
    int size;
    int used;
} darr;

/**
 * Handle errors by printing message and exiting.
 */
void handle_error(const char* msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

/**
 * Push a string to the dynamic array, resizing if needed.
 */
void darr_push(darr* arr, char* str) {
    if (arr->used == arr->size) {
        arr->size *= 2;
        arr->strs = realloc(arr->strs, arr->size * sizeof(char*));
        if (arr->strs == NULL) handle_error("realloc");
    }
    arr->strs[arr->used] = str;
    arr->used++;
}

/**
 * Free the dynamic array and its strings.
 */
void darr_free(darr* arr) {
    for (int i = 0; i < arr->used; i++) {
        free(arr->strs[i]);
    }
    free(arr->strs);
    free(arr);
}

/**
 * Clear the dynamic array (reset used, no free).
 */
void darr_clear(darr* arr) {
    arr->used = 0;
}

/**
 * Read options from stdin into a dynamic array.
 */
darr* get_options(void) {
    darr* options = malloc(sizeof(darr));
    if (options == NULL) handle_error("malloc");
    options->strs = malloc(D_SIZE * sizeof(char*));
    if (options->strs == NULL) handle_error("malloc");
    options->size = D_SIZE;
    options->used = 0;
    char* buffer = malloc(MAX_STR_SIZE + 1);
    if (buffer == NULL) handle_error("malloc");
    while (fgets(buffer, MAX_STR_SIZE + 1, stdin) != NULL) {
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
            len--;
        } else {
            int c;
            while ((c = fgetc(stdin)) != '\n' && c != EOF);
        }
        char* str = malloc((len + 1) * sizeof(char));
        if (str == NULL) handle_error("malloc");
        for (int j = 0; j < len; j++) str[j] = buffer[j];
        str[len] = '\0';
        darr_push(options, str);
    }
    free(buffer);
    return options;
}

/**
 * Convert multibyte string to wide char string.
 */
wchar_t* mbstowcs_alloc(const char* mbstr) {
    size_t len = mbstowcs(NULL, mbstr, 0) + 1;
    if (len == (size_t)-1) return NULL;
    wchar_t* wcstr = malloc(len * sizeof(wchar_t));
    if (wcstr) mbstowcs(wcstr, mbstr, len);
    return wcstr;
}

/**
 * Interactive fuzzy finder menu using ncurses.
 */
int mmenu(char** options, int options_len, const char* prompt) {
    setlocale(LC_ALL, "");
    FILE* f = fopen("/dev/tty", "r+");
    if (f == NULL) handle_error("fopen");
    SCREEN* screen = newterm(NULL, f, f);
    if (screen == NULL) {
        fprintf(stderr, "Error initializing ncurses\n");
        fclose(f);
        return -1;
    }
    set_term(screen);
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    signal(SIGWINCH, handle_resize);
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    wchar_t* wstr = malloc((MAX_STR_SIZE + 1) * sizeof(wchar_t));
    if (wstr == NULL) handle_error("malloc");
    wstr[0] = L'\0';
    int len = 0;
    wint_t wchr;
    int selection = 1;
    int top = 0;
    int filter_changed = 1;  // Initial compute
    darr* filtered = malloc(sizeof(darr));
    if (filtered == NULL) handle_error("malloc");
    filtered->strs = malloc(D_SIZE * sizeof(char*));  // Store indices as (char*)(intptr_t)
    if (filtered->strs == NULL) handle_error("malloc");
    filtered->size = D_SIZE;
    filtered->used = 0;
    printw("%s", prompt);
    int prompt_len = strlen(prompt);
    for (int i = 0; i < rows - 1 && i < options_len; i++) {
        move(i + 1, 0);
        if (i == 0) attron(A_STANDOUT);
        printw("%.*s", cols, options[i]);
        if (i == 0) attroff(A_STANDOUT);
    }
    move(0, prompt_len + len);
    refresh();
    while (1) {
        if (resize_flag) {
            resize_flag = 0;
            endwin();
            refresh();
            getmaxyx(stdscr, rows, cols);  // Update dimensions
            clear();
        }
        wchr = wgetch(stdscr);  // Wide input
        if (wchr == 27 || wchr == 3 || wchr == 4) {  // ESC, Ctrl+C, Ctrl+D
            free(filtered->strs);
            free(filtered);
            endwin();
            free(wstr);
            return -1;
        }
        if (wchr == L'\n' || wchr == KEY_ENTER || wchr == 13) {
            if (filtered->used > 0) {
                int i = (int)(intptr_t)filtered->strs[selection - 1];
                free(filtered->strs);
                free(filtered);
                endwin();
                free(wstr);
                return i;
            }
            break;
        }
        switch (wchr) {
            case KEY_BACKSPACE:
                if (len > 0) {
                    len--;
                    wstr[len] = L'\0';
                    filter_changed = 1;
                }
                break;
            case KEY_UP:
                if (selection > 1)
                    selection--;
                break;
            case KEY_DOWN:
                if (selection < filtered->used)
                    selection++;
                break;
            default:
                if (iswprint(wchr) && len < MAX_STR_SIZE) {
                    wstr[len] = wchr;
                    len++;
                    wstr[len] = L'\0';
                    filter_changed = 1;
                }
                break;
        }
        if (filter_changed) {
            darr_clear(filtered);
            for (int i = 0; i < options_len; i++) {
                wchar_t* wopt = mbstowcs_alloc(options[i]);
                if (wopt && wcsstr(wopt, wstr) != NULL) {
                    darr_push(filtered, (char*)(intptr_t)i);
                }
                free(wopt);
            }
            filter_changed = 0;
            selection = 1;
            top = 0;
        }
        int visible_rows = rows - 1;
        if (selection < top + 1) {
            top = selection - 1;
        } else if (selection > top + visible_rows) {
            top = selection - visible_rows;
        }
        move(0, prompt_len);
        clrtobot();
        printw("%ls", wstr);
        int line = 1;
        for (int f = top; f < filtered->used && line <= visible_rows; f++) {
            int i = (int)(intptr_t)filtered->strs[f];
            wchar_t* wopt = mbstowcs_alloc(options[i]);
            if (wopt) {
                move(line, 0);
                if (line == selection - top) attron(A_STANDOUT);
                printw("%.*ls", cols, wopt);
                if (line == selection - top) attroff(A_STANDOUT);
                line++;
            }
            free(wopt);
        }
        move(0, prompt_len + len);
        refresh();
    }
    free(filtered->strs);
    free(filtered);
    endwin();
    free(wstr);
    return -1;
}

int main(int argc, char** argv) {
    darr* options = get_options();
    int chosen;
    if (argc == 1)
        chosen = mmenu(options->strs, options->used, "> ");
    else
        chosen = mmenu(options->strs, options->used, argv[1]);
    if (chosen == -1) {
        printf("\n");
    } else if (argc > 2 && argv[2] != NULL && argv[2][0] == 't') {
        printf("%i\n", chosen);
    } else {
        printf("%s\n", options->strs[chosen]);
    }
    darr_free(options);
    return 0;
}
