#!/usr/bin/env python3
"""Mach-O load-command checks for the macOS release, readable on any host.

Usage:
  python3 -I contrib/devtools/macho-check.py bundle APP FLOOR
  python3 -I contrib/devtools/macho-check.py binary FILE FLOOR
  python3 -I contrib/devtools/macho-check.py kegs MANIFEST DIR

A small Mach-O reader (thin and fat files; LC_BUILD_VERSION and the older
LC_VERSION_MIN_MACOSX; the dylib and rpath commands) behind three checks. It
needs nothing but the Python standard library, so they run on Linux as well as
on a Mac, without otool.

bundle  The app bundle the DMG ships (check-macos-bundle.sh calls this). Fails if
        - Contents/Info.plist's LSMinimumSystemVersion is missing or is not FLOOR;
        - any Mach-O slice in the bundle has a minimum macOS above FLOOR (dyld
          refuses to load it there, whatever the plist claims);
        - any load command (LC_ID_DYLIB, the LC_LOAD_* family, LC_RPATH) names an
          absolute path outside the OS (/usr/lib, /System/Library). That covers
          /usr/local and /opt/homebrew, the build machine's Homebrew: such a
          reference works where it was built and fails on a user's Mac;
        - a non-weak @executable_path, @loader_path or @rpath load resolves to
          no file inside the bundle;
        - fewer than 10 Mach-O files could be read (a check that measured
          nothing would pass everything).

binary  A program shipped on its own, outside any bundle, such as the release's
        pink2d (check-macos-binary.sh calls this). Fails if FILE is not a
        Mach-O executable, or if any slice
        - records no minimum macOS, or one above FLOOR;
        - has a load command naming an absolute path outside the OS (/usr/lib,
          /System/Library), /usr/local and /opt/homebrew included;
        - has a non-weak @executable_path, @loader_path or @rpath load: with
          nothing shipped beside the program, there is nothing for it to find.

kegs    The tarballs macos-restore-pinned-kegs.sh downloaded into DIR, one per
        MANIFEST entry, as <name>--<version>.tar.gz. Reads them in place,
        nothing is extracted. Fails if
        - a member lies outside <name>/<version>/ (absolute, '..', or another
          keg), so extracting into the Cellar can only write that keg;
        - starting from the libraries Pink2 links (ROOTS below) and following
          every load they make, a reference under /usr/local does not resolve
          to a file in the pinned set (at its pinned version), or a reference
          names /opt/homebrew or another non-OS absolute path.

Exit 0 on PASS, 1 on FAIL, 2 on a usage or input error.
"""
import os
import plistlib
import posixpath
import re
import struct
import sys
import tarfile

# ---------------------------------------------------------------------------
# Mach-O reader
# ---------------------------------------------------------------------------

MH_MAGIC, MH_CIGAM = 0xFEEDFACE, 0xCEFAEDFE
MH_MAGIC_64, MH_CIGAM_64 = 0xFEEDFACF, 0xCFFAEDFE
FAT_MAGIC, FAT_MAGIC_64 = 0xCAFEBABE, 0xCAFEBABF

LC_REQ_DYLD = 0x80000000
LC_ID_DYLIB = 0xD
DYLIB_LOADS = {
    0xC: "LC_LOAD_DYLIB",
    0x18 | LC_REQ_DYLD: "LC_LOAD_WEAK_DYLIB",
    0x1F | LC_REQ_DYLD: "LC_REEXPORT_DYLIB",
    0x20: "LC_LAZY_LOAD_DYLIB",
    0x23 | LC_REQ_DYLD: "LC_LOAD_UPWARD_DYLIB",
}
LC_RPATH = 0x1C | LC_REQ_DYLD
LC_VERSION_MIN_MACOSX = 0x24
LC_BUILD_VERSION = 0x32
PLATFORM_MACOS = 1

CPU_NAMES = {7: "i386", 0x01000007: "x86_64", 12: "arm", 0x0100000C: "arm64"}


class MachOError(Exception):
    pass


def is_macho_magic(head):
    """True if the first four bytes look like a Mach-O or fat file."""
    if len(head) < 4:
        return False
    le = struct.unpack("<I", head[:4])[0]
    be = struct.unpack(">I", head[:4])[0]
    return le in (MH_MAGIC, MH_CIGAM, MH_MAGIC_64, MH_CIGAM_64) or be in (FAT_MAGIC, FAT_MAGIC_64)


def decode_version(v):
    return (v >> 16, (v >> 8) & 0xFF, v & 0xFF)


def parse_version(text):
    parts = str(text).strip().split(".")
    if not parts or not all(p.isdigit() for p in parts):
        raise ValueError("not a version: %r" % text)
    nums = [int(p) for p in parts] + [0, 0]
    return tuple(nums[:3])


def fmt_version(v):
    if v is None:
        return "none"
    major, minor, patch = v
    return "%d.%d.%d" % v if patch else "%d.%d" % (major, minor)


def _cstr(body, offset):
    if offset >= len(body):
        raise MachOError("string offset outside its load command")
    return body[offset:].split(b"\0", 1)[0].decode("utf-8", "replace")


def _parse_thin(data):
    if len(data) < 28:
        raise MachOError("truncated header")
    magic = struct.unpack("<I", data[:4])[0]
    if magic in (MH_MAGIC, MH_MAGIC_64):
        e = "<"
    elif magic in (MH_CIGAM, MH_CIGAM_64):
        e = ">"
    else:
        raise MachOError("bad thin magic")
    is64 = magic in (MH_MAGIC_64, MH_CIGAM_64)
    cputype, _sub, filetype, ncmds, sizeofcmds, _flags = struct.unpack(e + "iiIIII", data[4:28])
    off = 32 if is64 else 28
    end = off + sizeofcmds
    if end > len(data):
        raise MachOError("load commands run past the end of the file")
    sl = {
        "arch": CPU_NAMES.get(cputype, "cpu%d" % cputype),
        "filetype": filetype,
        "id": None,
        "loads": [],   # (command name, path)
        "rpaths": [],
        "minos": None,
    }
    for _ in range(ncmds):
        if off + 8 > end:
            raise MachOError("truncated load command")
        cmd, cmdsize = struct.unpack(e + "II", data[off:off + 8])
        if cmdsize < 8 or off + cmdsize > end:
            raise MachOError("bad load command size")
        body = data[off:off + cmdsize]
        if cmd == LC_ID_DYLIB or cmd in DYLIB_LOADS or cmd == LC_RPATH:
            name = _cstr(body, struct.unpack(e + "I", body[8:12])[0])
            if cmd == LC_ID_DYLIB:
                sl["id"] = name
            elif cmd == LC_RPATH:
                sl["rpaths"].append(name)
            else:
                sl["loads"].append((DYLIB_LOADS[cmd], name))
        elif cmd == LC_BUILD_VERSION:
            platform, minos = struct.unpack(e + "II", body[8:16])
            if platform == PLATFORM_MACOS:
                sl["minos"] = decode_version(minos)
        elif cmd == LC_VERSION_MIN_MACOSX:
            sl["minos"] = decode_version(struct.unpack(e + "I", body[8:12])[0])
        off += cmdsize
    return sl


def parse_macho(data):
    """Return the list of slices of a Mach-O file, or None if it is not one."""
    if not is_macho_magic(data[:4]):
        return None
    be = struct.unpack(">I", data[:4])[0]
    if be in (FAT_MAGIC, FAT_MAGIC_64):
        nfat = struct.unpack(">I", data[4:8])[0]
        # Java class files share 0xcafebabe; their next word is a version >= 45.
        if nfat == 0 or nfat > 20:
            return None
        slices = []
        for i in range(nfat):
            if be == FAT_MAGIC:
                rec = data[8 + 20 * i:28 + 20 * i]
                _cpu, _sub, offset, size, _align = struct.unpack(">iiIII", rec)
            else:
                rec = data[8 + 32 * i:40 + 32 * i]
                _cpu, _sub, offset, size, _align, _res = struct.unpack(">iiQQII", rec)
            slices.append(_parse_thin(data[offset:offset + size]))
        return slices
    return [_parse_thin(data)]


SYSTEM_PREFIXES = ("/usr/lib/", "/System/Library/")


def is_system(path):
    return path.startswith(SYSTEM_PREFIXES)


def homebrew_note(path):
    if path.startswith("/usr/local/"):
        return " (Intel Homebrew)"
    if path.startswith("/opt/homebrew/"):
        return " (Apple Silicon Homebrew)"
    return ""


# ---------------------------------------------------------------------------
# bundle
# ---------------------------------------------------------------------------

def cmd_bundle(app, floor_text):
    try:
        floor = parse_version(floor_text)
    except ValueError as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        return 2
    app = os.path.abspath(app.rstrip("/"))
    plist_path = os.path.join(app, "Contents", "Info.plist")
    if not os.path.isfile(plist_path):
        print("ERROR: no %s" % plist_path, file=sys.stderr)
        return 2
    failures = []

    with open(plist_path, "rb") as f:
        info = plistlib.load(f)
    declared = info.get("LSMinimumSystemVersion")
    exe_name = info.get("CFBundleExecutable")
    if declared is None:
        failures.append("Info.plist declares no LSMinimumSystemVersion (expected %s)" % floor_text)
    else:
        try:
            ok = parse_version(declared) == floor
        except ValueError:
            ok = False
        if not ok:
            failures.append("Info.plist LSMinimumSystemVersion is %r, expected %s" % (declared, floor_text))

    exe_dir = os.path.join(app, "Contents", "MacOS")
    machos = {}   # relpath -> slices
    unreadable = []
    for dirpath, dirnames, filenames in os.walk(app):
        dirnames.sort()
        for fn in sorted(filenames):
            path = os.path.join(dirpath, fn)
            if os.path.islink(path) or not os.path.isfile(path):
                continue
            with open(path, "rb") as f:
                head = f.read(4)
                if not is_macho_magic(head):
                    continue
                data = head + f.read()
            rel = os.path.relpath(path, app)
            try:
                slices = parse_macho(data)
            except MachOError as exc:
                unreadable.append("%s: %s" % (rel, exc))
                continue
            if slices:
                machos[rel] = slices

    for item in unreadable:
        failures.append("unreadable Mach-O %s" % item)

    # dyld searches the rpaths of every image up the load chain; in an app
    # bundle that chain starts at the main executable.
    exe_rpaths = []
    for sl in machos.get(os.path.join("Contents", "MacOS", str(exe_name)), []):
        exe_rpaths += [(rp, exe_dir) for rp in sl["rpaths"]]

    worst, worst_files, no_minos = None, [], []
    above, leaks, unresolved = [], [], []
    for rel, slices in sorted(machos.items()):
        loader_dir = os.path.dirname(os.path.join(app, rel))
        for sl in slices:
            tag = rel if len(slices) == 1 else "%s [%s]" % (rel, sl["arch"])
            m = sl["minos"]
            if m is None:
                no_minos.append(tag)
            else:
                if worst is None or m > worst:
                    worst, worst_files = m, [tag]
                elif m == worst:
                    worst_files.append(tag)
                if m > floor:
                    above.append("%s  %s" % (fmt_version(m), tag))
            refs = [("LC_ID_DYLIB", sl["id"])] if sl["id"] else []
            refs += sl["loads"] + [("LC_RPATH", p) for p in sl["rpaths"]]
            for kind, p in refs:
                if p.startswith("/") and not is_system(p):
                    leaks.append("%s: %s %s%s" % (tag, kind, p, homebrew_note(p)))
            for kind, p in sl["loads"]:
                if kind == "LC_LOAD_WEAK_DYLIB" or not p.startswith("@"):
                    continue
                rpaths = [(rp, loader_dir) for rp in sl["rpaths"]] + exe_rpaths
                if _bundle_resolve(p, loader_dir, exe_dir, rpaths, app) is None:
                    unresolved.append("%s: %s %s" % (tag, kind, p))

    measured = sum(1 for s in machos.values() for sl in s if sl["minos"] is not None)
    print("Bundle:         %s" % app)
    print("Executable:     Contents/MacOS/%s" % exe_name)
    print("Declared floor: %s (LSMinimumSystemVersion %s)" % (floor_text, declared if declared is not None else "absent"))
    print("Measured floor: %s over %d Mach-O slices in %d files" % (fmt_version(worst), measured, len(machos)))
    if worst_files:
        print("  at the measured floor: %s%s" % (", ".join(worst_files[:5]),
              " (+%d more)" % (len(worst_files) - 5) if len(worst_files) > 5 else ""))
    if no_minos:
        print("  no minimum recorded (not measured): %s" % ", ".join(no_minos))

    if measured < 10:
        failures.append("only %d Mach-O slices carry a minimum macOS; this check cannot vouch "
                        "for the floor, so it does not pretend to" % measured)
    if above:
        failures.append("%d Mach-O slices need a macOS above %s:\n      %s"
                        % (len(above), floor_text, "\n      ".join(above)))
    if leaks:
        failures.append("%d load commands name a path outside the bundle and the OS:\n      %s"
                        % (len(leaks), "\n      ".join(leaks)))
    if unresolved:
        failures.append("%d loads resolve to no file in the bundle:\n      %s"
                        % (len(unresolved), "\n      ".join(unresolved)))

    if failures:
        for f in failures:
            print("FAIL: %s" % f)
        return 1
    print("PASS: floor %s declared and honoured by all %d slices; no load command leaves the "
          "bundle or the OS" % (floor_text, measured))
    return 0


def _bundle_resolve(ref, loader_dir, exe_dir, rpaths, app):
    """Resolve an @-relative load inside the bundle; None if it finds no file there.

    rpaths is a list of (rpath, directory that @loader_path means in it)."""
    def inside(path):
        path = os.path.normpath(path)
        real = os.path.realpath(path)
        if os.path.isfile(real) and (real + os.sep).startswith(os.path.realpath(app) + os.sep):
            return real
        return None

    if ref.startswith("@executable_path/"):
        return inside(os.path.join(exe_dir, ref[len("@executable_path/"):]))
    if ref.startswith("@loader_path/"):
        return inside(os.path.join(loader_dir, ref[len("@loader_path/"):]))
    if ref.startswith("@rpath/"):
        rest = ref[len("@rpath/"):]
        for rp, base in rpaths:
            rp = rp.replace("@loader_path", base).replace("@executable_path", exe_dir)
            hit = inside(os.path.join(rp, rest))
            if hit:
                return hit
        return None
    return None


# ---------------------------------------------------------------------------
# binary
# ---------------------------------------------------------------------------

MH_EXECUTE = 2


def cmd_binary(path, floor_text):
    try:
        floor = parse_version(floor_text)
    except ValueError as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        return 2
    if not os.path.isfile(path):
        print("ERROR: no file %s" % path, file=sys.stderr)
        return 2
    with open(path, "rb") as f:
        data = f.read()
    try:
        slices = parse_macho(data)
    except MachOError as exc:
        print("FAIL: %s: unreadable Mach-O: %s" % (path, exc))
        return 1
    if not slices:
        print("FAIL: %s is not a Mach-O file" % path)
        return 1

    failures = []
    print("Binary:         %s" % path)
    print("Declared floor: %s" % floor_text)
    for sl in slices:
        tag = sl["arch"]
        print("  [%s] minos %s" % (tag, fmt_version(sl["minos"])))
        if sl["filetype"] != MH_EXECUTE:
            failures.append("[%s] is not an executable (filetype %d)" % (tag, sl["filetype"]))
        if sl["minos"] is None:
            failures.append("[%s] records no minimum macOS" % tag)
        elif sl["minos"] > floor:
            failures.append("[%s] needs macOS %s, above %s" % (tag, fmt_version(sl["minos"]), floor_text))
        for kind, p in sl["loads"] + [("LC_RPATH", rp) for rp in sl["rpaths"]]:
            print("    %-20s %s" % (kind, p))
            if p.startswith("/") and not is_system(p):
                failures.append("[%s] %s %s%s is outside the OS" % (tag, kind, p, homebrew_note(p)))
            elif p.startswith("@") and kind not in ("LC_LOAD_WEAK_DYLIB", "LC_RPATH"):
                failures.append("[%s] %s %s: nothing ships beside this program to resolve it"
                                % (tag, kind, p))
    if failures:
        for f in failures:
            print("FAIL: %s" % f)
        return 1
    print("PASS: %s loads only the OS's libraries and honours the %s floor"
          % (os.path.basename(path), floor_text))
    return 0


# ---------------------------------------------------------------------------
# kegs
# ---------------------------------------------------------------------------

# The libraries Pink2 links, per keg, as regular expressions on the path inside
# the keg. Each must match at least one Mach-O file, so a rename upstream cannot
# make the check vacuous. Everything they load is followed from here.
ROOTS = {
    "boost": [r"lib/libboost_(chrono|filesystem|program_options|thread)(-mt)?\.dylib"],
    "openssl@3": [r"lib/libssl\.3\.dylib", r"lib/libcrypto\.3\.dylib"],
    "miniupnpc": [r"lib/libminiupnpc(\.[0-9]+)*\.dylib"],
    "qrencode": [r"lib/libqrencode(\.[0-9]+)*\.dylib"],
    "qt@5": [r"lib/[^/]+\.framework/.+", r"plugins/.+"],
}

CELLAR = "/usr/local/Cellar"
OPT = "/usr/local/opt"


def read_manifest(path):
    built_on, kegs = None, []
    with open(path) as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            fields = line.split()
            if fields[0] == "built_on":
                built_on = fields[1] if len(fields) > 1 else None
                continue
            if len(fields) != 3:
                raise ValueError("bad manifest line: %r" % raw.rstrip())
            kegs.append(tuple(fields))
    return built_on, kegs


class Keg(object):
    def __init__(self, name, version):
        self.name, self.version = name, version
        self.files = set()      # regular files (and hard links), keg-relative
        self.links = {}         # symlink -> target, keg-relative
        self.dirs = set()
        self.machos = {}        # keg-relative -> slices
        self.root = "%s/%s/%s" % (CELLAR, name, version)


def load_keg(name, version, tar_path, problems):
    keg = Keg(name, version)
    prefix = "%s/%s/" % (name, version)
    with tarfile.open(tar_path, "r:gz") as tf:
        for m in tf:
            mname = m.name[2:] if m.name.startswith("./") else m.name
            mname = mname.rstrip("/")
            parts = mname.split("/")
            if mname.startswith("/") or ".." in parts:
                problems.append("%s: unsafe member %r" % (os.path.basename(tar_path), m.name))
                continue
            if mname in (name, "%s/%s" % (name, version)):
                continue
            if not mname.startswith(prefix):
                problems.append("%s: member %r is outside %s" % (os.path.basename(tar_path), m.name, prefix))
                continue
            rel = mname[len(prefix):]
            if m.issym():
                keg.links[rel] = m.linkname
            elif m.isdir():
                keg.dirs.add(rel)
            elif m.isfile() or m.islnk():
                keg.files.add(rel)
                f = tf.extractfile(m)
                if f is None:
                    continue
                head = f.read(4)
                if not is_macho_magic(head):
                    continue
                data = head + f.read()
                try:
                    slices = parse_macho(data)
                except MachOError as exc:
                    problems.append("%s/%s: unreadable Mach-O: %s" % (name, rel, exc))
                    continue
                if slices:
                    keg.machos[rel] = slices
            else:
                problems.append("%s: unexpected member type %r" % (os.path.basename(tar_path), m.name))
    return keg


def keg_realpath(kegs, keg, rel, depth=0):
    """Follow symlinks inside the pinned set; return (keg, rel) of a file or None."""
    if depth > 40:
        return None
    parts = [p for p in rel.split("/") if p not in ("", ".")]
    for i in range(len(parts)):
        cand = "/".join(parts[:i + 1])
        if cand in keg.links:
            target = keg.links[cand]
            rest = parts[i + 1:]
            if target.startswith("/"):
                return resolve_abs(kegs, posixpath.normpath(posixpath.join(target, *rest)), depth + 1)
            joined = posixpath.normpath(posixpath.join(keg.root, posixpath.dirname(cand), target, *rest))
            return resolve_abs(kegs, joined, depth + 1)
    final = "/".join(parts)
    return (keg, final) if final in keg.files else None


def resolve_abs(kegs, path, depth=0):
    """Map an installed absolute path to a file in the pinned set, or None."""
    path = posixpath.normpath(path)
    if path.startswith(CELLAR + "/"):
        bits = path[len(CELLAR) + 1:].split("/", 2)
        if len(bits) < 3:
            return None
        keg = kegs.get(bits[0])
        if keg is None or keg.version != bits[1]:
            return None
        return keg_realpath(kegs, keg, bits[2], depth)
    if path.startswith(OPT + "/"):
        bits = path[len(OPT) + 1:].split("/", 1)
        keg = kegs.get(bits[0])
        if keg is None or len(bits) < 2:
            return None
        return keg_realpath(kegs, keg, bits[1], depth)
    if path.startswith("/usr/local/"):
        # A brew-linked file, /usr/local/<rel> -> Cellar/<name>/<version>/<rel>.
        rel = path[len("/usr/local/"):]
        for keg in kegs.values():
            hit = keg_realpath(kegs, keg, rel, depth)
            if hit:
                return hit
    return None


def keg_resolve_ref(kegs, keg, rel, sl, ref):
    """Resolve one load of keg/rel. Returns ('system'|'set'|'missing'|'outside', hit)."""
    if is_system(ref):
        return "system", None
    installed_dir = posixpath.dirname(posixpath.join(keg.root, rel))
    if ref.startswith("@loader_path/"):
        hit = resolve_abs(kegs, posixpath.join(installed_dir, ref[len("@loader_path/"):]))
        return ("set", hit) if hit else ("missing", None)
    if ref.startswith("@rpath/"):
        for rp in sl["rpaths"]:
            rp = rp.replace("@loader_path", installed_dir)
            if rp.startswith("/") and not rp.startswith("/usr/local/"):
                continue
            hit = resolve_abs(kegs, posixpath.join(rp, ref[len("@rpath/"):]))
            if hit:
                return "set", hit
        return "missing", None
    if ref.startswith("@"):
        return "missing", None
    if ref.startswith("/usr/local/"):
        hit = resolve_abs(kegs, ref)
        return ("set", hit) if hit else ("missing", None)
    return "outside", None


def cmd_kegs(manifest, directory):
    try:
        built_on, entries = read_manifest(manifest)
    except (OSError, ValueError) as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        return 2
    if not entries:
        print("ERROR: %s lists no kegs" % manifest, file=sys.stderr)
        return 2
    problems = []
    kegs = {}
    for name, version, _sha in entries:
        tar_path = os.path.join(directory, "%s--%s.tar.gz" % (name, version))
        if not os.path.isfile(tar_path):
            print("ERROR: missing %s" % tar_path, file=sys.stderr)
            return 2
        kegs[name] = load_keg(name, version, tar_path, problems)

    missing_roots = [n for n in ROOTS if n not in kegs]
    if missing_roots:
        problems.append("manifest has no keg for %s, which Pink2 links" % ", ".join(missing_roots))

    work, seen = [], set()
    for name, patterns in ROOTS.items():
        keg = kegs.get(name)
        if keg is None:
            continue
        for pat in patterns:
            rx = re.compile(pat + r"\Z")
            hits = [r for r in sorted(keg.files | set(keg.links)) if rx.match(r)]
            nodes = []
            for r in hits:
                node = keg_realpath(kegs, keg, r)
                if node and node[1] in node[0].machos:
                    nodes.append(node)
            if not nodes:
                problems.append("%s: no Mach-O matches %s" % (name, pat))
            for node in nodes:
                key = (node[0].name, node[1])
                if key not in seen:
                    seen.add(key)
                    work.append(node)

    edges = {}     # keg name -> set of keg names it loads from
    weak_missing = []
    while work:
        keg, rel = work.pop()
        for sl in keg.machos[rel]:
            for rp in sl["rpaths"]:
                if rp.startswith("/") and not rp.startswith("/usr/local/") and not is_system(rp):
                    problems.append("%s/%s: LC_RPATH %s%s" % (keg.name, rel, rp, homebrew_note(rp)))
            for kind, ref in sl["loads"]:
                status, hit = keg_resolve_ref(kegs, keg, rel, sl, ref)
                if status == "system":
                    continue
                if status == "set":
                    edges.setdefault(keg.name, set()).add(hit[0].name)
                    key = (hit[0].name, hit[1])
                    if hit[1] not in hit[0].machos:
                        problems.append("%s/%s: %s %s is not a Mach-O file" % (keg.name, rel, kind, ref))
                    elif key not in seen:
                        seen.add(key)
                        work.append(hit)
                    continue
                msg = "%s/%s: %s %s%s" % (keg.name, rel, kind, ref, homebrew_note(ref))
                if kind == "LC_LOAD_WEAK_DYLIB":
                    weak_missing.append(msg)
                elif status == "outside":
                    problems.append(msg + " is outside /usr/local and the OS")
                else:
                    problems.append(msg + " does not resolve inside the pinned set")

    print("Pinned set (built_on %s): %s" % (built_on, ", ".join("%s %s" % (n, kegs[n].version) for n in kegs)))
    for name in kegs:
        checked = sorted(r for (n, r) in seen if n == name)
        mins = [sl["minos"] for r in checked for sl in kegs[name].machos[r] if sl["minos"]]
        others = sorted(edges.get(name, set()) - {name})
        print("  %-10s %4d Mach-O checked, highest minos %-6s loads from: %s"
              % (name, len(checked), fmt_version(max(mins)) if mins else "none",
                 ", ".join(others) if others else "itself and the OS only"))
    for msg in weak_missing:
        print("  note: weak load not in the set: %s" % msg)
    if problems:
        for p in problems:
            print("FAIL: %s" % p)
        return 1
    print("PASS: every load in the %d Mach-O files Pink2 links resolves inside the pinned set or the OS"
          % len(seen))
    return 0


def main(argv):
    if len(argv) == 4 and argv[1] == "bundle":
        return cmd_bundle(argv[2], argv[3])
    if len(argv) == 4 and argv[1] == "binary":
        return cmd_binary(argv[2], argv[3])
    if len(argv) == 4 and argv[1] == "kegs":
        return cmd_kegs(argv[2], argv[3])
    print(__doc__.split("\n\n")[1], file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
