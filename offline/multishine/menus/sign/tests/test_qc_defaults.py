#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""The console picker's 16 default texts (qc_pick.h qc_default) must equal chat_presets.json: the server posts ITS copy of the sender's list by index, so the
text the player picks on the cube must be the line the room sees.  usage: test_qc_defaults.py"""
import json
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
src = (HERE.parent / "qc_pick.h").read_text()
m = re.search(r"static const char \*const D\[16\] = \{(.*?)\};", src, re.S)
assert m, "qc_default table not found"
got = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
want = [x["text"] for x in json.loads((HERE.parents[3] / "chat" / "chat_presets.json").read_text())["messages"]]
if got != want:
    print("FAIL: qc_pick.h defaults differ from chat_presets.json\n", got, "\n", want)
    sys.exit(1)
print("qc defaults match chat_presets.json (16 texts)")
