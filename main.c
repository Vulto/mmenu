#include <stdio.h>
#include <stdlib.h>
#include <ncurses.h>
#include <locale.h>

#define MAX_STR_SIZE 128
#define D_SIZE 128
#define PATH "/tmp/mmenu"

typedef struct darr {
    char** strs;
    int size;
    int used;
} darr;

void handle_error(const char* msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void darr_push(darr* arr, char* str) {
    if (arr->used == arr->size) {
        arr->size *= 2;
        arr->strs = realloc(arr->strs, arr->size * sizeof(char*));
        if (arr->strs == NULL) handle_error("realloc");
    }
    arr->strs[arr->used] = str;
    arr->used++;
}

void darr_free(darr* arr) {
    for (int i = 0; i < arr->used; i++) {
        free(arr->strs[i]);
    }
    free(arr->strs);
    free(arr);
}

darr* get_options(void) {
    darr* options = malloc(sizeof(darr));
    if (options == NULL) handle_error("malloc");

    options->strs = malloc(D_SIZE * sizeof(char*));
    if (options->strs == NULL) handle_error("malloc");

    options->size = D_SIZE;
    options->used = 0;

    char chr;
    char* str;
    int i;
    while (1) {
        str = malloc(MAX_STR_SIZE * sizeof(char));
        if (str == NULL) handle_error("malloc");

        for (i = 0; i < MAX_STR_SIZE; i++) {
            chr = fgetc(stdin);
            if (chr == '\n' || chr == EOF) break;
            str[i] = chr;
        }
        str[i] = '\0';  // Null-terminate the string
        darr_push(options, str);

        if (chr == EOF) break;
    }

    return options;
}

int str_in_str(const char* substr, const char* str) {
    if (str[0] == '\0') return 0;
    int i = 0, j = 0;
    do {
        if (str[i] == substr[j]) j++;
        else j = 0;
        if (substr[j] == '\0') return 1;
        i++;
    } while (str[i] != '\0');

    return 0;
}

int str_len(const char* str) {
    int i = 0;
    while (str[i] != '\0') i++;
    return i;
}

int mmenu(char** options, int options_len, const char* prompt) {
    setlocale(LC_ALL, "");
    FILE* f = fopen("/dev/tty", "r+");
    if (f == NULL) handle_error("fopen");
    SCREEN* screen = newterm(NULL, f, f);
    set_term(screen);
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    char* str = malloc(MAX_STR_SIZE * sizeof(char));
    if (str == NULL) handle_error("malloc");

    int len = 0;
    int chr;
    int selection = 1;

    printw("%s", prompt);
    int prompt_len = str_len(prompt);

    for (int i = 0; i < rows - 1 && i < options_len; i++) {
        move(i + 1, 0);
        if (i == 0) attron(A_STANDOUT);
        printw("%.*s", cols, options[i]);
        if (i == 0) attroff(A_STANDOUT);
    }

    move(0, prompt_len + len);
    refresh();

    while ((chr = getch()) != 27) { // Escape key
        if (chr == '\n') {
            int filtered = 0;
            for (int i = 0; i < options_len; i++) {
                if (str_in_str(str, options[i])) {
                    filtered++;
                    if (filtered == selection) {
                        endwin();
                        free(str);
                        return i;
                    }
                }
            }
            break;
        }
        switch (chr) {
            case KEY_BACKSPACE:
            case KEY_LEFT:
                if (len > 0) {
                    len--;
                    str[len] = '\0';
                }
                break;
            case KEY_UP:
                if (selection > 1)
                    selection--;
                break;
            case KEY_DOWN:
                if (selection < rows - 1)
                    selection++;
                break;
            case KEY_RIGHT:
                break;
            default:
                if (len != MAX_STR_SIZE - 1 && chr < 255) {
                    str[len] = chr;
                    len++;
                }
                selection = 1;
                break;
        }
        move(0, prompt_len);
        clrtobot();
        printw("%s", str);
        for (int i = 0, line = 0; i < options_len && line < rows; i++) {
            if (str_in_str(str, options[i])) {
                line++;
                move(line, 0);
                if (line == selection) attron(A_STANDOUT);
                printw("%.*s", cols, options[i]);
                if (line == selection) attroff(A_STANDOUT);
            }
        }
        move(0, prompt_len + len);
        refresh();
    }
    endwin();
    free(str);
    return -1;
}

int main(int argc, char** argv) {
    darr* options = get_options();
    freopen("/dev/tty", "rw", stdin);
    FILE* file_ptr = fopen(PATH, "w");
    if (file_ptr == NULL) handle_error("fopen");

    int chosen;
    if (argc == 1)
        chosen = mmenu(options->strs, options->used, "> ");
    else
        chosen = mmenu(options->strs, options->used, argv[1]);

    if (chosen == -1)
        printf("\n");
    else if (argc > 2 && argv[2][0] == 't')
        printf("%i\n", chosen);
    else
        printf("%s\n", options->strs[chosen]);

    darr_free(options);
    return 0;
}
