OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
  . = 0x10000;
  .text : {
    *(.text.boot)
    *(.text .text.*)
    *(.rodata .rodata.*)
  }
  . = 0x20000;
  .data : {
    *(.data .data.*)
    *(.sdata .sdata.*)
    *(.bss .bss.*)
    *(COMMON)
  }
  /DISCARD/ : { *(.comment) *(.note .note.*) *(.eh_frame .eh_frame.*) }
}
