MEMORY { RAM : ORIGIN = 0x20000000, LENGTH = 600K }
ENTRY(_start)
SECTIONS {
  .text : { *(.text._start) *(.text .text.*) } > RAM
  .rodata : { *(.rodata .rodata.*) } > RAM
  .data : { *(.data .data.*) } > RAM
  .bss : { *(.bss .bss.*) *(COMMON) } > RAM
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.debug*) *(.comment) }
}
