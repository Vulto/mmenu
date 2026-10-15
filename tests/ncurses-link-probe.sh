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
