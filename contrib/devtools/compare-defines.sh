#!/bin/sh
# Compare the preprocessor defines the old makefile build gives src/main.cpp
# with the ones the CMake build gives it.
#
# Usage: contrib/devtools/compare-defines.sh <build-dir> [make vars...]
#   e.g. contrib/devtools/compare-defines.sh build
#        contrib/devtools/compare-defines.sh build-upnp1 USE_UPNP=1
#
# Prints the sorted -D set of `make -n -f makefile.unix obj/main.o <vars>` and
# the sorted -D set for src/main.cpp from <build-dir>/compile_commands.json
# (configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON), and exits 1 if they
# differ. This checks the Linux makefile (makefile.unix) only.

set -eu

# One collation for every sort below, so the two lists order alike.
LC_ALL=C
export LC_ALL

if [ $# -lt 1 ]; then
    echo "Usage: $0 <build-dir> [make vars...]" >&2
    exit 2
fi

BUILD_DIR=$1
shift

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

if diff -u "$TMP/make.defs" "$TMP/cmake.defs" >"$TMP/diff"; then
    echo "defines match"
else
    echo "defines DIFFER (-makefile +cmake):"
    cat "$TMP/diff"
    exit 1
fi
