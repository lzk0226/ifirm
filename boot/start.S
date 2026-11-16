/* 引导入口：只做四件事（§6.2.3） */
    .section .text.boot, "ax"
    .globl _start
_start:
    la      sp, _stack_top
    la      t0, __bss_start
    la      t1, __bss_end
1:  bgeu    t0, t1, 2f
    sd      zero, 0(t0)
    addi    t0, t0, 8
    j       1b
2:  call    kmain
3:  wfi
    j       3b

    .section .stack, "aw", @nobits
    .align  4
    .space  32768
    .globl  _stack_top
_stack_top:
