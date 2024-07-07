#define NOBUILD_IMPLEMENTATION
#include "./nobuild.h"

#define SOURCE "main.c"
#define BIN "mmenu"
#define PREFIX "/usr/local/bin/"
#define OLD "c.old"
#define CFLAGS  "-Wall",                    \
                "-Wextra",                  \
                "-Wfatal-errors",           \
                "-std=c2x",                 \
                "-pedantic",                \
                "-pedantic-errors",         \
                "-Wmissing-include-dirs",   \
                "-Wunused-variable",        \
                "-O3"


#define LIBS    "-lncurses"

char *cc(void){
    char *result = getenv("CC");
    return result ? result : "cc";
}

void Compile(void) {
    CMD(cc(), "-c", SOURCE);
}

void Link(void) {
    CMD(cc(), "-o", BIN, "main.o", CFLAGS, LIBS);
}

void Install(void) {
    CMD("doas", "cp", "-f", BIN, PREFIX);
}

void Wipe(void) {
    CMD("doas", "rm", "-v", PREFIX""BIN);
    CMD("rm", BIN, "c.old");
}

void test(void) {
    Compile();
    Link();
    CMD("../menu");
}

int main(int argc, char *argv[]) {
    GO_REBUILD_URSELF(argc, argv);

    if (argc < 2) {
        printf("Usage: %s [-c compile] [-l link] [-i install] [-w wipe]\n", argv[0]);
        return EXIT_SUCCESS;
    }

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];

        if (arg[0] == '-') {
            for (unsigned long int j = 1; j < strlen(arg); j++) {

                switch (arg[j]) {
                    case 'c': Compile();  break;
                    case 'l': Link();     break;
                    case 'i': Install();	break;
                    case 'w': Wipe();     break;
                    case 't': test();     break;
                    default: printf("Unknown option: %c\n", arg[j]);
                        break;
                }
            }
        }
    }
    return EXIT_SUCCESS;
}
