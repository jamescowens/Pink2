#!/usr/bin/env bash
#
# Check that Linux release binaries load only the libraries every Linux system
# has, so that they run on a machine with none of the build's dependencies.
#
# usage: check-hermetic.sh [--gui] <binary>...
#        e.g. check-hermetic.sh build/bin/pink2d build/bin/test_pink2
#             check-hermetic.sh --gui build/bin/Pinkcoin-Qt
#
# Reads each binary's NEEDED entries (readelf -d) and allows only:
#   - libc.so.N, libm.so.N and the ld-linux loader (Gridcoin's rule), and
#     glibc's own split libraries libpthread.so.0, libdl.so.2 and librt.so.1;
#   - with --gui, for every binary named, also the system X11 libraries a Qt 5
#     xcb GUI loads: libX11*, libxcb*, libxkbcommon(-x11), libfontconfig and
#     libfreetype.
# The patterns are anchored to the whole SONAME, so a name that merely contains
# an allowed one (libmylibc.so.6) is a leak. A binary must also carry no RPATH
# or RUNPATH: one names a directory of the build machine, which on a user's
# machine is at best missing.
#
# Fails (exit 1) if any binary is missing, is not ELF, needs any other library
# or has an RPATH or RUNPATH. Exit 2 on a usage error or a missing readelf.

export LC_ALL=C
set -euo pipefail

gui=0
binaries=()
for arg in "$@"; do
    case "$arg" in
        --gui) gui=1 ;;
        -*) echo "$0: unknown option: $arg" >&2; exit 2 ;;
        *) binaries+=("$arg") ;;
    esac
done
if [ "${#binaries[@]}" -eq 0 ]; then
    echo "usage: $0 [--gui] <binary>..." >&2
    exit 2
fi
if ! command -v readelf >/dev/null 2>&1; then
    echo "$0: readelf not found" >&2
    exit 2
fi

allowed='libc\.so\.[0-9]+|libm\.so\.[0-9]+|ld-linux[-a-z0-9_]*\.so\.[0-9]+'
allowed+='|libpthread\.so\.0|libdl\.so\.2|librt\.so\.1'
if [ "$gui" -eq 1 ]; then
    allowed+='|libX11(-[a-z0-9]+)*\.so\.[0-9]+|libxcb(-[a-z0-9]+)*\.so\.[0-9]+'
    allowed+='|libxkbcommon(-x11)?\.so\.[0-9]+|libfontconfig\.so\.[0-9]+|libfreetype\.so\.[0-9]+'
fi
allowed="^(${allowed})\$"

# A newline-separated list on one line, space-separated.
oneline() {
    printf '%s\n' "$1" | grep -v '^$' | tr '\n' ' ' | sed 's/ $//'
}

failures=0
for bin in "${binaries[@]}"; do
    if [ ! -f "$bin" ]; then
        echo "FAIL $bin: not found"
        failures=$((failures + 1))
        continue
    fi
    if ! dyn=$(readelf -d -W "$bin" 2>&1); then
        echo "FAIL $bin: not an ELF file: $dyn"
        failures=$((failures + 1))
        continue
    fi
    # One bare SONAME per line: "... (NEEDED) Shared library: [libm.so.6]".
    needed=$(printf '%s\n' "$dyn" | sed -n 's/.*(NEEDED).*\[\(.*\)\]$/\1/p')
    leaks=$(printf '%s\n' "$needed" | grep -vE "$allowed" | grep -v '^$' || true)
    paths=$(printf '%s\n' "$dyn" | sed -n 's/.*(\(RPATH\|RUNPATH\)).*\[\(.*\)\]$/\1 \2/p')
    if [ -n "$leaks" ] || [ -n "$paths" ]; then
        if [ -n "$leaks" ]; then
            echo "FAIL $bin: needs $(oneline "$leaks")"
        fi
        if [ -n "$paths" ]; then
            echo "FAIL $bin: has $(oneline "$paths")"
        fi
        echo "     (all NEEDED: $(oneline "$needed"))"
        failures=$((failures + 1))
    else
        echo "OK   $bin: NEEDED $(oneline "${needed:-nothing}")"
    fi
done

if [ "$failures" -gt 0 ]; then
    echo "check-hermetic: $failures of ${#binaries[@]} binaries failed" >&2
    exit 1
fi
