#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Card package builder.  Re-runnable; writes only below build/offline/cardpkg/.

  build_cardpkg.py s1                       stage 1 as proven on the console: exploit + the offline codeset inside the save (ms_stage1_silent.mgc)
  build_cardpkg.py s2 [--profile NAME]      exploit + ~2 KB card loader in the save; the codeset (04/C2) is the card file MultishineCode
  options: --stage-sd DIR (copy the card to DIR/swiss/saves/MemoryCardA.USA.raw)  --codes FILE (default build/offline/codeset/<profile>.bin)  --retail-iso ISO (guard CRC of the retail words, build host only)
           --mtime T (default 1780000000, reproducible)  --out DIR
Outputs in DIR (default build/offline/cardpkg/<stage>): stage1.gci [code.gci] card.raw (sizes, SHA-1, SHA-256).
The compiler is the pinned third_party/melee-gci-compiler (needs Python >= 3.10: python3.14 from Homebrew is used if python3 is older)."""
import argparse, hashlib, json, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
MGC = os.path.join(REPO, "third_party", "melee-gci-compiler", "melee_gci_compiler.py")
VMC = os.path.join(REPO, "tools", "vmc_builder", "vmc_builder.py")
DEFAULT_ISO = "/Applications/Super Smash Bros. Melee (USA) (En,Ja) (v1.02).iso"
sys.path.insert(0, os.path.join(REPO, "offline"))
import projname  # noqa: E402
GENERIC = os.environ.get("CL_LAYOUT", "") in ("generic", "low")      # Cubeboot generic-VMC-safe layout: list/loader/work area below 0x817E4000
LIST = {"generic": 0x817E0000, "low": 0x817C0000}.get(os.environ.get("CL_LAYOUT", ""), 0x817F4000)
CL_BASE = LIST + 0x40          # the list (copy table destination) + 7 framing lines (0x38) + C0 header (8)


def sh(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("command failed: %s" % " ".join(cmd[:3]))
    return r.stdout + r.stderr


def py310():
    if sys.version_info >= (3, 10):
        return sys.executable
    for c in ("python3.14", "/opt/homebrew/bin/python3.13", "/opt/homebrew/bin/python3.12", "/opt/homebrew/bin/python3.11", shutil.which("python3.12") or ""):
        if c and os.path.exists(c):
            return c
    raise SystemExit("MGC needs Python >= 3.10")


def digests(path):
    b = open(path, "rb").read()
    return {"bytes": len(b), "sha1": hashlib.sha1(b).hexdigest(), "sha256": hashlib.sha256(b).hexdigest()}


def compile_mgc(mgc, out_gci):
    sh([py310(), MGC, "-o", out_gci, mgc])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("stage", choices=("s1", "s2"))
    ap.add_argument("--profile", default="cardloader")
    ap.add_argument("--codes", help="Gecko list (04/06/C2) to export; default build/offline/codeset/<profile>.bin"); ap.add_argument("--retail-iso", default=DEFAULT_ISO)
    ap.add_argument("--exit-mode", help="card_export.py --exit-mode (0 = the exploit ends on the title screen, CARD_RC1_ROUTE.md section 19)")
    ap.add_argument("--goto-scene", help="card_export.py --goto-scene");ap.add_argument("--oneshot", action="append", default=[], help="boot-time hook address whose body the loader entry runs once (card_export.py --oneshot)"); ap.add_argument("--no-scratch30", action="store_true", help="OLD entry stub (r30 = the loader's slot number when the hook body runs: the Slippi Online Scene body then stores to address 0 and hangs the console; only to reproduce old cards, docs/design/CARD_ENTRY_STORE.md)"); ap.add_argument("--restore-manifest", help="card_export.py --restore-manifest: the payload manifest.json whose dol_words (widescreen group default OFF) get no patch"); ap.add_argument("--hole-base", help="with --codes: the T2 base the payload was built for (card_payload.py)"); ap.add_argument("--hole-size")
    ap.add_argument("--verify", choices=("dev", "release"), help="OTA stage-0 loader (docs/design/OTA_UPDATE.md): checks the Ed25519 signature, ABI, build_seq floor and falls back to MultishineCodeB. "
                    "dev: pins the public test key (RFC 8032 test 1), accepts unsigned files, display name 'DEV BUILD'; release: needs --pubkey (64 hex), always verifies")
    ap.add_argument("--pubkey"); ap.add_argument("--pubkey2"); ap.add_argument("--min-seq", type=int, help="loader constant build_seq floor (default 1)")
    ap.add_argument("--sign-key", help="sign the code file with this key file (tools/card/mscf_sign.py keygen)"); ap.add_argument("--sign-dev", action="store_true", help="sign with the published dev test key (watermarks the card-manager comment)")
    ap.add_argument("--seq", type=int, help="set build_seq of the signed code file"); ap.add_argument("--code-gci", help="use this finished code file (skip card_export; still signed when --sign-* is given)")
    ap.add_argument("--omit-code", action="store_true", help="leave MultishineCode off the card (tests: only the backup file present)"); ap.add_argument("--backup-code", help="a code file to add as MultishineCodeB (the previous version)"); ap.add_argument("--state-seq", type=int, help="add the floor file MultishineState with this build_seq"); ap.add_argument("--pack-gci", help="add this disc-file pack (tools/card/build_pack.py, MultishinePack.gci; docs/design/CARD_RC1_ROUTE.md section 10)")
    ap.add_argument("--compress", action="store_true", help="MSLZ-compressed code file (MSCF v2) and a loader built with CL_COMP=1 (decoder in the loader; docs/design/CARD_COMPRESSION.md). Without it the loader and the code file are the uncompressed ones")
    ap.add_argument("--standard-melee", action="store_true", help="s2 only: put THE standard MultishineMelee (tools/card/standard_save.py: everything unlocked, 999 coins, world records, items off, our art; replaces --coins/--records) on the card; needs --melee-template or $MS_MELEE_TEMPLATE")
    ap.add_argument("--replace-melee", action="store_true", help="with --standard-melee: replace a MultishineMelee the card already has (otherwise that is refused)")
    ap.add_argument("--records-sets", default="target,hrc,tenman,events", help="with --records: which sets (target,hrc,tenman,events; default all four, docs/design/MELEE_RECORDS.md)")
    ap.add_argument("--records", action="store_true", help="s2 only: write the Melee single-player records (tools/card/melee_records.py, section 18.7) into MultishineMelee (after --coins; the template of --coins/--melee-template is used when the card has no Melee save)")
    ap.add_argument("--coins", type=int, help="s2 only: put a ready MultishineMelee with this many Trophy Lottery coins (0..999) on the card instead of the empty save Melee makes at its first boot (tools/card/melee_coins.py, docs/design/CARD_RC1_ROUTE.md section 18; needs --melee-template or $MS_MELEE_TEMPLATE)"); ap.add_argument("--melee-template", help="a fresh Melee-made save (.gci or a card) for --coins (tools/card/melee_coins.py capture)")
    ap.add_argument("--options"); ap.add_argument("--chat"); ap.add_argument("--online-config"); ap.add_argument("--identity", help="YOUR OWN user.json (never searched for)")
    ap.add_argument("--stage-sd", help="also write the card as DIR/swiss/saves/MemoryCardA.USA.raw (the exact Swiss path and name; nothing is copied to a real SD)"); ap.add_argument("--cl-test", help="diagnostic loader build (CL_TEST=1 no card access, 2 no heap claim); never for a release"); ap.add_argument("--mtime", type=int, default=1780000000); ap.add_argument("--out"); ap.add_argument("--rebuild-codeset", action="store_true")
    a = ap.parse_args()
    out = os.path.abspath(a.out or os.path.join(REPO, "build", "offline", "cardpkg", a.stage)); os.makedirs(out, exist_ok=True)
    if a.rebuild_codeset:
        sh(["bash", os.path.join(REPO, "offline", "codeset", "build_codeset.sh"), "--profile", a.profile])
    pkg = {"stage": a.stage, "profile": a.profile, "files": {}}
    gci1 = os.path.join(out, "stage1.gci"); files = [gci1]
    if a.stage == "s1":
        compile_mgc(os.path.join(HERE, "ms_stage1_silent.mgc"), gci1)
    else:
        venv = {"CL_COMP": "1"} if a.compress else {}
        if a.verify:
            venv["CL_VERIFY"] = a.verify
            for k, v in (("CL_PUBKEY", a.pubkey), ("CL_PUBKEY2", a.pubkey2), ("CL_MIN_SEQ", a.min_seq)):
                if v:
                    venv[k] = str(v)
        cl = sh(["bash", os.path.join(HERE, "cl", "build_cl.sh"), "0x%08X" % CL_BASE], env=dict(os.environ, OUT=os.path.join(out, "cl"), CL_FILE_NAME=projname.SHORT + "Code", **venv, **({"CL_TEST": a.cl_test} if a.cl_test else {})))
        b = open(os.path.join(out, "cl", "cl.bin"), "rb").read()
        b += b"\x4e\x80\x00\x20" * 0 + b"\0" * (-len(b) % 8)
        clpad = os.path.join(out, "cl", "cl_c0.bin"); open(clpad, "wb").write(b)
        t = open(os.path.join(HERE, "ms_stage1_loader.mgc.in")).read()
        rel = os.path.relpath(REPO, out)
        pad = lambda text, n: " ".join("%02x" % c for c in text.encode().ljust(n, b"\0")[:n])
        sname, dname = projname.SHORT + "Melee", projname.SHORT
        if a.verify == "dev":
            dname = "DEV BUILD"         # shown by the card manager and Melee's own save screens: a dev loader is never mistaken for a release one
        if len(sname) > 23 or len(dname) > 11:
            raise SystemExit("project name too long for the loader's save names (internal <= 23, displayed <= 11 characters)")
        # our own Gecko list runner (ch/ms_codehandler.S, written from the public code type description; replaces the third-party codehandler.bin, THIRD_PARTY_NOTICES C3).  Word 0 of the image is the list address.
        sh(["bash", os.path.join(HERE, "ch", "build_ch.sh"), "0x%08X" % LIST], env=dict(os.environ, OUT=os.path.join(out, "ch")))
        handler = open(os.path.join(out, "ch", "ms_codehandler.bin"), "rb").read()
        assert len(handler) == 0xBA8
        copyfix = "!loc 80460a8c\n%08x" % LIST if GENERIC else ""     # the copy table's third record (the list's destination) follows LIST in the generic/low layouts
        hpath = os.path.join(out, "cl", "ms_codehandler.bin"); open(hpath, "wb").write(handler)
        t = t.replace("@COPYFIX@", copyfix).replace("@HANDLER@", os.path.relpath(hpath, out)).replace("@LIST@", "%08x" % LIST)
        t = t.replace("@SAVENAME@", pad(sname, 0x18)).replace("@DISPNAME@", pad(dname, 0x0c))
        t = t.replace("../../", rel + "/").replace("@CLBIN@", os.path.relpath(clpad, out)).replace("@CLLINES@", "%08x" % (len(b) // 8))
        mgc = os.path.join(out, "stage1_loader.mgc"); open(mgc, "w").write(t)
        compile_mgc(mgc, gci1)
        pkg["loader"] = {"base": "0x%08X" % CL_BASE, "bytes": len(b), "verify": a.verify or "off", "comp": bool(a.compress)}
        codes = a.codes or os.path.join(REPO, "build", "offline", "codeset", a.profile + ".bin")
        code = os.path.join(out, "code.gci"); files.append(code)
        if not (a.retail_iso and os.path.exists(a.retail_iso)):
            raise SystemExit("card_export needs the retail ISO (--retail-iso): the baker reads the retail DOL on this build host")
        cmd = [sys.executable, "-c", "pass"] if a.code_gci else [sys.executable, os.path.join(HERE, "card_export.py"), codes, "--retail-iso", a.retail_iso, "--out", code, "--mtime", str(a.mtime),
               "--report", os.path.join(out, "code_report.json")]
        if a.goto_scene:
            cmd += ["--goto-scene", a.goto_scene]
        if a.exit_mode is not None:
            cmd += ["--exit-mode", a.exit_mode]
        for o in a.oneshot:
            cmd += ["--oneshot", o]
        if a.no_scratch30:
            cmd += ["--no-scratch30"]
        if a.restore_manifest:
            cmd += ["--restore-manifest", a.restore_manifest]
        if a.hole_base:
            cmd += ["--hole-base", a.hole_base]
        if a.hole_size:
            cmd += ["--hole-size", a.hole_size]
        if a.compress and not a.code_gci:
            cmd += ["--compress"]
        pkg["code_file"] = sh(cmd).strip()
        if a.code_gci:
            shutil.copyfile(a.code_gci, code)
        if a.sign_key or a.sign_dev or a.seq:
            sys.path.insert(0, os.path.join(REPO, "tools", "card"))
            import mscf_sign as MS
            seed = MS.DEV_SEED if a.sign_dev else (MS.load_seed(a.sign_key) if a.sign_key else None)
            raw = open(code, "rb").read()
            if a.sign_dev or a.verify == "dev":          # a dev loader's file is watermarked whoever signed it (card manager comment; cosmetic, not signed)
                raw = MS.mark_dev(raw)
            open(code, "wb").write(MS.sign(raw, seed, a.seq) if (a.sign_key or a.sign_dev) else MS.set_seq(raw, a.seq))
        extra_gci = []
        if a.backup_code:
            sys.path.insert(0, os.path.join(REPO, "tools", "card"))
            import mscf_sign as MS
            bk = os.path.join(out, "codeB.gci"); open(bk, "wb").write(MS.rename_gci(open(a.backup_code, "rb").read(), projname.SHORT + "CodeB")); extra_gci.append(bk)
        if a.pack_gci:
            extra_gci.append(os.path.abspath(a.pack_gci))
        if a.state_seq:
            sys.path.insert(0, os.path.join(REPO, "tools", "card"))
            import mscf_sign as MS
            st = os.path.join(out, "state.gci"); open(st, "wb").write(MS.make_state(a.state_seq, a.mtime)); extra_gci.append(st)
    card = os.path.join(out, "card.raw")
    if os.path.exists(card):
        os.remove(card)
    extra = []
    for flag, val in (("--options", a.options), ("--chat", a.chat), ("--online-config", a.online_config), ("--identity", a.identity)):
        if val:
            extra += [flag, val]
    sh([sys.executable, VMC, "create", "--save-gci", gci1, "--mtime", str(a.mtime), "--out", card] + extra)
    if a.stage == "s2":
        card2 = card + ".tmp"
        sh([sys.executable, VMC, "import-gci", *([] if a.omit_code else [code]), *extra_gci, "--card", card, "--out", card2]); os.replace(card2, card)
    if a.standard_melee:
        if a.stage != "s2" or a.coins is not None or a.records:
            raise SystemExit("--standard-melee is for stage s2 and replaces --coins / --records")
        sh([sys.executable, os.path.join(REPO, "tools", "card", "standard_save.py"), "apply", "--in", card, "--out", card + ".std", "--template",
            a.melee_template or os.environ.get("MS_MELEE_TEMPLATE") or os.path.join(REPO, ".analysis-deps", "work", "coins", "MultishineMelee_template.gci")]
           + (["--replace"] if a.replace_melee else [])); os.replace(card + ".std", card)
        pkg["melee_standard"] = True
    if a.coins is not None:
        if a.stage != "s2":
            raise SystemExit("--coins is for stage s2")
        sh([sys.executable, os.path.join(REPO, "tools", "card", "melee_coins.py"), "apply", "--in", card, "--out", card + ".coins", "--coins", str(a.coins)]
           + (["--template", a.melee_template] if a.melee_template else [])); os.replace(card + ".coins", card)
        pkg["melee_coins"] = a.coins
    if a.records:
        if a.stage != "s2":
            raise SystemExit("--records is for stage s2")
        sh([sys.executable, os.path.join(REPO, "tools", "card", "melee_records.py"), "apply", "--in", card, "--out", card + ".rec", "--set", a.records_sets]
           + (["--template", a.melee_template] if a.melee_template else [])); os.replace(card + ".rec", card)
        side = card + ".rec.records.json"
        if os.path.exists(side):
            os.replace(side, card + ".records.json")
            pkg["melee_records"] = json.load(open(card + ".records.json")).get("dataset_version")
    sh([sys.executable, VMC, "validate", card])
    if a.stage_sd:
        dst = os.path.join(os.path.abspath(a.stage_sd), "swiss", "saves", "MemoryCardA.USA.raw")
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(card, dst)
        pkg["staged"] = os.path.relpath(dst, REPO) if dst.startswith(REPO) else dst
    for f in files + [card]:
        pkg["files"][os.path.relpath(f, REPO)] = digests(f)
    json.dump(pkg, open(os.path.join(out, "package.json"), "w"), indent=1)
    print(json.dumps(pkg, indent=1))


if __name__ == "__main__":
    main()
