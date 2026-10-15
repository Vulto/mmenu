#define NOBUILD_IMPLEMENTATION
#include "nobuild.h"

#include <stdio.h>
#include <stdlib.h>

#undef REBUILD_URSELF
#define REBUILD_URSELF(binary_path, source_path) \
    CMD(compiler, "-std=c2x", "-O2", "-Wall", "-Wextra", "-Wpedantic", \
        "-Werror", "-D_DEFAULT_SOURCE", "-o", binary_path, source_path)

int main(int argc, char **argv)
{
    const char *compiler = getenv("CC");
    if (!compiler || !*compiler) compiler = "cc";

    GO_REBUILD_URSELF(argc, argv);

    const char *output = getenv("MMENU_BUILD_OUTPUT");
    if (!output || !*output) output = "mmenu";

    const char *prefix = getenv("PREFIX");
    if (!prefix || !*prefix) prefix = "/usr/local";

    char bin_dir[PATH_MAX];
    char man_dir[PATH_MAX];
    char binary_path[PATH_MAX];
    char manpage_path[PATH_MAX];

    int n = snprintf(bin_dir, sizeof(bin_dir), "%s/bin", prefix);
    if (n < 0 || (size_t)n >= sizeof(bin_dir))
        PANIC("installation prefix is too long");

    n = snprintf(man_dir, sizeof(man_dir), "%s/share/man/man1", prefix);
    if (n < 0 || (size_t)n >= sizeof(man_dir))
        PANIC("installation prefix is too long");

    n = snprintf(binary_path, sizeof(binary_path), "%s/mmenu", bin_dir);
    if (n < 0 || (size_t)n >= sizeof(binary_path))
        PANIC("installation path is too long");

    n = snprintf(manpage_path, sizeof(manpage_path), "%s/mmenu.1", man_dir);
    if (n < 0 || (size_t)n >= sizeof(manpage_path))
        PANIC("man page installation path is too long");

    CMD("sh", "-c",
        "exec \"$1\" -std=c2x -O2 -Wall -Wextra -Wpedantic -Werror "
        "-D_DEFAULT_SOURCE -o \"$2\" main.c $(pkg-config --libs ncursesw)",
        "sh", compiler, output);
    CMD("mkdir", "-p", bin_dir, man_dir);
    CMD("install", "-m", "0755", output, binary_path);
    CMD("install", "-m", "0644", "mmenu.1", manpage_path);

    INFO("Installed %s", binary_path);
    INFO("Installed %s", manpage_path);
    return 0;
}
