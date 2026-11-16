/* 陷阱入口：34 字帧 + 4 个 CSR（本文件是 §12.3 冻结布局的实现） */
    .section .text
    .globl trap_entry
    .align 4
trap_entry:
    csrrw   sp, sscratch, sp          /* sp <- 内核栈顶，sscratch <- 用户 sp */
    addi    sp, sp, -288
    sd      x1,   8(sp)
    sd      x3,  24(sp)
    sd      x4,  32(sp)
    sd      x5,  40(sp)
    sd      x6,  48(sp)
    sd      x7,  56(sp)
    sd      x8,  64(sp)
    sd      x9,  72(sp)
    sd      x10, 80(sp)
    sd      x11, 88(sp)
    sd      x12, 96(sp)
    sd      x13,104(sp)
    sd      x14,112(sp)
    sd      x15,120(sp)
    sd      x16,128(sp)
    sd      x17,136(sp)
    sd      x18,144(sp)
    sd      x19,152(sp)
    sd      x20,160(sp)
    sd      x21,168(sp)
    sd      x22,176(sp)
    sd      x23,184(sp)
    sd      x24,192(sp)
    sd      x25,200(sp)
    sd      x26,208(sp)
    sd      x27,216(sp)
    sd      x28,224(sp)
    sd      x29,232(sp)
    sd      x30,240(sp)
    sd      x31,248(sp)
    csrr    t0, sscratch
    sd      t0,  16(sp)               /* 用户 sp */
    la      t0, _stack_top
    csrw    sscratch, t0              /* 恢复不变量：sscratch 恒为内核栈顶 */
    csrr    t0, sstatus
    sd      t0, 256(sp)
    csrr    t0, sepc
    sd      t0, 264(sp)
    csrr    t0, scause
    sd      t0, 272(sp)
    csrr    t0, stval
    sd      t0, 280(sp)
    sd      zero, 0(sp)
    mv      a0, sp
    call    trap_dispatch
    mv      sp, a0

    .globl trap_return
trap_return:                          /* a0 = 帧指针；也用于第一次进入用户态 */
    mv      sp, a0
    la      t0, _stack_top
    csrw    sscratch, t0
    ld      x1,   8(sp)
    ld      x3,  24(sp)
    ld      x4,  32(sp)
    ld      x5,  40(sp)
    ld      x6,  48(sp)
    ld      x7,  56(sp)
    ld      x8,  64(sp)
    ld      x9,  72(sp)
    ld      x10, 80(sp)
    ld      x11, 88(sp)
    ld      x12, 96(sp)
    ld      x13,104(sp)
    ld      x14,112(sp)
    ld      x15,120(sp)
    ld      x16,128(sp)
    ld      x17,136(sp)
    ld      x18,144(sp)
    ld      x19,152(sp)
    ld      x20,160(sp)
    ld      x21,168(sp)
    ld      x22,176(sp)
    ld      x23,184(sp)
    ld      x24,192(sp)
    ld      x25,200(sp)
    ld      x26,208(sp)
    ld      x27,216(sp)
    ld      x28,224(sp)
    ld      x29,232(sp)
    ld      x30,240(sp)
    ld      x31,248(sp)
    ld      t0, 264(sp)
    csrw    sepc, t0
    ld      t0, 256(sp)
    csrw    sstatus, t0
    ld      t0,  40(sp)
    ld      sp,  16(sp)
    sret
