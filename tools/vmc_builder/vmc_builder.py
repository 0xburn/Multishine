#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""vmc_builder: make Swiss-style raw virtual memory card (VMC) images, or add the Multishine files to a card you supply.

  vmc_builder.py create --sd-root SD --slot A --region USA --options [--chat ] [--identity user.json]
  vmc_builder.py create --out card.raw ...                    (explicit path instead of the Swiss path scheme)
  vmc_builder.py add --card swiss/saves/MemoryCardA.USA.raw --options [--identity user.json] [--replace]
  vmc_builder.py add --card in.raw --out new.raw ...          (leave the original alone, no backup needed)
  vmc_builder.py verify CARD.raw [--json]
  vmc_builder.py template                                     (print a starting options JSON)
  vmc_builder.py export-gci --card CARD --only options,identity,online,codes,bridge,melee --gci-out DIR     (single saves as standard .gci)
  vmc_builder.py import-gci --card CARD FILE.gci... [--replace] [--out NEW.raw]                            (one .gci into a card image)
  vmc_builder.py set-net --card CARD [--out NEW.raw | --in-place] [--mm-ip A.B.C.D] [--dns-ip A.B.C.D] [--clear]   (--status-ip/--status-port still parse but the console ignores them since)
                                                              (DEV/LAB images only: network addresses inside MultishineBridge)
  vmc_builder.py show-net CARD [--json]                       (print that section)
  vmc_builder.py set-config --card CARD --config  [--out NEW.raw | --in-place] [--release] [--replace-online]   (ONE schema: options, dev-only network, online setup)
  vmc_builder.py show-config CARD [--json]                    (all of it, secrets never shown)
  vmc_builder.py validate-config  [--release]         (schema check only; --release refuses the dev-only part)
  vmc_builder.py make-gci --gci-out DIR [--options O.json] [--identity user.json] [--online-config S.json] [--codes A#1,B#2] [--melee-from-card CARD]

Swiss path scheme: <SD root>/swiss/saves/MemoryCard{A,B}.{USA|JPN|EUR}.raw.
Files placed: MultishineOptions (settings, optional quick-chat texts, no account data), MultishineOnline (only with --online-config: server, pinned key and device
secret of the online image, offline/multishine/menus/on) and, only when you pass your own
user.json, MultishineIdentity (a secret: treat the card like a password).  Melee's own save is never read or changed.
Add never overwrites a card without first writing a verified backup next to it; it refuses if that backup already exists.
--dry-run does everything in memory and reports what would happen; nothing is written.  Stdlib only;.
"""
import argparse
import json
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import vmc_card as C  # noqa: E402
import vmc_files as F  # noqa: E402
import vmc_gci as G  # noqa: E402
import vmc_loader as L  # noqa: E402
import vmc_netcfg as NC  # noqa: E402
import vmc_config as CFG  # noqa: E402
import vmc_validate as V  # noqa: E402

REGIONS = ("USA", "JPN", "EUR")
SLOTS = ("A", "B")
MELEE_SAVE = "SuperSmashBros0110290334"
MELEE_BLOCKS = 11
BACKUP_SUFFIX = ".bak"


class ToolError(Exception):
    pass


def swiss_path(sd_root, slot, region):
    return os.path.join(sd_root, "swiss", "saves", "MemoryCard%s.%s.raw" % (slot, region))


def collect_files(a):
    """[(label, entry, data)] for the files requested on the command line."""
    files = []
    if a.chat and not a.options:
        raise ToolError("--chat needs --options (the quick-chat texts live inside the options file)")
    if a.options:
        e, d = F.options_file(a.options, a.chat, a.mtime)
        files.append((F.OPTIONS_NAME, e, d))
    if a.identity:
        e, d, code, name = F.identity_file(a.identity, a.mtime)
        files.append((F.IDENTITY_NAME, e, d))
        a._identity_note = "identity for %s (%s)" % (code, name)
    if getattr(a, "online_config", None):
        e, d, code, server = F.online_file(a.online_config, a.mtime)
        files.append((F.ONLINE_NAME, e, d))
        a._online_note = "online setup for %s at %s (holds the device secret)" % (code, server)
    if getattr(a, "codes", None) is not None:
        e, d = F.codes_file(a.codes.split(","), a.mtime)
        files.append((F.CODES_NAME, e, d))
    if getattr(a, "loader_gci", None):
        try:
            e, d, info = L.load_gci(a.loader_gci)
        except L.LoaderError as x:
            raise ToolError("--loader-gci: %s" % x)
        files.append((info["name"], L.entry_with_mtime(e, a.mtime), d))
        a._loader_names = (info["name"],)
        a._loader_note = "stage-1 loader %s, %d blocks, sha256 %s (opaque file: structure checked only)" % (
            info["name"], info["blocks"], info["sha256"])
    if getattr(a, "save_gci", None):
        try:
            e, d, info = L.load_gci(a.save_gci, allow_save_name=True)
        except L.LoaderError as x:
            raise ToolError("--save-gci: %s" % x)
        if info["name"] != MELEE_SAVE:
            raise ToolError("--save-gci: the file is not named %s (use --loader-gci for other loader files)" % MELEE_SAVE)
        files.append((info["name"], L.entry_with_mtime(e, a.mtime), d))
        a._loader_names = tuple(getattr(a, "_loader_names", ())) + (info["name"],)
        a._loader_note = "Melee-named save file from --save-gci takes the slot of Melee's own save: %d blocks, sha256 %s (opaque file: structure checked only)" % (
            info["blocks"], info["sha256"])
    if not files and not getattr(a, "empty", False):
        raise ToolError("nothing to place: give --options, --identity, --online-config, --loader-gci and/or --save-gci")
    return files


def room_check(card, files, skip, replacing=False):
    """Policy rule 5: keep room for Melee's save when it is not on the card yet."""
    if skip or card.find(MELEE_SAVE) is not None or any(l == MELEE_SAVE for l, _, _ in files):
        return
    need, slots = 0, 0
    for label, _, d in files:
        if replacing and card.find(label) is not None:
            continue                      # replaced in place: net zero
        need += len(d) // C.BLOCK
        slots += 1
    if card.free_blocks() - need < MELEE_BLOCKS or C.DIR_ENTRIES - len(card.entries()) - slots < 1:
        raise ToolError("the card would have no room left for Melee's own save (11 blocks and a directory entry); "
                        "use a bigger card or --no-room-check")


def place(card, files, replace):
    done = []
    for label, entry, data in files:
        old = card.find(label)
        if old is not None:
            if not replace:
                raise ToolError("%s is already on the card (use --replace to swap in the new one)" % label)
            card.remove(old)
        card.add(entry, data)
        done.append(label)
    return done


def check_untouched(before, after, extra=()):
    """Every file that was on the card before must be byte-identical afterwards (except our own, deliberately replaced)."""
    for e in before.entries():
        if (e["name"] in (F.OPTIONS_NAME, F.IDENTITY_NAME, F.ONLINE_NAME, F.CODES_NAME) or e["name"] in extra) and e["gamecode"] == C.GAME_CODE:
            continue
        n = after.find(e["name"], e["gamecode"], e["maker"])
        if n is None or after.read(n) != before.read(e) or n["raw"][:0x28] != e["raw"][:0x28]:
            raise ToolError("internal check failed: file %r would change; nothing written" % e["name"])


def atomic_write(path, data, private):
    d = os.path.dirname(os.path.abspath(path))
    fd, tmp = tempfile.mkstemp(prefix=".vmc-", dir=d)
    try:
        with os.fdopen(fd, "wb") as f:      # own the descriptor first: a failure below must not leak it (Windows cannot delete an open file)
            if hasattr(os, "fchmod"):       # no fchmod on Windows (file modes there are only the read-only flag)
                os.fchmod(f.fileno(), 0o600 if private else (os.stat(path).st_mode & 0o777))
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def write_new(path, data, private):
    """Create a file that must not exist yet."""
    if os.path.lexists(path):
        raise ToolError("%s already exists; refusing to overwrite it" % path)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600 if private else 0o644)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
    except BaseException:
        try:
            os.unlink(path)
        except OSError:
            pass
        raise


def holds_secret(a):
    return bool(a.identity or getattr(a, "online_config", None))


def identity_guard(a, out_path):
    """A card holding an identity or a device secret must not land inside a git work tree unless ignored (same rule as multishine-provision)."""
    if not holds_secret(a):
        return
    probe = os.path.dirname(os.path.abspath(out_path))
    while not os.path.isdir(probe):          # the target dir may not exist yet (create, dry-run)
        probe = os.path.dirname(probe)
    try:
        F.P.check_output_path(os.path.join(probe, os.path.basename(out_path)))
    except F.P.ProvisionError as e:
        raise ToolError("card with identity: " + str(e))


def say_files(out, labels, a, slot):
    for l in labels:
        out.write("  + %s\n" % l)
    if getattr(a, "_loader_note", None):
        out.write("  %s\n" % a._loader_note)
    if getattr(a, "_online_note", None):
        out.write("  %s\n  The card now holds a device secret: do not share, upload or cloud-sync it.\n" % a._online_note)
    if getattr(a, "_identity_note", None):
        out.write("  identity: %s\n" % a._identity_note)
        out.write("  The card now holds your playKey: do not share, upload or cloud-sync it. Policy: keep the identity on a\n"
                  "  personal card in slot B when cards are shared (docs/research/CARD_SLOT_POLICY.md rule 10).\n")
        if slot == "A":
            out.write("  warning: this is a slot A card; slot A cards are the ones people copy and share.\n")


def cmd_create(a, out):
    files = collect_files(a)
    if a.out:
        path, slot = a.out, None
    elif a.sd_root:
        path, slot = swiss_path(a.sd_root, a.slot, a.region), a.slot
    else:
        raise ToolError("give --sd-root DIR (Swiss path scheme) or --out FILE")
    if os.path.lexists(path):
        raise ToolError("%s already exists; create never overwrites (use add, or another path)" % path)
    identity_guard(a, path)
    card = C.format_card(C.SIZES_MIB[a.size], a.region)
    room_check(card, files, a.no_room_check)
    labels = place(card, files, False)
    problems = V.errors(V.validate(bytes(card.img)))
    if problems:
        raise ToolError("internal check failed: " + "; ".join(problems))
    out.write("%s%s: new %s MiB card, region %s, %d free blocks left\n" % (
        "[dry run] would create " if a.dry_run else "created ", path, a.size, a.region, card.free_blocks()))
    say_files(out, labels, a, slot)
    if not a.dry_run:
        write_new(path, bytes(card.img), holds_secret(a))
    return 0


def cmd_add(a, out):
    files = collect_files(a)
    src = a.card
    try:
        with open(src, "rb") as f:
            raw = f.read()
    except OSError as e:
        raise ToolError("cannot read %s (%s)" % (src, e.strerror))
    before = C.Card(raw)
    bad = before.consistency()
    if bad:
        raise ToolError("the card is not consistent, refusing to touch it: " + "; ".join(bad))
    card = C.Card(raw)
    room_check(card, files, a.no_room_check, a.replace)
    labels = place(card, files, a.replace)
    check_untouched(before, card, getattr(a, "_loader_names", ()))
    problems = V.errors(V.validate(bytes(card.img)))
    if problems:
        raise ToolError("internal check failed after the edit: " + "; ".join(problems))
    dest = a.out or src
    inplace = os.path.abspath(dest) == os.path.abspath(src)
    backup = src + BACKUP_SUFFIX
    identity_guard(a, dest)
    if inplace and os.path.lexists(backup):
        raise ToolError("backup %s already exists; refusing to overwrite it (move it away first)" % backup)
    if not inplace and os.path.lexists(dest):
        raise ToolError("%s already exists; refusing to overwrite it" % dest)
    out.write("%s%s: %d free blocks left\n" % ("[dry run] would update " if a.dry_run else "updated ", dest, card.free_blocks()))
    say_files(out, labels, a, None)
    if inplace:
        out.write("  backup: %s%s\n" % (backup, " (would be written first)" if a.dry_run else ""))
    if a.dry_run:
        return 0
    if inplace:
        with open(backup, "xb") as f:               # 'x': fails if it appeared meanwhile
            f.write(raw)
            f.flush()
            os.fsync(f.fileno())
        with open(backup, "rb") as f:
            if f.read() != raw:
                raise ToolError("backup does not match the card; card left untouched")
        os.chmod(backup, os.stat(src).st_mode & 0o777)   # a backup of a card with an identity stays private
        atomic_write(dest, bytes(card.img), holds_secret(a))
    else:
        write_new(dest, bytes(card.img), holds_secret(a))
    return 0


def verify_card(path):
    """-> report dict (never contains uid or playKey)."""
    try:
        with open(path, "rb") as f:
            raw = f.read()
    except OSError as e:
        raise ToolError("cannot read %s (%s)" % (path, e.strerror))
    card = C.Card(raw)
    rep = {"path": path, "bytes": len(raw), "megabits": card.size_mbit, "blocks": card.total,
           "encoding": {0: "ANSI", 1: "SJIS"}.get(card.encoding, "unknown (%d)" % card.encoding),
           "free_blocks": card.free_blocks(), "notes": list(card.notes), "problems": card.consistency(), "files": []}
    for e in card.entries():
        item = {"name": e["name"], "game": (e["gamecode"] + e["maker"]).decode("ascii", "replace"),
                "blocks": e["blocks"], "first": e["first"]}
        if e["gamecode"] == C.GAME_CODE and e["maker"] == C.MAKER:
            try:
                data = card.read(e)
            except C.CardError:
                data = None                           # already reported in problems
            if data is not None and e["name"] == F.OPTIONS_NAME:
                item["multishine"], item["content"] = "options", F.describe_options(data)
            elif data is not None and e["name"] == F.ONLINE_NAME:
                item["multishine"], item["content"] = "online", F.describe_online(data)
            elif data is not None and e["name"] == F.CODES_NAME:
                item["multishine"], item["content"] = "codes", F.describe_codes(data)
            elif data is not None and e["name"] == F.IDENTITY_NAME:
                item["multishine"], item["content"] = "identity", F.describe_identity(data)
            elif e["name"] == MELEE_SAVE:
                item["multishine"] = "melee-save"
        rep["files"].append(item)
    rep["melee_save"] = any(f.get("multishine") == "melee-save" for f in rep["files"])
    return rep


def cmd_clear_codes(a, out):
    """Delete the direct-code history file (MultishineCodes) from a card image: the host way to wipe it.  Other files are not touched; the
    in-place form writes a verified backup first, like 'add'."""
    try:
        with open(a.card, "rb") as f:
            raw = f.read()
    except OSError as e:
        raise ToolError("cannot read %s (%s)" % (a.card, e.strerror))
    card = C.Card(raw)
    bad = card.consistency()
    if bad:
        raise ToolError("the card is not consistent, refusing to touch it: " + "; ".join(bad))
    before = C.Card(raw)
    ents = [e for e in card.entries() if e["name"] == F.CODES_NAME and e["gamecode"] == C.GAME_CODE and e["maker"] == C.MAKER]
    if not ents:
        out.write("%s: no %s file on this card, nothing to do\n" % (a.card, F.CODES_NAME))
        return 0
    card.remove(ents[0])
    problems = V.errors(V.validate(bytes(card.img)))
    if problems:
        raise ToolError("internal check failed after the edit: " + "; ".join(problems))
    keep = {(e["name"], bytes(before.read(e))) for e in before.entries() if e["name"] != F.CODES_NAME}
    if keep != {(e["name"], bytes(card.read(e))) for e in card.entries()}:
        raise ToolError("internal check failed: another file changed")
    dest = a.out or a.card
    inplace = os.path.abspath(dest) == os.path.abspath(a.card)
    backup = a.card + BACKUP_SUFFIX
    if inplace and os.path.lexists(backup):
        raise ToolError("backup %s already exists; refusing to overwrite it (move it away first)" % backup)
    if not inplace and os.path.lexists(dest):
        raise ToolError("%s already exists; refusing to overwrite it" % dest)
    out.write("%s%s: %s removed, %d free blocks\n" % ("[dry run] would update " if a.dry_run else "updated ", dest, F.CODES_NAME, card.free_blocks()))
    if a.dry_run:
        return 0
    if inplace:
        with open(backup, "xb") as f:
            f.write(raw)
            f.flush()
            os.fsync(f.fileno())
        atomic_write(dest, bytes(card.img), False)
    else:
        write_new(dest, bytes(card.img), False)
    return 0


def _read_card(path):
    try:
        with open(path, "rb") as f:
            raw = f.read()
    except OSError as e:
        raise ToolError("cannot read %s (%s)" % (path, e.strerror))
    card = C.Card(raw)
    bad = card.consistency()
    if bad:
        raise ToolError("the card is not consistent, refusing to use it: " + "; ".join(bad))
    return raw, card


def _gci_out_dir(a, secret):
    d = os.path.abspath(a.gci_out)
    if secret:
        identity_guard(argparse.Namespace(identity=True), os.path.join(d, "x.gci"))
    return d


def _write_gcis(a, items, out, verb):
    """items = [(entry, gci bytes)].  Never overwrites; files holding a secret are mode 0600."""
    secret = any(G.is_secret(bytes(e[8:40]).split(b"\0")[0].decode("ascii", "replace")) for e, _ in items)
    d = _gci_out_dir(a, secret)
    names = [G.gci_filename(e) for e, _ in items]
    for n in names:
        if os.path.lexists(os.path.join(d, n)):
            raise ToolError("%s already exists; refusing to overwrite it" % os.path.join(d, n))
    for (e, blob), n in zip(items, names):
        nm = bytes(e[8:40]).split(b"\0")[0].decode("ascii", "replace")
        out.write("%s%s (%d blocks)%s\n" % ("[dry run] would write " if a.dry_run else verb + " ", os.path.join(d, n), (len(blob) - G.GCI_ENTRY) // C.BLOCK,
                                           "  SECRET: private file (0600), do not share, upload or cloud-sync it" if G.is_secret(nm) else ""))
        if not a.dry_run:
            write_new(os.path.join(d, n), blob, G.is_secret(nm))


def cmd_export_gci(a, out):
    """One .gci per selected file of a card image; the card is only read."""
    raw, card = _read_card(a.card)
    names = G.resolve_names(a.only) if a.only else []
    names += [n for n in (G.resolve_names(a.name) if a.name else []) if n not in names]
    ents = card.entries()
    if a.all:
        sel = ents
    else:
        if not names:
            raise ToolError("say what to export: --only options,identity,online,codes,bridge,melee, --name FILENAME, or --all")
        sel = []
        for n in names:
            hit = [e for e in ents if e["name"] == n]
            if not hit:
                raise ToolError("%s is not on the card%s" % (n, " (Melee makes its own save on first boot; export it from a card that has one)" if n == G.MELEE_SAVE else ""))
            sel += hit
    if not sel:
        raise ToolError("the card has no files")
    items = [(e["raw"], G.export_gci(card, e)) for e in sel]
    for e in sel:
        if e["name"] == G.MELEE_SAVE and e["gamecode"] == C.GAME_CODE:
            for p in G.melee_problems(card.read(e)):
                out.write("  warning: %s\n" % p)
    _write_gcis(a, items, out, "wrote")
    return 0


def cmd_make_gci(a, out):
    """The Multishine files (and a Melee save taken from a card that has one) as separate .gci files; no card image is made."""
    a.empty = True                                         # only --melee-from-card may be given
    files = collect_files(a)
    items = [(e, bytes(e) + bytes(d)) for _, e, d in files]
    if a.melee_from_card:
        _, src = _read_card(a.melee_from_card)
        m = src.find(G.MELEE_SAVE)
        if m is None:
            raise ToolError("no Melee save on %s; Melee makes one on its first boot, or take it from a card that has one" % a.melee_from_card)
        data = src.read(m)
        bad = G.melee_problems(data)
        if bad:
            raise ToolError("the Melee save on %s is damaged: %s" % (a.melee_from_card, "; ".join(bad)))
        items.append((m["raw"], G.export_gci(src, m)))
    if not items:
        raise ToolError("nothing to write: give --options, --identity, --online-config, --codes and/or --melee-from-card")
    for _, blob in items:
        G.parse_gci(blob)                                  # what we hand out must pass our own import checks
    if G.MELEE_SAVE not in [bytes(e[8:40]).split(b"\0")[0].decode() for e, _ in items]:
        out.write("  note: no Melee save in this set; Melee creates its own on first boot (needs 11 free blocks and a free entry)\n")
    _write_gcis(a, items, out, "wrote")
    say_files(out, [], a, None)
    return 0


def cmd_import_gci(a, out):
    """Put .gci files into a card image: duplicates refused; --replace swaps one (backup first when the card is edited in place)."""
    raw, card = _read_card(a.card)
    infos, files = [], []
    for p in a.gci:
        try:
            with open(p, "rb") as f:
                blob = f.read(C.MAX_BYTES + 1)
        except OSError as e:
            raise ToolError("cannot read %s (%s)" % (p, e.strerror))
        try:
            e, d, info = G.parse_gci(blob)
        except G.GciError as x:
            raise ToolError("%s: %s" % (p, x))
        files.append((info["name"], e, d))
        if info["name"] in [i[0] for i in infos]:
            raise ToolError("%s is given twice" % info["name"])
        infos.append((info["name"], blob))
    room_check(card, files, a.no_room_check, a.replace)
    for name, blob in infos:
        try:
            G.import_gci(card, blob, a.replace, a.ignore_region)
        except G.GciError as x:
            raise ToolError("%s: %s" % (name, x))
    problems = V.errors(V.validate(bytes(card.img)))
    if problems:
        raise ToolError("internal check failed after the edit: " + "; ".join(problems))
    secret = any(G.is_secret(n) for n, _ in infos)
    dest = a.out or a.card
    inplace = os.path.abspath(dest) == os.path.abspath(a.card)
    backup = a.card + BACKUP_SUFFIX
    if secret:
        identity_guard(argparse.Namespace(identity=True), dest)
    if inplace and os.path.lexists(backup):
        raise ToolError("backup %s already exists; refusing to overwrite it (move it away first)" % backup)
    if not inplace and os.path.lexists(dest):
        raise ToolError("%s already exists; refusing to overwrite it" % dest)
    out.write("%s%s: %d free blocks left\n" % ("[dry run] would update " if a.dry_run else "updated ", dest, card.free_blocks()))
    for name, _ in infos:
        out.write("  + %s%s\n" % (name, "  (replaced; Melee's own save: its progress on this card is gone, the backup keeps it)" if a.replace and name == G.MELEE_SAVE else ""))
    if secret:
        out.write("  The card now holds a secret (identity or device secret): do not share, upload or cloud-sync it.\n")
    if inplace:
        out.write("  backup: %s%s\n" % (backup, " (would be written first)" if a.dry_run else ""))
    if a.dry_run:
        return 0
    if inplace:
        with open(backup, "xb") as f:
            f.write(raw)
            f.flush()
            os.fsync(f.fileno())
        with open(backup, "rb") as f:
            if f.read() != raw:
                raise ToolError("backup does not match the card; card left untouched")
        os.chmod(backup, os.stat(a.card).st_mode & 0o777)
        atomic_write(dest, bytes(card.img), secret or bool(os.stat(a.card).st_mode & 0o077 == 0))
    else:
        write_new(dest, bytes(card.img), secret)
    return 0


def _bridge_block(card):
    """-> (entry or None, first block bytes or None)"""
    ent = card.find(F.BRIDGE_NAME)
    if ent is None:
        return None, None
    return ent, bytearray(card.read(ent)[:C.BLOCK])


def cmd_show_net(a, out):
    raw, card = _read_card(a.card)
    ent, blk = _bridge_block(card)
    if ent is None:
        state, cfg = "no-file", {}
    else:
        state, cfg = NC.decode(blk[NC.OFF:NC.OFF + NC.LEN])
    if a.json:
        out.write(json.dumps({"card": a.card, "file": F.BRIDGE_NAME, "state": state, "config": cfg}, indent=1) + "\n")
        return 0
    out.write("%s: %s %s\n" % (a.card, F.BRIDGE_NAME, {"no-file": "is not on the card (no override)", "absent": "has no net-config section (baked addresses are used)",
                                                         "active": "carries a net-config section:", "damaged": "has a DAMAGED net-config section (a dev image ignores it)",
                                                         "invalid": "has an INVALID net-config section (a dev image ignores it)",
                                                         "newer": "has a net-config section of a newer version (ignored)"}[state]))
    for k, label in (("mm_ip", "matchmaker address"), ("status_ip", "status server"), ("status_port", "status port"), ("dns_ip", "DNS server")):
        if cfg.get(k):
            out.write("  %-20s %s\n" % (label, cfg[k]))
    if state == "active":
        out.write("  Only a DEV/LAB image (built with netcfg) reads this; a release image ignores it.\n")
    return 0


def cmd_set_net(a, out):
    """Write, update or remove the dev/lab net-config section in the card file MultishineBridge.  Works on a COPY unless --in-place (then a verified
    .bak is written first, like 'add').  The bridge's cache record and every other file are left byte-identical; a missing MultishineBridge file is created
    (one block, like the console does).  Never touches Melee's own save."""
    raw, before = _read_card(a.card)
    card = C.Card(raw)
    ent, blk = _bridge_block(card)
    old_state, old = ("no-file", {}) if ent is None else NC.decode(blk[NC.OFF:NC.OFF + NC.LEN])
    if ent is not None and old_state == "absent" and any(blk[NC.OFF:NC.OFF + NC.LEN]):
        raise ToolError("the bytes at 0x%X of %s are in use by something that is not a net-config section; refusing to overwrite them" % (NC.OFF, F.BRIDGE_NAME))
    new = {"mm_ip": a.mm_ip, "status_ip": a.status_ip, "status_port": a.status_port, "dns_ip": a.dns_ip}
    if a.clear:
        if any(v is not None for v in new.values()):
            raise ToolError("--clear takes no addresses")
        sec, cfg = bytes(NC.LEN), {}
    else:
        cfg = NC.merge(old if old_state == "active" else {}, new)
        try:
            sec = NC.encode(cfg)
        except NC.NetCfgError as e:
            raise ToolError(str(e))
    if a.clear and ent is None:
        out.write("%s: no %s file on this card, nothing to clear\n" % (a.card, F.BRIDGE_NAME))
        return 0
    if ent is None:
        blk = bytearray(C.BLOCK)
        blk[0:10] = b"Multishine"
        blk[32:32 + len("Slippi bridge status")] = b"Slippi bridge status"
        entry = bytearray(F.P.make_dir_entry(F.BRIDGE_NAME, 0, 1, F.P.PERM_PUBLIC, a.mtime))
        struct.pack_into(">I", entry, 0x2C, 0xFFFFFFFF)       # no icon, no banner (same as the console-created file)
        struct.pack_into(">HH", entry, 0x30, 0, 0)
        blk[NC.OFF:NC.OFF + NC.LEN] = sec
        room_check(card, [(F.BRIDGE_NAME, bytes(entry), bytes(blk))], a.no_room_check)
        card.add(bytes(entry), bytes(blk))
    else:
        first = card.chain(ent)[0]
        o = first * C.BLOCK + NC.OFF
        card.img[o:o + NC.LEN] = sec
    problems = V.errors(V.validate(bytes(card.img)))
    if problems:
        raise ToolError("internal check failed after the edit: " + "; ".join(problems))
    check_untouched(before, card, (F.BRIDGE_NAME,))
    nb = card.find(F.BRIDGE_NAME)
    if card.read(nb)[:NC.OFF] != (bytes(blk[:NC.OFF]) if ent is None else C.Card(raw).read(ent)[:NC.OFF]) or card.read(nb)[NC.OFF + NC.LEN:] != (bytes(blk[NC.OFF + NC.LEN:]) if ent is None else C.Card(raw).read(ent)[NC.OFF + NC.LEN:]):
        raise ToolError("internal check failed: bytes of %s outside the section changed" % F.BRIDGE_NAME)
    secret = card.find(F.IDENTITY_NAME) is not None or card.find(F.ONLINE_NAME) is not None
    inplace = bool(a.in_place)
    if inplace and a.out:
        raise ToolError("give --out or --in-place, not both")
    dest = a.card if inplace else (a.out or "%s.netcfg%s" % os.path.splitext(a.card))
    backup = a.card + BACKUP_SUFFIX
    if secret:
        identity_guard(argparse.Namespace(identity=True), dest)
    if inplace and os.path.lexists(backup):
        raise ToolError("backup %s already exists; refusing to overwrite it (move it away first)" % backup)
    if not inplace and os.path.lexists(dest):
        raise ToolError("%s already exists; refusing to overwrite it" % dest)
    out.write("%s%s: %s %s\n" % ("[dry run] would write " if a.dry_run else "wrote ", dest, F.BRIDGE_NAME, "created" if ent is None else "updated"))
    if a.clear:
        out.write("  net-config section removed\n")
    for k, label in (("mm_ip", "matchmaker address"), ("status_ip", "status server"), ("status_port", "status port"), ("dns_ip", "DNS server")):
        if cfg.get(k):
            out.write("  %-20s %s\n" % (label, cfg[k]))
    out.write("  DEV/LAB images only: a release image ignores this section. The matchmaker name (mm.slippi.gg) and port never come from the card.\n")
    if secret:
        out.write("  This card holds an identity or device secret: do not share, upload or cloud-sync it.\n")
    if inplace:
        out.write("  backup: %s%s\n" % (backup, " (would be written first)" if a.dry_run else ""))
    if a.dry_run:
        return 0
    if inplace:
        with open(backup, "xb") as f:
            f.write(raw)
            f.flush()
            os.fsync(f.fileno())
        with open(backup, "rb") as f:
            if f.read() != raw:
                raise ToolError("backup does not match the card; card left untouched")
        os.chmod(backup, os.stat(a.card).st_mode & 0o777)
        atomic_write(dest, bytes(card.img), secret or bool(os.stat(a.card).st_mode & 0o077 == 0))
    else:
        write_new(dest, bytes(card.img), secret or bool(os.stat(a.card).st_mode & 0o077 == 0))
    return 0


def cmd_validate_config(a, out):
    try:
        with open(a.config, "r", encoding="utf-8") as f:
            cfg = CFG.parse(json.load(f), a.release)
    except (OSError, ValueError) as e:
        raise ToolError("cannot read %s as JSON (%s)" % (a.config, getattr(e, "strerror", None) or e))
    except CFG.ConfigError as e:
        raise ToolError("%s: %s" % (a.config, e))
    out.write("%s: valid%s (options %d, network %s, online %s)\n" % (a.config, " for a release image" if a.release else "", len(cfg["options"]),
                                                                  "dev-only section" if cfg["network"] else "-", cfg["online"] or "-"))
    return 0


def _online_slots(card):
    ent = card.find(F.ONLINE_NAME)
    if ent is None:
        return None
    return F.onfile.slots_view(bytes(card.read(ent)[F.onfile.PAYLOAD_OFF:F.onfile.PAYLOAD_OFF + F.onfile.SL_END]))


def _options_state(card):
    ent = card.find(F.OPTIONS_NAME)
    if ent is None:
        return None, "no-file", None
    blk = card.read(ent)[:C.BLOCK]
    st, vals = CFG.read_options(blk)
    return ent, st, vals


def _slots_text(sl):
    """Identity slots of the online setup, never a secret."""
    if sl is None:
        return "      identity slots: none yet (one identity, from the old layout: the console moves it into the slot of the server it was linked on at the first link)\n"
    return "      identity slots: " + "; ".join("%s %s" % (x["env"], ("%s (%s) device %s" % (x["code"], x["name"], x["device"][:8])) if x["linked"] else "empty") for x in sl) + "\n"


def cmd_show_config(a, out):
    raw, card = _read_card(a.card)
    _, ost, vals = _options_state(card)
    bent, bblk = _bridge_block(card)
    nst, ncfg = ("no-file", {}) if bent is None else NC.decode(bblk[NC.OFF:NC.OFF + NC.LEN])
    rep = {"card": a.card, "options": {"file": F.OPTIONS_NAME, "state": ost, "values": CFG.describe_options(vals) if vals else {}},
           "network": {"file": F.BRIDGE_NAME, "state": nst, "dev_only": True, "config": ncfg},
           "online": {"file": F.ONLINE_NAME, "present": card.find(F.ONLINE_NAME) is not None, "slots": _online_slots(card)},
           "identity": {"file": F.IDENTITY_NAME, "present": card.find(F.IDENTITY_NAME) is not None}}
    if a.json:
        out.write(json.dumps(rep, indent=1) + "\n")
        return 0
    out.write("%s\n  options (%s): %s\n" % (a.card, F.OPTIONS_NAME, ost))
    for k, v in rep["options"]["values"].items():
        out.write("    %-20s %s\n" % (k, "(baked default)" if v is None else v))
    out.write("  network, DEV/LAB only (%s): %s\n" % (F.BRIDGE_NAME, nst))
    for k, v in ncfg.items():
        if v:
            out.write("    %-20s %s\n" % (k, v))
    out.write("  online setup (%s): %s\n  identity (%s): %s (contents never shown)\n" % (
        F.ONLINE_NAME, "present" if rep["online"]["present"] else "absent", F.IDENTITY_NAME, "present" if rep["identity"]["present"] else "absent"))
    if rep["online"]["present"]:
        out.write(_slots_text(rep["online"]["slots"]).replace("      ", "  ", 1))
    return 0


def cmd_set_config(a, out):
    """Apply one config file to a card image (copy by default; --in-place with a verified .bak).  Options: value bytes edited in place (file created when
    missing).  Network: the dev-only section (same rules as set-net).  Online: places MultishineOnline (--replace-online to swap).  Nothing else is touched."""
    try:
        with open(a.config, "r", encoding="utf-8") as f:
            cfg = CFG.parse(json.load(f), a.release)
    except (OSError, ValueError) as e:
        raise ToolError("cannot read %s as JSON (%s)" % (a.config, getattr(e, "strerror", None) or e))
    except CFG.ConfigError as e:
        raise ToolError("%s: %s" % (a.config, e))
    raw, before = _read_card(a.card)
    card = C.Card(raw)
    lines, extra_names = [], [F.OPTIONS_NAME, F.BRIDGE_NAME]
    if cfg["options"]:
        ent, st, vals = _options_state(card)
        if st in ("damaged", "newer"):
            raise ToolError("%s on this card is %s; refusing to edit it (a newer file is never overwritten, a damaged one must be removed first)" % (F.OPTIONS_NAME, st))
        if ent is None:
            gci = F.optfile.build_gci(F.optfile.pack(CFG.apply_options([optfile_unset()] * F.optfile.NVAL, cfg["options"])), mtime=a.mtime)
            room_check(card, [(F.OPTIONS_NAME, gci[:G.GCI_ENTRY], gci[G.GCI_ENTRY:])], a.no_room_check)
            card.add(gci[:G.GCI_ENTRY], gci[G.GCI_ENTRY:])
            lines.append("%s created" % F.OPTIONS_NAME)
        else:
            blk = CFG.write_options(card.read(ent)[:C.BLOCK], CFG.apply_options(vals, cfg["options"]))
            o = card.chain(ent)[0] * C.BLOCK
            card.img[o:o + C.BLOCK] = blk
            lines.append("%s updated (only the value bytes and the CRC)" % F.OPTIONS_NAME)
        for k, v in cfg["options"].items():
            lines.append("  %-20s %s" % (k, "baked default" if v is None else v))
    if cfg["network"]:
        bent, bblk = _bridge_block(card)
        old_state, old = ("no-file", {}) if bent is None else NC.decode(bblk[NC.OFF:NC.OFF + NC.LEN])
        if bent is not None and old_state == "absent" and any(bblk[NC.OFF:NC.OFF + NC.LEN]):
            raise ToolError("the bytes at 0x%X of %s are in use by something else; refusing to overwrite them" % (NC.OFF, F.BRIDGE_NAME))
        sec = NC.encode(NC.merge(old if old_state == "active" else {}, cfg["network"]))
        if bent is None:
            nb = bytearray(C.BLOCK)
            nb[0:10] = b"Multishine"
            nb[32:32 + len("Slippi bridge status")] = b"Slippi bridge status"
            nb[NC.OFF:NC.OFF + NC.LEN] = sec
            entry = bytearray(F.P.make_dir_entry(F.BRIDGE_NAME, 0, 1, F.P.PERM_PUBLIC, a.mtime))
            struct.pack_into(">I", entry, 0x2C, 0xFFFFFFFF)
            struct.pack_into(">HH", entry, 0x30, 0, 0)
            room_check(card, [(F.BRIDGE_NAME, bytes(entry), bytes(nb))], a.no_room_check)
            card.add(bytes(entry), bytes(nb))
        else:
            o = card.chain(bent)[0] * C.BLOCK + NC.OFF
            card.img[o:o + NC.LEN] = sec
        lines.append("%s net-config section written (DEV/LAB images only)" % F.BRIDGE_NAME)
    secret = False
    if cfg["online"]:
        e, d, code, server = F.online_file(cfg["online"], a.mtime, cfg.get("online_server"))
        room_check(card, [(F.ONLINE_NAME, e, d)], a.no_room_check, a.replace_online)
        place(card, [(F.ONLINE_NAME, e, d)], a.replace_online)
        lines.append("%s placed (server %s, connect code %s)" % (F.ONLINE_NAME, server, code))
        secret = True
        extra_names.append(F.ONLINE_NAME)
    secret = secret or card.find(F.IDENTITY_NAME) is not None or card.find(F.ONLINE_NAME) is not None
    problems = V.errors(V.validate(bytes(card.img)))
    if problems:
        raise ToolError("internal check failed after the edit: " + "; ".join(problems))
    check_untouched(before, card, tuple(extra_names))
    _commit(a, out, raw, card, secret, lines)
    return 0


def optfile_unset():
    return CFG.optfile.UNSET


def _commit(a, out, raw, card, secret, lines):
    inplace = bool(a.in_place)
    if inplace and a.out:
        raise ToolError("give --out or --in-place, not both")
    dest = a.card if inplace else (a.out or "%s.config%s" % os.path.splitext(a.card))
    backup = a.card + BACKUP_SUFFIX
    if secret:
        identity_guard(argparse.Namespace(identity=True), dest)
    if inplace and os.path.lexists(backup):
        raise ToolError("backup %s already exists; refusing to overwrite it (move it away first)" % backup)
    if not inplace and os.path.lexists(dest):
        raise ToolError("%s already exists; refusing to overwrite it" % dest)
    out.write("%s%s\n" % ("[dry run] would write " if a.dry_run else "wrote ", dest))
    for l in lines:
        out.write("  %s\n" % l)
    if secret:
        out.write("  This card holds an identity or device secret: do not share, upload or cloud-sync it.\n")
    if inplace:
        out.write("  backup: %s%s\n" % (backup, " (would be written first)" if a.dry_run else ""))
    if a.dry_run:
        return
    private = secret or bool(os.stat(a.card).st_mode & 0o077 == 0)
    if inplace:
        with open(backup, "xb") as f:
            f.write(raw)
            f.flush()
            os.fsync(f.fileno())
        with open(backup, "rb") as f:
            if f.read() != raw:
                raise ToolError("backup does not match the card; card left untouched")
        os.chmod(backup, os.stat(a.card).st_mode & 0o777)
        atomic_write(dest, bytes(card.img), private)
    else:
        write_new(dest, bytes(card.img), private)


def cmd_verify(a, out):
    rep = verify_card(a.card)
    bad_files = any(f.get("content") and not f["content"]["ok"] for f in rep["files"])
    if a.json:
        out.write(json.dumps(rep, indent=1) + "\n")
        return 1 if rep["problems"] or bad_files else 0
    out.write("%s: %d bytes, %d Mbit, %d blocks, encoding %s, %d free blocks\n" % (
        rep["path"], rep["bytes"], rep["megabits"], rep["blocks"], rep["encoding"], rep["free_blocks"]))
    for n in rep["notes"]:
        out.write("  note: %s\n" % n)
    for f in rep["files"]:
        out.write("  %-6s %-32s %3d blocks, first %d\n" % (f["game"], f["name"], f["blocks"], f["first"]))
        c = f.get("content")
        if f.get("multishine") == "options":
            if c["ok"]:
                out.write("      options: %s\n" % ", ".join("%s=%s" % (k, "default" if v is None else v) for k, v in c["options"].items()))
                out.write("      quick-chat texts: %s\n" % ("custom table" if c["chat"] else "none (build defaults)"))
            else:
                out.write("      INVALID options file: %s\n" % c["error"])
        elif f.get("multishine") == "online":
            if c["ok"]:
                out.write("      online setup: %s (%s) at %s%s, key %s, ranked %s, unranked %s; the device secret is not shown\n" % (
                    c["code"], c["name"], c["server"], (" (name %s, resolved by DNS at menu time when the address is 0)" % c["server_host"]) if c.get("server_host") else "", c["pin"], "yes" if c["ranked"] else "no", "yes" if c["unranked"] else "no"))
            else:
                out.write("      online setup file: %s\n" % c["error"])
            if c["ok"]:
                out.write(_slots_text(c.get("slots")))
        elif f.get("multishine") == "codes":
            if c["ok"]:
                out.write("      direct codes (newest first): %s\n" % (", ".join(c["codes"]) or "none"))
            else:
                out.write("      codes file: %s\n" % c["error"])
        elif f.get("multishine") == "identity":
            if c["ok"]:
                out.write("      identity: %s (%s); playKey and uid not shown\n" % (c["connectCode"], c["displayName"]))
            else:
                out.write("      INVALID identity file: %s\n" % c["error"])
    out.write("  Melee save present: %s\n" % ("yes" if rep["melee_save"] else "no"))
    for p in rep["problems"]:
        out.write("  PROBLEM: %s\n" % p)
    bad = bool(rep["problems"] or bad_files)
    out.write("  result: %s\n" % ("card has problems" if bad else "card is consistent"))
    return 1 if bad else 0


def cmd_validate(a, out):
    try:
        with open(a.card, "rb") as f:
            raw = f.read()
    except OSError as e:
        raise ToolError("cannot read %s (%s)" % (a.card, e.strerror))
    res = V.validate(raw)
    for sev, t in res:
        out.write("  %s: %s\n" % (sev.upper(), t))
    bad = bool(V.errors(res))
    out.write("%s: %s\n" % (a.card, "INVALID" if bad else "structurally valid (a loader file's function is not checked)"))
    return 1 if bad else 0


def cmd_stage2(a, out):
    try:
        rec = L.stage2_record(a.file, a.sd_path)
    except L.LoaderError as x:
        raise ToolError(str(x))
    out.write(L.record_json(rec))
    return 0


def cmd_template(a, out):
    out.write(json.dumps(F.options_template(), indent=2) + "\n")
    return 0


def parser():
    ap = argparse.ArgumentParser(prog="vmc_builder", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)

    def common(p):
        p.add_argument("--options", help="options JSON (see 'template'); places MultishineOptions")
        p.add_argument("--chat", help="quick-chat JSON stored inside the options file")
        p.add_argument("--identity", help="YOUR OWN Slippi user.json (the path is never searched for); places MultishineIdentity")
        p.add_argument("--online-config", help="online setup JSON (offline/multishine/menus/on/onfile.py): server, pinned key, device identity; places MultishineOnline (a secret)")
        p.add_argument("--codes", help="comma-separated direct connect codes (newest first) to pre-fill MultishineCodes; public data")
        p.add_argument("--loader-gci", help="an opaque stage-1 loader GCI you compiled yourself (structure and size checked, placed as is)")
        p.add_argument("--save-gci", help="a community card GCI named like Melee's own save (opaque; it takes the slot of Melee's save, so use a fresh card)")
        p.add_argument("--mtime", type=int, help="file time stamp (unix seconds), default now; for reproducible images")
        p.add_argument("--dry-run", action="store_true", help="check everything and report, write nothing")
        p.add_argument("--no-room-check", action="store_true", help="skip the keep-room-for-Melee's-save rule")
    c = sp.add_parser("create", help="new card image")
    common(c)
    c.add_argument("--sd-root", help="SD card root; writes swiss/saves/MemoryCard<slot>.<region>.raw below it")
    c.add_argument("--out", help="explicit output path instead of the Swiss path scheme")
    c.add_argument("--empty", action="store_true", help="create a healthy card with no files at all (Melee makes its own save on first boot)")
    c.add_argument("--slot", choices=SLOTS, default="A")
    c.add_argument("--region", choices=REGIONS, default="USA")
    c.add_argument("--size", choices=sorted(C.SIZES_MIB, key=float), default="16", help="card size in MiB (default 16 = 2043 blocks)")
    ad = sp.add_parser("add", help="add the files to a card image you supply")
    common(ad)
    ad.add_argument("--card", required=True, help="the existing raw card image")
    ad.add_argument("--out", help="write a new image here and leave --card untouched (no backup then)")
    ad.add_argument("--replace", action="store_true", help="replace our file if it is already on the card")
    ex = sp.add_parser("export-gci", help="write single files of a card image as standard .gci files")
    ex.add_argument("--card", required=True)
    ex.add_argument("--only", help="comma list of options,identity,online,codes,bridge,melee")
    ex.add_argument("--name", help="exact file name(s) on the card, comma separated")
    ex.add_argument("--all", action="store_true", help="every file on the card")
    ex.add_argument("--gci-out", required=True, help="output folder (created; existing files are never overwritten)")
    ex.add_argument("--dry-run", action="store_true")
    mg = sp.add_parser("make-gci", help="write the Multishine files as separate .gci files (no card image)")
    common(mg)
    mg.add_argument("--gci-out", required=True, help="output folder (created; existing files are never overwritten)")
    mg.add_argument("--melee-from-card", help="a card image that already has Melee's save: that one file is extracted (checksums verified)")
    ig = sp.add_parser("import-gci", help="put .gci files into a card image")
    ig.add_argument("gci", nargs="+")
    ig.add_argument("--card", required=True)
    ig.add_argument("--out", help="write a new image here and leave --card untouched (no backup then)")
    ig.add_argument("--replace", action="store_true", help="swap a file that is already on the card (verified backup first)")
    ig.add_argument("--ignore-region", action="store_true", help="skip the region/encoding check")
    ig.add_argument("--no-room-check", action="store_true")
    ig.add_argument("--dry-run", action="store_true")
    cc = sp.add_parser("clear-codes", help="delete the direct-code history file (MultishineCodes) from a card image")
    cc.add_argument("--card", required=True)
    cc.add_argument("--out", help="write a new image here and leave --card untouched (no backup then)")
    cc.add_argument("--dry-run", action="store_true")
    sn = sp.add_parser("set-net", help="DEV/LAB images only: write/update/remove the network-address section inside MultishineBridge")
    sn.add_argument("--card", required=True)
    sn.add_argument("--out", help="write a new image here (default: <card>.netcfg.raw; the original is never touched)")
    sn.add_argument("--in-place", action="store_true", help="edit --card itself (verified .bak first)")
    sn.add_argument("--mm-ip", help="matchmaker IPv4 address (skips the DNS lookup; the name and port stay baked)")
    sn.add_argument("--status-ip", help="IGNORED by the console since 2026-10-11 (the status server was removed from the Slippi path); stored for old command lines only")
    sn.add_argument("--status-port", type=int, help="IGNORED by the console since 2026-10-11 (see --status-ip)")
    sn.add_argument("--dns-ip", help="DNS server IPv4 address")
    sn.add_argument("--clear", action="store_true", help="remove the section")
    sn.add_argument("--mtime", type=int, help="file time stamp when the file has to be created")
    sn.add_argument("--dry-run", action="store_true")
    sn.add_argument("--no-room-check", action="store_true")
    sc = sp.add_parser("set-config", help="apply one config file (options, dev-only network, online setup) to a card image")
    sc.add_argument("--card", required=True)
    sc.add_argument("--config", required=True, help='JSON {"format": "multishine-card-config", "version": 1, "options": {...}, "network": {...}, "online": "setup.json"}')
    sc.add_argument("--out", help="write a new image here (default: <card>.config.raw)")
    sc.add_argument("--in-place", action="store_true", help="edit --card itself (verified .bak first)")
    sc.add_argument("--release", action="store_true", help="refuse the dev-only sections")
    sc.add_argument("--replace-online", action="store_true", help="swap MultishineOnline if it is already on the card")
    sc.add_argument("--mtime", type=int)
    sc.add_argument("--dry-run", action="store_true")
    sc.add_argument("--no-room-check", action="store_true")
    shc = sp.add_parser("show-config", help="print the options, network section and which files exist (no secrets)")
    shc.add_argument("card")
    shc.add_argument("--json", action="store_true")
    vc = sp.add_parser("validate-config", help="check a config JSON against the schema")
    vc.add_argument("config")
    vc.add_argument("--release", action="store_true")
    shn = sp.add_parser("show-net", help="print the net-config section of a card image")
    shn.add_argument("card")
    shn.add_argument("--json", action="store_true")
    v = sp.add_parser("verify", help="parse a card and report its contents")
    v.add_argument("card")
    v.add_argument("--json", action="store_true")
    va = sp.add_parser("validate", help="strict structural check of a card image (Dolphin-free)")
    va.add_argument("card")
    s2 = sp.add_parser("stage2-record", help="print the stage-2 record (size, sha256, SD path) for a stage-2 file")
    s2.add_argument("file")
    s2.add_argument("--sd-path", default="/multishine/stage2.bin")
    sp.add_parser("template", help="print a starting options JSON")
    return ap


def main(argv=None, out=None, err=None):
    out, err = out or sys.stdout, err or sys.stderr
    a = parser().parse_args(argv)
    try:
        return {"create": cmd_create, "add": cmd_add, "verify": cmd_verify, "clear-codes": cmd_clear_codes, "set-net": cmd_set_net, "set-config": cmd_set_config, "show-config": cmd_show_config, "validate-config": cmd_validate_config, "show-net": cmd_show_net, "export-gci": cmd_export_gci, "make-gci": cmd_make_gci, "import-gci": cmd_import_gci, "validate": cmd_validate, "stage2-record": cmd_stage2, "template": cmd_template}[a.cmd](a, out)
    except (ToolError, C.CardError, F.FileError, L.LoaderError, G.GciError, NC.NetCfgError, CFG.ConfigError) as e:
        err.write("vmc_builder: %s\n" % e)
        return 1


if __name__ == "__main__":
    sys.exit(main())
