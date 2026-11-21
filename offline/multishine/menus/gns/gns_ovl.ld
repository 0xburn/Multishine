/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Flat image of the Game Night match-time block (gns_pack.py): code, rodata, data in the file; bss after it.  GNS_BASE comes from --defsym (0x100000 for the packed file). */
ENTRY(gns_api)
SECTIONS
{
  . = GNS_BASE;
  .image : {
    *(.text .text.*)
    . = ALIGN(4);
    *(.rodata .rodata.* .sdata2 .sdata2.*)
    . = ALIGN(4);
    *(.data .data.* .sdata .sdata.*)
    . = ALIGN(32);
    __gns_img_end = .;
  }
  .bss (NOLOAD) : {
    *(.bss .bss.* .sbss .sbss.* COMMON)
    . = ALIGN(32);
    __gns_bss_end = .;
  }
  /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) *(.gnu*) *(.PPC.EMB*) }
}
