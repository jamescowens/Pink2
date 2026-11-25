#!/usr/bin/env bash
#
# Check that release binaries carry the hardening the build links them with.
#
# usage: check-binary-hardening.sh <binary>...
#        e.g. check-binary-hardening.sh build/bin/pink2d build/bin/Pinkcoin-Qt
#             check-binary-hardening.sh build/bin/pink2d.exe build/bin/Pinkcoin-Qt.exe
#
# ELF (Linux), read with readelf:
#   - PIE: the ELF type is DYN (ENABLE_PIE; the Linux release is PIE);
#   - full RELRO: a GNU_RELRO program header (-z relro), and BIND_NOW in
#     DT_FLAGS or NOW in DT_FLAGS_1 (-z now);
#   - no executable stack: a GNU_STACK program header whose flags lack E
#     (without the header, a loader may make the stack executable).
# PE (Windows), read with objdump -p:
#   - DllCharacteristics has DYNAMIC_BASE (ASLR, --dynamicbase) and NX_COMPAT
#     (DEP, --nxcompat).
#
# The format comes from the file's first bytes. For PE, objdump is $OBJDUMP if
# set, else x86_64-w64-mingw32-objdump if installed, else objdump.
#
# Fails (exit 1) if any binary is missing, is neither ELF nor PE, or lacks any
# of its format's properties. Exit 2 on a usage error or a missing tool.

export LC_ALL=C
set -euo pipefail

if [ "$#" -eq 0 ]; then
    echo "usage: $0 <binary>..." >&2
    exit 2
fi

need() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "$0: $1 not found" >&2
        exit 2
    fi
}

pe_objdump() {
    if [ -n "${OBJDUMP:-}" ]; then
        echo "$OBJDUMP"
    elif command -v x86_64-w64-mingw32-objdump >/dev/null 2>&1; then
        echo x86_64-w64-mingw32-objdump
    else
        echo objdump
    fi
}

# Sets $problems (one per line) and $found (space-separated) for an ELF file.
check_elf() {
    local bin=$1 header phdrs dyn stack
    need readelf
    header=$(readelf -h -W "$bin")
    phdrs=$(readelf -l -W "$bin")
    dyn=$(readelf -d -W "$bin")

    if grep -Eq '^[[:space:]]*Type:[[:space:]]+DYN([[:space:]]|$)' <<<"$header"; then
        found+=" PIE"
    else
        problems+="not PIE ($(sed -n 's/^[[:space:]]*Type:[[:space:]]*//p' <<<"$header"))"$'\n'
    fi

    if grep -Eq '^[[:space:]]*GNU_RELRO[[:space:]]' <<<"$phdrs"; then
        found+=" RELRO"
    else
        problems+="no GNU_RELRO program header"$'\n'
    fi

    # " 0x...1e (FLAGS)    BIND_NOW", " 0x...6ffffffb (FLAGS_1)    Flags: NOW PIE"
    if grep -Eq '\(FLAGS\).*[[:space:]]BIND_NOW([[:space:]]|$)|\(FLAGS_1\).*[[:space:]]NOW([[:space:]]|$)' <<<"$dyn"; then
        found+=" BIND_NOW"
    else
        problems+="no BIND_NOW (DT_FLAGS BIND_NOW or DT_FLAGS_1 NOW)"$'\n'
    fi

    # "GNU_STACK off vaddr paddr filesz memsz FLAGS align": the flags are the
    # fields between memsz and align ("RW", or "RWE").
    stack=$(awk '$1 == "GNU_STACK" && !seen { seen = 1; for (i = 7; i < NF; i++) f = f $i; print f }' <<<"$phdrs")
    if [ -z "$stack" ]; then
        problems+="no GNU_STACK program header"$'\n'
    elif [[ "$stack" == *E* ]]; then
        problems+="executable stack (GNU_STACK $stack)"$'\n'
    else
        found+=" NX-stack(GNU_STACK $stack)"
    fi
}

# The same for a PE file.
check_pe() {
    local bin=$1 tool chars has flag
    tool=$(pe_objdump)
    need "$tool"
    # The names objdump prints under the DllCharacteristics value, one per
    # tab-indented line. awk reads to the end: stopping early would kill
    # objdump with SIGPIPE, which pipefail turns into a failure.
    chars=$("$tool" -p "$bin" | awk '
        /^DllCharacteristics/ { on = 1; next }
        on && /^\t/ { print $1; next }
        { on = 0 }')
    has=$(awk 'NF { printf "%s%s", s, $1; s = " " }' <<<"$chars")
    for flag in DYNAMIC_BASE NX_COMPAT; do
        if grep -qx "$flag" <<<"$chars"; then
            found+=" $flag"
        else
            problems+="DllCharacteristics lacks $flag (has: ${has:-none})"$'\n'
        fi
    done
}

failures=0
for bin in "$@"; do
    found=""
    problems=""
    if [ ! -f "$bin" ]; then
        echo "FAIL $bin: not found"
        failures=$((failures + 1))
        continue
    fi
    magic=$(head -c 4 "$bin" | od -An -tx1 | tr -d ' \n')
    case "$magic" in
        7f454c46) format=ELF; check_elf "$bin" ;;
        4d5a*)    format=PE;  check_pe "$bin" ;;
        *)
            echo "FAIL $bin: neither ELF nor PE (first bytes $magic)"
            failures=$((failures + 1))
            continue
            ;;
    esac
    if [ -n "$problems" ]; then
        printf '%s\n' "$problems" | grep -v '^$' | sed "s|^|FAIL $bin ($format): |"
        failures=$((failures + 1))
    else
        echo "OK   $bin ($format):$found"
    fi
done

if [ "$failures" -gt 0 ]; then
    echo "check-binary-hardening: $failures of $# binaries failed" >&2
    exit 1
fi
