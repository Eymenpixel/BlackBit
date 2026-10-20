ENTRY(_start)
SECTIONS {
  . = 0x40080000;
  .text : ALIGN(4096) { KEEP(*(.text.boot)) *(.text*) }
  .rodata : ALIGN(4096) { *(.rodata*) }
  .data : ALIGN(4096) { *(.data*) }
  .bss : ALIGN(4096) { *(COMMON) *(.bss*) }
  _image_end = .;
}
