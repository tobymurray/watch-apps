MEMORY { RAM (xrw) : ORIGIN = 0x20000000, LENGTH = 600K }
ENTRY(_start)
SECTIONS {
  .text : { KEEP(*(.text._start)) *(.text*) } > RAM
  .rodata : { *(.rodata*) } > RAM
  .data : { *(.data*) } > RAM
  .bss : { *(.bss*) *(COMMON) } > RAM
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
