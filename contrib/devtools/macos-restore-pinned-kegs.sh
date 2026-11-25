#!/usr/bin/env bash
#
# Restore Pink2's pinned Homebrew kegs for Intel macOS from the S3 mirror.
#
# Thinned from Gridcoin's script of the same name. Why the kegs are pinned at
# all is in macos-pinned-kegs.txt: Homebrew's Intel support is Tier 3, a formula
# without an Intel bottle is source-built on the runner (hours for Qt, and the
# result needs the RUNNER's macOS), and qt@5 is on its way out of Homebrew.
#
# What is NOT here, unlike Gridcoin's: the install of the Homebrew dependency
# closure. Gridcoin's Qt 6 kegs load pcre2, zstd, glib and friends; Pink2's five
# kegs load nothing outside themselves and the OS. That is not assumed: before
# Homebrew is touched, macho-check.py reads every library Pink2 links straight
# out of the tarballs and follows each load command, and the run fails if any
# /usr/local reference resolves outside the pinned set. Installing `brew deps
# qt@5` (18 formulae) would only add libraries nothing loads, and source builds.
#
# WHAT IT DOES, AND IN THIS ORDER
#
#   1. Download each keg, ${KEG_BASE}/<name>--<version>.tar.gz, and verify its
#      sha256 against the manifest.
#   2. Check the tarballs: every member under <name>/<version>/, and every load
#      of the libraries Pink2 links resolves inside the set (macho-check.py).
#   3. Unpack each keg into $(brew --cellar), replacing any copy of that exact
#      version, then PRUNE every other version of the formula, so `brew link`
#      has nothing to choose between.
#   4. Link (plain, or --force for keg-only formulae such as openssl@3 and
#      qt@5), then `brew pin`, so a later `brew upgrade` cannot undo it.
#   5. Verify that the Cellar holds only the pinned version and opt/ links it.
#
# Nothing needs AWS credentials: the objects are world-readable, and only
# publishing them needs a signed request from the bucket's maintainer.
#
# usage: macos-restore-pinned-kegs.sh [manifest]
#        macos-restore-pinned-kegs.sh --verify-only DIR [manifest]
#        manifest defaults to macos-pinned-kegs.txt next to this script.
#        --verify-only does steps 1 and 2 into DIR (kept, and reused on a rerun
#        when the hash still matches) and never runs brew; it works on Linux.
# env:   KEG_BASE  the directory URL holding the tarballs (default below)
#        BREW      brew executable (default: the one on PATH)

export LC_ALL=C
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
KEG_BASE="${KEG_BASE:-https://gridcoin-depends-backup-sources.s3.us-east-1.amazonaws.com/homebrew-kegs/sonoma-x86_64/pink2}"
KEG_BASE="${KEG_BASE%/}"

usage() {
    echo "usage: $0 [manifest]" >&2
    echo "       $0 --verify-only DIR [manifest]" >&2
    exit 2
}

VERIFY_ONLY=0
DIR=""
if [ "${1:-}" = "--verify-only" ]; then
    [ "$#" -ge 2 ] || usage
    VERIFY_ONLY=1
    DIR="$2"
    shift 2
fi
[ "$#" -le 1 ] || usage
MANIFEST="${1:-${here}/macos-pinned-kegs.txt}"

if [ ! -f "${MANIFEST}" ]; then
    echo "ERROR: manifest not found: ${MANIFEST}" >&2
    exit 1
fi

PYTHON="$(command -v python3 || true)"
if [ -z "${PYTHON}" ]; then
    echo "ERROR: python3 is required for the load-command check" >&2
    exit 1
fi

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

BUILT_ON="$(grep -E '^built_on[[:space:]]' "${MANIFEST}" | awk '{print $2}')"
if [ -z "${BUILT_ON}" ]; then
    echo "ERROR: manifest has no 'built_on' line" >&2
    exit 1
fi

NAMES=() VERSIONS=() HASHES=()
while read -r name version hash; do
    case "${name}" in ''|built_on) continue ;; esac
    if [ -z "${hash}" ]; then
        echo "ERROR: ${name} has no sha256 in ${MANIFEST}" >&2
        exit 1
    fi
    NAMES+=("${name}")
    VERSIONS+=("${version}")
    HASHES+=("${hash}")
done < <(sed -E 's/#.*//' "${MANIFEST}")

if [ "${#NAMES[@]}" -eq 0 ]; then
    echo "ERROR: manifest lists no formulae" >&2
    exit 1
fi

if [ "${VERIFY_ONLY}" -eq 0 ]; then
    BREW="${BREW:-$(command -v brew || true)}"
    if [ -z "${BREW}" ] || [ ! -x "${BREW}" ]; then
        echo "ERROR: no usable brew (BREW='${BREW}')" >&2
        exit 1
    fi
    CELLAR="$("${BREW}" --cellar)"
    PREFIX="$("${BREW}" --prefix)"
    DIR="$(mktemp -d)"
    trap 'rm -rf "${DIR}"' EXIT
fi
mkdir -p "${DIR}"

echo "pinned (${BUILT_ON}) from ${KEG_BASE}: ${NAMES[*]}"

# ---------------------------------------------------------------------------
# 1. Download and verify.
# ---------------------------------------------------------------------------
for i in "${!NAMES[@]}"; do
    name="${NAMES[$i]}"
    version="${VERSIONS[$i]}"
    want="${HASHES[$i]}"
    file="${DIR}/${name}--${version}.tar.gz"
    url="${KEG_BASE}/${name}--${version}.tar.gz"

    if [ -f "${file}" ] && [ "$(sha256_of "${file}")" = "${want}" ]; then
        echo "--- ${name} ${version}: already downloaded, sha256 OK"
        continue
    fi
    echo "--- ${name} ${version}"
    rm -f "${file}" "${file}.part"
    if ! curl -fsSL --retry 3 "${url}" -o "${file}.part"; then
        rm -f "${file}.part"
        echo "ERROR: no mirrored keg at ${url}" >&2
        echo "       Build it on a native Intel macOS ${BUILT_ON%%-*} machine and upload" >&2
        echo "       it, or correct the version in ${MANIFEST}. Falling back to a" >&2
        echo "       source build here would take hours and is deliberately not done." >&2
        exit 1
    fi
    # Verify before anything reads it: the point is never to unpack an object
    # we cannot vouch for into the tree that becomes a release.
    got="$(sha256_of "${file}.part")"
    if [ "${got}" != "${want}" ]; then
        rm -f "${file}.part"
        echo "ERROR: sha256 mismatch for ${name} ${version}" >&2
        echo "       expected ${want}" >&2
        echo "       got      ${got}" >&2
        echo "       The mirrored object has changed. Do NOT update the manifest to" >&2
        echo "       match without establishing why." >&2
        exit 1
    fi
    mv "${file}.part" "${file}"
    echo "    sha256 OK ($(wc -c < "${file}" | tr -d ' ') bytes)"
done

# ---------------------------------------------------------------------------
# 2. Member paths and load commands, from the tarballs.
# ---------------------------------------------------------------------------
"${PYTHON}" -I "${here}/macho-check.py" kegs "${MANIFEST}" "${DIR}"

if [ "${VERIFY_ONLY}" -eq 1 ]; then
    echo "verify-only: Homebrew not touched; tarballs kept in ${DIR}"
    exit 0
fi

mkdir -p "${CELLAR}"

# ---------------------------------------------------------------------------
# 3. Unpack each keg and prune every other version of it.
# ---------------------------------------------------------------------------
for i in "${!NAMES[@]}"; do
    name="${NAMES[$i]}"
    version="${VERSIONS[$i]}"
    echo "--- restoring ${name} ${version}"
    # Remove any existing keg of this exact version first. tar overlays files
    # but does not delete ones the archive lacks, so the result would no longer
    # be the tree whose hash was verified.
    rm -rf "${CELLAR:?}/${name}/${version}"
    tar -xzf "${DIR}/${name}--${version}.tar.gz" -C "${CELLAR}"
    for d in "${CELLAR}/${name}"/*; do
        [ -e "${d}" ] || continue
        if [ "$(basename "${d}")" != "${version}" ]; then
            echo "    pruning $(basename "${d}")"
            rm -rf "${d}"
        fi
    done
done

# ---------------------------------------------------------------------------
# 4. Link, then pin.
# ---------------------------------------------------------------------------
for name in "${NAMES[@]}"; do
    # Plain link first; --force only where required (keg-only formulae), since
    # force-linking plants headers ahead of the SDK.
    if ! "${BREW}" link --overwrite "${name}" >/dev/null 2>&1; then
        "${BREW}" link --overwrite --force "${name}" >/dev/null
    fi
    "${BREW}" pin "${name}" >/dev/null 2>&1 || true
done

# ---------------------------------------------------------------------------
# 5. Verify, rather than assume. A wrong version here is a silently
#    mis-targeted release build, so this is a hard failure.
# ---------------------------------------------------------------------------
fail=0
for i in "${!NAMES[@]}"; do
    name="${NAMES[$i]}"
    version="${VERSIONS[$i]}"
    present=""
    for d in "${CELLAR}/${name}"/*; do
        [ -e "${d}" ] || continue
        present="${present:+${present} }$(basename "${d}")"
    done
    linked="$(basename "$(readlink "${PREFIX}/opt/${name}" 2>/dev/null || echo none)")"
    printf '%-16s cellar=[%s] linked=%s\n' "${name}" "${present}" "${linked}"
    [ "${present}" = "${version}" ] || { echo "  ERROR: cellar should hold only ${version}" >&2; fail=1; }
    [ "${linked}" = "${version}" ]  || { echo "  ERROR: linked version should be ${version}" >&2; fail=1; }
done
exit "${fail}"
