#include <assert.h>
#include <clock.h>
#include <console.h>
#include <defs.h>
#include <kdebug.h>
#include <memlayout.h>
#include <mmu.h>
#include <riscv.h>
#include <stdio.h>
#include <trap.h>

#define TICK_NUM 100

static void print_ticks() {
    cprintf("%d ticks\n", TICK_NUM);
#ifdef DEBUG_GRADE
    cprintf("End of Test.\n");
    panic("EOT: kernel seems ok.");
#endif
}

/* idt_init - initialize IDT to each of the entry points in kern/trap/vectors.S
 */
void idt_init(void) {

    extern void __alltraps(void);
    /* Set sup0 scratch register to 0, indicating to exception vector
       that we are presently executing in the kernel */
    write_csr(sscratch, 0);
    /* Set the exception vector address */
    write_csr(stvec, &__alltraps);
}

/* trap_in_kernel - test if trap happened in kernel */
bool trap_in_kernel(struct trapframe *tf) {
    return (tf->status & SSTATUS_SPP) != 0;
}

void print_trapframe(struct trapframe *tf) {
    cprintf("trapframe at %p\n", tf);
    print_regs(&tf->gpr);
    cprintf("  status   0x%016llx\n", tf->status);
    cprintf("  epc      0x%016llx\n", tf->epc);
    cprintf("  badvaddr 0x%016llx\n", tf->badvaddr);
    cprintf("  cause    0x%016llx\n", tf->cause);
}

void print_regs(struct pushregs *gpr) {
    cprintf("  zero     0x%016llx\n", gpr->zero);
    cprintf("  ra       0x%016llx\n", gpr->ra);
    cprintf("  sp       0x%016llx\n", gpr->sp);
    cprintf("  gp       0x%016llx\n", gpr->gp);
    cprintf("  tp       0x%016llx\n", gpr->tp);
    cprintf("  t0       0x%016llx\n", gpr->t0);
    cprintf("  t1       0x%016llx\n", gpr->t1);
    cprintf("  t2       0x%016llx\n", gpr->t2);
    cprintf("  s0       0x%016llx\n", gpr->s0);
    cprintf("  s1       0x%016llx\n", gpr->s1);
    cprintf("  a0       0x%016llx\n", gpr->a0);
    cprintf("  a1       0x%016llx\n", gpr->a1);
    cprintf("  a2       0x%016llx\n", gpr->a2);
    cprintf("  a3       0x%016llx\n", gpr->a3);
    cprintf("  a4       0x%016llx\n", gpr->a4);
    cprintf("  a5       0x%016llx\n", gpr->a5);
    cprintf("  a6       0x%016llx\n", gpr->a6);
    cprintf("  a7       0x%016llx\n", gpr->a7);
    cprintf("  s2       0x%016llx\n", gpr->s2);
    cprintf("  s3       0x%016llx\n", gpr->s3);
    cprintf("  s4       0x%016llx\n", gpr->s4);
    cprintf("  s5       0x%016llx\n", gpr->s5);
    cprintf("  s6       0x%016llx\n", gpr->s6);
    cprintf("  s7       0x%016llx\n", gpr->s7);
    cprintf("  s8       0x%016llx\n", gpr->s8);
    cprintf("  s9       0x%016llx\n", gpr->s9);
    cprintf("  s10      0x%016llx\n", gpr->s10);
    cprintf("  s11      0x%016llx\n", gpr->s11);
    cprintf("  t3       0x%016llx\n", gpr->t3);
    cprintf("  t4       0x%016llx\n", gpr->t4);
    cprintf("  t5       0x%016llx\n", gpr->t5);
    cprintf("  t6       0x%016llx\n", gpr->t6);
}

// LAB3 inherited; adapted for Lab4 2411020
#include <sbi.h>

/* 根据 RISC-V 指令编码规则计算 epc 处指令的长度：
 * 最低两位为 0b11 的是 32 位标准指令（4 字节），否则是 16 位压缩指令（2 字节）。
 * 例如开启 C 扩展后 ebreak 会被汇编成 2 字节的 c.ebreak，不能一律 +4。 */
static inline uintptr_t trap_inst_len(uintptr_t epc) {
    uint16_t low = *(volatile uint16_t *)epc;
    return ((low & 0x3) == 0x3) ? 4 : 2;
}

void interrupt_handler(struct trapframe *tf) {
    // scause 最高位是 Interrupt 标志，左移再右移一位将其清除，得到中断编号
    intptr_t cause = (tf->cause << 1) >> 1;
    switch (cause) {
        case IRQ_U_SOFT:
            cprintf("User software interrupt\n");
            break;
        case IRQ_S_SOFT:
            cprintf("Supervisor software interrupt\n");
            break;
        case IRQ_H_SOFT:
            cprintf("Hypervisor software interrupt\n");
            break;
        case IRQ_M_SOFT:
            cprintf("Machine software interrupt\n");
            break;
        case IRQ_U_TIMER:
            cprintf("User timer interrupt\n");
            break;
        case IRQ_S_TIMER:
            // (1) OpenSBI 一次只能设置一个时钟事件，必须先预约下一次时钟中断
            clock_set_next_event();
            // (2) 每次时钟中断计数加一
            ticks++;
            // (3) 每 TICK_NUM 次中断输出一次 "100 ticks"
            if (ticks % TICK_NUM == 0) {
                print_ticks();

            }
            break;
        case IRQ_H_TIMER:
            cprintf("Hypervisor timer interrupt\n");
            break;
        case IRQ_M_TIMER:
            cprintf("Machine timer interrupt\n");
            break;
        case IRQ_U_EXT:
            cprintf("User external interrupt\n");
            break;
        case IRQ_S_EXT:
            cprintf("Supervisor external interrupt\n");
            break;
        case IRQ_H_EXT:
            cprintf("Hypervisor external interrupt\n");
            break;
        case IRQ_M_EXT:
            cprintf("Machine external interrupt\n");
            break;
        default:
            // 无法识别的中断：输出编号并打印完整 trapframe 辅助调试
            cprintf("Unknown interrupt: cause %d\n", (int)cause);
            print_trapframe(tf);
            break;
    }
}

void exception_handler(struct trapframe *tf) {
    switch (tf->cause) {
        case CAUSE_ILLEGAL_INSTRUCTION:
            // 输出异常类型和触发异常的指令地址，然后跳过这条指令，
            // 否则 sret 会回到同一条非法指令，形成异常死循环
            cprintf("Exception type: Illegal instruction\n");
            cprintf("Illegal instruction at 0x%016llx\n", tf->epc);
            tf->epc += trap_inst_len(tf->epc);
            break;
        case CAUSE_BREAKPOINT:
            // ebreak 可能是 4 字节的 ebreak，也可能是 2 字节的 c.ebreak
            cprintf("Exception type: breakpoint\n");
            cprintf("Breakpoint at 0x%016llx\n", tf->epc);
            tf->epc += trap_inst_len(tf->epc);
            break;
        default:
            // 本实验不要求处理的其他异常，统一打印 trapframe 兜底
            print_trapframe(tf);
            panic("unhandled synchronous exception");
    }
}


static inline void trap_dispatch(struct trapframe *tf) {
    if ((intptr_t)tf->cause < 0) {
        // interrupts
        interrupt_handler(tf);
    } else {
        // exceptions
        exception_handler(tf);
    }
}

/* *
 * trap - handles or dispatches an exception/interrupt. if and when trap()
 * returns,
 * the code in kern/trap/trapentry.S restores the old CPU state saved in the
 * trapframe and then uses the iret instruction to return from the exception.
 * */
void trap(struct trapframe *tf) {
    // dispatch based on what type of trap occurred
    trap_dispatch(tf);
}
