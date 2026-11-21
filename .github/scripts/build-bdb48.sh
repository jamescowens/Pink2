#!/usr/bin/env bash
# Build Berkeley DB 4.8.30.NC (static, with the C++ API) into PREFIX for the
# compat CI jobs. Pinkcoin wallets stay on 4.8 so that 2.4.0.0 nodes can still
# open them.
#
# Same recipe as Gridcoin's bundled BDB 5.3 (src/bdb53/CMakeLists.txt):
#   - rename BDB's __atomic_compare_exchange and atomic_init, which collide
#     with compiler builtins in current clang and GCC;
#   - relax the C diagnostics that GCC 14 and clang 16 turned into errors,
#     because several of BDB's configure probes rely on them.
# The 2010 config.guess and config.sub do not know arm64 macOS, so they are
# replaced with automake's copies when one is available.
set -euo pipefail

PREFIX=${1:?usage: build-bdb48.sh <prefix>}
VER=db-4.8.30.NC
SHA256=12edc0df75bf9abd7f82f821795bcee50f42cb2e5f76a6a281b85732798364ef

WORK=$(mktemp -d)
cd "$WORK"
curl -fsSL --retry 3 -o "$VER.tar.gz" "https://download.oracle.com/berkeley-db/$VER.tar.gz"
# Compare the digest as a string: macOS 15's BSD sha256sum rejects a -c list on stdin.
if [ "$(uname)" = Darwin ]; then
    actual=$(shasum -a 256 "$VER.tar.gz" | awk '{print $1}')
else
    actual=$(sha256sum "$VER.tar.gz" | awk '{print $1}')
fi
if [ "$actual" != "$SHA256" ]; then
    echo "$VER.tar.gz: SHA-256 $actual, expected $SHA256" >&2
    exit 1
fi
echo "$VER.tar.gz: SHA-256 OK"
tar xzf "$VER.tar.gz"
cd "$VER"

files=$(grep -rlE --include='*.[ch]' '__atomic_compare_exchange|atomic_init' .)
perl -pi -e 's/\b__atomic_compare_exchange\b/__atomic_compare_exchange_db/g; s/\batomic_init\b/atomic_init_db/g' $files

automake_share=""
if command -v brew >/dev/null 2>&1 && brew --prefix automake >/dev/null 2>&1; then
    automake_share="$(brew --prefix automake)/share"
fi
for f in config.guess config.sub; do
    for src in /usr/share/misc/$f /usr/share/automake-*/$f ${automake_share:+$automake_share/automake-*/$f}; do
        if [ -f "$src" ]; then
            cp -f "$src" "dist/$f"
            echo "using $src"
            break
        fi
    done
done

CFLAGS="-O2 -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-incompatible-pointer-types"
if [ "$(uname)" = Darwin ]; then
    CC=clang CXX=clang++
    CFLAGS="$CFLAGS -Wno-implicit-function-declaration"
else
    CC=gcc CXX=g++
fi

cd build_unix
../dist/configure --prefix="$PREFIX" --libdir="$PREFIX/lib" --includedir="$PREFIX/include" --enable-cxx --disable-shared --disable-replication --with-pic \
    CC="$CC" CXX="$CXX" CFLAGS="$CFLAGS" CXXFLAGS="-O2"
make -j"$(getconf _NPROCESSORS_ONLN)" libdb_cxx-4.8.a libdb-4.8.a
make install_lib install_include
grep -E '^#define HAVE_(MUTEX|ATOMIC)_' db_config.h || true
ls -l "$PREFIX/lib"
