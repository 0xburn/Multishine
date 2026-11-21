#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Generate sdc25519_fe.inc: straight-line fe_mul / fe_sq for the 10-limb radix 2^25.5 field GF(2^255-19) (signed int32 limbs, int64 sums,
the representation used by ref10/donna), plus the curve constants converted to limbs.  The generator also PROVES the int32/int64 headroom:
for inputs at the documented bounds it computes the worst case of every precomputed constant multiple and every accumulator and asserts they
fit.  Run: python3 offline/net/tls/gen_fe.py > offline/net/tls/sdc25519_fe.inc"""
import sys

P = 2**255 - 19
E = [0, 26, 51, 77, 102, 128, 153, 179, 204, 230]      # bit position of limb i = ceil(25.5 i)
W = [26, 25] * 5                                       # limb widths
# input bounds (absolute) for mul/sq operands: reduced outputs are within +-2^25 / +-2^24; an unreduced add/sub of two reduced values
# doubles that; we allow up to 1.65*2^26 / 1.65*2^25 like ref10 (covers add of two reduced values with margin)
BOUND = [1.65 * 2 ** (26 if i % 2 == 0 else 25) for i in range(10)]
I32 = 2**31 - 1
I64 = 2**63 - 1


def coef(i, j):
    c = 1
    if i % 2 == 1 and j % 2 == 1: c *= 2
    if i + j >= 10: c *= 19
    return c


def gen_mul():
    out = ["static void fe_mul(fe h, const fe f, const fe g)", "{"]
    for i in range(10): out.append(f"    sdc_i32 f{i} = f[{i}], g{i} = g[{i}];")
    # f_i_2 for odd i, g_j_19 for j>=1
    for i in (1, 3, 5, 7, 9):
        assert BOUND[i] * 2 <= I32
        out.append(f"    sdc_i32 f{i}_2 = 2 * f{i};")
    for j in range(1, 10):
        assert BOUND[j] * 19 <= I32, (j, BOUND[j] * 19)
        out.append(f"    sdc_i32 g{j}_19 = 19 * g{j};")
    worst_all = 0
    for k in range(10):
        terms = []
        worst = 0
        for i in range(10):
            j = (k - i) % 10
            wrap = i + j >= 10
            c = coef(i, j)
            fi = f"f{i}_2" if (i % 2 == 1 and j % 2 == 1) else f"f{i}"
            gj = f"g{j}_19" if wrap else f"g{j}"
            terms.append(f"(sdc_i64)({fi}) * {gj}")
            worst += c * BOUND[i] * BOUND[j]
        worst_all = max(worst_all, worst)
        out.append(f"    sdc_i64 h{k} = " + "\n        + ".join(terms) + ";")
    assert worst_all < I64 / 2, worst_all          # headroom for the carry additions below
    sys.stderr.write("mul worst accumulator 2^%.2f\n" % __import__("math").log2(worst_all))
    out += carry_chain("SDC25519_COUNT_MUL")
    out.append("}")
    return "\n".join(out)


def sq_decomp(i, j):
    """Return (a, b) with a*f_i * b*f_j == C f_i f_j, both factors int32-safe, for the i<=j unordered pair."""
    c = coef(i, j) * (1 if i == j else 2)
    best = None
    for a in (1, 2, 19, 38):
        if c % a: continue
        b = c // a
        if b not in (1, 2, 19, 38): continue
        ok = BOUND[i] * a <= I32 and BOUND[j] * b <= I32
        if ok:
            # prefer decompositions that need fewer distinct precomputes
            cost = (a != 1) + (b != 1)
            if best is None or cost < best[0]: best = (cost, a, b)
    assert best, (i, j, c)
    return best[1], best[2]


def gen_sq():
    need = set()
    terms = {k: [] for k in range(10)}
    worst = {k: 0 for k in range(10)}
    for i in range(10):
        for j in range(i, 10):
            a, b = sq_decomp(i, j)
            k = (i + j) % 10
            ni = f"f{i}" if a == 1 else f"f{i}_{a}"
            nj = f"f{j}" if b == 1 else f"f{j}_{b}"
            if a != 1: need.add((i, a))
            if b != 1: need.add((j, b))
            terms[k].append(f"(sdc_i64)({ni}) * {nj}")
            worst[k] += coef(i, j) * (1 if i == j else 2) * BOUND[i] * BOUND[j]
    out = ["static void fe_sq(fe h, const fe f)", "{"]
    for i in range(10): out.append(f"    sdc_i32 f{i} = f[{i}];")
    for (i, a) in sorted(need): out.append(f"    sdc_i32 f{i}_{a} = {a} * f{i};")
    assert max(worst.values()) < I64 / 2
    sys.stderr.write("sq worst accumulator 2^%.2f\n" % __import__("math").log2(max(worst.values())))
    for k in range(10):
        out.append(f"    sdc_i64 h{k} = " + "\n        + ".join(terms[k]) + ";")
    out += carry_chain("SDC25519_COUNT_SQ")
    out.append("}")
    return "\n".join(out)


def carry_chain(counter):
    # ref10 order: 0&4, 1&5, 2&6, 3&7, 4&8, 9 (x19 into 0), 0
    o = ["    {", "    sdc_i64 c0, c1, c2, c3, c4, c5, c6, c7, c8, c9;", "    " + counter + ";"]
    def c(i, bits, into, mul=""):
        half = 1 << (bits - 1)
        o.append(f"    c{i} = (h{i} + (sdc_i64){half}) >> {bits}; h{into} += c{i}{mul}; h{i} -= c{i} * (sdc_i64){1 << bits};")
    c(0, 26, 1); c(4, 26, 5)
    c(1, 25, 2); c(5, 25, 6)
    c(2, 26, 3); c(6, 26, 7)
    c(3, 25, 4); c(7, 25, 8)
    c(4, 26, 5); c(8, 26, 9)
    c(9, 25, 0, " * 19")
    c(0, 26, 1)
    for i in range(10): o.append(f"    h[{i}] = (sdc_i32)h{i};")
    o.append("    }")
    return o


def limbs(x):
    x %= P
    return [(x >> E[i]) & ((1 << W[i]) - 1) for i in range(10)]


def const(name, x):
    return "static const fe %s = { %s };" % (name, ", ".join(str(v) for v in limbs(x)))


def main():
    print("/* SPDX-License-Identifier: GPL-3.0-or-later OR GPL-2.0-only */")
    print("/* GENERATED by gen_fe.py - do not edit.  10-limb radix 2^25.5 field code for sdc25519.c. */")
    print(gen_mul()); print(); print(gen_sq()); print()
    d = (-121665 * pow(121666, -1, P)) % P
    sqrtm1 = pow(2, (P - 1) // 4, P)
    by = 4 * pow(5, -1, P) % P
    # recover x of the base point (even x? RFC 8032: x with sign bit 0)
    xx = (by * by - 1) * pow(d * by * by + 1, -1, P) % P
    bx = pow(xx, (P + 3) // 8, P)
    if (bx * bx - xx) % P: bx = bx * sqrtm1 % P
    if bx & 1: bx = P - bx
    assert (bx * bx - xx) % P == 0
    print(const("fe_d", d)); print(const("fe_d2", 2 * d)); print(const("fe_sqrtm1", sqrtm1))
    print(const("fe_bx", bx)); print(const("fe_by", by))


if __name__ == "__main__":
    main()
