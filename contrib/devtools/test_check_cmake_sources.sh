#!/bin/sh
# Negative test for check-cmake-sources.py: drop src/alert.cpp from pink2_core in a copy of the
# codemodel reply; the checker must exit 1 and name the file.
# Usage: test_check_cmake_sources.sh [BUILD_DIR]   (default: <repo>/build)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
build=${1:-$root/build}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cmake-t5.XXXXXX") || exit 2
trap 'rm -rf "$tmp"' EXIT

# Make sure a reply exists (the checker creates it when missing).
python3 -I "$here/check-cmake-sources.py" --build-dir "$build" >/dev/null 2>&1
reply=$build/.cmake/api/v1/reply
[ -d "$reply" ] || { echo "FAIL: no codemodel reply in $build"; exit 1; }
cp -R "$reply" "$tmp/reply"

python3 -I - "$tmp/reply" <<'PY' || exit 1
import glob, json, sys
d = sys.argv[1]
for f in glob.glob(d + "/target-pink2_core-*.json"):
    j = json.load(open(f))
    n = len(j["sources"])
    j["sources"] = [s for s in j["sources"] if s["path"] != "src/alert.cpp"]
    assert len(j["sources"]) == n - 1, "src/alert.cpp not found in pink2_core"
    json.dump(j, open(f, "w"))
    break
else:
    sys.exit("no pink2_core target file")
PY

out=$(python3 -I "$here/check-cmake-sources.py" --build-dir "$build" --reply-dir "$tmp/reply" 2>&1)
rc=$?
echo "$out"
if [ "$rc" -eq 1 ] && echo "$out" | grep -q 'src/alert.cpp'; then
    echo "PASS: drift detected"
    exit 0
fi
echo "FAIL: expected exit 1 naming src/alert.cpp, got exit $rc"
exit 1
