/* Regression tests for shared capacity growth arithmetic. */
#define main mmenu_cli_main
#include "../main.c"
#undef main

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "input-capacity regression failed at line %d: %s\n", __LINE__, #condition); \
        return EXIT_FAILURE; \
    } \
} while (0)

int main(void) {
    int capacity = -1;

    CHECK(mmenu_next_capacity(0, INITIAL_CAP, sizeof(char *), &capacity));
    CHECK(capacity == INITIAL_CAP);
    CHECK(mmenu_next_capacity(INITIAL_CAP, INITIAL_CAP, sizeof(char *), &capacity));
    CHECK(capacity == INITIAL_CAP * 2);

    capacity = 73;
    errno = 0;
    CHECK(!mmenu_next_capacity(INT_MAX / 2 + 1, INITIAL_CAP,
                               sizeof(char *), &capacity));
    CHECK(errno == ENOMEM);
    CHECK(capacity == 73);

    errno = 0;
    CHECK(!mmenu_next_capacity(0, 2, SIZE_MAX, &capacity));
    CHECK(errno == ENOMEM);
    CHECK(capacity == 73);

    errno = 0;
    CHECK(!mmenu_next_capacity(-1, INITIAL_CAP, sizeof(char *), &capacity));
    CHECK(errno == EINVAL);
    CHECK(capacity == 73);

    size_t bytes = 0;
    CHECK(mmenu_size_add(12, 1, &bytes));
    CHECK(bytes == 13);
    errno = 0;
    CHECK(!mmenu_size_add(SIZE_MAX, 1, &bytes));
    CHECK(errno == ENOMEM);
    CHECK(bytes == 13);

    CHECK(mmenu_size_mul(13, sizeof(wchar_t), &bytes));
    CHECK(bytes == 13 * sizeof(wchar_t));
    errno = 0;
    CHECK(!mmenu_size_mul(SIZE_MAX / 2 + 1, 2, &bytes));
    CHECK(errno == ENOMEM);
    CHECK(bytes == 13 * sizeof(wchar_t));
    errno = 0;
    CHECK(!mmenu_size_mul(1, 1, NULL));
    CHECK(errno == ENOMEM);

    CHECK(mmenu_query_cursor_column(2, 3, 80) == 5);
    CHECK(mmenu_query_cursor_column(79, 0, 80) == 79);
    CHECK(mmenu_query_cursor_column(79, 1, 80) == 79);
    CHECK(mmenu_query_cursor_column(SIZE_MAX, 128, 80) == 79);
    CHECK(mmenu_query_cursor_column(0, 128, 80) == 79);
    CHECK(mmenu_query_cursor_column(SIZE_MAX, 128, 0) == 0);

    CHECK(setlocale(LC_CTYPE, "C.UTF-8") != NULL);
    CHECK(mmenu_prompt_display_width("abc", INT_MAX) == 3);
    CHECK(mmenu_prompt_display_width("界a", INT_MAX) == 3);
    CHECK(mmenu_prompt_display_width("界a", 2) == 2);
    CHECK(mmenu_prompt_display_width("abc", 0) == 0);
    CHECK(mmenu_prompt_display_width(NULL, 80) == 0);

    return EXIT_SUCCESS;
}
