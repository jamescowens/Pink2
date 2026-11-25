#!/usr/bin/env bash
# Run smoke.sh against the Windows programs, under wine.
#
# usage: smoke-wine.sh <pink2d.exe> [Pinkcoin-Qt.exe]
#
# smoke.sh starts the program it is given as the server, backgrounded, and runs
# it again as the RPC client, reading the client's output. Each .exe gets a
# wrapper here that does both under wine:
#   - a server or GUI start (only -options on the command line) execs wine,
#     whose process becomes the Windows program, so smoke.sh's $!, kill and
#     wait act on the program itself;
#   - an RPC client call (a word that is not an -option, pink2d's own test for
#     one) has its CRLF line ends made LF, so that smoke.sh's
#     `[ "$(cli getblockcount)" = 0 ]` compares a bare 0.
# Pinkcoin-Qt.exe links only Qt's windows platform plugin, so it needs a
# display: wine's null graphics driver gives it one without an X server, and
# QT_QPA_PLATFORM defaults to windows here (smoke.sh's own default, offscreen,
# is not linked in).
#
# The wine prefix is a new one in a temporary directory, removed afterwards,
# unless WINEPREFIX is set (a GUI run then leaves the null driver set in it).
# WINEDEBUG defaults to -all, and WINEDLLOVERRIDES to "mscoree,mshtml=" (no
# Mono or Gecko install prompt). Exits with smoke.sh's status.
set -uo pipefail

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: $0 <pink2d.exe> [Pinkcoin-Qt.exe]" >&2
    exit 2
fi
if ! command -v wine >/dev/null 2>&1; then
    echo "$0: wine not found" >&2
    exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)

work=$(mktemp -d)
if [ -z "${WINEPREFIX:-}" ]; then
    export WINEPREFIX="$work/prefix"
fi
export WINEDEBUG="${WINEDEBUG:--all}"
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-mscoree,mshtml=}"
cleanup() {
    wineserver -k >/dev/null 2>&1 || true
    rm -rf "$work"
}
trap cleanup EXIT

# wrap <program.exe> <wrapper>
wrap() {
    local exe
    if [ ! -f "$1" ]; then
        echo "::error::$1 not found"
        exit 1
    fi
    printf -v exe '%q' "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
    cat > "$2" <<EOF
#!/usr/bin/env bash
for arg in "\$@"; do
    case "\$arg" in
        -*) ;;
        *) wine $exe "\$@" | tr -d '\r'; exit "\${PIPESTATUS[0]}" ;;
    esac
done
exec wine $exe "\$@"
EOF
    chmod +x "$2"
}

echo "== wine $(wine --version), prefix $WINEPREFIX"
wineboot --init >/dev/null 2>&1 || true
progs=("$work/pink2d")
wrap "$1" "$work/pink2d"
if [ "$#" -eq 2 ]; then
    if ! wine reg add 'HKCU\Software\Wine\Drivers' /v Graphics /d null /f >/dev/null; then
        echo "::error::wine reg add failed; cannot select wine's null graphics driver"
        exit 1
    fi
    wrap "$2" "$work/Pinkcoin-Qt"
    progs+=("$work/Pinkcoin-Qt")
fi

QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-windows}" "$here/smoke.sh" "${progs[@]}"
