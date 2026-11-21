#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""'title stay': the title screen never leaves on its own ("always stays on the title screen when idle, no autoplay match or how-to-play
video").

gm_Scene_Title_OnFrame (0x801A1C18, vendor/melee/src/melee/gm/gmtitle.c; retail words read from the 1.02 DOL):
    0x801A1C4C..58  frame_count++ (r13-0x4F88 = 0x804D6718), after the 20-frame countdown (r13-0x4F8C)
    0x801A1C5C      cmplwi r0,0x258                  (frame_count vs 600)
    0x801A1C60      ble +0x18  (40810018)  -> 0x801A1C78: the input branch (START -> exit data = buttons -> GM_MENU; debug Y/A/X)
    0x801A1C64..74  frame_count > 600: exit data 0, gm_801A4B60 = leave the scene; the title mode's onExit then picks GM_OPENING_MV (gm_801BF708(1):
                    the attract loop of opening movie / demo match / how-to-play) because no START bit is set
This patch changes exactly ONE word: 0x801A1C60 `ble +0x18` -> `b +0x18` (48000018), so every frame takes the input branch and the timeout exit is never
reached.  START is untouched (same code path as before); the counter still counts (harmless: a u32, it would take 2.2 years to wrap).  The title entered
after the opening movie runs the same OnFrame, so it stays too.  On the card route the word is a guarded patch of the card code file (retail word checked
by the loader's guard CRC); the stock disc's first title (before the card loads) is retail.
  python3 tools/title_stay_patch.py IN.dol OUT.dol
"""
import pathlib, struct, sys

REPO = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "offline/dolpatch"))
import dol_patch  # noqa: E402

ADDR, OLD, NEW = 0x801A1C60, 0x40810018, 0x48000018
CONTEXT = {0x801A1C5C: 0x28000258,      # cmplwi r0,0x258 (600 frames)
           0x801A1C64: 0x48002F39,      # bl gm_GetCurrentSceneExitData (the timeout exit)
           0x801A1C70: 0x48002EF1,      # bl gm_801A4B60 (end the scene)
           0x801A1C78: 0x57E004E7}      # rlwinm. r0,r31,0,19,19: the START test, the branch target
TIMEOUT = 600


def code04():
    """the Gecko 04 code the payload adds (contract key title_stay)"""
    return struct.pack(">II", 0x04000000 | (ADDR & 0x01FFFFFF), NEW)


def leaves_after(word, start_at=None, frames=100000):
    """Model of the decision at ADDR (the countdown already over): the frame (1-based) on which the title scene leaves, and why, or (None, None).
    word: the instruction at ADDR (OLD = retail, NEW = patched); start_at: the frame START is pressed, or None."""
    if word not in (OLD, NEW):
        raise ValueError("unknown word %08X at %08X" % (word, ADDR))
    count = 0
    for f in range(1, frames + 1):
        count = (count + 1) & 0xFFFFFFFF
        take_input = count <= TIMEOUT if word == OLD else True       # ble: unsigned compare result 'less or equal'; b: always
        if not take_input:
            return f, "timeout"
        if start_at is not None and f >= start_at:
            return f, "start"
    return None, None


def file_offset(dol, addr):
    for s in dol_patch.Dol(dol).loaded():
        if s["addr"] <= addr < s["addr"] + s["size"]:
            return s["offset"] + addr - s["addr"]
    raise ValueError("address %08X is not in a loaded section" % addr)


def word(dol, addr):
    return struct.unpack_from(">I", dol, file_offset(dol, addr))[0]


def check_retail(dol):
    """the retail words around the site: raises when this is another version (or already patched)"""
    for a, w in sorted(CONTEXT.items()) + [(ADDR, OLD)]:
        cur = word(dol, a)
        if cur != w:
            raise ValueError("word at %08X is %08X, expected the retail %08X (already patched or another version)" % (a, cur, w))


def patch_dol(dol):
    check_retail(dol)
    out = bytearray(dol)
    struct.pack_into(">I", out, file_offset(dol, ADDR), NEW)
    return bytes(out)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    pathlib.Path(sys.argv[2]).write_bytes(patch_dol(pathlib.Path(sys.argv[1]).read_bytes()))
