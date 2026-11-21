#!/usr/bin/env python3.14
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Release shape guard ( L2): a release build must carry no lab-only switch or marker.

Two layers, both run by tools/release.sh:
  config   (before building/signing)  --opts "$MS_BLOB_OPTS" --xcf "$XCF" [--allow SWITCH,...]
            checks the engine option list (MS_BLOB_OPTS), the extra compile flags (MS_XCF / MS_EXTRA_CFLAGS / REP_XCF / REC_XCF / SG_EXTRA_CFLAGS),
            and the environment (SG_LAB=1, SG_TLSPROBE, REP_TEST_ROOTS, MS_MATCH_MSG ...).
  images   (after the build)          --images FILE.mscf ...  [--allow ...]
            decodes each card file (compressed MSCF v2 included) and searches the image for the literal markers the lab switches leave
            (REP-TEST-ROOTS, the synthetic report's ids).  Switches that leave no literal string (rbread, netcfg, hubdiag, MM_*) are covered by the config layer only.
--allow names switches a deliberate test build may carry (RELEASE_TEST modes); the normal release passes none except the explicit ALLOWED_NOW below.
Exit 0 = clean, 1 = a lab switch or marker found.
"""
import argparse, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "card"))

# Switches allowed in the normal release ON PURPOSE.  Each entry needs an and a removal note.
ALLOWED_NOW = {}                                # MS_REP_LINE removed: the release carries no lab or temporary switch

# switch -> regex looked for in compile flags / option lists / environment values
FLAG_SWITCHES = {
    "MM_FTSTATS":       r"-DMM_FTSTATS\b",
    "MM_PROBE":         r"-DMM_PROBE\b",
    "MM_FC":            r"-DMM_FC\b",                 # frame cost readout: per-match time split uploaded as LAB_DIAG lines (RELEASE_TEST=fc only)
    "MM_RB_OFF":        r"-DMM_RB_OFF\b",
    "MM_BOOTLOOP":      r"-DMM_BOOTLOOP\b",
    "MS_MATCH_MSG":     r"-DMS_MATCH_MSG\b",
    "MS_REP_LINE":      r"-DMS_REP_LINE\b",
    "MS_REP_SELFTEST":  r"-DMS_REP_SELFTEST",          # also _MODE, _CSS
    "MS_REC_SELFTEST":  r"-DMS_REC_SELFTEST\b",
    "GNB_TEST_REFUSE":  r"-DGNB_TEST_REFUSE\b",
    "MS_CSSPROBE":      r"-DMS_CSSPROBE\b",
    "SD_LAB_HG":        r"-DSD_LAB_HG",                # hgtrace, nofix
    "MS_TLS_PROBE":     r"-DMS_TLS_PROBE\b",           # tlsprobe
    "MS_HUB_DIAG":      r"-DMS_HUB_DIAG\b",            # hubdiag
    "SBR_NETCFG":       r"-DSBR_NETCFG\b",             # netcfg
    "MS_RB_READOUT":    r"-DMS_RB_READOUT\b",          # rbread
    "SG_LAB=1":         r"-DSG_LAB=1\b",
    "SG_TLSPROBE":      r"-DSG_TLSPROBE\b",
    "MNC_LAB_SAMENAT":  r"-DMNC_LAB_SAMENAT\b",        # labnat
    "MS_LAB_ITEMS_ALL": r"-DMS_LAB_ITEMS_ALL\b",       # itemsall
    # on-screen readouts ("No on-screen readouts, man. Send to the server if you must."): never in a release and in NO RELEASE_TEST mode (no allow list names them)
    "MM_RB_SHOW":       r"-DMM_RB_SHOW\b",             # draws the rbread readout lines (RB/PK/HSD/DP/T1/T2/PM/FT, and with MM_FTSTATS FS/SW/S1/S2/NW/SQ/IO/PR/DS/DJ/XE/NT) on the character screen
    "MS_HEAPPROBE":     r"-DMS_HEAPPROBE\b",           # heap probe text (glue/s17 hp_glue.inc, profile s17-compat-hp)
    "MS_TMM_LAB_LINK":  r"-DMS_TMM_LAB_LINK\b",           # tiered matchmaking: fakes the link class in Dolphin (br_glue.c); never in a release
    "TMM_WINDOW":       r"-DTMM_(SLIPPI|OURS)_WINDOW_MS",     # tiered matchmaking: a lab window (Dolphin's Slippi connect takes about 24 s); the release uses the constants of tmm_core.h
    "ON_DIAG":          r"-DON_DIAG\b",                # card status text on the CSS user name (on_backend.c diagnostic image)
}
# MS_BLOB_OPTS keys that are lab only: key -> accepted-off values
OPT_SWITCHES = {"rbread": ("off", ""), "netcfg": ("off", ""), "hubdiag": ("off", ""), "tlsprobe": ("off", ""),
                "hgtrace": ("off", ""), "itemsall": ("off", ""), "labnat": ("off", ""), "livediag": ("off", "")}
# environment variables that must be unset (or 0) in a release shell
ENV_SWITCHES = {"SG_LAB": ("", "0"), "SG_TLSPROBE": ("", "0"), "SG_LAB_PIN": ("",), "REP_TEST_ROOTS": ("",), "MS_MATCH_MSG": ("", "0"),
                "REC_XCF": ("",), "REP_XCF": ("",), "SG_EXTRA_CFLAGS": ("",), "MS_REP_SELFTEST": ("", "0")}
ENV_FLAG_VARS = ("MS_XCF", "MS_EXTRA_CFLAGS", "REP_XCF", "REC_XCF", "SG_EXTRA_CFLAGS", "XCF")
# literal strings a lab switch leaves in an image
IMAGE_MARKERS = {
    "MS_TMM_LAB_LINK": [b"MS-TMM-LAB"],
    "MS_LAB_POISON_NE": [b"MS-NE-LAB"],
    "REP_TEST_ROOTS":  [b"REP-TEST-ROOTS"],
    "MS_REP_SELFTEST": [b"SELFTESTOPPUID", b"-00000000-selftest", b".00000000-selftest"],
}


def check_config(opts, flags, env, allow):
    bad = []
    for part in [p for p in opts.split(",") if p]:
        k, _, v = part.partition("=")
        if k in OPT_SWITCHES and v not in OPT_SWITCHES[k] and k not in allow:
            bad.append("MS_BLOB_OPTS %s=%s" % (k, v or "on"))
    for name, rx in FLAG_SWITCHES.items():
        if name in allow:
            continue
        for src, val in [("flags", flags)] + [(v, env.get(v, "")) for v in ENV_FLAG_VARS]:
            if val and re.search(rx, val):
                if name in ALLOWED_NOW:
                    print("  allowed on purpose: %s (%s) -- %s" % (name, src, ALLOWED_NOW[name]))
                else:
                    bad.append("%s in %s" % (name, src))
    for var, ok in ENV_SWITCHES.items():
        if env.get(var, "") not in ok and var not in allow:
            bad.append("environment %s=%s" % (var, env.get(var)))
    return sorted(set(bad))


def check_images(files, allow):
    import mscf_sign
    bad = []
    for f in files:
        try:
            raw = open(f, "rb").read()
            if len(raw) % mscf_sign.M.BLOCK == 0:       # a published .mscf is the card body without the 0x40-byte GCI entry (the publish tool (outside this repository)); parse wants the entry
                raw = bytes(mscf_sign.GCI_HDR) + raw
            p = mscf_sign.parse(raw)
        except Exception as e:      # an undecodable file cannot be vouched for
            bad.append("%s: cannot decode (%s)" % (f, e)); continue
        if not p["img"]:
            bad.append("%s: empty image after decode" % f); continue
        for sw, marks in IMAGE_MARKERS.items():
            if sw in allow:
                continue
            for m in marks:
                if m in p["img"] or m in p["d"]:
                    bad.append("%s: marker %r of %s" % (os.path.basename(f), m.decode(), sw))
    return bad


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--opts", default=None, help="the MS_BLOB_OPTS value")
    ap.add_argument("--xcf", default="", help="the extra compile flags")
    ap.add_argument("--allow", default="", help="comma list of switches a test build may carry")
    ap.add_argument("--images", nargs="*", default=[], help="built .mscf files to scan")
    a = ap.parse_args(argv)
    allow = set(x for x in a.allow.split(",") if x)
    bad = []
    if a.opts is not None:
        bad += check_config(a.opts, a.xcf, os.environ, allow)
    if a.images:
        bad += check_images(a.images, allow)
    if bad:
        print("REFUSED: lab-only switch or marker in a release build (docs/PRUNING_PLAN.md L2):")
        for b in bad:
            print("  - " + b)
        return 1
    print("release shape ok (%s%s)" % ("config" if a.opts is not None else "", " images=%d" % len(a.images) if a.images else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
