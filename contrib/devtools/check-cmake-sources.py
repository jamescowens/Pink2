#!/usr/bin/env python3
"""Check that the CMake build compiles the same sources as the old build files.

Usage: python3 -I contrib/devtools/check-cmake-sources.py --build-dir BUILD [--reply-dir DIR]

Reads the CMake File API codemodel of BUILD (writes the query file
.cmake/api/v1/query/codemodel-v2 and re-runs `cmake BUILD` if no reply exists) and compares:

  daemon  src/makefile.unix OBJS (+ obj/txdb-leveldb.o, makefile.unix:156) with the union of the
          sources of pink2d and pink2_core. obj/x.o maps to src/x.cpp, or src/x.S for scrypt-*.
  tests   src/test/*.cpp + src/noui.cpp with the sources of test_pink2.
  GUI     pinkcoin-qt.pro SOURCES and FORMS with the sources of Pinkcoin-Qt plus those of every
          in-tree library target it links (leveldb, memenv and libdb are outside the comparison).
          Only done when the Pinkcoin-Qt target exists.

Rules for the GUI comparison:
  * Only entries at the top level of the .pro count. Anything inside a { } block is scoped
    (windows:, macx:, win32:, contains(...) ...) and is excluded from both sides, with one
    exception: the contains(USE_QRCODE, 1) block counts as expected exactly when the build's
    ENABLE_QRENCODE cache entry is ON.
  * Headers, generated files (moc_*, qrc_*, ui_*, build.h, the processed qrc, and anything else
    in the build tree, such as the static Qt plugin import file) and anything outside the source
    tree are ignored; only .cpp/.S sources and .ui forms are compared.
  * Platform-only CMake sources (.mm, .rc) are ignored for the same reason as the scoped .pro
    entries.

Prints each file present on one side only. Exit 0 if all agree, 1 on drift, 2 on a usage or
environment error.
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys

EXTS = ('.cpp', '.S', '.ui')
EXCLUDED_LIBS = ('leveldb', 'memenv', 'libdb', 'db')


def die(msg):
    print('error: ' + msg, file=sys.stderr)
    sys.exit(2)


def load_reply(reply_dir):
    idx = sorted(glob.glob(os.path.join(reply_dir, 'index-*.json')))
    if not idx:
        return None
    with open(idx[-1]) as f:
        index = json.load(f)
    obj = [o for o in index['objects'] if o['kind'] == 'codemodel'][0]
    with open(os.path.join(reply_dir, obj['jsonFile'])) as f:
        cm = json.load(f)
    targets = {}
    for t in cm['configurations'][0]['targets']:
        with open(os.path.join(reply_dir, t['jsonFile'])) as f:
            targets[t['name']] = json.load(f)
    return cm['paths']['source'], targets


def ensure_reply(build_dir, reply_dir):
    r = load_reply(reply_dir)
    if r:
        return r
    q = os.path.join(build_dir, '.cmake', 'api', 'v1', 'query')
    os.makedirs(q, exist_ok=True)
    open(os.path.join(q, 'codemodel-v2'), 'w').close()
    p = subprocess.run(['cmake', build_dir], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       universal_newlines=True)
    if p.returncode != 0:
        die('cmake %s failed:\n%s' % (build_dir, p.stdout))
    r = load_reply(reply_dir)
    if not r:
        die('no codemodel reply in ' + reply_dir)
    return r


def cmake_sources(target, srcdir, builddir):
    """Set of source-tree-relative .cpp/.S/.ui paths of a target, generated files excluded."""
    out = set()
    # The build tree, unless it is the source tree itself (an in-source build).
    builddir = os.path.realpath(builddir)
    if builddir == os.path.realpath(srcdir):
        builddir = None
    for s in target.get('sources', []):
        if s.get('isGenerated'):
            continue
        p = s['path']
        full = os.path.realpath(p if os.path.isabs(p) else os.path.join(srcdir, p))
        if builddir and full.startswith(builddir + os.sep):
            continue
        if os.path.isabs(p):
            if not p.startswith(srcdir + os.sep):
                continue
            p = os.path.relpath(p, srcdir)
        if p.startswith('..') or not p.endswith(EXTS):
            continue
        b = os.path.basename(p)
        if b.startswith(('moc_', 'qrc_', 'ui_')):
            continue
        out.add(os.path.normpath(p))
    return out


def makefile_objs(srcdir):
    path = os.path.join(srcdir, 'src', 'makefile.unix')
    text = open(path).read().replace('\\\n', ' ')
    objs = []
    for line in text.splitlines():
        m = re.match(r'\s*OBJS\s*(\+?=)(.*)', line)
        if m:
            objs += re.findall(r'obj/([\w.-]+)\.o\b', m.group(2))
    if 'txdb-leveldb' not in objs:
        die('obj/txdb-leveldb.o not added to OBJS in makefile.unix')
    res = set()
    for o in objs:
        s = os.path.join(srcdir, 'src', o + '.S')
        res.add('src/%s.S' % o if o.startswith('scrypt-') and os.path.exists(s)
                else 'src/%s.cpp' % o)
    return res


def pro_sources(srcdir, qr_enabled):
    """Unscoped SOURCES and FORMS of pinkcoin-qt.pro (see the module docstring)."""
    lines = open(os.path.join(srcdir, 'pinkcoin-qt.pro')).read().replace('\\\n', ' ').splitlines()
    res = set()
    depth = 0
    qr_depth = None  # depth at which a contains(USE_QRCODE, 1) block was opened
    for line in lines:
        line = line.split('#', 1)[0]
        st = line.strip()
        counted = depth == 0 or (qr_enabled and qr_depth is not None and depth == qr_depth + 1)
        m = re.match(r'(SOURCES|FORMS)\s*\+?=(.*)', st)
        if m and counted:
            res.update(os.path.normpath(x) for x in m.group(2).split()
                       if x.endswith(EXTS))
        opens, closes = line.count('{'), line.count('}')
        if opens and re.match(r'contains\(\s*USE_QRCODE\s*,\s*1\s*\)\s*\{', st) and depth == 0:
            qr_depth = 0
        depth += opens - closes
        if qr_depth is not None and depth <= qr_depth:
            qr_depth = None
    return res


def cache_value(build_dir, name):
    try:
        for line in open(os.path.join(build_dir, 'CMakeCache.txt')):
            m = re.match(r'%s:[A-Z]+=(.*)' % re.escape(name), line.strip())
            if m:
                return m.group(1).strip().upper() in ('ON', '1', 'TRUE', 'YES', 'Y')
    except OSError:
        pass
    return False


def linked_in_tree(targets, name):
    """Names of in-tree targets the named target depends on, transitively, minus excluded ones."""
    by_id = {t['id']: n for n, t in targets.items()}
    seen, todo = set(), [name]
    while todo:
        for d in targets[todo.pop()].get('dependencies', []):
            n = by_id.get(d['id'])
            if n and n not in seen and n not in EXCLUDED_LIBS and not n.startswith('bdb'):
                seen.add(n)
                todo.append(n)
    return seen


def compare(label, old, new, old_name, new_name):
    bad = False
    for f in sorted(old - new):
        print('%s: %s is in %s but not in %s' % (label, f, old_name, new_name))
        bad = True
    for f in sorted(new - old):
        print('%s: %s is in %s but not in %s' % (label, f, new_name, old_name))
        bad = True
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument('--build-dir', required=True)
    ap.add_argument('--reply-dir', help='read this codemodel reply directory instead of the '
                    "build dir's (test seam)")
    a = ap.parse_args()
    build = os.path.abspath(a.build_dir)
    reply = a.reply_dir or os.path.join(build, '.cmake', 'api', 'v1', 'reply')
    if a.reply_dir:
        r = load_reply(reply)
        if not r:
            die('no codemodel reply in ' + reply)
    else:
        r = ensure_reply(build, reply)
    srcdir, targets = r
    drift = False

    for need in ('pink2_core', 'pink2d'):
        if need not in targets:
            die('target %s missing (daemon build not configured)' % need)
    new = (cmake_sources(targets['pink2_core'], srcdir, build)
           | cmake_sources(targets['pink2d'], srcdir, build))
    drift |= compare('daemon', makefile_objs(srcdir), new, 'makefile.unix', 'CMake pink2d+pink2_core')

    if 'test_pink2' in targets:
        old = set('src/test/' + os.path.basename(f) for f in
                  glob.glob(os.path.join(srcdir, 'src', 'test', '*.cpp')))
        old.add('src/noui.cpp')
        drift |= compare('tests', old, cmake_sources(targets['test_pink2'], srcdir, build),
                         'src/test + noui.cpp', 'CMake test_pink2')
    else:
        print('tests: test_pink2 not configured, skipped')

    if 'Pinkcoin-Qt' in targets:
        new = cmake_sources(targets['Pinkcoin-Qt'], srcdir, build)
        for lib in linked_in_tree(targets, 'Pinkcoin-Qt'):
            new |= cmake_sources(targets[lib], srcdir, build)
        old = pro_sources(srcdir, cache_value(build, 'ENABLE_QRENCODE'))
        drift |= compare('gui', old, new, 'pinkcoin-qt.pro', 'CMake Pinkcoin-Qt')
    else:
        print('gui: Pinkcoin-Qt not configured, skipped')

    print('DRIFT' if drift else 'OK: CMake and the old build compile the same sources')
    sys.exit(1 if drift else 0)


if __name__ == '__main__':
    main()
