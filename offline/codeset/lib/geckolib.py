# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Shared helpers for the offline codeset build: gecko config expansion, gecko text-output
parsing, Gecko op decoding and retail main.dol section lookup.

Nothing in here contains or copies slippi-ssbm-asm source; it only reads the files of an
(out-of-tree copy of the) asm tree and the output of the gecko tool.
"""
import hashlib
import os
import re
import struct

# --------------------------------------------------------------------------- gecko config


def gecko_walk(asm_root, folder, recursive):
    """Exactly reproduce gecko's collectFilesFromFolder (gecko.go @e3183ba):
    ReadDir is name-sorted; a folder's own .asm files come first, then its sub-folders,
    depth-first (sub-folders are pushed to the *front* of the queue)."""
    out = []
    folders = [folder]
    while folders:
        f = folders.pop(0)
        names = sorted(os.listdir(os.path.join(asm_root, f)))
        new = []
        for n in names:
            p = os.path.join(f, n)
            if os.path.isdir(os.path.join(asm_root, p)):
                if recursive:
                    new.append(p)
                continue
            if os.path.splitext(n)[1] == ".asm":
                out.append(p)
        folders = new + folders
    return out


def expand_build(asm_root, entry):
    """One gecko build entry -> list of (module_path, kind). kind in {'asm','bin'}."""
    t = entry["type"]
    if t == "injectFolder":
        return [(p, "asm") for p in gecko_walk(asm_root, entry["sourceFolder"], entry.get("isRecursive", False))]
    if t == "inject":
        return [(entry["sourceFile"], "asm")]
    if t == "binary":
        return [(entry["sourceFile"], "bin")]
    raise ValueError("unsupported gecko build type %r" % t)


def normalize_annotations(cfg):
    """Return a copy of a gecko config whose binary entries carry their source path as the
    last ' | ' component of the line annotation (bytes are unaffected), so that the text
    output can be mapped back to modules."""
    out = {"outputFiles": [], "codes": []}
    for code in cfg["codes"]:
        c = dict(code)
        c["build"] = []
        for b in code["build"]:
            b = dict(b)
            if b["type"] == "binary":
                ann = b.get("annotation", "")
                b["annotation"] = "%s | %s" % (ann, b["sourceFile"]) if ann else b["sourceFile"]
            c["build"].append(b)
        out["codes"].append(c)
    return out


# --------------------------------------------------------------------------- gecko text output

LINE_RE = re.compile(r"^([0-9A-Fa-f]{8}) ([0-9A-Fa-f]{8})(?:\s*#(.*))?$")


def parse_gecko_text(text):
    """Parse gecko text output (or a Dolphin ini [Gecko] block).
    Returns list of dicts {section, path, words:[(a,v),...]} in output order. A module starts at
    every annotated code line; the module path is the last ' | ' component of the annotation."""
    mods = []
    section = None
    in_gecko = None
    for raw in text.splitlines():
        s = raw.strip()
        if s.startswith("[") and s.endswith("]"):
            in_gecko = s == "[Gecko]"
            continue
        if in_gecko is False:
            continue
        if s.startswith("$"):
            section = re.sub(r"\s*\[[^\]]*\]\s*$", "", s[1:]).strip()
            continue
        m = LINE_RE.match(s)
        if not m:
            continue
        a, v, ann = int(m.group(1), 16), int(m.group(2), 16), m.group(3)
        if ann is not None:
            path = ann.split(" | ")[-1].strip()
            mods.append({"section": section, "path": path, "annotation": ann.strip(), "words": []})
        elif not mods or mods[-1]["section"] != section:
            raise ValueError("unannotated code line starts a module in section %r: %s" % (section, s))
        mods[-1]["words"].append((a, v))
    return mods


def ini_code_bytes(text, sections=None):
    """Raw code bytes of a Dolphin ini [Gecko] block (optionally only some $sections)."""
    out = bytearray()
    section = None
    in_gecko = False
    for raw in text.splitlines():
        s = raw.strip()
        if s.startswith("[") and s.endswith("]"):
            in_gecko = s == "[Gecko]"
            continue
        if not in_gecko:
            continue
        if s.startswith("$"):
            section = re.sub(r"\s*\[[^\]]*\]\s*$", "", s[1:]).strip()
            continue
        m = LINE_RE.match(s)
        if m and (sections is None or section in sections):
            out += struct.pack(">II", int(m.group(1), 16), int(m.group(2), 16))
    return bytes(out)


def words_to_bytes(words):
    return b"".join(struct.pack(">II", a, v) for a, v in words)


def decode_ops(words):
    """Split Gecko code words into ops. Only 04/06/C2 are produced by gecko/netplay.json;
    anything else raises."""
    ops = []
    i = 0
    while i < len(words):
        a, v = words[i]
        t = (a >> 24) & 0xFE
        addr = (a & 0x01FFFFFF) | 0x80000000
        if t == 0x04:
            ops.append({"type": "04", "address": addr, "value": v, "code_bytes": 8})
            i += 1
        elif t == 0xC2:
            n = v
            payload = []
            for k in range(n):
                payload += [words[i + 1 + k][0], words[i + 1 + k][1]]
            ops.append({"type": "C2", "address": addr, "lines": n, "payload": payload,
                        "code_bytes": 8 * (1 + n)})
            i += 1 + n
        elif t == 0x06:
            n = (v + 7) // 8
            data = []
            for k in range(n):
                data += [words[i + 1 + k][0], words[i + 1 + k][1]]
            ops.append({"type": "06", "address": addr, "length": v, "payload": data,
                        "code_bytes": 8 * (1 + n)})
            i += 1 + n
        else:
            raise ValueError("unsupported Gecko codetype %02X at line word %08X" % (t, a))
    return ops


# --------------------------------------------------------------------------- DOL


class Dol:
    def __init__(self, path):
        self.path = path
        b = open(path, "rb").read()
        self.data = b
        self.sha1 = hashlib.sha1(b).hexdigest()
        off = struct.unpack(">18I", b[0:0x48])
        addr = struct.unpack(">18I", b[0x48:0x90])
        size = struct.unpack(">18I", b[0x90:0xD8])
        self.bss_addr, self.bss_size, self.entry = struct.unpack(">III", b[0xD8:0xE4])
        self.sections = []
        for k in range(18):
            if size[k]:
                name = ("T%d" % k) if k < 7 else ("D%d" % (k - 7))
                self.sections.append((name, addr[k], size[k], off[k]))

    def section_of(self, a):
        for name, ad, sz, _ in self.sections:
            if ad <= a < ad + sz:
                return name
        if self.bss_addr <= a < self.bss_addr + self.bss_size:
            return "BSS"
        return "none"

    def word(self, a):
        for name, ad, sz, off in self.sections:
            if ad <= a and a + 4 <= ad + sz:
                return struct.unpack(">I", self.data[off + a - ad: off + a - ad + 4])[0]
        return None


def sha256_file(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def sha1_file(p):
    return hashlib.sha1(open(p, "rb").read()).hexdigest()
