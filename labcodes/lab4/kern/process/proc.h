#ifndef LAB4_PROC_H
#define LAB4_PROC_H
#include <vmm.h>
#include <trap.h>
#define PROC_NAME_LEN 15
#define MAX_PROCESS 64
#define MAX_PID 4096
#define HASH_SIZE 32
#define CLONE_VM 0x100
struct context {
 uintptr_t ra,sp,s0,s1,s2,s3,s4,s5,s6,s7,s8,s9,s10,s11;
};
enum proc_state { PROC_UNINIT,PROC_SLEEPING,PROC_RUNNABLE,PROC_ZOMBIE };
struct proc_struct {
 enum proc_state state;
 int pid,runs;
 uintptr_t kstack;
 volatile bool need_resched;
 struct proc_struct *parent;
 struct mm_struct *mm;
 struct context context;
 struct trapframe *tf;
 uintptr_t pgdir;
 uint32_t flags;
 char name[PROC_NAME_LEN+1];
 list_entry_t list_link,hash_link;
 int exit_code;
};
#define le2proc(le,member) to_struct(le,struct proc_struct,member)
extern list_entry_t proc_list,hash_list[HASH_SIZE];
extern struct proc_struct *current,*idleproc,*initproc;
extern int nr_process;
void proc_init(void);
void cpu_idle(void) __attribute__((noreturn));
int kernel_thread(int (*fn)(void *),void *arg,uint32_t flags);
int do_fork(uint32_t flags,uintptr_t stack,struct trapframe *tf);
void proc_run(struct proc_struct *proc);
void do_exit(int code) __attribute__((noreturn));
void thread_yield(void);
struct proc_struct *find_proc(int pid);
char *set_proc_name(struct proc_struct *proc,const char *name);
void switch_to(struct context *from,struct context *to);
#endif
