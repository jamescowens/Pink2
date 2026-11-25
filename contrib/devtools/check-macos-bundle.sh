#!/usr/bin/env bash
#
# Check a macOS app bundle against the floor it is released for.
#
# usage: check-macos-bundle.sh <app> <declared-floor>
#        e.g. check-macos-bundle.sh /Volumes/Pinkcoin-Qt/Pinkcoin-Qt.app 14.0
#
# The bundle's real macOS floor is the HIGHEST minimum among the Mach-O files
# it ships, because dyld refuses to load a library built for a newer macOS than
# the one running, whatever LSMinimumSystemVersion claims. Homebrew sets no
# deployment target, so a bottle carries the minimum of whatever machine built
# it. And a load command that still names the build machine's Homebrew
# (/usr/local/..., /opt/homebrew/...) works there and fails on a user's Mac.
# Both only show on someone else's machine, so this asserts them against the
# artifact, never against the configure flags.
#
# Fails (exit 1) if
#   - Info.plist's LSMinimumSystemVersion is missing or differs from the floor;
#   - any Mach-O in the bundle has a minimum macOS above the floor;
#   - any load command names an absolute path outside the OS (/usr/lib,
#     /System/Library), Homebrew's prefixes included;
#   - a non-weak relative load (@executable_path, @loader_path, @rpath) finds
#     no file in the bundle;
#   - fewer than 10 Mach-O files could be measured.
# Exit 2 on a usage error. The reader is contrib/devtools/macho-check.py, so
# this also runs on Linux against an unpacked bundle.

export LC_ALL=C
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <app> <declared-floor>" >&2
    exit 2
fi
if [ ! -d "$1" ]; then
    echo "ERROR: not a directory: $1" >&2
    exit 2
fi

here="$(cd "$(dirname "$0")" && pwd)"
exec python3 -I "${here}/macho-check.py" bundle "$1" "$2"
