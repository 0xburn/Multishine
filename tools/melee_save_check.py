# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Offline check of the HSD (sysdolphin) block checksums of a Melee save inside a raw memory card image:
  python3 tools/melee_save_check.py CARD.raw FIRST_BLOCK COUNT
Implements hsd_803B31CC / hsd_803B2B20 / fn_803B302C of vendor/melee/src/sysdolphin/baselib/card.c (decrypt chain + 16-byte sum). The save blocks are 8 KiB; on our card the
file starts at card block 5 (comment, banner, icons) and its data blocks are 6..15. Prints OK or BAD per block."""
import sys
K=[0x26,0xFF,0xE8,0xEF,0x42,0xD6,0x01,0x54,0x14,0xA3,0x80,0xFD,0x6E]
def b2(src):
    sp=[0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10]
    for i,v in enumerate(src): sp[i%16]=(sp[i%16]+v)&0xff
    for i in range(1,16):
        if sp[i-1]==sp[i]: sp[i]^=0xff
    return bytes(sp)
def f302c(prev,cur):
    m=prev%7; c=cur
    if m==0: c=((c&1)|((c<<1)&4)|((c<<2)&0x10)|((c<<3)&0x40)|((c>>3)&2)|((c>>2)&8)|((c>>1)&0x20)|(c&0x80))&0xff
    elif m==1: c=(((c<<1)&2)|((c<<6)&0x80)|(c&4)|((c>>3)&1)|((c<<1)&0x20)|((c>>1)&0x10)|((c>>3)&8)|((c>>1)&0x40))&0xff
    elif m==2: c=(((c&1)<<2)|((c<<2)&8)|((c<<4)&0x40)|((c<<1)&0x10)|((c<<3)&0x80)|((c>>4)&2)|((c>>6)&1)|((c>>2)&0x20))&0xff
    elif m==3: c=(((c<<4)&0x10)|((c>>1)&1)|((c<<3)&0x20)|((c>>2)&2)|((c>>1)&8)|((c<<1)&0x40)|((c<<1)&0x80)|((c>>5)&4))&0xff
    elif m==4: c=(((c<<3)&8)|((c<<4)&0x20)|((c>>1)&2)|((c<<4)&0x80)|((c<<2)&0x40)|((c>>3)&4)|((c>>2)&0x10)|((c>>7)&1))&0xff
    elif m==5: c=(((c&1)<<5)|((c<<5)&0x40)|((c&4)<<5)|(c&8)|((c>>2)&4)|((c>>5)&1)|((c>>5)&2)|((c>>3)&0x10))&0xff
    else: c=(((c<<6)&0x40)|(c&2)|((c>>2)&1)|((c<<2)&0x20)|(c&0x10)|((c<<2)&0x80)|((c>>4)&4)|((c>>4)&8))&0xff
    c^=K[prev%13]; c^=prev
    return c&0xff
def verify(blk):
    d=bytearray(blk); prev=d[15]
    for i in range(16,len(d)):
        cur=d[i]; d[i]=f302c(prev&0xff,cur); prev=cur
    return b2(bytes(d[16:]))==bytes(d[:16]), bytes(d)
if __name__=="__main__":
    d=open(sys.argv[1],'rb').read()
    first=int(sys.argv[2]); n=int(sys.argv[3])
    for b in range(first,first+n):
        ok,_=verify(d[b*8192:(b+1)*8192])
        print("block",b,"checksum", "OK" if ok else "BAD")
