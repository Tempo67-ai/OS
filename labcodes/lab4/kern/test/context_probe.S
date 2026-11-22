#include <riscv.h>
.text
.globl context_probe
context_probe:
    addi sp, sp, -128
    sd ra, 0(sp)
    sd a0, 8(sp)
    sd s0, 16(sp)
    sd s1, 24(sp)
    sd s2, 32(sp)
    sd s3, 40(sp)
    sd s4, 48(sp)
    sd s5, 56(sp)
    sd s6, 64(sp)
    sd s7, 72(sp)
    sd s8, 80(sp)
    sd s9, 88(sp)
    sd s10, 96(sp)
    sd s11, 104(sp)
    li t0, 5
    sd t0, 112(sp)
    addi s0, a0, 0
    addi s1, a0, 1
    addi s2, a0, 2
    addi s3, a0, 3
    addi s4, a0, 4
    addi s5, a0, 5
    addi s6, a0, 6
    addi s7, a0, 7
    addi s8, a0, 8
    addi s9, a0, 9
    addi s10, a0, 10
    addi s11, a0, 11
.Lagain:
    call thread_yield
    ld t0, 8(sp)
    addi t1, t0, 0
    bne s0, t1, .Lfail
    addi t1, t0, 1
    bne s1, t1, .Lfail
    addi t1, t0, 2
    bne s2, t1, .Lfail
    addi t1, t0, 3
    bne s3, t1, .Lfail
    addi t1, t0, 4
    bne s4, t1, .Lfail
    addi t1, t0, 5
    bne s5, t1, .Lfail
    addi t1, t0, 6
    bne s6, t1, .Lfail
    addi t1, t0, 7
    bne s7, t1, .Lfail
    addi t1, t0, 8
    bne s8, t1, .Lfail
    addi t1, t0, 9
    bne s9, t1, .Lfail
    addi t1, t0, 10
    bne s10, t1, .Lfail
    addi t1, t0, 11
    bne s11, t1, .Lfail
    ld t0, 112(sp)
    addi t0, t0, -1
    sd t0, 112(sp)
    bnez t0, .Lagain
    li a0, 0
    j .Ldone
.Lfail:
    li a0, 1
.Ldone:
    ld s0, 16(sp)
    ld s1, 24(sp)
    ld s2, 32(sp)
    ld s3, 40(sp)
    ld s4, 48(sp)
    ld s5, 56(sp)
    ld s6, 64(sp)
    ld s7, 72(sp)
    ld s8, 80(sp)
    ld s9, 88(sp)
    ld s10, 96(sp)
    ld s11, 104(sp)
    ld ra, 0(sp)
    addi sp, sp, 128
    ret
