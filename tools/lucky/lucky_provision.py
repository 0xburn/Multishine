#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write the Lucky Stats report key as the card file MultishineLucky.

  lucky_provision.py --out lucky.gci [--key-file FILE]

The key is asked for without echo (or read from a 0600 file). It is never taken from the command line, printed or logged.
Use a Multishine key from Lucky Stats > Account > API keys: it can only send game results, and can be revoked there.
The output file holds the key unencrypted: treat it, and any card image it goes into, like a password."""
import argparse, getpass, os, re, struct, sys, zlib
R = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(R, "offline", "provision"))
import multishine_provision as P  # noqa: E402

NAME = "MultishineLucky"
KEY_RE = re.compile(r"l7u_[a-f0-9]{64}")


def build(key):
    if not KEY_RE.fullmatch(key):
        raise ValueError("not a Lucky Stats upload key")
    body = b"LKYS" + bytes((1, 0, 0, 0)) + key.encode().ljust(72, b"\0")
    blk = bytearray(P.BLOCK)
    blk[0:32] = b"Multishine".ljust(32, b"\0")
    blk[32:64] = b"Lucky Stats key (private)".ljust(32, b"\0")
    P.put_icon(blk, NAME)
    blk[0x1000:0x1000 + len(body) + 4] = body + struct.pack(">I", zlib.crc32(body))
    return P.make_dir_entry(NAME, 0, 1, P.PERM_PUBLIC | P.PERM_NOCOPY | P.PERM_NOMOVE, None) + bytes(blk)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--key-file")
    a = ap.parse_args()
    if a.key_file:
        if os.stat(a.key_file).st_mode & 0o077:
            sys.exit("error: %s is readable by others (chmod 600)" % a.key_file)
        key = open(a.key_file).read().strip()
    else:
        key = getpass.getpass("Lucky Stats upload key: ").strip()
    try:
        data = build(key)
    except ValueError as e:
        sys.exit("error: %s" % e)
    fd = os.open(a.out, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(data)
    print("wrote %s (mode 0600). Keep it private." % a.out)


if __name__ == "__main__":
    main()
