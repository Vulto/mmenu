#define _GNU_SOURCE
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#define MMENU_IMPLEMENTATION
#include "../mmenu.h"

static int SetTerminalSize(unsigned short rows, unsigned short cols)
{
    int tty_fd = open("/dev/tty", O_RDWR);
    if (tty_fd < 0) return 0;
    struct winsize Size = {.ws_row = rows, .ws_col = cols};
    int result = ioctl(tty_fd, TIOCSWINSZ, &Size);
    close(tty_fd);
    return result == 0;
}

int main(int argc, char **argv)
{
    const char *options[] = {"alpha", "beta", "needle", "café-日本語"};
    const char *null_entry_options[] = {"alpha", NULL, "needle"};
    char long_option[512];
    memset(long_option, 'x', sizeof(long_option) - 1);
    long_option[sizeof(long_option) - 1] = '\0';
    const char *narrow_options[] = {long_option, "beta", "needle"};
    const char invalid_option[] = "\xff" "bad";
    const char invalid_prompt[] = "\xff" "prompt";
    const char *invalid_utf8_options[] = {invalid_option, "alpha"};
    const char *ascii_options[] = {"alpha", "beta", "needle"};
    const char *many_options[140];
    for (size_t i = 0; i < sizeof(many_options) / sizeof(many_options[0]); i++) {
        many_options[i] = options[i % (sizeof(options) / sizeof(options[0]))];
    }
    int count = 4;
    const char *prompt = "test> ";
    pid_t resize_child = -1;
    int narrow_mode = 0;
    int many_mode = 0;
    int ascii_mode = argc > 1 && strcmp(argv[1], "ascii") == 0;
    int invalid_utf8_mode = argc > 1 && strcmp(argv[1], "invalid-utf8") == 0;
    int invalid_prompt_mode = argc > 1 && strcmp(argv[1], "invalid-prompt") == 0;
    int fuzzy_mode = argc > 1 && strcmp(argv[1], "fuzzy") == 0;

    if (argc > 1 && strcmp(argv[1], "empty") == 0) {
        count = 0;
    } else if (argc > 1 && strcmp(argv[1], "null-empty") == 0) {
        count = 0;
    } else if (argc > 1 && strcmp(argv[1], "narrow") == 0) {
        if (!SetTerminalSize(2, 8)) return 2;
        narrow_mode = 1;
    } else if (argc > 1 && strcmp(argv[1], "tiny") == 0) {
        if (!SetTerminalSize(1, 1)) return 2;
        narrow_mode = 1;
    } else if (argc > 1 && strcmp(argv[1], "wide") == 0) {
        if (!SetTerminalSize(24, 80)) return 2;
        narrow_mode = 1;
    } else if (argc > 1 && strcmp(argv[1], "one-row") == 0) {
        if (!SetTerminalSize(1, 40)) return 2;
        narrow_mode = 1;
    } else if (argc > 1 && strcmp(argv[1], "one-column") == 0) {
        if (!SetTerminalSize(8, 1)) return 2;
        narrow_mode = 1;
    } else if (argc > 1 && strcmp(argv[1], "small-grid") == 0) {
        if (!SetTerminalSize(3, 20)) return 2;
        narrow_mode = 1;
    } else if (argc > 1 && strcmp(argv[1], "resize") == 0) {
        resize_child = fork();
        if (resize_child == 0) {
            usleep(80000);
            int tty_fd = open("/dev/tty", O_RDWR);
            if (tty_fd < 0) _exit(3);
            struct winsize Size = {.ws_row = 10, .ws_col = 40};
            if (ioctl(tty_fd, TIOCSWINSZ, &Size) < 0) _exit(4);
            close(tty_fd);
            kill(getppid(), SIGWINCH);
            _exit(0);
        }
        if (resize_child < 0) return 2;
    } else if (argc > 1 && strcmp(argv[1], "many") == 0) {
        count = (int)(sizeof(many_options) / sizeof(many_options[0]));
        if (!getenv("MMENU_SWEEP_API")) {
            if (mmenu(many_options, count, prompt) != -1) return 1;
            puts("MMENU_RESULT=-1");
            return 0;
        }
        many_mode = 1;
    } else if (argc > 1 && strcmp(argv[1], "null-prompt") == 0) {
        prompt = NULL;
    } else if (argc > 1 && strcmp(argv[1], "invalid-null-options") == 0) {
        if (mmenu(NULL, 1, prompt) != -1) return 1;
        puts("MMENU_API_RESULT=invalid-rejected");
        return 0;
    } else if (argc > 1 && strcmp(argv[1], "invalid-negative-count") == 0) {
        if (mmenu(options, -1, prompt) != -1) return 1;
        puts("MMENU_API_RESULT=invalid-rejected");
        return 0;
    } else if (argc > 1 && strcmp(argv[1], "invalid-null-entry") == 0) {
        if (mmenu(null_entry_options, 3, prompt) != -1) return 1;
        puts("MMENU_API_RESULT=invalid-rejected");
        return 0;
    } else if (argc > 1 && strcmp(argv[1], "invalid-flags") == 0) {
        if (mmenuEx(options, 4, prompt, 2u) != -1) return 1;
        puts("MMENU_API_RESULT=invalid-rejected");
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "repeat") == 0) {
        int first = mmenu(options, count, prompt);
        printf("\nMMENU_FIRST=%d\n", first);
        fflush(stdout);
        int second = mmenu(options, count, prompt);
        printf("\nMMENU_SECOND=%d\n", second);
        fflush(stdout);
        return 0;
    }

    int result;
    if (many_mode) result = mmenu(many_options, count, prompt);
    else if (narrow_mode) result = mmenu(narrow_options, 3, prompt);
    else if (invalid_utf8_mode) result = mmenu(invalid_utf8_options, 2, prompt);
    else if (invalid_prompt_mode) result = mmenu(ascii_options, 3, invalid_prompt);
    else if (ascii_mode) result = mmenu(ascii_options, 3, prompt);
    else if (argc > 1 && strcmp(argv[1], "null-empty") == 0) result = mmenu(NULL, 0, prompt);
    else if (fuzzy_mode) result = mmenuEx(options, count, prompt, MMENU_FUZZY);
    else result = mmenu(options, count, prompt);
    printf("\nMMENU_RESULT=%d\n", result);
    return 0;
}
