/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Flat image of the sign-in overlay (sg_ovl_pack.py): code, rodata, data in the file; bss after it.  SGO_BASE comes from --defsym (0 for the packed file). */
ENTRY(sgo_api)
SECTIONS
{
  . = SGO_BASE;
  .image : {
    *(.text .text.*)
    . = ALIGN(4);
    *(.rodata .rodata.* .sdata2 .sdata2.*)
    . = ALIGN(4);
    *(.data .data.* .sdata .sdata.*)
    . = ALIGN(32);
    __sgo_img_end = .;
  }
  .bss (NOLOAD) : {
    *(.bss .bss.* .sbss .sbss.* COMMON)
    . = ALIGN(32);
    __sgo_bss_end = .;
  }
  /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) *(.gnu*) *(.PPC.EMB*) }
}
