#!/usr/bin/env bash
#
# Print the oldest glibc each Linux binary can run on.
#
# usage: glibc-floor.sh <binary>...
#        e.g. glibc-floor.sh build/bin/pink2d build/bin/Pinkcoin-Qt
#
# For each binary, prints one line: the highest GLIBC_x.y symbol version it
# needs (from readelf -V, the version-needs section), then the undefined symbols
# that need that version, which are the ones that set the floor:
#   build/bin/pink2d: GLIBC_2.34 (__libc_start_main pthread_create ...)
# A binary that needs no versioned glibc symbol prints "none".
#
# The floor is the build machine's: a binary links against the glibc it is built
# on, so only a build on the oldest supported distribution gives the release's.
#
# Fails (exit 1) if any binary is missing or is not ELF. Exit 2 on a usage error
# or a missing readelf.

export LC_ALL=C
set -euo pipefail

if [ "$#" -eq 0 ]; then
    echo "usage: $0 <binary>..." >&2
    exit 2
fi
if ! command -v readelf >/dev/null 2>&1; then
    echo "$0: readelf not found" >&2
    exit 2
fi

failures=0
for bin in "$@"; do
    if [ ! -f "$bin" ]; then
        echo "$bin: not found"
        failures=$((failures + 1))
        continue
    fi
    if ! vers=$(readelf -V -W "$bin" 2>&1); then
        echo "$bin: not an ELF file: $vers"
        failures=$((failures + 1))
        continue
    fi
    floor=$(printf '%s\n' "$vers" | grep -oE 'Name: GLIBC_[0-9]+(\.[0-9]+)+' \
        | sed 's/^Name: //' | sort -u -V | tail -n 1 || true)
    if [ -z "$floor" ]; then
        echo "$bin: none"
        continue
    fi
    # Undefined dynamic symbols bound to that version: "... UND name@GLIBC_2.34 (3)".
    syms=$(readelf --dyn-syms -W "$bin" \
        | awk -v v="@${floor}" '$7 == "UND" { n = $8; if (substr(n, length(n) - length(v) + 1) == v) print substr(n, 1, length(n) - length(v)) }' \
        | sort -u | tr '\n' ' ' | sed 's/ $//')
    echo "$bin: $floor (${syms})"
done

if [ "$failures" -gt 0 ]; then
    exit 1
fi
