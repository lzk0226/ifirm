OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
  . = 0x80200000;

  .text : {
    KEEP(*(.text.boot))
    *(.text .text.*)
  }

  .rodata : ALIGN(16) { *(.rodata .rodata.*) }
  .data   : ALIGN(16) { *(.data .data.*) }
  .bss    : ALIGN(16) {
    __bss_start = .;
    *(.bss .bss.*) *(COMMON)
    __bss_end = .;
  }

  . = ALIGN(16);
  . = . + 0x4000;
  _stack_top = .;

  _image_start = 0x80200000;
  _image_end = .;
}
