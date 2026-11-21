# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Raw GameCube memory card image model for the VMC builder (stdlib only, our own code).

A raw card image is what Dolphin calls a .raw card and what Swiss serves as a virtual memory card.  Layout (all big-endian):
  block 0        header: serial[12] fmt time u64 | sram bias u32 | sram lang u32 | unknown u32 | device id u16 |
                 size in megabits u16 (+0x22) | encoding u16 (+0x24, 0 ANSI, 1 SJIS) | ... | checksum pair at +0x1FC
  blocks 1, 2    directory, two copies; 127 entries of 0x40 bytes; update counter u16 at +0x1FFA; checksum pair at +0x1FFC
  blocks 3, 4    block allocation map (BAT), two copies; checksum pair at +0, update counter u16 at +4, free blocks u16 at +6,
                 last allocated u16 at +8, then u16 chain entry per data block (block n -> entry n - 5; 0 = free, 0xFFFF = end)
  blocks 5..     data; block size 0x2000
Checksum = additive 16-bit pair (sum of words, sum of ~words; a result of 0xFFFF is stored as 0).
The format follows what the console SDK and Dolphin accept and what offline/provision already write;
Swiss's source was read only to confirm names and regions, nothing was copied.
Unverified: that Swiss and a real console accept an image made here.
"""
import struct

BLOCK = 0x2000
SYS_BLOCKS = 5
DIR_ENTRIES = 127
ENTRY = 0x40
MIN_BYTES = 0x80000          # 4 Mbit, 64 blocks (59 usable)
MAX_BYTES = 0x1000000        # 128 Mbit (2043 usable)
SIZES_MIB = {"0.5": 0x80000, "1": 0x100000, "2": 0x200000, "4": 0x400000, "8": 0x800000, "16": 0x1000000}
GAME_CODE = b"GALE"
MAKER = b"01"
FREE_ENTRY = b"\xFF\xFF\xFF\xFF"
END = 0xFFFF
EPOCH_2000 = 946684800


class CardError(Exception):
    """User-facing error about a card image."""


def checksum(data):
    c1 = c2 = 0
    for (w,) in struct.iter_unpack(">H", bytes(data)):
        c1 = (c1 + w) & 0xFFFF
        c2 = (c2 + (w ^ 0xFFFF)) & 0xFFFF
    return (0 if c1 == 0xFFFF else c1), (0 if c2 == 0xFFFF else c2)


def _hdr_ok(b):
    return struct.unpack_from(">HH", b, 0x1FC) == checksum(b[:0x1FC])


def _dir_ok(b):
    return struct.unpack_from(">HH", b, 0x1FFC) == checksum(b[:0x1FFC])


def _bat_ok(b):
    return struct.unpack_from(">HH", b, 0) == checksum(b[4:])


def _dir_counter(b):
    return struct.unpack_from(">H", b, 0x1FFA)[0]


def _bat_counter(b):
    return struct.unpack_from(">H", b, 4)[0]


def check_size(n):
    if n < MIN_BYTES or n > MAX_BYTES or n & (n - 1):
        raise CardError("not a raw memory card image: size %d is not a power of two between 512 KiB and 16 MiB" % n)


def format_card(size_bytes=MAX_BYTES, region="USA", fmt_time=0, serial=bytes(12)):
    """A freshly formatted, empty card.  Encoding byte: SJIS (1) for JPN, ANSI (0) otherwise (what Dolphin writes per region)."""
    check_size(size_bytes)
    total = size_bytes // BLOCK
    h = bytearray(b"\xFF" * BLOCK)
    h[0:12] = serial
    struct.pack_into(">Q", h, 0x0C, fmt_time)
    struct.pack_into(">IIIHHH", h, 0x14, 0, 0, 0, 0, size_bytes * 8 // (1 << 20), 1 if region == "JPN" else 0)
    struct.pack_into(">HH", h, 0x1FC, *checksum(h[:0x1FC]))
    d = bytearray(b"\xFF" * BLOCK)
    struct.pack_into(">H", d, 0x1FFA, 0)
    struct.pack_into(">HH", d, 0x1FFC, *checksum(d[:0x1FFC]))
    bat = bytearray(BLOCK)
    struct.pack_into(">HHH", bat, 4, 0, total - SYS_BLOCKS, SYS_BLOCKS - 1)
    struct.pack_into(">HH", bat, 0, *checksum(bat[4:]))
    img = bytearray(h + d + d + bat + bat)
    img += b"\x00" * (size_bytes - len(img))
    return Card(img)


class Card:
    """A parsed raw card image held in memory (self.img is the mutable bytearray)."""

    def __init__(self, img):
        self.img = bytearray(img)
        self.notes = []          # non-fatal findings from parsing
        n = len(self.img)
        check_size(n)
        self.total = n // BLOCK
        hdr = bytes(self.img[:BLOCK])
        if not _hdr_ok(hdr):
            raise CardError("card header checksum mismatch")
        self.size_mbit = struct.unpack_from(">H", hdr, 0x22)[0]
        if self.size_mbit * (1 << 20) // 8 != n:
            raise CardError("card header says %d Mbit but the image is %d bytes" % (self.size_mbit, n))
        self.encoding = struct.unpack_from(">H", hdr, 0x24)[0]
        self.serial = hdr[:12]
        self.dir_off = self._pick(1, _dir_ok, _dir_counter, "directory")
        self.bat_off = self._pick(3, _bat_ok, _bat_counter, "block allocation map")

    def _pick(self, first, ok, counter, what):
        good = []
        for i in (0, 1):
            off = (first + i) * BLOCK
            if ok(bytes(self.img[off:off + BLOCK])):
                good.append((counter(self.img[off:off + BLOCK]), i, off))
            else:
                self.notes.append("%s copy %d has a bad checksum" % (what, i + 1))
        if not good:
            raise CardError("%s checksum mismatch in both copies" % what)
        if len(good) == 2 and good[0][0] != good[1][0]:
            self.notes.append("%s copies differ in update counter (%d, %d): the newer one is used" % (what, good[0][0], good[1][0]))
        return max(good, key=lambda g: (g[0], g[1]))[2]

    # -- tables
    def dirblk(self):
        return bytearray(self.img[self.dir_off:self.dir_off + BLOCK])

    def batblk(self):
        return bytearray(self.img[self.bat_off:self.bat_off + BLOCK])

    def entries(self):
        d = self.dirblk()
        out = []
        for i in range(DIR_ENTRIES):
            e = bytes(d[i * ENTRY:(i + 1) * ENTRY])
            if e[:4] == FREE_ENTRY:
                continue
            out.append({"slot": i, "gamecode": e[0:4], "maker": e[4:6],
                        "name": e[8:40].split(b"\0")[0].decode("ascii", "replace"),
                        "first": struct.unpack_from(">H", e, 0x36)[0], "blocks": struct.unpack_from(">H", e, 0x38)[0],
                        "perm": e[0x34], "mtime": struct.unpack_from(">I", e, 0x28)[0] + EPOCH_2000, "raw": e})
        return out

    def free_blocks(self):
        return struct.unpack_from(">H", self.batblk(), 6)[0]

    def count_free(self):
        bat = self.batblk()
        return sum(1 for b in range(SYS_BLOCKS, self.total) if struct.unpack_from(">H", bat, 10 + 2 * (b - SYS_BLOCKS))[0] == 0)

    def chain(self, ent):
        bat = self.batblk()
        out, blk = [], ent["first"]
        while blk != END:
            if not SYS_BLOCKS <= blk < self.total or len(out) > ent["blocks"]:
                raise CardError("block chain of %r is out of range or too long" % ent["name"])
            out.append(blk)
            blk = struct.unpack_from(">H", bat, 10 + 2 * (blk - SYS_BLOCKS))[0]
        if len(out) != ent["blocks"]:
            raise CardError("block chain of %r has %d blocks, directory says %d" % (ent["name"], len(out), ent["blocks"]))
        return out

    def find(self, name, gamecode=GAME_CODE, maker=MAKER):
        for e in self.entries():
            if e["name"] == name and e["gamecode"] == gamecode and e["maker"] == maker:
                return e
        return None

    def read(self, ent):
        return b"".join(bytes(self.img[b * BLOCK:(b + 1) * BLOCK]) for b in self.chain(ent))

    def consistency(self):
        """List of problems found by a full cross-check of directory, chains and map (empty = consistent)."""
        bad = []
        owner = {}
        for e in self.entries():
            try:
                for b in self.chain(e):
                    if b in owner:
                        bad.append("block %d used by %r and %r" % (b, owner[b], e["name"]))
                    owner[b] = e["name"]
            except CardError as x:
                bad.append(str(x))
        bat = self.batblk()
        used = sum(1 for b in range(SYS_BLOCKS, self.total) if struct.unpack_from(">H", bat, 10 + 2 * (b - SYS_BLOCKS))[0] != 0)
        if used != len(owner) and not bad:
            bad.append("block map marks %d blocks used, directory chains cover %d" % (used, len(owner)))
        if self.count_free() != self.free_blocks():
            bad.append("free block count %d does not match the map (%d free)" % (self.free_blocks(), self.count_free()))
        return bad

    # -- editing (both table copies are rewritten, update counters + 1, checksums recomputed)
    def _store(self, d, bat):
        struct.pack_into(">H", d, 0x1FFA, (_dir_counter(d) + 1) & 0xFFFF)
        struct.pack_into(">HH", d, 0x1FFC, *checksum(d[:0x1FFC]))
        struct.pack_into(">H", bat, 4, (_bat_counter(bat) + 1) & 0xFFFF)
        struct.pack_into(">HH", bat, 0, *checksum(bat[4:]))
        for blk in (1, 2):
            self.img[blk * BLOCK:(blk + 1) * BLOCK] = d
        for blk in (3, 4):
            self.img[blk * BLOCK:(blk + 1) * BLOCK] = bat
        self.dir_off, self.bat_off = 2 * BLOCK, 4 * BLOCK

    def add(self, entry, data):
        """Add a file: entry = 0x40 byte directory entry template, data = whole blocks.  First-fit; never touches other files."""
        entry = bytes(entry)
        if len(entry) != ENTRY or not data or len(data) % BLOCK:
            raise CardError("file data must be a whole number of 0x2000-byte blocks")
        n = len(data) // BLOCK
        d, bat = self.dirblk(), self.batblk()
        slot = None
        for i in range(DIR_ENTRIES):
            e = bytes(d[i * ENTRY:(i + 1) * ENTRY])
            if e[:4] == FREE_ENTRY:
                slot = i if slot is None else slot
            elif e[0:6] == entry[0:6] and e[8:40] == entry[8:40]:
                raise CardError("a file with this name and game code is already on the card")
        if slot is None:
            raise CardError("the card directory is full (127 files)")
        free = [b for b in range(SYS_BLOCKS, self.total) if struct.unpack_from(">H", bat, 10 + 2 * (b - SYS_BLOCKS))[0] == 0]
        if len(free) < n:
            raise CardError("not enough free blocks on the card (%d needed, %d free)" % (n, len(free)))
        chain = free[:n]
        for i, b in enumerate(chain):
            struct.pack_into(">H", bat, 10 + 2 * (b - SYS_BLOCKS), chain[i + 1] if i + 1 < n else END)
            self.img[b * BLOCK:(b + 1) * BLOCK] = data[i * BLOCK:(i + 1) * BLOCK]
        struct.pack_into(">H", bat, 6, struct.unpack_from(">H", bat, 6)[0] - n)
        struct.pack_into(">H", bat, 8, chain[-1])
        e = bytearray(entry)
        struct.pack_into(">HH", e, 0x36, chain[0], n)
        d[slot * ENTRY:(slot + 1) * ENTRY] = e
        self._store(d, bat)
        return chain

    def remove(self, ent):
        """Delete a file: directory entry freed, map entries cleared (the old data blocks are left as they are)."""
        d, bat = self.dirblk(), self.batblk()
        chain = self.chain(ent)
        for b in chain:
            struct.pack_into(">H", bat, 10 + 2 * (b - SYS_BLOCKS), 0)
        struct.pack_into(">H", bat, 6, struct.unpack_from(">H", bat, 6)[0] + len(chain))
        d[ent["slot"] * ENTRY:(ent["slot"] + 1) * ENTRY] = b"\xFF" * ENTRY
        self._store(d, bat)
