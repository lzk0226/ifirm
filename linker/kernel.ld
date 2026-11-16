OUTPUT_ARCH(riscv)
ENTRY(_start)

BASE = 0x80200000;
SECTIONS
{
  . = BASE;
  _image_start = .;
  .text : {
    KEEP(*(.text.boot))
    *(.text .text.*)
  }
  . = ALIGN(4096);
  .rodata : { *(.rodata .rodata.*) *(.srodata .srodata.*) }
  . = ALIGN(4096);
  .data : { *(.data .data.*) *(.sdata .sdata.*) }
  . = ALIGN(4096);
  __bss_start = .;
  .bss : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) }
  . = ALIGN(4096);
  __bss_end = .;
  .stack : { *(.stack) }
  . = ALIGN(4096);
  _image_end = .;
  /DISCARD/ : { *(.comment) *(.note .note.*) *(.eh_frame .eh_frame.*) }
}
