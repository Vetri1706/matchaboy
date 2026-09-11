/* SPDX-License-Identifier: GPL-3.0-only */
ENTRY(_start)
MEMORY { ROM (rx) : ORIGIN = 0x08000000, LENGTH = 32M
         EWRAM (rw) : ORIGIN = 0x02000000, LENGTH = 256K
         IWRAM (rwx) : ORIGIN = 0x03000000, LENGTH = 30K }
SECTIONS {
 .header : { KEEP(*(.header)) } > ROM
 .boot : { *(.text.boot) } > ROM
 .text : { __code_start = .; *(.text*) . = ALIGN(4); __code_end = .; } > IWRAM AT> ROM
 __code_load = LOADADDR(.text);
 .rodata : { *(.rodata*) } > ROM
 .ARM.exidx : { *(.ARM.exidx*) } > ROM
 .bss (NOLOAD) : { __bss_start = .; KEEP(*(.game)) *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > EWRAM
 .data : { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > EWRAM AT> ROM
 __data_load = LOADADDR(.data);
 /DISCARD/ : { *(.comment) *(.note*) }
}
