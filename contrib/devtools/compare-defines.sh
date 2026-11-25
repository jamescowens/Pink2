#!/bin/sh
# Compare the preprocessor defines the old makefile build gives src/main.cpp
# with the ones the CMake build gives it.
#
# Usage: contrib/devtools/compare-defines.sh [--ignore REGEX]... <build-dir> [make vars...]
#   e.g. contrib/devtools/compare-defines.sh build
#        contrib/devtools/compare-defines.sh build-upnp1 USE_UPNP=1
#        contrib/devtools/compare-defines.sh \
#            --ignore '^BOOST_[A-Z0-9_]+_(STATIC_LINK|DYN_LINK|NO_LIB)$|^BOOST_THREAD_USE_LIB$' \
#            build-dep USE_UPNP=1
#
# Prints the sorted -D set of `make -n -f makefile.unix obj/main.o <vars>` and
# the sorted -D set for src/main.cpp from <build-dir>/compile_commands.json
# (configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON), and exits 1 if they
# differ. This checks the Linux makefile (makefile.unix) only.
#
# --ignore REGEX (an extended regular expression, may be repeated,
# before or after <build-dir>) removes every define whose NAME matches it from
# both sides before they are compared. The name is what follows -D, up to any
# '='; anchor the expression (^...$) to match whole names. Each removed define
# is printed, so the log shows what the comparison did not cover. The release
# build uses it for the linkage-selection macros Boost's CMake package puts on
# a static Boost (BOOST_<LIB>_STATIC_LINK, BOOST_THREAD_USE_LIB), which choose
# between the Windows DLL and static entry points and compile no different code
# on Linux.

set -eu

# One collation for every sort below, so the two lists order alike.
LC_ALL=C
export LC_ALL

usage() {
    echo "Usage: $0 [--ignore REGEX]... <build-dir> [make vars...]" >&2
    exit 2
}

# Options may come anywhere; everything else is <build-dir>, then the make
# variables, which are rotated to the end of "$@" in their order.
IGNORE=""
BUILD_DIR=""
n=$#
while [ "$n" -gt 0 ]; do
    a=$1
    shift
    n=$((n - 1))
    case $a in
        --ignore)
            [ "$n" -gt 0 ] || usage
            re=$1
            shift
            n=$((n - 1))
            ;;
        --ignore=*)
            re=${a#--ignore=}
            ;;
        -*)
            echo "$0: unknown option: $a" >&2
            usage
            ;;
        *)
            if [ -z "$BUILD_DIR" ]; then
                BUILD_DIR=$a
            else
                set -- "$@" "$a"
            fi
            continue
            ;;
    esac
    [ -n "$re" ] || { echo "$0: --ignore needs a non-empty REGEX" >&2; exit 2; }
    # grep exits 2 on an invalid expression, 0 or 1 otherwise.
    rc=0
    printf '\n' | grep -E -e "$re" >/dev/null 2>&1 || rc=$?
    [ "$rc" -le 1 ] || { echo "$0: --ignore: not an extended regular expression: $re" >&2; exit 2; }
    IGNORE="${IGNORE:+$IGNORE|}($re)"
done
[ -n "$BUILD_DIR" ] || usage

TOP=$(cd "$(dirname "$0")/../.." && pwd)
CC_JSON="$BUILD_DIR/compile_commands.json"
if [ ! -f "$CC_JSON" ]; then
    echo "$0: $CC_JSON not found (configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)" >&2
    exit 2
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

# The old build: the -D tokens on the compile line for obj/main.o.
make -n -C "$TOP/src" -f makefile.unix obj/main.o "$@" 2>"$TMP/make.err" \
    | tr ' ' '\n' | grep '^-D' | sort -u >"$TMP/make.defs" || true
if [ ! -s "$TMP/make.defs" ]; then
    echo "$0: no defines from makefile.unix" >&2
    cat "$TMP/make.err" >&2
    exit 2
fi

# The CMake build: the -D tokens for src/main.cpp.
python3 -I - "$CC_JSON" "$TOP/src/main.cpp" >"$TMP/cmake.defs" <<'PY'
import json, os, shlex, sys
db, main = sys.argv[1], sys.argv[2]
for entry in json.load(open(db)):
    if os.path.realpath(os.path.join(entry.get("directory", ""), entry["file"])) == os.path.realpath(main):
        args = entry.get("arguments") or shlex.split(entry["command"])
        out = set()
        i = 0
        while i < len(args):
            a = args[i]
            if a == "-D" and i + 1 < len(args):
                out.add("-D" + args[i + 1])
                i += 1
            elif a.startswith("-D"):
                out.add(a)
            i += 1
        print("\n".join(sorted(out)))
        break
else:
    sys.exit("no entry for %s in %s" % (main, db))
PY

echo "== makefile.unix $* =="
cat "$TMP/make.defs"
echo "== CMake ($BUILD_DIR) =="
cat "$TMP/cmake.defs"

# --ignore: split each list into the defines compared and the ones dropped, by
# name. The expression goes through the environment, not awk -v, which would
# rewrite its backslashes.
if [ -n "$IGNORE" ]; then
    for side in make cmake; do
        IGNORE_RE=$IGNORE awk -v kept="$TMP/$side.kept" -v dropped="$TMP/$side.dropped" '
            { name = $0; sub(/^-D/, "", name); sub(/=.*/, "", name)
              if (name ~ ENVIRON["IGNORE_RE"]) print > dropped; else print > kept }
            END { printf "" > kept; printf "" > dropped }' "$TMP/$side.defs"
        mv "$TMP/$side.kept" "$TMP/$side.defs"
    done
    echo "== ignored (names matching $IGNORE) =="
    echo "makefile.unix:"
    sed 's/^/    /' "$TMP/make.dropped"
    echo "CMake:"
    sed 's/^/    /' "$TMP/cmake.dropped"
fi

if diff -u "$TMP/make.defs" "$TMP/cmake.defs" >"$TMP/diff"; then
    echo "defines match"
else
    echo "defines DIFFER (-makefile +cmake):"
    cat "$TMP/diff"
    exit 1
fi
