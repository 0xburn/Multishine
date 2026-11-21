#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""GameCube GCM/ISO reader and layout-preserving rebuilder (Python 3, stdlib only).

Subcommands
  info     ISO                      header / system-area summary
  ls       ISO [-l]                 list the filesystem (FST order; -l adds index/offset/size)
  extract  ISO DEST [--sys-only]    extract sys/ (boot.bin, bi2.bin, apploader.img, main.dol,
                                    fst.bin) and files/
  manifest ISO [-o FILE]            JSON {path: {size, sha1, offset}} for every sys + disc file
  build    IN OUT [options]         write a NEW image; IN is only ever opened read-only
  verify   ISO [--manifest M] [--against ORIG.iso]
                                    structural checks + re-hash every file against expectations

Build options
  --dol FILE                  replace main.dol (any size; the FST moves if the DOL grows)
  --replace DISCPATH=FILE     replace one file (any size), repeatable
  --add DISCPATH=FILE         add one new file (parent dirs are created), repeatable
  --add-dir DIR[:DISCDIR]     add every regular file below DIR (must not exist on disc yet)
  --overlay-dir DIR[:DISCDIR] like --add-dir but files that already exist are replaced
  --title TEXT                disc title in boot.bin (0x20, 64 bytes)
  --game-id ID6               game id (boot.bin 0x0, 6 ASCII bytes)
  --banner-short-title/--banner-short-maker/--banner-long-title/--banner-long-maker/
  --banner-description        rewrite opening.bnr BNR1 text fields (same size, latin-1)
  --sorted-insert             insert new FST entries in case-insensitive name order instead of
                              appending them at the end of their directory (append keeps every
                              existing FST entry number unchanged)
  --manifest-out FILE         write the expected manifest of OUT (input for `verify`)

Layout policy (derived from the retail GALE01 rev 2 image)
  * Same disc size as the input (retail: 1,459,978,240 bytes = 0x57058000, full-size GC disc).
  * boot.bin @0, bi2.bin @0x440, apploader @0x2440, DOL at its original offset (0x1E800 retail),
    FST kept at its original offset if the (possibly new) DOL and FST still fit there,
    otherwise placed right after the DOL (32-byte aligned, as retail: DOL end 0x456DE0 -> FST
    0x456E00). boot.bin 0x420/0x424/0x428/0x42C (dol off, fst off, fst size, max fst size) are
    regenerated; 0x434/0x438 (user-area start/length) are moved only if the FST grows past them.
  * Every unchanged file stays at its original offset. A changed file stays in place when the
    new data fits before the next occupied byte; otherwise it (and every new file) is placed
    first-fit into free space at 0x8000 alignment (the alignment of 839/1209 retail files).
  * Bytes not covered by any object in the output are copied from the input when they were
    also padding in the input (retail padding is pseudo-random filler), and zeroed when they
    used to hold data that moved. Hence a no-op rebuild is byte-identical to the input.
"""

import argparse
import hashlib
import json
import os
import pathlib
import struct
import sys

HEADER_SIZE = 0x440
BI2_OFFSET, BI2_SIZE = 0x440, 0x2000
APPLOADER_OFFSET = 0x2440
OFF_DOL, OFF_FST, OFF_FST_SIZE, OFF_FST_MAX = 0x420, 0x424, 0x428, 0x42C
OFF_USER_POS, OFF_USER_LEN = 0x434, 0x438
TITLE_OFFSET, TITLE_SIZE = 0x20, 0x40
GC_MAGIC_OFFSET, GC_MAGIC = 0x1C, 0xC2339F3D
FILE_ALIGN = 0x8000
FST_ALIGN = 0x20
CHUNK = 4 << 20

BNR_INFO = 0x1820
BNR_FIELDS = {  # name: (offset in opening.bnr, size)
    "short_title": (BNR_INFO + 0x00, 0x20),
    "short_maker": (BNR_INFO + 0x20, 0x20),
    "long_title": (BNR_INFO + 0x40, 0x40),
    "long_maker": (BNR_INFO + 0x80, 0x40),
    "description": (BNR_INFO + 0xC0, 0x80),
}


class GcmError(Exception):
    pass


def be32(data, off):
    return struct.unpack_from(">I", data, off)[0]


def align(value, a):
    return (value + a - 1) // a * a


def sha1_bytes(data):
    return hashlib.sha1(data).hexdigest()


# ---------------------------------------------------------------------------
# data sources
# ---------------------------------------------------------------------------
class Src:
    """A byte range: from the input image, a host file, or in-memory bytes."""

    def __init__(self, kind, size, offset=0, path=None, data=None):
        self.kind, self.size, self.offset, self.path, self.data = kind, size, offset, path, data

    @classmethod
    def iso(cls, offset, size):
        return cls("iso", size, offset=offset)

    @classmethod
    def file(cls, path):
        path = pathlib.Path(path)
        if not path.is_file():
            raise GcmError("not a regular file: {}".format(path))
        return cls("file", path.stat().st_size, path=path)

    @classmethod
    def mem(cls, data):
        return cls("mem", len(data), data=bytes(data))

    def chunks(self, iso_stream):
        if self.kind == "mem":
            yield self.data
            return
        if self.kind == "iso":
            stream, pos = iso_stream, self.offset
            stream.seek(pos)
            remaining = self.size
            while remaining:
                block = stream.read(min(CHUNK, remaining))
                if not block:
                    raise GcmError("input image ends early")
                remaining -= len(block)
                yield block
            return
        with self.path.open("rb") as stream:
            remaining = self.size
            while remaining:
                block = stream.read(min(CHUNK, remaining))
                if not block:
                    raise GcmError("{} shrank while reading".format(self.path))
                remaining -= len(block)
                yield block

    def read(self, iso_stream):
        return b"".join(self.chunks(iso_stream))

    def sha1(self, iso_stream):
        h = hashlib.sha1()
        for block in self.chunks(iso_stream):
            h.update(block)
        return h.hexdigest()


# ---------------------------------------------------------------------------
# filesystem tree
# ---------------------------------------------------------------------------
class Node:
    def __init__(self, name, is_dir, src=None, orig_offset=None, orig_index=None):
        self.name = name
        self.is_dir = is_dir
        self.children = [] if is_dir else None
        self.src = src
        self.orig_offset = orig_offset  # input disc offset (None for new files)
        self.orig_index = orig_index
        self.changed = False
        self.offset = None  # output disc offset (assigned at build)

    def child(self, name):
        for c in self.children:
            if c.name.lower() == name.lower():
                return c
        return None


def walk(node, prefix=""):
    """Pre-order (FST order) traversal yielding (path, node); root is skipped."""
    for c in node.children:
        path = prefix + c.name
        yield path, c
        if c.is_dir:
            yield from walk(c, path + "/")


def check_name(name):
    if not name or name in (".", "..") or "/" in name or "\\" in name or "\0" in name:
        raise GcmError("invalid file name {!r}".format(name))
    try:
        name.encode("ascii")
    except UnicodeEncodeError:
        raise GcmError("non-ASCII file name {!r}".format(name))


def parse_fst(fst, image_size):
    if len(fst) < 12 or not fst[0] & 1:
        raise GcmError("FST root entry is not a directory")
    count = be32(fst, 8)
    if count == 0 or count * 12 > len(fst):
        raise GcmError("invalid FST entry count {}".format(count))
    strtab = fst[count * 12:]

    def name_at(off):
        end = strtab.find(b"\0", off)
        if off >= len(strtab) or end < 0:
            raise GcmError("FST name offset out of range")
        return strtab[off:end].decode("ascii")

    root = Node("", True, orig_index=0)
    stack = [(count, root)]
    for index in range(1, count):
        while index >= stack[-1][0]:
            stack.pop()
        w0, a, b = struct.unpack_from(">III", fst, index * 12)
        name = name_at(w0 & 0xFFFFFF)
        check_name(name)
        parent = stack[-1][1]
        if w0 >> 24:
            if not (index < b <= stack[-1][0]):
                raise GcmError("bad directory range for {}".format(name))
            node = Node(name, True, orig_index=index)
            parent.children.append(node)
            stack.append((b, node))
        else:
            if a + b > image_size:
                raise GcmError("file outside image: {}".format(name))
            parent.children.append(Node(name, False, Src.iso(a, b), orig_offset=a, orig_index=index))
    return root


def build_fst(root):
    """Serialise the tree in pre-order. Returns (fst_bytes, [(file_node, entry_index)])."""
    entries, names, files = [], bytearray(), []

    def visit(node, parent_index):
        for c in node.children:
            index = len(entries)
            name_off = len(names)
            names.extend(c.name.encode("ascii") + b"\0")
            if c.is_dir:
                entries.append([0x01000000 | name_off, parent_index, None])
                visit(c, index)
                entries[index][2] = len(entries)
            else:
                entries.append([name_off, None, c.src.size])
                files.append((c, index))

    entries.append([0x01000000, 0, None])  # root (name offset 0, like retail)
    visit(root, 0)
    entries[0][2] = len(entries)
    if len(names) > 0xFFFFFF:
        raise GcmError("FST string table too large")
    return entries, names, files


def encode_fst(entries, names):
    out = bytearray()
    for w0, a, b in entries:
        out += struct.pack(">III", w0, a, b)
    out += names
    return bytes(out)


# ---------------------------------------------------------------------------
# image
# ---------------------------------------------------------------------------
def dol_extent(header):
    ends = []
    for i in range(18):
        off, size = be32(header, i * 4), be32(header, 0x90 + i * 4)
        if off and size:
            ends.append(off + size)
    if not ends:
        raise GcmError("DOL has no sections")
    return max(ends)


class Image:
    def __init__(self, path):
        self.path = pathlib.Path(path)
        self.size = self.path.stat().st_size
        self.stream = self.path.open("rb")
        self.boot = bytearray(self._read(0, HEADER_SIZE))
        if be32(self.boot, GC_MAGIC_OFFSET) != GC_MAGIC:
            raise GcmError("{}: no GameCube magic at 0x1C".format(path))
        self.bi2 = self._read(BI2_OFFSET, BI2_SIZE)
        apl = self._read(APPLOADER_OFFSET, 0x20)
        self.apploader_size = align(0x20 + be32(apl, 0x14) + be32(apl, 0x18), 0x20)
        self.dol_offset = be32(self.boot, OFF_DOL)
        self.fst_offset = be32(self.boot, OFF_FST)
        self.fst_size = be32(self.boot, OFF_FST_SIZE)
        self.fst_max = be32(self.boot, OFF_FST_MAX)
        if APPLOADER_OFFSET + self.apploader_size > self.dol_offset:
            raise GcmError("apploader overlaps DOL")
        self.dol_size = dol_extent(self._read(self.dol_offset, 0x100))
        if self.fst_offset + self.fst_size > self.size or self.dol_offset + self.dol_size > self.size:
            raise GcmError("DOL/FST outside image")
        self.fst = self._read(self.fst_offset, self.fst_size)
        self.root = parse_fst(self.fst, self.size)

    def _read(self, off, n):
        self.stream.seek(off)
        data = self.stream.read(n)
        if len(data) != n:
            raise GcmError("image ends early at 0x{:X}".format(off))
        return data

    def close(self):
        self.stream.close()

    # system files as (name, Src)
    def sys_files(self):
        return [
            ("sys/boot.bin", Src.iso(0, HEADER_SIZE)),
            ("sys/bi2.bin", Src.iso(BI2_OFFSET, BI2_SIZE)),
            ("sys/apploader.img", Src.iso(APPLOADER_OFFSET, self.apploader_size)),
            ("sys/main.dol", Src.iso(self.dol_offset, self.dol_size)),
            ("sys/fst.bin", Src.iso(self.fst_offset, self.fst_size)),
        ]

    def lookup(self, discpath, create_dirs=False):
        parts = [p for p in discpath.strip("/").split("/") if p]
        if not parts:
            raise GcmError("empty disc path")
        node = self.root
        for i, part in enumerate(parts[:-1]):
            nxt = node.child(part)
            if nxt is None:
                if not create_dirs:
                    return None, node, parts[-1]
                check_name(part)
                nxt = Node(part, True)
                node.children.append(nxt)
            if not nxt.is_dir:
                raise GcmError("{} is a file, not a directory".format("/".join(parts[:i + 1])))
            node = nxt
        return node.child(parts[-1]), node, parts[-1]

    def manifest(self):
        """{path: {size, sha1, offset}} read straight from this image."""
        out = {}
        for name, src in self.sys_files():
            out[name] = {"size": src.size, "sha1": src.sha1(self.stream), "offset": src.offset}
        for path, node in walk(self.root):
            if not node.is_dir:
                out["files/" + path] = {"size": node.src.size, "sha1": node.src.sha1(self.stream),
                                        "offset": node.src.offset}
        return out


# ---------------------------------------------------------------------------
# build
# ---------------------------------------------------------------------------
class Builder:
    def __init__(self, image, sorted_insert=False):
        self.img = image
        self.boot = bytearray(image.boot)
        self.dol = Src.iso(image.dol_offset, image.dol_size)
        self.sorted_insert = sorted_insert
        self.log = []

    def set_dol(self, path):
        self.dol = Src.file(path)
        self.log.append("dol: {} ({} bytes)".format(path, self.dol.size))

    def replace(self, discpath, src):
        node, _, _ = self.img.lookup(discpath)
        if node is None or node.is_dir:
            raise GcmError("--replace: no such file on disc: {}".format(discpath))
        node.src, node.changed = src, True
        self.log.append("replace: {} ({} bytes)".format(discpath, src.size))

    def add(self, discpath, src):
        node, parent, name = self.img.lookup(discpath, create_dirs=True)
        if node is not None:
            raise GcmError("--add: already on disc: {} (use --replace/--overlay-dir)".format(discpath))
        check_name(name)
        new = Node(name, False, src)
        new.changed = True
        if self.sorted_insert:
            pos = len(parent.children)
            for i, c in enumerate(parent.children):
                if c.name.lower() > name.lower():
                    pos = i
                    break
            parent.children.insert(pos, new)
        else:
            parent.children.append(new)
        self.log.append("add: {} ({} bytes)".format(discpath, src.size))

    def add_dir(self, hostdir, discdir, allow_replace):
        hostdir = pathlib.Path(hostdir)
        if not hostdir.is_dir():
            raise GcmError("not a directory: {}".format(hostdir))
        found = False
        for p in sorted(hostdir.rglob("*")):
            if p.is_file() and not p.name.startswith("."):
                found = True
                rel = p.relative_to(hostdir).as_posix()
                discpath = (discdir.strip("/") + "/" + rel) if discdir.strip("/") else rel
                node, _, _ = self.img.lookup(discpath)
                if node is not None and allow_replace:
                    self.replace(discpath, Src.file(p))
                else:
                    self.add(discpath, Src.file(p))
        if not found:
            raise GcmError("no files in {}".format(hostdir))

    def set_title(self, title):
        raw = title.encode("ascii")
        if len(raw) >= TITLE_SIZE:
            raise GcmError("title longer than {} bytes".format(TITLE_SIZE - 1))
        self.boot[TITLE_OFFSET:TITLE_OFFSET + TITLE_SIZE] = raw.ljust(TITLE_SIZE, b"\0")
        self.log.append("title: {!r}".format(title))

    def set_game_id(self, gid):
        raw = gid.encode("ascii")
        if len(raw) != 6:
            raise GcmError("game id must be 6 ASCII bytes")
        self.boot[0:6] = raw
        self.log.append("game id: {}".format(gid))

    def set_banner(self, fields):
        node, _, _ = self.img.lookup("opening.bnr")
        if node is None:
            raise GcmError("no opening.bnr on disc")
        bnr = bytearray(node.src.read(self.img.stream))
        if bnr[:4] != b"BNR1" or len(bnr) < BNR_INFO + 0x140:
            raise GcmError("opening.bnr is not a single-language BNR1 banner")
        for key, text in fields.items():
            off, size = BNR_FIELDS[key]
            raw = text.encode("latin-1")
            if len(raw) >= size:
                raise GcmError("banner {} longer than {} bytes".format(key, size - 1))
            bnr[off:off + size] = raw.ljust(size, b"\0")
            self.log.append("banner {}: {!r}".format(key, text))
        node.src, node.changed = Src.mem(bnr), True

    # -- layout ------------------------------------------------------------
    def layout(self):
        img = self.img
        disc = img.size
        dol_off = img.dol_offset
        dol_end = dol_off + self.dol.size
        entries, names, files = build_fst(img.root)
        fst_size = len(entries) * 12 + len(names)
        # FST: keep in place if possible, else right after the DOL
        if dol_end <= img.fst_offset and fst_size <= img.fst_size:
            fst_off = img.fst_offset
        elif dol_end <= img.fst_offset and img.fst_offset + fst_size <= self._first_file_offset():
            fst_off = img.fst_offset
        else:
            fst_off = align(dol_end, FST_ALIGN)
        fst_end = fst_off + fst_size
        user_pos = be32(img.boot, OFF_USER_POS)
        user_len = be32(img.boot, OFF_USER_LEN)
        sys_end = fst_end
        if user_pos and fst_end > user_pos:
            user_pos = align(fst_end, FILE_ALIGN)
            user_len = disc - user_pos
            sys_end = user_pos
        # occupancy: [start, end) sorted list
        used = [(0, sys_end)]

        def overlaps(a, b):
            for s, e in used:
                if a < e and s < b:
                    return True
            return False

        def reserve(a, b):
            if b > a:
                used.append((a, b))
                used.sort()

        movable = []
        # pass 1: unchanged files keep their offset unless the system area grew over them
        for node, _ in files:
            size = node.src.size
            if not node.changed and node.orig_offset is not None:
                if size == 0 or not overlaps(node.orig_offset, node.orig_offset + size):
                    node.offset = node.orig_offset if (size or node.orig_offset >= sys_end) else sys_end
                    reserve(node.offset, node.offset + size)
                    continue
            movable.append(node)
        # pass 2: changed files in place if they fit, else first-fit
        relocated = []
        for node in movable:
            size = node.src.size
            if node.orig_offset is not None and node.orig_offset >= sys_end and \
                    not overlaps(node.orig_offset, node.orig_offset + size):
                node.offset = node.orig_offset
            elif size == 0:
                node.offset = sys_end
            else:
                node.offset = self._first_fit(used, size, sys_end, disc)
                relocated.append(node)
            reserve(node.offset, node.offset + size)
        # emit FST with final offsets
        for node, index in files:
            entries[index][1] = node.offset
        fst = encode_fst(entries, names)
        boot = self.boot
        struct.pack_into(">IIII", boot, OFF_DOL, dol_off, fst_off, fst_size, max(fst_size, img.fst_max))
        struct.pack_into(">II", boot, OFF_USER_POS, user_pos, user_len)
        self.fst_off, self.fst, self.files, self.relocated = fst_off, fst, files, relocated
        self.log.append("fst: 0x{:X} size 0x{:X} (input 0x{:X} size 0x{:X})".format(
            fst_off, fst_size, img.fst_offset, img.fst_size))
        for node in relocated:
            self.log.append("placed: {} @0x{:X} ({} bytes{})".format(
                node.name, node.offset, node.src.size,
                ", moved from 0x{:X}".format(node.orig_offset) if node.orig_offset is not None else ", new"))

    def _first_file_offset(self):
        offs = [n.orig_offset for _, n in walk(self.img.root)
                if not n.is_dir and n.orig_offset is not None and n.src.size]
        return min(offs) if offs else self.img.size

    @staticmethod
    def _first_fit(used, size, floor, disc):
        for a in (FILE_ALIGN, 0x20):
            pos = align(floor, a)
            for s, e in used:
                if pos + size <= s:
                    break
                if e > pos:
                    pos = align(e, a)
            if pos + size <= disc:
                return pos
        raise GcmError("no free space for {} bytes".format(size))

    # -- write -------------------------------------------------------------
    def objects(self):
        img = self.img
        objs = [
            (0, Src.mem(self.boot), "sys/boot.bin"),
            (BI2_OFFSET, Src.iso(BI2_OFFSET, BI2_SIZE), "sys/bi2.bin"),
            (APPLOADER_OFFSET, Src.iso(APPLOADER_OFFSET, img.apploader_size), "sys/apploader.img"),
            (img.dol_offset, self.dol, "sys/main.dol"),
            (self.fst_off, Src.mem(self.fst), "sys/fst.bin"),
        ]
        paths = {id(n): p for p, n in walk(img.root)}
        for node, _ in self.files:
            if node.src.size:
                objs.append((node.offset, node.src, "files/" + paths[id(node)]))
        objs.sort(key=lambda o: o[0])
        for (a, sa, na), (b, _, nb) in zip(objs, objs[1:]):
            if a + sa.size > b:
                raise GcmError("layout overlap: {} and {}".format(na, nb))
        if objs[-1][0] + objs[-1][1].size > img.size:
            raise GcmError("layout exceeds disc size")
        return objs

    def input_padding(self):
        """Sorted [start, end) ranges that hold no object in the input image."""
        img = self.img
        occ = [(0, HEADER_SIZE), (BI2_OFFSET, BI2_OFFSET + BI2_SIZE),
               (APPLOADER_OFFSET, APPLOADER_OFFSET + img.apploader_size),
               (img.dol_offset, img.dol_offset + img.dol_size),
               (img.fst_offset, img.fst_offset + img.fst_size)]
        occ += [(n.orig_offset, n.orig_offset + n.orig_size) for n in self._orig_files() if n.orig_size]
        occ.sort()
        pad, pos = [], 0
        for s, e in occ:
            if s > pos:
                pad.append((pos, s))
            pos = max(pos, e)
        if pos < img.size:
            pad.append((pos, img.size))
        return pad

    def _orig_files(self):
        return self._orig

    def write(self, out_path):
        img = self.img
        # remember the input layout before sources were swapped
        objs = self.objects()
        pad = self.input_padding()
        tmp = pathlib.Path(str(out_path) + ".part")
        manifest = {}
        with tmp.open("wb") as out:
            pos = 0

            def fill(a, b):
                # copy input padding where the input also had padding, zero elsewhere
                cur = a
                for ps, pe in pad:
                    if pe <= cur or ps >= b:
                        continue
                    if ps > cur:
                        zero(out, ps - cur)
                    s, e = max(ps, cur), min(pe, b)
                    for block in Src.iso(s, e - s).chunks(img.stream):
                        out.write(block)
                    cur = e
                if cur < b:
                    zero(out, b - cur)

            for off, src, name in objs:
                fill(pos, off)
                h = hashlib.sha1()
                for block in src.chunks(img.stream):
                    h.update(block)
                    out.write(block)
                manifest[name] = {"size": src.size, "sha1": h.hexdigest(), "offset": off}
                pos = off + src.size
            fill(pos, img.size)
            if out.tell() != img.size:
                raise GcmError("internal: wrote {} bytes, expected {}".format(out.tell(), img.size))
        for node, _ in self.files:  # zero-size files
            if not node.src.size:
                manifest["files/" + self._path(node)] = {"size": 0, "sha1": sha1_bytes(b""),
                                                         "offset": node.offset}
        os.replace(tmp, out_path)
        return dict(sorted(manifest.items()))

    def _path(self, node):
        for p, n in walk(self.img.root):
            if n is node:
                return p
        raise KeyError(node.name)


def zero(out, n):
    block = bytes(min(CHUNK, n))
    while n:
        k = min(len(block), n)
        out.write(block[:k])
        n -= k


def make_builder(image, sorted_insert):
    b = Builder(image, sorted_insert)
    orig = []
    for _, n in walk(image.root):
        if not n.is_dir:
            n.orig_size = n.src.size
            orig.append(n)
    b._orig = orig
    return b


# ---------------------------------------------------------------------------
# verify
# ---------------------------------------------------------------------------
def structural_check(img):
    """Return a list of problems (empty == OK)."""
    problems = []
    b = img.boot
    if be32(b, OFF_FST_SIZE) > be32(b, OFF_FST_MAX):
        problems.append("fst size > max fst size")
    if img.fst_offset % 4 or img.dol_offset % 4:
        problems.append("DOL/FST offset not 4-byte aligned")
    objs = [(0, HEADER_SIZE, "boot.bin"), (BI2_OFFSET, BI2_SIZE, "bi2.bin"),
            (APPLOADER_OFFSET, img.apploader_size, "apploader"),
            (img.dol_offset, img.dol_size, "main.dol"), (img.fst_offset, img.fst_size, "fst.bin")]
    count = be32(img.fst, 8)
    if count * 12 > img.fst_size:
        problems.append("FST entry table exceeds FST size")
    for path, n in walk(img.root):
        if not n.is_dir:
            if n.src.offset % 4:
                problems.append("unaligned file offset: " + path)
            if n.src.size:
                objs.append((n.src.offset, n.src.size, path))
    objs.sort()
    for (a, sa, na), (bb, _, nb) in zip(objs, objs[1:]):
        if a + sa > bb:
            problems.append("overlap: {} [0x{:X}+0x{:X}] and {} @0x{:X}".format(na, a, sa, nb, bb))
    if objs[-1][0] + objs[-1][1] > img.size:
        problems.append("object beyond end of image")
    # directory ranges / parent links
    for i in range(1, count):
        w0, a, nxt = struct.unpack_from(">III", img.fst, i * 12)
        if w0 >> 24:
            if not (i < nxt <= count) or a >= i:
                problems.append("bad dir entry {}".format(i))
            elif a and not (img.fst[a * 12] and struct.unpack_from(">I", img.fst, a * 12 + 8)[0] >= nxt):
                problems.append("dir entry {} parent {} does not enclose it".format(i, a))
    return problems


def compare_manifests(actual, expected, label):
    bad = []
    for k in sorted(set(actual) | set(expected)):
        a, e = actual.get(k), expected.get(k)
        if a is None:
            bad.append("missing in image: " + k)
        elif e is None:
            bad.append("unexpected in image: " + k)
        elif a["sha1"] != e["sha1"] or a["size"] != e["size"]:
            bad.append("{}: size/sha1 {} {} != {} {} ({})".format(
                k, a["size"], a["sha1"], e["size"], e["sha1"], label))
    return bad


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def split_mapping(text, opt):
    if "=" not in text:
        raise GcmError("{} expects DISCPATH=FILE, got {!r}".format(opt, text))
    a, b = text.split("=", 1)
    return a, b


def split_dir(text):
    # DIR[:DISCDIR]; tolerate ':' in host paths by splitting on the last ':' only if
    # the right-hand side has no '/' prefix ambiguity
    if ":" in text and not os.path.isdir(text):
        a, b = text.rsplit(":", 1)
        return a, b
    return text, ""


def same_file(a, b):
    try:
        return os.path.samefile(a, b)
    except FileNotFoundError:
        return False


def cmd_info(args):
    img = Image(args.iso)
    b = img.boot
    print("image      {} ({} bytes)".format(img.path, img.size))
    print("game id    {} rev {}  disc {}".format(b[0:6].decode("ascii", "replace"), b[7], b[6]))
    print("title      {}".format(b[TITLE_OFFSET:TITLE_OFFSET + TITLE_SIZE].split(b"\0")[0].decode("latin-1")))
    print("apploader  0x{:X} size 0x{:X} ({})".format(APPLOADER_OFFSET, img.apploader_size,
                                                    img._read(APPLOADER_OFFSET, 10).decode("latin-1")))
    print("main.dol   0x{:X} size 0x{:X}".format(img.dol_offset, img.dol_size))
    print("fst        0x{:X} size 0x{:X} max 0x{:X} entries {}".format(
        img.fst_offset, img.fst_size, img.fst_max, be32(img.fst, 8)))
    print("user area  0x{:X} len 0x{:X}  (0x430 = 0x{:08X})".format(
        be32(b, OFF_USER_POS), be32(b, OFF_USER_LEN), be32(b, 0x430)))
    nfiles = sum(1 for _, n in walk(img.root) if not n.is_dir)
    print("files      {}".format(nfiles))


def cmd_ls(args):
    img = Image(args.iso)
    for path, n in walk(img.root):
        if args.long:
            if n.is_dir:
                print("{:5d} {:>10} {:>10} {}/".format(n.orig_index, "", "", path))
            else:
                print("{:5d} 0x{:08X} {:10d} {}".format(n.orig_index, n.src.offset, n.src.size, path))
        else:
            print(path + ("/" if n.is_dir else ""))


def cmd_extract(args):
    img = Image(args.iso)
    dest = pathlib.Path(args.dest)
    if dest.exists() and any(dest.iterdir()):
        raise GcmError("destination not empty: {}".format(dest))
    targets = list(img.sys_files())
    if not args.sys_only:
        targets += [("files/" + p, n.src) for p, n in walk(img.root) if not n.is_dir]
        for p, n in walk(img.root):
            if n.is_dir:
                (dest / "files" / p).mkdir(parents=True, exist_ok=True)
    for name, src in targets:
        target = dest / name
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("wb") as out:
            for block in src.chunks(img.stream):
                out.write(block)
    print("extracted {} files to {}".format(len(targets), dest))


def cmd_manifest(args):
    img = Image(args.iso)
    m = img.manifest()
    text = json.dumps(m, indent=1, sort_keys=True) + "\n"
    if args.output:
        pathlib.Path(args.output).write_text(text)
        print("{} entries -> {}".format(len(m), args.output))
    else:
        sys.stdout.write(text)


def cmd_build(args):
    if same_file(args.input, args.output) or os.path.abspath(args.input) == os.path.abspath(args.output):
        raise GcmError("output must differ from input")
    if os.path.exists(args.output) and not args.force:
        raise GcmError("output exists (use --force): {}".format(args.output))
    img = Image(args.input)
    b = make_builder(img, args.sorted_insert)
    sources = []
    if args.dol:
        b.set_dol(args.dol)
        sources.append(args.dol)
    for item in args.replace or []:
        d, f = split_mapping(item, "--replace")
        b.replace(d, Src.file(f))
        sources.append(f)
    for item in args.add or []:
        d, f = split_mapping(item, "--add")
        b.add(d, Src.file(f))
        sources.append(f)
    for item in args.overlay_dir or []:
        h, d = split_dir(item)
        b.add_dir(h, d, allow_replace=True)
    for item in args.add_dir or []:
        h, d = split_dir(item)
        b.add_dir(h, d, allow_replace=False)
    if args.title is not None:
        b.set_title(args.title)
    if args.game_id is not None:
        b.set_game_id(args.game_id)
    banner = {k: getattr(args, "banner_" + k) for k in BNR_FIELDS if getattr(args, "banner_" + k) is not None}
    if banner:
        b.set_banner(banner)
    for s in sources:
        if same_file(s, args.output):
            raise GcmError("output would overwrite a source file: {}".format(s))
    b.layout()
    manifest = b.write(args.output)
    for line in b.log:
        print(line)
    print("wrote {} ({} bytes, {} entries in manifest)".format(args.output, img.size, len(manifest)))
    if args.manifest_out:
        pathlib.Path(args.manifest_out).write_text(json.dumps(manifest, indent=1, sort_keys=True) + "\n")
        print("manifest -> {}".format(args.manifest_out))


def cmd_verify(args):
    img = Image(args.iso)
    problems = structural_check(img)
    for p in problems:
        print("STRUCTURE: " + p)
    actual = img.manifest()
    bad = []
    if args.manifest:
        expected = json.loads(pathlib.Path(args.manifest).read_text())
        bad += compare_manifests(actual, expected, "manifest")
    if args.against:
        ref = Image(args.against).manifest()
        skip = set(args.ignore or [])
        if args.files_only:
            skip |= {k for k in set(ref) | set(actual) if k.startswith("sys/")}
        bad += compare_manifests({k: v for k, v in actual.items() if k not in skip},
                                 {k: v for k, v in ref.items() if k not in skip}, "against")
    for line in bad:
        print("MISMATCH: " + line)
    nfiles = sum(1 for k in actual if k.startswith("files/"))
    print("verified {}: {} disc files + {} sys files hashed, {} structural problems, {} mismatches".format(
        args.iso, nfiles, len(actual) - nfiles, len(problems), len(bad)))
    return 1 if problems or bad else 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("info"); s.add_argument("iso"); s.set_defaults(fn=cmd_info)
    s = sub.add_parser("ls"); s.add_argument("iso"); s.add_argument("-l", "--long", action="store_true")
    s.set_defaults(fn=cmd_ls)
    s = sub.add_parser("extract"); s.add_argument("iso"); s.add_argument("dest")
    s.add_argument("--sys-only", action="store_true"); s.set_defaults(fn=cmd_extract)
    s = sub.add_parser("manifest"); s.add_argument("iso"); s.add_argument("-o", "--output")
    s.set_defaults(fn=cmd_manifest)
    s = sub.add_parser("build", aliases=["rebuild"]); s.add_argument("input"); s.add_argument("output")
    s.add_argument("--dol"); s.add_argument("--replace", action="append"); s.add_argument("--add", action="append")
    s.add_argument("--add-dir", action="append"); s.add_argument("--overlay-dir", action="append")
    s.add_argument("--title"); s.add_argument("--game-id")
    for k in BNR_FIELDS:
        s.add_argument("--banner-" + k.replace("_", "-"), dest="banner_" + k)
    s.add_argument("--sorted-insert", action="store_true")
    s.add_argument("--manifest-out"); s.add_argument("--force", action="store_true")
    s.set_defaults(fn=cmd_build)
    s = sub.add_parser("verify"); s.add_argument("iso"); s.add_argument("--manifest")
    s.add_argument("--against", help="reference image whose files must hash identically")
    s.add_argument("--ignore", action="append", help="manifest key to skip with --against")
    s.add_argument("--files-only", action="store_true", help="with --against: compare files/ only")
    s.set_defaults(fn=cmd_verify)
    args = p.parse_args(argv)
    try:
        return args.fn(args) or 0
    except GcmError as e:
        print("error: {}".format(e), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
