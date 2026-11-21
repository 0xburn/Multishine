#!/usr/bin/env python3.14
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for check_release_shape.py (config layer).  Run: python3.14 tools/release/test_check_release_shape.py"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_release_shape as C

clean = {}
REL = "hubovl=ovl,menuovl=ovl,flashblank=on,qchat=on,repovl=on"
assert C.check_config(REL, "", clean, set()) == []                        # the real release recipe passes
assert C.check_config(REL, "-DMS_REP_LINE=1", clean, set())                # the temporary report line is refused since
for opt in ("rbread=on", "rbread=rt", "netcfg=on", "hubdiag=on", "tlsprobe=on", "hgtrace=on", "itemsall=on"):
    assert C.check_config(REL + "," + opt, "", clean, set()), opt
for fl in ("MM_FTSTATS", "MM_PROBE", "MS_MATCH_MSG", "MS_REP_SELFTEST", "MS_REC_SELFTEST", "GNB_TEST_REFUSE", "MS_CSSPROBE", "SG_LAB", "MS_TMM_LAB_LINK", "TMM_SLIPPI_WINDOW_MS"):
    assert C.check_config(REL, "-D%s=1" % fl, clean, set()), fl
for fl in ("MM_RB_SHOW", "MS_HEAPPROBE", "ON_DIAG"):                       # on-screen readouts: refused even next to a RELEASE_TEST mode's own allow list
    assert C.check_config(REL + ",rbread=on", "-DMM_FTSTATS=1 -D%s=1" % fl, clean, {"rbread", "MS_RB_READOUT", "MM_FTSTATS"}), fl
for var in ("SG_LAB","REP_TEST_ROOTS", "SG_TLSPROBE", "MS_MATCH_MSG"):
    assert C.check_config(REL, "", {var: "1"}, set()), var
assert C.check_config(REL, "", {"MS_XCF": "-DMM_PROBE=1"}, set())           # a flag smuggled through the environment
assert C.check_config(REL + ",rbread=on", "-DMM_FTSTATS=1", clean, {"rbread", "MM_FTSTATS"}) == []   # a RELEASE_TEST mode's own switches
import tempfile                                                              # a published .mscf has no 0x40 GCI entry: it must get past the size check
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "card"))
import mscf_sign
with tempfile.NamedTemporaryFile(suffix=".mscf", delete=False) as t:
    t.write(bytes(mscf_sign.M.BLOCK * 2))
r = C.check_images([t.name], set()); os.unlink(t.name)
assert r and "size" not in r[0], r
print("check_release_shape tests ok")
