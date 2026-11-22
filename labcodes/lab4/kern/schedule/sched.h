#ifndef LAB4_SCHED_H
#define LAB4_SCHED_H
#include <proc.h>
void schedule(void);
void wakeup_proc(struct proc_struct *proc);
#endif
