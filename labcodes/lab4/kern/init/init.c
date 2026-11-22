#include <defs.h>
#include <string.h>
#include <stdio.h>
#include <console.h>
#include <dtb.h>
#include <pmm.h>
#include <kmalloc.h>
#include <vmm.h>
#include <trap.h>
#include <proc.h>
#include <intr.h>
#include <clock.h>
int kern_init(void) __attribute__((noreturn));
int kern_init(void) {
 extern char edata[],end[];
 memset(edata,0,end-edata);cons_init();cprintf("Lab4 reconstructed from os26guidebook + repository Lab3\n");
 idt_init();dtb_init();pmm_init();kmalloc_init();check_kmalloc();vmm_init();proc_init();
 clock_init();intr_enable();cpu_idle();
}
