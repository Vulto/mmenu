#!/bin/sh
set -eu

Root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
TmpDir=$(mktemp -d)
trap 'rm -rf "$TmpDir"' EXIT HUP INT TERM

cat > "$TmpDir/fake-cc" <<'EOF'
#!/bin/sh
HasNcursesw=false
HasTinfow=false
HasTinfo=false
for Arg in "$@"; do
    case "$Arg" in
        -lncursesw) HasNcursesw=true ;;
        -ltinfow) HasTinfow=true ;;
        -ltinfo) HasTinfo=true ;;
    esac
done
[ "$HasNcursesw" = true ] || exit 2
case "${NCURSES_PROBE_EXPECT:?}" in
    ncursesw)
        [ "$HasTinfow" = false ] && [ "$HasTinfo" = false ]
        ;;
    tinfow)
        [ "$HasTinfow" = true ]
        ;;
    tinfo)
        [ "$HasTinfow" = false ] && [ "$HasTinfo" = true ]
        ;;
    unavailable)
        exit 2
        ;;
    *)
        exit 2
        ;;
esac
EOF
chmod +x "$TmpDir/fake-cc"

ExpectProbe() {
    Expected=$1
    ExpectedFlags=$2
    Actual=$(NCURSES_PROBE_EXPECT="$Expected" sh "$Root/ncurses-link-flags.sh" "$TmpDir/fake-cc")
    if [ "$Actual" != "$ExpectedFlags" ]; then
        printf 'FAIL: %s probe returned %s, expected %s\n' "$Expected" "$Actual" "$ExpectedFlags" >&2
        exit 1
    fi
    printf 'PASS: %s linker probe selects %s\n' "$Expected" "$ExpectedFlags"
}

ExpectProbe ncursesw '-lncursesw'
ExpectProbe tinfow '-lncursesw -ltinfow'
ExpectProbe tinfo '-lncursesw -ltinfo'

if NCURSES_PROBE_EXPECT=unavailable sh "$Root/ncurses-link-flags.sh" "$TmpDir/fake-cc" >"$TmpDir/unavailable.out" 2>"$TmpDir/unavailable.err"; then
    printf 'FAIL: linker probe succeeded when every candidate was unavailable\n' >&2
    exit 1
fi
if ! grep -Fq 'Unable to link ncursesw with known terminfo library combinations.' "$TmpDir/unavailable.err"; then
    printf 'FAIL: unavailable linker probe did not report a clear diagnostic\n' >&2
    cat "$TmpDir/unavailable.err" >&2
    exit 1
fi
printf 'PASS: unavailable ncursesw linker candidates fail with a diagnostic\n'
