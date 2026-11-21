# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the Multishine save-file icons (tools/brand/make_save_art.py) and for the builders that write them into the card files.
Host only, stdlib.  Run: python3 -m unittest -v tools/tests/test_save_art.py"""
import hashlib
import os
import struct
import sys
import unittest

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(TOOLS)
sys.path.insert(0, os.path.join(TOOLS, "brand"))
sys.path.insert(0, os.path.join(TOOLS, "vmc_builder"))
sys.path.insert(0, os.path.join(TOOLS, "release"))
sys.path.insert(0, os.path.join(REPO, "offline", "provision"))
sys.path.insert(0, os.path.join(REPO, "offline", "cardloader"))
sys.path.insert(0, os.path.join(REPO, "offline", "options"))
import make_save_art as A  # noqa: E402
import multishine_provision as P  # noqa: E402
import vmc_files as F  # noqa: E402
import mscf as M  # noqa: E402
import optfile  # noqa: E402
import save_art as SA  # noqa: E402

GOLDEN = {
    "code": "5c7b92b7fc74f34c3d48274abc2b994fbdac14b26b155d2e65782488373306a5",
    "ovl": "bd388bae38b7029a2da2b4641a56689c3908dee769e71aa54df5abf5c3f2479c",
    "identity": "f679257140741acce06ee9181d95a9f14a363265eaaf53040749ce2ad87179c2",
    "options": "5fef5f7d8cd4b5f9910409fd586a0004fc9c60e43719ed43c8d21bc8c9e5caf7",
    "online": "6c2f1b4da2e28e9a82be7357699446a0056cc73dc947a28e3cbab3a9f8873dc9",
    "codes": "17a23fd9fd6b483e8da9843953754f95cc7af6cd6712b8bdf968f474414a4c09",
    "state": "a25e0a90d5c96ff214f3897367c1c6d58d5c72f5d4acbbc662718ba450c7e4c4",
    "pack": "8a9503b7ef6fb9cd96a2b0e00f1b1bb969fd39a48f167736175dc81c806181f8",
    "bridge": "2b6676cba1fcc6cd15495a01f34b172dfe570f8755cb31da834247a07cca3bfb",
}


class Art(unittest.TestCase):
    def test_golden_hashes(self):
        self.assertEqual(sorted(GOLDEN), sorted(A.KINDS))
        for k in A.KINDS:
            self.assertEqual(hashlib.sha256(A.icon_ci8(k)).hexdigest(), GOLDEN[k], k + " (re-run make_save_art.py --hashes if the art changed on purpose)")

    def test_deterministic_and_distinct(self):
        self.assertEqual(A.icon_ci8("code"), A.icon_ci8("code"))
        self.assertEqual(len({A.icon_ci8(k) for k in A.KINDS}), len(A.KINDS))

    def test_layout(self):
        b = A.icon_ci8("options")
        self.assertEqual(len(b), A.ICON_LEN)
        self.assertEqual(struct.unpack_from(">H", b, 0x400)[0], 0)           # palette entry 0 = fully transparent
        img = A.decode_ci8(b)
        self.assertEqual(img[0][0][3], 0)                                    # rounded corner
        self.assertEqual(img[16][1][3], 255)                                 # tile body is opaque
        self.assertTrue(max(b[:0x400]) < 101)

    def test_kind_of(self):
        n = F.projname.SHORT
        got = {x: A.kind_of(n + x) for x in ("Code", "CodeB", "Codes", "Ovl", "OvlN", "Identity", "Options", "Online", "State", "Pack", "Bridge", "Melee", "Zzz")}
        self.assertEqual(got, {"Code": "code", "CodeB": "code", "Codes": "codes", "Ovl": "ovl", "OvlN": "ovl", "Identity": "identity", "Options": "options",
                               "Online": "online", "State": "state", "Pack": "pack", "Bridge": "bridge", "Melee": None, "Zzz": None})
        self.assertIsNone(A.kind_of("SuperSmashBros0110290334"))


SHELL_GOLDEN = ("310da41678316038766a3d24789e47a2baa46e0183dea6693965fac4cde9917c",   # banner from the real logo, polished
                "81ffe721c57fc95457219af2feb7dfbf527774814d1253a9fc525e3147751342", "ea76013766dbd1217a25db8f396934d7ecdcfeaa8754bfb02e8958d2915aee49")


class LoaderShell(unittest.TestCase):
    def test_shell_art_golden_and_footprint(self):
        b, i, p = SA.make_regions()
        self.assertEqual((len(b), len(i), len(p)), (6144, 1024, 512))        # same footprint as the disc art: payload offsets never move
        self.assertEqual(tuple(hashlib.sha256(x).hexdigest() for x in (b, i, p)), SHELL_GOLDEN)

    def test_strip_changes_only_art_regions(self):
        g = bytearray(os.urandom(0x2040)); g[0:6] = b"GALE01"; g[7] = 2
        struct.pack_into(">I", g, 0x2C, 0x40); struct.pack_into(">H", g, 0x30, 1); struct.pack_into(">H", g, 0x38, 11)
        s = SA.strip(bytes(g))
        self.assertTrue(SA.is_shell(s))
        self.assertEqual(s[:0x80], bytes(g[:0x80]))                          # header, flags, offsets, comment untouched
        self.assertEqual(s[0x1E90:], bytes(g[0x1E90:]))                      # payload untouched
        self.assertEqual(SA.strip(s), s)                                     # idempotent


class Builders(unittest.TestCase):
    def check(self, entry, data, name, blocks=1):
        self.assertEqual(entry[7], 0)                                        # no banner
        self.assertEqual(struct.unpack_from(">I", entry, 0x2C)[0], P.ICON_OFFSET)
        self.assertEqual(struct.unpack_from(">H", entry, 0x30)[0], 1)        # CI8, palette after the icon
        self.assertEqual(struct.unpack_from(">H", entry, 0x38)[0], blocks)
        self.assertEqual(len(data), blocks * P.BLOCK)
        self.assertEqual(data[P.ICON_OFFSET:P.ICON_OFFSET + P.ICON_LEN], P.icon_for(name), name)
        self.assertLessEqual(P.ICON_OFFSET + P.ICON_LEN, P.PAYLOAD_OFFSET)   # the icon never reaches the payload

    def test_files(self):
        g = optfile.build_gci(bytes(optfile.SIZE))
        self.check(g[:0x40], g[0x40:], optfile.FILE_NAME)
        e, d = F.codes_file(["ABCD#123"])
        self.check(e, d, F.CODES_NAME)
        i = P.build_gci({"uid": "u", "playKey": "k", "connectCode": "AB#1", "displayName": "n"})
        self.check(i[:0x40], i[0x40:], P.DEFAULT_NAME)
        self.assertEqual(i[0x40:0x4A], b"Multishine")

    def test_code_file(self):
        img = bytes(64)
        base = (M.HOLE_MAX - M.WA_SIZE - M.BLOCK - 0x2000) & ~31
        g = M.build_gci(img, base, 0x2000, [])
        self.check(g[:0x40], g[0x40:], M.FILE_NAME, blocks=len(g[0x40:]) // M.BLOCK)
        self.assertEqual(g[0x40 + 32:0x40 + 62].rstrip(b"\0"), b"Needed to play. Keep it.")


if __name__ == "__main__":
    unittest.main()
