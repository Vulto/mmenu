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

int main(int argc, char **argv)
{
    const char *options[] = {"alpha", "beta", "needle", "café-日本語"};
    const char *null_entry_options[] = {"alpha", NULL, "needle"};
    const char *many_options[140];
    for (size_t i = 0; i < sizeof(many_options) / sizeof(many_options[0]); i++) {
        many_options[i] = options[i % (sizeof(options) / sizeof(options[0]))];
    }
    int count = 4;
    const char *prompt = "test> ";
    pid_t resize_child = -1;

    if (argc > 1 && strcmp(argv[1], "empty") == 0) {
        count = 0;
    } else if (argc > 1 && strcmp(argv[1], "null-empty") == 0) {
        count = 0;
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
        if (mmenu(many_options, count, prompt) != -1) return 1;
        puts("MMENU_RESULT=-1");
        return 0;
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
    }

    int result = (argc > 1 && strcmp(argv[1], "null-empty") == 0) ? mmenu(NULL, 0, prompt) : mmenu(options, count, prompt);
    printf("\nMMENU_RESULT=%d\n", result);
    return 0;
}
