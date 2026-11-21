# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Stage-1 loader slot for the VMC builder (our own code, stdlib only).

The builder does NOT contain, generate or describe any save-exploit mechanics.  A stage-1 loader is an OPAQUE GCI file that you
compile yourself with a tool you trust ( names the pinned upstream compiler); this module only
checks that the file is a well-formed GCI that fits the size budget and places it on a card image with the card model's rules
(first fit, directory and block map updated, both copies, checksums).  Nothing here says whether the loader works on a console.

Interface (our definition):
  * GCI = 0x40-byte directory entry + N whole 0x2000-byte blocks, game code GALE, maker 01 (a Melee-region save), N <= LOADER_MAX_BLOCKS.
  * stage 2 (the engine, far larger than a save can hold) is NOT on the card: it lives on the SD/BBA under /multishine/ and is named by
    a stage-2 record: {"format": "multishine-stage2", "version": 1, "path": "/multishine/stage2.bin", "size": N, "sha256": hex}.
    The record is written next to the card image as JSON (host side) so the SD and card can be checked against each other.
"""
import hashlib
import json
import re
import struct

import vmc_card as C

GCI_ENTRY = 0x40
LOADER_MAX_BLOCKS = 64            # budget we allow for stage 1 (512 KiB); a stage 1 should be a handful of blocks
STAGE2_FORMAT, STAGE2_VERSION = "multishine-stage2", 1
STAGE2_PATH_RE = re.compile(r"^/multishine/[A-Za-z0-9._-]{1,48}$")
RESERVED_NAMES = ("SuperSmashBros0110290334", "MultishineOptions", "MultishineIdentity")


class LoaderError(Exception):
    pass


def parse_gci(buf, allow_save_name=False):
    """-> (entry bytes, data bytes, info dict) or LoaderError.  Pure structure checks.

    allow_save_name: accept the file name of Melee's own save (a community card file IS a Melee-named save and takes that slot; see
 stock-disc section).  Only header fields are looked at, never the data."""
    buf = bytes(buf)
    if len(buf) < GCI_ENTRY + C.BLOCK or (len(buf) - GCI_ENTRY) % C.BLOCK:
        raise LoaderError("not a GCI: size must be 0x40 + a whole number of 0x2000-byte blocks")
    e, data = buf[:GCI_ENTRY], buf[GCI_ENTRY:]
    n = len(data) // C.BLOCK
    if e[0:4] != C.GAME_CODE or e[4:6] != C.MAKER:
        raise LoaderError("GCI game code/maker is not GALE/01")
    if e[6] != 0xFF:
        raise LoaderError("GCI directory entry byte 6 must be 0xFF")
    if struct.unpack_from(">H", e, 0x3A)[0] != 0xFFFF:
        raise LoaderError("GCI directory entry padding invalid")
    raw_name = e[8:40]
    name = raw_name.split(b"\0")[0]
    if not name or any(c < 0x20 or c > 0x7E for c in name):
        raise LoaderError("GCI file name is empty or not printable ASCII")
    if raw_name[len(name):].strip(b"\0"):
        raise LoaderError("GCI file name has bytes after its terminator")
    if struct.unpack_from(">H", e, 0x38)[0] != n:
        raise LoaderError("GCI block count %d does not match the data (%d blocks)"
                          % (struct.unpack_from(">H", e, 0x38)[0], n))
    if n > LOADER_MAX_BLOCKS:
        raise LoaderError("stage-1 file is %d blocks, budget is %d" % (n, LOADER_MAX_BLOCKS))
    name = name.decode("ascii")
    if name in RESERVED_NAMES and not (allow_save_name and name == RESERVED_NAMES[0]):
        raise LoaderError("file name %r is reserved (Melee's own save or our option/identity files)" % name)
    comment = struct.unpack_from(">I", e, 0x2C)[0]
    if comment != 0xFFFFFFFF and comment >= len(data):
        raise LoaderError("GCI comment/image offset points outside the data")
    return e, data, {"name": name, "blocks": n, "bytes": len(data), "perm": e[0x34],
                     "sha256": hashlib.sha256(data).hexdigest()}


def load_gci(path, allow_save_name=False):
    try:
        with open(path, "rb") as f:
            return parse_gci(f.read(C.MAX_BYTES), allow_save_name)
    except OSError as x:
        raise LoaderError("cannot read %s (%s)" % (path, x.strerror))


def entry_with_mtime(entry, mtime):
    """Optionally pin the file time stamp for reproducible images; first block is set by Card.add."""
    e = bytearray(entry)
    if mtime is not None:
        struct.pack_into(">I", e, 0x28, max(0, int(mtime) - C.EPOCH_2000))
    return bytes(e)


def stage2_record(stage2_path, sd_path="/multishine/stage2.bin"):
    """Host-side record that ties the card's stage-1 to the stage-2 file on the SD (hash only; the file is never copied or run)."""
    if not STAGE2_PATH_RE.match(sd_path):
        raise LoaderError("stage-2 SD path must look like /multishine/<name>")
    try:
        with open(stage2_path, "rb") as f:
            blob = f.read()
    except OSError as x:
        raise LoaderError("cannot read %s (%s)" % (stage2_path, x.strerror))
    if not blob:
        raise LoaderError("stage-2 file is empty")
    rec = {"format": STAGE2_FORMAT, "version": STAGE2_VERSION, "path": sd_path, "size": len(blob),
           "sha256": hashlib.sha256(blob).hexdigest()}
    if blob[:4] == b"MSPK" and len(blob) >= 200:
        # signed payload container (tools/payload_pack): additive "container" key, record version unchanged
        ver, comp, kind, _f, seq, ssz, rsz, _m = struct.unpack_from(">BBBBIIII", blob, 4)
        rec["container"] = {"version": ver, "comp": comp, "build_seq": seq, "stored_size": ssz, "raw_size": rsz,
                            "build_id": blob[24:40].rstrip(b"\0").decode("ascii", "replace"),
                            "codeset_sha256": blob[40:72].hex(), "stored_sha256": blob[72:104].hex(),
                            "raw_sha256": blob[104:136].hex(), "header_sha256": hashlib.sha256(blob[:136]).hexdigest()}
    return rec


def record_json(rec):
    return json.dumps(rec, indent=1, sort_keys=True) + "\n"
