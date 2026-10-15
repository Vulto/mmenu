#!/bin/sh
# Print portable ncursesw linker flags without requiring pkg-config.
set -eu
CC=${1:-${CC:-cc}}
TmpDir=$(mktemp -d)
trap 'rm -rf "$TmpDir"' EXIT HUP INT TERM
cat > "$TmpDir/probe.c" <<'EOF'
#if defined(__has_include)
#  if __has_include(<ncursesw/curses.h>)
#    include <ncursesw/curses.h>
#  else
#    include <ncurses.h>
#  endif
#else
#  include <ncurses.h>
#endif
int main(void) { return keypad(stdscr, TRUE); }
EOF
if "$CC" -std=c2x -D_DEFAULT_SOURCE -Werror -o "$TmpDir/probe" "$TmpDir/probe.c" -lncursesw >/dev/null 2>&1; then
    printf '%s\n' '-lncursesw'
elif "$CC" -std=c2x -D_DEFAULT_SOURCE -Werror -o "$TmpDir/probe" "$TmpDir/probe.c" -lncursesw -ltinfow >/dev/null 2>&1; then
    printf '%s\n' '-lncursesw -ltinfow'
elif "$CC" -std=c2x -D_DEFAULT_SOURCE -Werror -o "$TmpDir/probe" "$TmpDir/probe.c" -lncursesw -ltinfo >/dev/null 2>&1; then
    printf '%s\n' '-lncursesw -ltinfo'
else
    printf 'Unable to link ncursesw with known terminfo library combinations.\n' >&2
    "$CC" -std=c2x -D_DEFAULT_SOURCE -Werror -o "$TmpDir/probe" "$TmpDir/probe.c" -lncursesw
    exit 1
fi
