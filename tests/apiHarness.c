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

static const char *test_search_text(const char *option, int index, void *userdata) {
    (void)option;
    const char *const *search_text = userdata;
    return search_text[index];
}

static const char *test_display_text(const char *option, int index, void *userdata) {
    (void)option;
    const char *const *display_text = userdata;
    return display_text[index];
}

static int test_preview_callback(const char *option, int index,
                                const wchar_t *query, char *output,
                                size_t output_size, void *userdata) {
    (void)userdata;
    int result = snprintf(output, output_size, "api-preview:%s:%d:%ls",
                          option, index, query);
    return result < 0 ? -1 : 0;
}


static int test_count_preview_callback(const char *option, int index,
                                       const wchar_t *query, char *output,
                                       size_t output_size, void *userdata) {
    int *calls = userdata;
    (void)option;
    (void)index;
    (void)query;
    (*calls)++;
    int result = snprintf(output, output_size, "preview-call-count");
    return result < 0 ? -1 : 0;
}

static int test_long_preview_callback(const char *option, int index,
                                      const wchar_t *query, char *output,
                                      size_t output_size, void *userdata) {
    (void)option;
    (void)index;
    (void)query;
    (void)userdata;
    if (output_size == 0) return -1;
    size_t length = output_size - 1;
    if (length > 200) length = 200;
    memset(output, 'x', length);
    output[length] = '\0';
    return 0;
}

static int test_wide_preview_callback(const char *option, int index,
                                      const wchar_t *query, char *output,
                                      size_t output_size, void *userdata) {
    (void)option;
    (void)index;
    (void)query;
    (void)userdata;
    if (output_size == 0) return -1;
    static const char glyph[] = "日";
    size_t used = 0;
    while (output_size - used > sizeof(glyph) - 1 &&
           used + sizeof(glyph) - 1 <= 600) {
        memcpy(output + used, glyph, sizeof(glyph) - 1);
        used += sizeof(glyph) - 1;
    }
    output[used] = '\0';
    return 0;
}

static int test_resize_preview_callback(const char *option, int index,
                                        const wchar_t *query, char *output,
                                        size_t output_size, void *userdata) {
    (void)option;
    (void)index;
    (void)query;
    (void)userdata;
    const char *marker = getenv("MMENU_PREVIEW_MARKER");
    if (marker) {
        FILE *file = fopen(marker, "a");
        if (!file) return -1;
        int failed = fputc('x', file) == EOF;
        if (fclose(file) != 0) failed = 1;
        if (failed) return -1;
    }
    int result = snprintf(output, output_size, "resize-preview");
    return result < 0 ? -1 : 0;
}

typedef struct {
    const char *const *records;
    size_t count;
    size_t next;
    int pending_once;
} stream_fixture_t;

static int test_stream_next(void *userdata, const char **record) {
    stream_fixture_t *stream = userdata;
    if (stream->pending_once) {
        stream->pending_once = 0;
        return MMENU_STREAM_PENDING;
    }
    if (stream->next >= stream->count) return MMENU_STREAM_EOF;
    *record = stream->records[stream->next++];
    return MMENU_STREAM_RECORD;
}

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
    const char *many_options[600];
    for (size_t i = 0; i < sizeof(many_options) / sizeof(many_options[0]); i++) {
        many_options[i] = options[i % (sizeof(options) / sizeof(options[0]))];
    }
    int count = 4;
    const char *prompt = "test> ";
    if (argc > 1 && strcmp(argv[1], "tiebreak") == 0) {
        const char *rank_options[] = {"ab-long", "ab"};
        mmenu_config config = {0};
        config.initial_query = "ab";
        config.tiebreak = "length";
        int result = mmenuConfigured(rank_options, 2, prompt, MMENU_FUZZY, &config);
        printf("MMENU_RESULT=%d\n", result);
        return result < 0 ? 1 : 0;
    }
    if (argc > 1 && strcmp(argv[1], "invalid-tiebreak") == 0) {
        mmenu_config config = {0};
        config.tiebreak = "length,length";
        int result = mmenuConfigured(options, count, prompt, MMENU_FUZZY, &config);
        if (result != -1) return 1;
        puts("MMENU_API_RESULT=invalid-rejected");
        return 0;
    }

    if (argc > 1 &&
        (strcmp(argv[1], "cycle") == 0 ||
         strcmp(argv[1], "cycle-default") == 0 ||
         strcmp(argv[1], "cycle-empty") == 0)) {
        mmenu_config config = {0};
        config.initial_query = strcmp(argv[1], "cycle-empty") == 0
                                   ? "no-such-option" : "a";
        config.cycle = strcmp(argv[1], "cycle") == 0 ||
                       strcmp(argv[1], "cycle-empty") == 0;
        int result = mmenuConfigured(options, count, prompt, 0, &config);
        printf("MMENU_RESULT=%d\n", result);
        return result < -1 ? 1 : 0;
    }

    pid_t resize_child = -1;
    int narrow_mode = 0;
    int many_mode = 0;
    int multi_mode = argc > 1 && strcmp(argv[1], "multi") == 0;
    int ascii_mode = argc > 1 && strcmp(argv[1], "ascii") == 0;
    int invalid_utf8_mode = argc > 1 && strcmp(argv[1], "invalid-utf8") == 0;
    int invalid_prompt_mode = argc > 1 && strcmp(argv[1], "invalid-prompt") == 0;
    int fuzzy_mode = argc > 1 && strncmp(argv[1], "fuzzy", 5) == 0;

    if (argc > 1 && strcmp(argv[1], "no-tty") == 0) {
        if (mmenu(options, count, prompt) != -1) return 1;
        puts("MMENU_API_RESULT=terminal-unavailable");
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "nth-api") == 0) {
        const char *field_options[] = {"first alpha", "second needle", "third other"};
        const char *search_text[] = {"alpha", "needle", "other"};
        const char *display_text[] = {"DISPLAY_ALPHA", "DISPLAY_NEEDLE", "DISPLAY_OTHER"};
        mmenu_config config = {
            .initial_query = "needle",
            .search_text = test_search_text,
            .search_text_userdata = (void *)search_text,
            .display_text = test_display_text,
            .display_text_userdata = (void *)display_text
        };
        int result = mmenuConfigured(field_options, 3, prompt, 0, &config);
        printf("MMENU_RESULT=%d\n", result);
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "disabled") == 0) {
        mmenu_config config = {.initial_query = "no-such-option"};
        int result = mmenuConfigured(options, count, prompt, MMENU_DISABLED, &config);
        printf("MMENU_RESULT=%d\n", result);
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "fuzzy-no-sort") == 0) {
        const char *order_options[] = {"zzab", "abzz", "acb"};
        mmenu_config config = {.initial_query = "ab"};
        int result = mmenuConfigured(order_options, 3, prompt,
                                     MMENU_FUZZY | MMENU_NO_SORT, &config);
        printf("MMENU_RESULT=%d\n", result);
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "case-sensitive") == 0) {
        const char *case_options[] = {"alpha", "Alpha", "ALPHA"};
        mmenu_config config = {.initial_query = "Alpha"};
        int result = mmenuConfigured(case_options, 3, prompt,
                                     MMENU_CASE_SENSITIVE, &config);
        printf("MMENU_RESULT=%d\n", result);
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "extended") == 0) {
        const char *extended_options[] = {"beta", "alpha needle", "needle alpha"};
        mmenu_config config = {.initial_query = "needle alpha"};
        int result = mmenuConfigured(extended_options, 3, prompt,
                                     MMENU_EXTENDED, &config);
        printf("MMENU_RESULT=%d\n", result);
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "stream") == 0) {
        const char *stream_records[] = {"alpha", "needle", "other"};
        stream_fixture_t stream = {
            .records = stream_records,
            .count = sizeof(stream_records) / sizeof(stream_records[0]),
            .pending_once = 1
        };
        mmenu_config config = {
            .initial_query = "needle",
            .stream_next = test_stream_next,
            .stream_userdata = &stream
        };
        int result = mmenuConfigured(NULL, 0, prompt, 0, &config);
        printf("MMENU_RESULT=%d\n", result);
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "empty") == 0) {
        count = 0;
    } else if (argc > 1 && strcmp(argv[1], "null-empty") == 0) {
        count = 0;
    } else if (argc > 1 && strcmp(argv[1], "fuzzy-narrow") == 0) {
        if (!SetTerminalSize(2, 8)) return 2;
        narrow_mode = 1;
    } else if (argc > 1 && strcmp(argv[1], "fuzzy-tiny") == 0) {
        if (!SetTerminalSize(1, 1)) return 2;
        narrow_mode = 1;
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
    } else if (argc > 1 &&
               (strcmp(argv[1], "resize") == 0 ||
                strcmp(argv[1], "resize-preview") == 0)) {
        if (strcmp(argv[1], "resize-preview") == 0 &&
            !SetTerminalSize(8, 39)) return 2;
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
    } else if (argc > 1 &&
               (strcmp(argv[1], "many") == 0 ||
                strcmp(argv[1], "many-edit") == 0 ||
                strcmp(argv[1], "fuzzy-many") == 0)) {
        count = (int)(sizeof(many_options) / sizeof(many_options[0]));
        if (strcmp(argv[1], "many") == 0 && !getenv("MMENU_SWEEP_API")) {
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
        if (mmenuEx(options, 4, prompt, 128u) != -1) return 1;
        puts("MMENU_API_RESULT=invalid-rejected");
        return 0;
    } else if (argc > 1 && strcmp(argv[1], "invalid-no-sort") == 0) {
        if (mmenuEx(options, 4, prompt, MMENU_NO_SORT) != -1) return 1;
        puts("MMENU_API_RESULT=invalid-rejected");
        return 0;
    } else if (argc > 1 && strcmp(argv[1], "invalid-case-flags") == 0) {
        if (mmenuEx(options, 4, prompt, MMENU_CASE_SENSITIVE | MMENU_SMART_CASE) != -1) return 1;
        puts("MMENU_API_RESULT=invalid-rejected");
        return 0;
    } else if (argc > 1 && strcmp(argv[1], "invalid-config-keys") == 0) {
        mmenu_config config = {.key_up = L'j', .key_down = L'j'};
        if (mmenuConfigured(options, count, prompt, 0, &config) != -1) return 1;
        puts("MMENU_API_RESULT=invalid-rejected");
        return 0;
    } else if (argc > 1 && strcmp(argv[1], "invalid-config-query") == 0) {
        char long_query[130];
        memset(long_query, 'x', sizeof(long_query) - 1);
        long_query[sizeof(long_query) - 1] = '\0';
        mmenu_config config = {.initial_query = long_query};
        if (mmenuConfigured(options, count, prompt, 0, &config) != -1) return 1;
        puts("MMENU_API_RESULT=invalid-rejected");
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "resize-preview") == 0) {
        mmenu_config config = {
            .preview = test_resize_preview_callback
        };
        int result = mmenuConfigured(options, count, prompt, 0, &config);
        printf("MMENU_RESULT=%d\n", result);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "preview-long") == 0) {
        if (!SetTerminalSize(8, 40)) return 2;
        mmenu_config config = {
            .preview = test_long_preview_callback
        };
        int result = mmenuConfigured(options, count, prompt, 0, &config);
        printf("MMENU_RESULT=%d\n", result);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "preview-unicode") == 0) {
        if (!SetTerminalSize(8, 40)) return 2;
        mmenu_config config = {
            .preview = test_wide_preview_callback
        };
        int result = mmenuConfigured(options, count, prompt, 0, &config);
        printf("MMENU_RESULT=%d\n", result);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "cycle-preview-single") == 0) {
        int preview_calls = 0;
        mmenu_config config = {
            .initial_query = "needle",
            .preview = test_count_preview_callback,
            .preview_userdata = &preview_calls,
            .cycle = 1
        };
        int result = mmenuConfigured(options, count, prompt, 0, &config);
        printf("MMENU_PREVIEW_CALLS=%d\n", preview_calls);
        printf("MMENU_RESULT=%d\n", result);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "preview-callback") == 0) {
        mmenu_config config = {
            .initial_query = "needle",
            .preview = test_preview_callback
        };
        int result = mmenuConfigured(options, count, prompt, 0, &config);
        printf("MMENU_RESULT=%d\n", result);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "configured-query") == 0) {
        mmenu_config config = {.initial_query = "needle"};
        int result = mmenuConfigured(options, count, prompt, 0, &config);
        printf("MMENU_RESULT=%d\n", result);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "configured-query-edit") == 0) {
        const char *query_options[] = {"needle", "needling", "alpha"};
        mmenu_config config = {.initial_query = "needle"};
        int result = mmenuConfigured(query_options, 3, prompt, 0, &config);
        printf("MMENU_RESULT=%d\n", result);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "configured-keys") == 0) {
        mmenu_config config = {.key_down = L'j', .key_accept = L'x'};
        int result = mmenuConfigured(options, count, prompt, 0, &config);
        printf("MMENU_RESULT=%d\n", result);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "exact-typing") == 0) {
        int result = mmenuEx(options, count, prompt, MMENU_EXACT);
        printf("MMENU_RESULT=%d\n", result);
        return 0;
    }

    if (multi_mode) {
        int *selected = NULL;
        int selected_count = 0;
        int status = mmenuMulti(options, count, prompt, 0, &selected, &selected_count);
        if (status != 0) {
            puts("MMENU_MULTI_RESULT=-1");
        } else {
            printf("MMENU_MULTI_RESULT=");
            for (int i = 0; i < selected_count; i++)
                printf("%s%d", i ? "," : "", selected[i]);
            putchar('\n');
        }
        free(selected);
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
    if (many_mode && fuzzy_mode)
        result = mmenuEx(many_options, count, prompt, MMENU_FUZZY);
    else if (many_mode) result = mmenu(many_options, count, prompt);
    else if (narrow_mode && fuzzy_mode)
        result = mmenuEx(narrow_options, 3, prompt, MMENU_FUZZY);
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
