#!/bin/sh
set -eu

Root=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
CC=${CC:-cc}
CFLAGS=${CFLAGS:--O2 -std=c2x -Wall -Wextra -Wpedantic -Werror}
Output=${MMENU_BUILD_OUTPUT:-"$Root/build/mmenu"}
Action=${1:-build}

if [ "$#" -gt 1 ]; then
    printf 'Usage: %s [build|install]\n' "$0" >&2
    exit 2
fi
case "$Action" in
    build|install) ;;
    *)
        printf 'Usage: %s [build|install]\n' "$0" >&2
        exit 2
        ;;
esac

if ! command -v "$CC" >/dev/null 2>&1; then
    printf 'Compiler not found: %s\n' "$CC" >&2
    exit 127
fi

NCURSES_LIBS=$(sh "$Root/ncurses-link-flags.sh" "$CC")
mkdir -p "$(dirname -- "$Output")"
# shellcheck disable=SC2086 # CFLAGS and NCURSES_LIBS intentionally expand to argument lists.
"$CC" $CFLAGS -D_DEFAULT_SOURCE -o "$Output" "$Root/main.c" $NCURSES_LIBS

if [ "$Action" = install ]; then
    Prefix=${PREFIX:-/usr/local}
    mkdir -p "$Prefix/bin" "$Prefix/share/man/man1"
    install -m 0755 "$Output" "$Prefix/bin/mmenu"
    install -m 0644 "$Root/mmenu.1" "$Prefix/share/man/man1/mmenu.1"
    printf 'Installed %s\n' "$Prefix/bin/mmenu"
    printf 'Installed %s\n' "$Prefix/share/man/man1/mmenu.1"
else
    printf 'Built %s\n' "$Output"
fi
