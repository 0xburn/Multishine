# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Signed payload container tests: RFC 8032 vectors, MSLZ, MSPK container, the freestanding C decoder
and verifier sketch (ASan/UBSan, differential against Python, hostile input), stage2-record integration.  Host only, inert bytes.
Run: python3 -m unittest -v tools/tests/test_payload_pack.py"""
import hashlib, os, random, subprocess, sys, tempfile, unittest

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(TOOLS)
sys.path.insert(0, os.path.join(TOOLS, "payload_pack"))
sys.path.insert(0, os.path.join(TOOLS, "vmc_builder"))
import ed25519_ref as ed, mslz, container as K, payload_pack as PP  # noqa: E402
import vmc_loader as L  # noqa: E402

H = bytes.fromhex
S17 = os.path.join(REPO, "build/offline/s17/net/s17.bin")
DEF = os.path.join(REPO, "build/offline/codeset/default.bin")


def synthetic(n=60000, seed=5):
    r = random.Random(seed)
    words = [bytes(r.randrange(256) for _ in range(r.choice((4, 8, 16)))) for _ in range(300)]
    out = bytearray()
    while len(out) < n:
        out += r.choice(words) if r.random() < 0.8 else bytes(r.randrange(256) for _ in range(r.randrange(1, 30)))
    return bytes(out[:n])


class Ed25519(unittest.TestCase):
    VEC = [  # RFC 8032 section 7.1 tests 1, 2 and "SHA(abc)"
        ("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
         "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"),
        ("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
         "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"),
        ("833fe62409237b9d62ec77587520911e9a759cec1d19755b7da901b96dca3d42", "ec172b93ad5e563bf4932c70e1245034c35467ef2efd4d64ebf819683467e2bf",
         hashlib.sha512(b"abc").hexdigest(),
         "dc2a4459e7369633a52b1bf277839a00201009a3efbf3ecb69bea2186c26b58909351fc9ac90b3ecfdfbc7c66431e0303dca179c138ac17ad9bef1177331a704"),
    ]

    def test_rfc_vectors(self):
        for sk, pk, msg, sig in self.VEC:
            self.assertEqual(ed.public_key(H(sk)).hex(), pk)
            self.assertEqual(ed.sign(H(sk), H(msg)).hex(), sig)
            self.assertTrue(ed.verify(H(pk), H(msg), H(sig)))

    def test_rejects(self):
        sk, pk, msg, sig = self.VEC[1]
        s = bytearray(H(sig))
        for i in (0, 31, 32, 63):
            t = bytearray(s); t[i] ^= 1
            self.assertFalse(ed.verify(H(pk), H(msg), bytes(t)))
        self.assertFalse(ed.verify(H(pk), b"x", H(sig)))
        self.assertFalse(ed.verify(H(self.VEC[0][1]), H(msg), H(sig)))
        self.assertFalse(ed.verify(H(pk), H(msg), H(sig)[:63]))
        big = bytearray(s); big[32:] = (ed.Q + 5).to_bytes(32, "little")      # non-canonical S
        self.assertFalse(ed.verify(H(pk), H(msg), bytes(big)))
        self.assertFalse(ed.verify(b"\xff" * 32, H(msg), H(sig)))             # not a curve point

    def test_roundtrip_random_seed(self):
        seed = ed.generate_seed()
        self.assertTrue(ed.verify(ed.public_key(seed), b"hello", ed.sign(seed, b"hello")))


class Mslz(unittest.TestCase):
    def test_roundtrip(self):
        for d in (b"", b"a", b"abcd" * 1000, bytes(70000), synthetic(), os.urandom(5000)):
            c = mslz.compress(d)
            self.assertEqual(mslz.decompress(c, len(d)), d)

    def test_real_files(self):
        for p in (S17, DEF):
            if not os.path.exists(p):
                continue
            d = open(p, "rb").read()
            c = mslz.compress(d)
            self.assertEqual(mslz.decompress(c, len(d)), d)
            self.assertLess(len(c), len(d))

    def test_hostile(self):
        for bad in (b"", b"\xf0", b"\x10a\x00\x00", b"\x10a\x05\x00\x00", b"\x1f" + b"a" + b"\x01\x00", b"\xff" + b"\xff" * 10):
            with self.assertRaises(mslz.LZError):
                mslz.decompress(bad, 100)
        with self.assertRaises(mslz.LZError):
            mslz.decompress(mslz.compress(bytes(1000)), 999)


class Container(unittest.TestCase):
    seed = bytes(range(32))

    @classmethod
    def setUpClass(cls):
        cls.pub = ed.public_key(cls.seed)
        cls.raw = synthetic(30000)
        cls.cs = hashlib.sha256(b"codeset").digest()
        cls.blob = K.pack(cls.raw, cls.seed, 7, "Multishine-t1", cls.cs)

    def test_ok(self):
        info, raw = K.verify(self.blob, self.pub, min_seq=7, codeset_sha=self.cs)
        self.assertEqual(raw, self.raw)
        self.assertEqual((info["build_seq"], info["build_id"], info["comp"]), (7, "Multishine-t1", 1))

    def test_deterministic(self):
        self.assertEqual(self.blob, K.pack(self.raw, self.seed, 7, "Multishine-t1", self.cs))

    def test_uncompressed(self):
        b = K.pack(self.raw, self.seed, 1, "x", self.cs, compress=False)
        self.assertEqual(K.verify(b, self.pub)[1], self.raw)

    def test_every_header_and_payload_bit_class(self):
        for i in list(range(0, 200, 3)) + [200, 201, len(self.blob) - 1]:
            t = bytearray(self.blob); t[i] ^= 0x10
            with self.assertRaises(K.PackError):
                K.verify(bytes(t), self.pub)

    def test_policy(self):
        with self.assertRaisesRegex(K.PackError, "downgrade"):
            K.verify(self.blob, self.pub, min_seq=8)
        with self.assertRaisesRegex(K.PackError, "signature"):
            K.verify(self.blob, ed.public_key(bytes(32)))
        with self.assertRaisesRegex(K.PackError, "newer loader"):
            K.verify(K.pack(self.raw, self.seed, 7, "x", self.cs, min_loader=2), self.pub)
        with self.assertRaisesRegex(K.PackError, "codeset"):
            K.verify(self.blob, self.pub, codeset_sha=bytes(32))
        with self.assertRaises(K.PackError):
            K.verify(self.blob[:-1], self.pub)
        with self.assertRaises(K.PackError):
            K.pack(self.raw, self.seed, 0, "x", self.cs)

    def test_signed_garbage_is_still_refused_safely(self):
        # a validly signed header over a stored payload whose hash does not match the payload: refused at the stored-hash step
        hdr = K.build_header(3, "t", self.cs, b"abc", b"abc", 0)
        blob = hdr + ed.sign(self.seed, hdr) + b"abd"
        with self.assertRaisesRegex(K.PackError, "stored hash"):
            K.verify(blob, self.pub)

    def test_keygen_refused_in_repo_and_cli(self):
        self.assertEqual(PP.main(["keygen", "--out", os.path.join(REPO, "docs", "never.key")]), 2)
        self.assertFalse(os.path.exists(os.path.join(REPO, "docs", "never.key")))
        with tempfile.TemporaryDirectory() as d:
            kp, raw, out = (os.path.join(d, n) for n in ("k", "raw", "c"))
            if PP.in_git_tree(kp):
                self.skipTest("temp dir inside a git tree")
            open(raw, "wb").write(synthetic(2000))
            self.assertEqual(PP.main(["keygen", "--out", kp]), 0)
            self.assertEqual(oct(os.stat(kp).st_mode & 0o777), "0o600")
            pub = ed.public_key(open(kp, "rb").read()).hex()
            self.assertEqual(PP.main(["pack", raw, "--seed", kp, "--seq", "2", "--build-id", "cli", "--out", out]), 0)
            self.assertEqual(PP.main(["verify", out, "--pub", pub]), 0)
            self.assertEqual(PP.main(["verify", out, "--pub", pub, "--min-seq", "3"]), 1)

    def test_stage2_record_carries_container_hash(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "stage2.bin")
            open(p, "wb").write(self.blob)
            rec = L.stage2_record(p)
            self.assertEqual(rec["sha256"], hashlib.sha256(self.blob).hexdigest())
            c = rec["container"]
            self.assertEqual((c["build_seq"], c["build_id"]), (7, "Multishine-t1"))
            self.assertEqual(c["header_sha256"], hashlib.sha256(self.blob[:136]).hexdigest())
            open(p, "wb").write(b"plain")
            self.assertNotIn("container", L.stage2_record(p))


class CSide(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="ppb")
        r = subprocess.run(["bash", os.path.join(REPO, "offline/payload_pack/run_tests.sh"), cls.tmp], capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError("C build failed:\n" + r.stdout + r.stderr)
        cls.bin = [l.split(None, 1)[1] for l in r.stdout.splitlines() if l.startswith("BIN ")][0]
        cls.log = r.stdout

    def run_bin(self, *a):
        r = subprocess.run([self.bin] + list(a), capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return r.stdout.split()

    def test_decode_differential(self):
        datas = [synthetic(40000, 1), bytes(9000), synthetic(5000, 9)]
        datas += [open(p, "rb").read() for p in (S17, DEF) if os.path.exists(p)]
        for i, d in enumerate(datas):
            c = os.path.join(self.tmp, "c%d" % i); o = os.path.join(self.tmp, "o%d" % i)
            open(c, "wb").write(mslz.compress(d))
            st = self.run_bin("dec", c, str(len(d)), o)
            self.assertEqual(st, ["0", str(len(d))])
            self.assertEqual(open(o, "rb").read(), d)
            st = self.run_bin("dec", c, str(len(d) - 1), o)             # cap one too small
            self.assertEqual(st[0], "-3")

    def test_sha256_matches_hashlib(self):
        for n in (0, 1, 55, 56, 63, 64, 65, 119, 120, 128, 1000):
            p = os.path.join(self.tmp, "s"); open(p, "wb").write(os.urandom(n))
            self.assertEqual(self.run_bin("sha", p)[0], hashlib.sha256(open(p, "rb").read()).hexdigest())

    def test_hostile_streams(self):
        for name, bad in (("trunc", b"\xf0"), ("badoff", b"\x10a\x05\x00\x00"), ("zero", b"\x10a\x00\x00"), ("huge", b"\xf0" + b"\xff" * 5000)):
            p = os.path.join(self.tmp, name); open(p, "wb").write(bad)
            self.assertNotEqual(self.run_bin("dec", p, "1000", p + ".o")[0], "0", name)
        seedf = os.path.join(self.tmp, "seedstream"); open(seedf, "wb").write(mslz.compress(synthetic(3000, 4)))
        self.assertEqual(self.run_bin("fuzz", "1", "20000", seedf), ["fuzz", "ok"])

    def test_pack_open_order_and_errors(self):
        seed = bytes(range(32)); cs = bytes(32)
        raw = synthetic(20000, 2)
        blob = K.pack(raw, seed, 5, "c-test", cs)
        f = os.path.join(self.tmp, "blob"); o = os.path.join(self.tmp, "blob.out")
        open(f, "wb").write(blob)
        st = self.run_bin("open", f, "accept", "5", o)
        self.assertEqual(st, ["0", str(len(raw)), "5"]); self.assertEqual(open(o, "rb").read(), raw)
        self.assertEqual(self.run_bin("open", f, "accept", "6", o)[0], "-12")        # downgrade
        self.assertEqual(self.run_bin("open", f, "reject", "0", o)[0], "-11")        # signature refused first
        for off, code in ((0, "-10"), (210, "-15"), (len(blob) - 1, "-15")):
            t = bytearray(blob); t[off] ^= 1
            open(f, "wb").write(bytes(t))
            got = self.run_bin("open", f, "accept", "0", o)[0]
            if code:
                self.assertEqual(got, code, off)
            else:
                self.assertNotEqual(got, "0")
        # a decodable-looking but wrong payload under a correct stored hash: raw hash step
        hdr = K.build_header(5, "t", cs, b"\x40abcd", b"x" * 4, 1)
        bad = hdr + bytes(64) + b"\x40abcd"
        open(f, "wb").write(bad)
        self.assertNotEqual(self.run_bin("open", f, "accept", "0", o)[0], "0")
        self.assertIn("ms_lz", self.log)


if __name__ == "__main__":
    unittest.main()
