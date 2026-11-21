# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
# Source-level guards for the Friends hand-offs (Z on Friends > Add a friend gave a black screen in Dolphin).  sg_entry.c is console-only code (no host build), so the
# ordering that matters is checked on the text: the glue is asked (env opt 70 / 71) and a refusal handled BEFORE anything blanks the screen or arms a hand-off state; ml_glue.c refuses
# with -12 when the Multishine system files are missing; the toast accept path (hub_soc.c) never accepts a match it cannot enter.
import os, re, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
MENUS = os.path.join(HERE, "..", "..")


def rd(*p):
    with open(os.path.join(MENUS, *p)) as f:
        return f.read()


class HandoffGate(unittest.TestCase):
    def test_add_friend_asks_before_cover(self):
        s = rd("sign", "sg_entry.c")
        i = s.index("G.fr.play == 5u")
        j = s.index("0x80350100u", i)                      # VISetBlack
        k = s.index("g_id->opt(71, 0)", i)
        self.assertLess(k, j)
        self.assertLess(k, s.index("c->gn_state = MNC_GN_ADDFR;", i))
        self.assertIn("rc71 == -12", s[i:j])
        self.assertIn("system files not set up", s[i:j])

    def test_watch_and_challenge_check_the_mark(self):
        s = rd("sign", "sg_entry.c")
        i = s.index("G.fr.play == 4u")
        self.assertLess(s.index("g_id->opt(70, 0) != 0", i), s.index("c->gn_state = MNC_GN_WATCH;", i))
        i = s.index("G.fr.play == 1u")
        self.assertLess(s.index("g_id->opt(70, 0) != 0", i), s.index("frdn_go_css(", i))

    def test_glue_refuses_without_data(self):
        s = rd("ml", "ml_glue.c")
        m = re.search(r"if \(\(id == 70 \|\| id == 71\) && ml_online_data_missing\(ml_net_cost\(107\)\)\)\s*return -12;", s)
        self.assertTrue(m)
        self.assertLess(m.start(), s.index("g_slip_back = 3;", m.start()))

    def test_invites_accept_checks_data_first(self):
        # the Friends Invites: a Mano a mano accept is held (inv_chk) until the glue answered env opt 72 (a question only, -12 = no system files)
        f = rd("sign", "frd.c")
        a = f.index("if (f->arg_yes && f->arg_type == 1u) { f->inv_chk = 1; return; }")
        self.assertLess(a, f.index("busy(f, FRD_REQ_INVRESP, FRD_LIST);", a))
        self.assertIn("system files not set up", f[f.index("void frd_inv_checked"):f.index("static void list_frame")])
        s = rd("sign", "sg_entry.c")
        i = s.index("G.fr.inv_chk")
        self.assertIn("g_id->opt(72, 0)", s[i:i + 600])
        self.assertIn("rc72 == -12", s[i:i + 700])
        g = rd("ml", "ml_glue.c")
        self.assertRegex(g, r"if \(id == 72\)[^\n]*\n\s*return ml_online_data_missing\(ml_net_cost\(107\)\) \? -12 : 0;")
        self.assertIn("if (id >= 60 && id <= 73)", g)

    def test_toast_accept_checks_data_first(self):
        s = rd("hub", "hub_soc.c")
        a = s.index("S.T.act == TST_A_INVITE && data_files_missing()")
        self.assertLess(a, s.index("start(now, OP_RESP)", a))
        self.assertIn("go_direct(s->peer, S.port)) tst_done(&S.T, now, 1, 0); else tst_done(&S.T, now, 0,", s)


if __name__ == "__main__":
    unittest.main()
