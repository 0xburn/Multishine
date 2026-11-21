/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
ENTRY(cl_entry)
SECTIONS {
  .text : { KEEP(*(.entry)) *(.text .text.*) *(.rodata .rodata.* .sdata2 .sdata2.*) *(.data .data.*) }
  /DISCARD/ : { *(.comment) *(.eh_frame) *(.sbss .sbss.* .bss .bss.*) }
}
