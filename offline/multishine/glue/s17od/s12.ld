/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* S12: copy of; adds the S12 config (.s12cfg, 0x40 B) at +0xB80 and telemetry (.s12tel, 0x200 B) at +0xBC0, code from +0xDC0;
   __s12_image_lo/hi bound the image (camera-task skip hooks).
   S11: copy of; adds the S11 config (.s11cfg, 0x40 B) at +0xA40 and telemetry (.s11tel, 0x100 B) at +0xA80, code from +0xB80.
   S10: copy of; adds the S10 config (.s10cfg, 0x40 B) at +0x900 and telemetry (.s10tel, 0x100 B) at +0x940, code from +0xA40.
   S7: copy of; adds the S7 wire config (.s7cfg, 0x40 B) at +0x800 and status (.s7status, 0xC0 B) at +0x840, code from +0x900.
   S6D: copy of; adds the S6d telemetry block .s6drb at +0x700 (0x100 B), code from +0x800.
   S6C: copy of; adds the S6c telemetry block .s6crb at +0x600 (0x100 B), code from +0x700.
   Flat S6b net image: tramp +0, cfg +0x40 (0x78 B), status +0x100 (0x100 B), finalized-frame trace ring +0x200
   (32 x 5 words), Slippi checksum ring +0x480 (0x40 B), rollback statistics +0x4C0 (0x140 B), then code, rodata, data,
   bss (bss emitted as zeros: the patched section is file backed).  The last 8 bytes are zero (patcher's branch-back slot). */
ENTRY(s3_entry)
SECTIONS
{
  .text : {
    __s12_image_lo = .;
    KEEP(*(.text.s3_entry))
    . = 0x40;
    KEEP(*(.s3cfg))
    . = 0x100;
    KEEP(*(.s3status))
    . = 0x200;
    KEEP(*(.s3trace))
    . = 0x480;
    KEEP(*(.s3cks))
    . = 0x4C0;
    KEEP(*(.s6rb))
    . = 0x600;
    KEEP(*(.s6crb))
    . = 0x700;
    KEEP(*(.s6drb))
    . = 0x800;
    KEEP(*(.s7cfg))
    . = 0x840;
    KEEP(*(.s7status))
    . = 0x900;
    KEEP(*(.s10cfg))
    . = 0x940;
    KEEP(*(.s10tel))
    . = 0xA40;
    KEEP(*(.s11cfg))
    . = 0xA80;
    KEEP(*(.s11tel))
    . = 0xB80;
    KEEP(*(.s12cfg))
    . = 0xBC0;
    KEEP(*(.s12tel))
    . = 0xDC0;
    __s12_code = .;
    *(.text .text.*)
    . = ALIGN(4);
    *(.rodata .rodata.* .sdata2 .sdata2.*)
    . = ALIGN(32);
    *(.data .data.* .sdata .sdata.*)
    . = ALIGN(32);
    *(.bss .bss.* .sbss .sbss.* COMMON)
    . = ALIGN(32);
    LONG(0) LONG(0)
    . = ALIGN(32);
    __s12_image_hi = .;
  }
  /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) *(.gnu*) }
}
