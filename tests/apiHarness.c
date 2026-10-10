#define MMENU_IMPLEMENTATION
#include "../mmenu.h"

int main(int argc, char **argv)
{
    const char *options[] = {"alpha", "beta", "needle"};
    int count = 3;

    if (argc > 1 && strcmp(argv[1], "empty") == 0) {
        count = 0;
    }

    int result = mmenu(options, count, "test> ");
    printf("\nMMENU_RESULT=%d\n", result);
    return 0;
}
