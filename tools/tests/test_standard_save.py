# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for tools/card/standard_save.py (the one standard MultishineMelee save).  Needs the Melee-made template (gitignored; the main checkout's
a local work folder or $MS_MELEE_TEMPLATE); every test skips cleanly without it.  Host only, stdlib.
Run: python3 -m unittest -v tools/tests/test_standard_save.py"""
import glob
import os
import struct
import sys
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO, "tools", "card"))
import standard_save as S  # noqa: E402
import melee_coins as MC   # noqa: E402
import vmc_card as C       # noqa: E402
import vmc_gci as G        # noqa: E402


def find_template():
    c = [os.environ.get("MS_MELEE_TEMPLATE"), MC.DEFAULT_TEMPLATE]
    c += glob.glob(os.path.join(REPO, "..", "..", "..", ".analysis-deps", "work", "coins", "MultishineMelee_template.gci"))   # a checkout nested three levels below the main one
    for p in c:
        if p and os.path.isfile(p):
            return p
    return None


TEMPLATE = find_template()


@unittest.skipUnless(TEMPLATE, "no Melee save template (MS_MELEE_TEMPLATE or .analysis-deps/work/coins/MultishineMelee_template.gci)")
class Standard(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.gci = S.build(TEMPLATE)

    def test_deterministic(self):
        self.assertEqual(S.build(TEMPLATE), self.gci)

    def test_all_properties(self):
        self.assertEqual(S.problems(self.gci), [])

    def test_all_events_completed_with_records(self):
        import melee_records as MR
        body = self.gci[0x40:]
        for b in MC.file1_sectors(body):                       # both copies of the game data
            evs = MR.read_events(MC.decode_sector(body[b * C.BLOCK:(b + 1) * C.BLOCK])[1])
            self.assertEqual(len(evs), 51)
            self.assertTrue(all(e["done"] and e["value"] > 0 for e in evs), "every Event Match completed with a record")

    def test_shape(self):
        self.assertEqual(len(self.gci), 0x40 + 11 * 0x2000)
        self.assertEqual(struct.unpack_from(">I", self.gci, 0x28)[0], S.MTIME)

    def test_no_melee_art(self):
        import hashlib
        import save_art as SA
        self.assertNotEqual(hashlib.sha256(self.gci[SA.BANNER_OFF:SA.BANNER_OFF + SA.BANNER_LEN]).hexdigest(), SA.DISC_BANNER_SHA256)
        self.assertNotEqual(hashlib.sha256(self.gci[SA.ICON_OFF:SA.ICON_OFF + SA.ICON_LEN + SA.PAL_LEN]).hexdigest(), SA.DISC_ICON_SHA256)

    def test_verify_catches_damage(self):
        g = bytearray(self.gci)
        g[0x100] ^= 1                                  # banner pixel: art and digest break
        self.assertTrue(S.problems(bytes(g)))
        _, data, _ = G.parse_gci(self.gci)
        _, dec = MC.decode_sector(data[1 * C.BLOCK:2 * C.BLOCK])
        dec[MC.DATA_OFF + S.O_FEAT] = 0                # one copy loses the feature mask
        g = bytearray(self.gci)
        g[0x40 + C.BLOCK:0x40 + 2 * C.BLOCK] = MC.encode_sector(dec)
        self.assertTrue(any("feature" in p or "differ" in p for p in S.problems(bytes(g))))

    def test_apply_to_card(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            card = os.path.join(d, "c.raw")
            open(card, "wb").write(bytes(C.format_card(2 * 1024 * 1024).img))
            what, img = S.apply_card(card, os.path.join(d, "n.raw"), TEMPLATE)
            self.assertEqual(what, "added")
            open(card + "2", "wb").write(img)
            with self.assertRaises(S.StdError):
                S.apply_card(card + "2", os.path.join(d, "m.raw"), TEMPLATE)
            what, img2 = S.apply_card(card + "2", os.path.join(d, "m.raw"), TEMPLATE, replace=True)
            self.assertEqual(what, "replaced")
            cc = C.Card(img2)
            self.assertEqual(cc.read(cc.find(S.NAME)), self.gci[0x40:])


if __name__ == "__main__":
    unittest.main()
