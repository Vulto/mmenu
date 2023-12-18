#define NOBUILD_IMPLEMENTATION
#include "./nobuild.h"

#define BIN "mmenu"
#define SRC "main.c"
#define DESTDIR "/usr/local/bin/"
#define LIBS "-lncurses"

char *cc(void){
    char *result = getenv("CC");
    return result ? result : "cc";
}

void Build(void) {
	CMD(cc(), "-o", BIN, SRC, LIBS);
}

void Install(void) {
	CMD("doas", "cp", BIN, DESTDIR);
}

void Remove(void) {
	CMD("doas", "rm", "-v", DESTDIR""BIN);
}

void Clean(void) {
	CMD("rm", BIN, "c.old");
}

int main(int argc, char *argv[]) {
	
    GO_REBUILD_URSELF(argc, argv);
    if (argc <= 1){
		Build();
		return EXIT_SUCCESS;
	}

    if (argc > 1){
        if (strcmp(argv[1], "install") == 0){
			Install();
        }else if (strcmp(argv[1], "remove") == 0){
			Remove();
        }else if (strcmp(argv[1], "clean") == 0){
			Clean();
        }
    }
    return EXIT_SUCCESS;
}
