#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""offline/multishine build entry (stdlib only),  steps (a) and (b).

  build.py list
  build.py blob   --profile s17-compat [--base 0x...] [--out DIR] [--flag K=V]
  build.py copies [--at REV] [--profile P]     every copy under offline/multishine equals its origin; the s17 profiles' engine copies
                                               are checked by instead (they differ from their origins by design)
  build.py verify --profile P|all [--base 0x...] [--at REV]
        rebuilds the stage image with its EXISTING build script (scratch dir, current sources) and with this builder from the
        copies, and compares blob, every object and the ELF; also the stored reference blob where it is still valid.
        Exit 1 at the first difference (the first differing object is named), never "fixes" anything.

The compat profiles compile exactly the file set, flags and link order of the existing stage scripts (MANIFEST.json).
Sources are staged into OUT/stage under their ORIGINAL virtual path (offline/s17/..., offline/bba/...) so the relative
includes of the copies resolve unchanged; canonical library files (bba, net, exi_shim, common/sdenet, chat core) are used
in place, not copied.  Nothing is written outside OUT (default a local work folder/<profile>).
No emulator, no Docker, nothing on screen.  default.bin (SHA-1 68725e80...) is checked before and after.
"""
import argparse, hashlib, json, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
P = os.environ.get("PPC_PREFIX", "/opt/devkitpro/devkitPPC/bin/powerpc-eabi")
DEFAULT_BIN = os.path.join(REPO, "build/offline/codeset/default.bin")


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for ch in iter(lambda: f.read(1 << 20), b""):
            h.update(ch)
    return h.hexdigest()


def manifest():
    with open(os.path.join(HERE, "MANIFEST.json")) as f:
        return json.load(f)


def check_default(m):
    got = sha1(DEFAULT_BIN)
    if got != m["default_bin_sha1"]:
        sys.exit("default.bin SHA-1 is %s, expected %s: refusing to run" % (got, m["default_bin_sha1"]))


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode:
        sys.stderr.write("FAILED: %s\n%s%s\n" % (" ".join(cmd[:6]) + " ...", r.stdout[-1500:], r.stderr[-3000:]))
        sys.exit(1)
    return r


def subst(args, out, stage):
    return [a.replace("@OUT@", out).replace("@STAGE@", stage).replace("@REPO@", REPO) for a in args]


def stage_tree(prof, out):
    """Copy the profile's files to OUT/stage under their virtual (original) paths; returns (stage, sorted source list)."""
    stage = os.path.join(out, "stage")
    shutil.rmtree(stage, ignore_errors=True)
    srcs = []
    for vpath, dest in prof["stage"].items():
        srcs.append((vpath, os.path.join(HERE, dest)))
    for vpath in prof["external"]:
        srcs.append((vpath, os.path.join(REPO, vpath)))
    for vpath, src in srcs:
        if not os.path.isfile(src):
            sys.exit("missing source %s (for %s)" % (src, vpath))
        dst = os.path.join(stage, vpath)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
    return stage, sorted(srcs)


def build_id(srcs, options=None):
    h = hashlib.sha1()
    for vpath, src in srcs:
        with open(src, "rb") as f:
            h.update(f.read())
    for k in sorted(options or {}):   # a non-default option changes the id (default builds keep their ids)
        h.update(("option %s=%s\n" % (k, options[k])).encode())
    return h.hexdigest()


def blob(m, name, base=None, out=None, opts=None, quiet=False):
    prof = m["profiles"][name]
    base = base or prof["base"]
    out = os.path.abspath(out or os.path.join(REPO, ".analysis-deps/work/multishine", name))
    check_default(m)
    shutil.rmtree(os.path.join(out, "o"), ignore_errors=True)
    obj = os.path.join(out, "o")
    os.makedirs(os.path.join(obj, "gen"), exist_ok=True)
    stage, srcs = stage_tree(prof, out)
    if opts is None:   # env MS_BLOB_OPTS="netcfg=on,...": only the keys this profile declares (dev/lab builds); the explicit opts argument is unchanged
        env = dict(kv.split("=", 1) for kv in os.environ.get("MS_BLOB_OPTS", "").split(",") if "=" in kv)
        opts = {k: v for k, v in env.items() if k in prof.get("options", {})}
    flags = list(prof["flags"])
    chosen = {}
    unit_extra = {}      # option "units_when": {value: {unit: [flags]}}: flags for single units only (e.g. nosched: the menu / online / bridge units, never the rollback engine)
    for k, o in prof.get("options", {}).items():
        v = (opts or {}).get(k, o["default"])
        chosen[k] = v
        flags += o["when"].get(v, [])
        for un, uf in o.get("units_when", {}).get(v, {}).items():
            unit_extra.setdefault(un, []).extend(uf)
    for k in (opts or {}):
        if k not in prof.get("options", {}):
            sys.exit("unknown --flag %s for profile %s" % (k, name))
    flags = subst(flags, obj, stage) + m["lr_cflags"]
    extra_cf = os.environ.get("MS_EXTRA_CFLAGS", "").split()   # card route: MS_EXTRA_CFLAGS=-DSD_CARD_HOLE (offline/cardloader/card_payload.py); part of the build id when set
    flags += extra_cf
    for cmd in prof.get("pre", []):
        r = run(subst(cmd, obj, stage))
        sys.stderr.write(r.stderr)
    cc = [P + "-gcc"] + flags + m["common_cflags"]
    for u in prof["units"]:
        src = os.path.join(stage, u["src"])
        o = os.path.join(obj, u["obj"] + ".o")
        if u["kind"] == "c":
            run(cc + u["args"] + unit_extra.get(u["obj"], []) + ["-c", src, "-o", o], cwd=stage)
        else:
            run([P + "-gcc"] + u["args"] + unit_extra.get(u["obj"], []) + ["-c", src, "-o", o], cwd=stage)
    img = prof["image"]
    objs = [os.path.join(obj, n + ".o") for n in prof["link_order"]]
    elf, binp = os.path.join(obj, img + ".elf"), os.path.join(obj, img + ".bin")
    mp = os.path.join(obj, img + ".map")
    link = [P + "-gcc", "-nostartfiles", "-nostdlib", "-Wl,-T," + os.path.join(stage, prof["ld"]),
            "-Wl,-Ttext=" + base, "-Wl,--gc-sections", "-Wl,-Map," + mp]
    link += ["-Wl,--undefined=" + s for s in prof["undefined"]] + objs + ["-lgcc", "-o", elf]
    run(link)
    run([P + "-objcopy", "-O", "binary", elf, binp])
    with open(os.path.join(obj, img + ".nm"), "w") as f:
        f.write(run([P + "-nm", "-n", elf]).stdout)
    if prof.get("nms"):
        with open(os.path.join(obj, img + ".nms"), "w") as f:
            f.write(run([P + "-nm", "-S", "-n", elf]).stdout)
    # ---- checks, as the stage scripts do ----
    ck = prof["checks"]
    if run([P + "-nm", "-u", elf]).stdout.strip():
        sys.exit("unresolved symbols")
    names = [u["obj"] for u in prof["units"]]
    arith = names if ck["fpu_arith"] == "all" else ck["fpu_arith"]
    for n in arith:
        d = run([P + "-objdump", "-d", os.path.join(obj, n + ".o")]).stdout
        if re.search(r"\s(lfs|stfs|fmr|fadd|fmul|fctiw|frsp|fsub|fdiv|fcmp)\s", d):
            sys.exit("FPU arithmetic in %s.o" % n)
    for n in ck["fpu_ldst"]:
        d = run([P + "-objdump", "-d", os.path.join(obj, n + ".o")]).stdout
        if re.search(r"\s(lf[sd]|stf[sd])\s", d):
            sys.exit("FPU load/store in %s.o" % n)
    if ck["softfloat_map"] and re.search(r"_(add|sub|mul|div)sf3|_fix(uns)?sfsi|_float(un)?sisf", open(mp).read()):
        sys.exit("soft-float routine linked")
    lr = os.path.join(REPO, "offline/lr_check.py")
    run([sys.executable, lr, "check"] + [os.path.join(obj, n + ".o") for n in ck["lr_c"]])
    run([sys.executable, lr, "verify"] + [os.path.join(obj, n + ".o") for n in ck["lr_asm"]])
    run([sys.executable, lr, "libgcc", mp])
    check_default(m)
    non_default = {k: v for k, v in chosen.items() if v != prof["options"][k]["default"]}
    if extra_cf:
        non_default["extra_cflags"] = " ".join(extra_cf)
    res = {"profile": name, "base": base, "bin": binp, "elf": elf, "obj_dir": obj, "sha1": sha1(binp), "bytes": os.path.getsize(binp),
           "build_id": build_id(srcs, non_default), "options": chosen, "units": len(prof["units"]), "files": len(srcs)}
    if not quiet:
        print("profile %s base %s options %s" % (name, base, chosen or "-"))
        print("  units: %s" % " ".join(u["obj"] for u in prof["units"]))
        print("  sources: %d files, build id %s" % (len(srcs), res["build_id"]))
        print("  blob %s  %s  (%d bytes)" % (res["sha1"], binp, res["bytes"]))
    return res


def objs_of(d):
    return {f: sha1(os.path.join(d, f)) for f in sorted(os.listdir(d)) if f.endswith(".o")}


def first_diff(a, b):
    A, B = open(a, "rb").read(), open(b, "rb").read()
    for i in range(min(len(A), len(B))):
        if A[i] != B[i]:
            return i
    return min(len(A), len(B)) if len(A) != len(B) else None


def archive_root(rev, work):
    """Checkout of `offline` at git revision REV (the stage sources other contributors may be editing in the working tree)."""
    root = os.path.join(work, "at")
    os.makedirs(root)
    subprocess.run("git -C '%s' archive %s offline | tar -x -C '%s'" % (REPO, rev, root), shell=True, check=True)
    return root


def verify(m, name, base=None, at=None):
    prof = m["profiles"][name]
    base = base or prof["base"]
    ref = prof["reference"]
    work = os.path.join(REPO, ".analysis-deps/work/multishine", "verify_" + name)
    shutil.rmtree(work, ignore_errors=True)
    outa = os.path.join(work, "existing")
    os.makedirs(outa)
    check_default(m)
    env = dict(os.environ, OUT=outa)
    root = archive_root(at, work) if at else REPO
    r = subprocess.run(["bash", os.path.join(root, ref["script"]), base], cwd=root, env=env, capture_output=True, text=True)
    if r.returncode:
        sys.exit("existing script failed:\n" + r.stderr[-2000:])
    img = prof["image"]
    a_bin = os.path.join(outa, img + ".bin")
    b = blob(m, name, base=base, out=os.path.join(work, "ms"), quiet=True)
    ok = True
    print("%s @ %s" % (name, base))
    print("  existing script (%s): %s" % ("sources at " + at if at else "working tree", sha1(a_bin)))
    print("  multishine build.py (copies)     : %s  (%d files, build id %s)" % (b["sha1"], b["files"], b["build_id"][:12]))
    oa, ob = objs_of(outa), objs_of(b["obj_dir"])
    link_names = prof["link_order"]
    diff = [n for n in link_names if oa.get(n + ".o") != ob.get(n + ".o")]
    print("  objects: %d compared, %d differ%s" % (len(ob), len(diff), (" (first: %s.o)" % diff[0]) if diff else ""))
    if set(oa) != set(ob):
        print("  object sets differ: only existing %s, only multishine %s" % (sorted(set(oa) - set(ob)), sorted(set(ob) - set(oa))))
        ok = False
    if diff:
        ok = False
    if sha1(a_bin) != b["sha1"]:
        ok = False
        fd = first_diff(a_bin, b["bin"])
        print("  BLOB DIFFERS, first differing byte offset %s" % fd)
    e_same = sha1(os.path.join(outa, img + ".elf")) == sha1(b["elf"])
    print("  ELF identical: %s (informational)" % e_same)
    st = os.path.join(REPO, ref["bin"])
    if base == prof["base"] and os.path.exists(st):
        same = sha1(st) == b["sha1"]
        print("  stored reference %s: %s %s" % (ref["bin"], sha1(st)[:8], "identical" if same else ("DIFFERS" if ref["stored_bin_valid"] else "differs (known stale, see BUILD_CONSOLIDATION 3.3; not a failure)")))
        if not same and ref["stored_bin_valid"] and not at:
            ok = False      # with --at the stored output may belong to a later tree (other contributors rebuild it): informational only
    print("  verdict: %s" % ("IDENTICAL" if ok else "DIFFERENT"))
    return ok


def copies(m, at=None, names=None):
    ok = True
    rows = 0
    for name, prof in m["profiles"].items():
        if names and name not in names:
            continue
        for vpath, dest in prof["stage"].items():
            if vpath in prof.get("own", []):
                continue          # a file that exists only here (derived profiles), no origin to compare with
            rows += 1
            if at:
                org = hashlib.sha1(subprocess.run(["git", "-C", REPO, "show", "%s:%s" % (at, vpath)], capture_output=True).stdout).hexdigest()
            else:
                org = sha1(os.path.join(REPO, vpath))
            if sha1(os.path.join(HERE, dest)) != org:
                print("copy differs from its origin: %s -> %s" % (vpath, dest))
                ok = False
    print("copies: %d entries checked, %s" % (rows, "all equal their origin" if ok else "DIFFERENCES (origin edited after the copy?)"))
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["list", "blob", "copies", "verify"])
    ap.add_argument("--profile", default="all")
    ap.add_argument("--base")
    ap.add_argument("--out")
    ap.add_argument("--flag", action="append", default=[])
    ap.add_argument("--at", help="verify/copies: compare against the origin sources at this git revision (e.g. c99cea4, the commit the copies were taken from) instead of the working tree")
    a = ap.parse_args()
    m = manifest()
    if not os.path.exists(P + "-gcc"):
        sys.exit("devkitPPC not found at %s (set PPC_PREFIX)" % P)
    names = list(m["profiles"]) if a.profile == "all" else [a.profile]
    if a.cmd == "verify" and a.profile == "all":
        names = [n for n in names if "reference" in m["profiles"][n]]      # derived profiles have no stage script to compare with
    for n in names:
        if n not in m["profiles"]:
            sys.exit("unknown profile %s" % n)
    if a.cmd == "list":
        for n in names:
            p = m["profiles"][n]
            print("%-11s image %s base %s, %d units, %d copied + %d canonical files, options %s" % (n, p["image"], p["base"], len(p["units"]), len(p["stage"]), len(p["external"]), list(p["options"]) or "-"))
        return 0
    if a.cmd == "copies":
        return 0 if copies(m, a.at, None if a.profile == "all" else names) else 1
    if a.cmd == "blob":
        opts = dict(f.split("=", 1) for f in a.flag)
        for n in names:
            blob(m, n, base=a.base, out=a.out, opts=opts)
        return 0
    check_default(m)
    ok = all([verify(m, n, base=a.base, at=a.at) for n in names])
    check_default(m)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
