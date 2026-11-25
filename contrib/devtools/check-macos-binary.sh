#!/usr/bin/env bash
#
# Check a macOS program that ships on its own, outside any app bundle.
#
# usage: check-macos-binary.sh <binary> <declared-floor>
#        e.g. check-macos-binary.sh build/bin/pink2d 14.0
#
# The release's pink2d goes out as a bare file in a tarball, so nothing beside it
# can satisfy a library it loads: it must load only the OS's libraries, as
# 2.4.0.0's did. A load of the build machine's Homebrew (/usr/local/...,
# /opt/homebrew/...) works there and fails on a user's Mac.
#
# Fails (exit 1) if the file is not a Mach-O executable, or if any slice
#   - records no minimum macOS, or one above the floor;
#   - has a load command naming an absolute path outside the OS (/usr/lib,
#     /System/Library), Homebrew's prefixes included;
#   - has a non-weak @executable_path, @loader_path or @rpath load.
# Exit 2 on a usage error. The reader is contrib/devtools/macho-check.py, so
# this also runs on Linux. For the app bundle, use check-macos-bundle.sh.

export LC_ALL=C
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <binary> <declared-floor>" >&2
    exit 2
fi

here="$(cd "$(dirname "$0")" && pwd)"
exec python3 -I "${here}/macho-check.py" binary "$1" "$2"
