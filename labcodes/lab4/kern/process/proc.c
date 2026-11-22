#include <proc.h>
#include <sched.h>
#include <kmalloc.h>
#include <sync.h>
#include <string.h>
#include <stdio.h>
#include <clock.h>
#include <sbi.h>
#include <error.h>
list_entry_t proc_list,hash_list[HASH_SIZE];
struct proc_struct *current,*idleproc,*initproc;
int nr_process;
static int last_pid;
static size_t initial_pages;
static volatile unsigned workers_done,init_done;
extern void forkrets(struct trapframe *tf) __attribute__((noreturn));
extern void kernel_thread_entry(void);
extern int context_probe(uintptr_t seed);

/* LAB4 function2 2411020: explicit safe initial state, mm remains NULL. */
static struct proc_struct *alloc_proc(void) {
 struct proc_struct *p=kmalloc(sizeof(*p));if(!p)return NULL;
 memset(p,0,sizeof(*p));p->state=PROC_UNINIT;p->pid=-1;p->pgdir=boot_pgdir_pa;
 list_init(&p->list_link);list_init(&p->hash_link);return p;
}
char *set_proc_name(struct proc_struct *p,const char *name) {
 memset(p->name,0,sizeof(p->name));strncpy(p->name,name,PROC_NAME_LEN);return p->name;
}
struct proc_struct *find_proc(int pid) {
 if(pid==0)return idleproc;
 if(pid<1||pid>=MAX_PID)return NULL;
 list_entry_t *head=&hash_list[pid%HASH_SIZE];
 for(list_entry_t *l=list_next(head);l!=head;l=list_next(l)) {
  struct proc_struct *p=le2proc(l,hash_link);if(p->pid==pid)return p;
 }
 return NULL;
}
static int get_pid(void) {
 for(int i=0;i<MAX_PID-1;i++) {
  if(++last_pid>=MAX_PID)last_pid=1;
  if(!find_proc(last_pid))return last_pid;
 }
 return -E_NO_FREE_PROC;
}
static int setup_kstack(struct proc_struct *p) {
 struct Page *page=alloc_pages(KSTACKPAGE);if(!page)return -E_NO_MEM;
 p->kstack=(uintptr_t)page2kva(page);return 0;
}
static void put_kstack(struct proc_struct *p) {free_pages(kva2page((void *)p->kstack),KSTACKPAGE);}
static void forkret(void) {forkrets(current->tf);}
/* LAB4 function2 2411020: this stage does not clone user address spaces. */
static int copy_mm(uint32_t flags,struct proc_struct *p) {
 (void)flags;
 if(current->mm!=NULL)return -E_INVAL;
 p->mm=NULL;p->pgdir=boot_pgdir_pa;return 0;
}
static void copy_thread(struct proc_struct *p,uintptr_t esp,struct trapframe *tf) {
 p->tf=(struct trapframe *)(p->kstack+KSTACKSIZE-sizeof(*tf));
 *p->tf=*tf;p->tf->gpr.a0=0;
 p->tf->gpr.sp=esp?esp:(uintptr_t)p->tf;
 p->context.ra=(uintptr_t)forkret;p->context.sp=(uintptr_t)p->tf;
}
int do_fork(uint32_t flags,uintptr_t stack,struct trapframe *tf) {
 /* LAB4 function2 2411020: publish only after all fallible allocations. */
 if(nr_process>=MAX_PROCESS)return -E_NO_FREE_PROC;
 struct proc_struct *p=alloc_proc();if(!p)return -E_NO_MEM;
 int error=setup_kstack(p);if(error)goto bad_proc;
 error=copy_mm(flags,p);if(error)goto bad_stack;
 copy_thread(p,stack,tf);p->parent=current;
 bool intr_flag;local_intr_save(intr_flag);
 int pid=get_pid();
 if(pid<0){local_intr_restore(intr_flag);error=pid;goto bad_stack;}
 p->pid=pid;list_add_before(&proc_list,&p->list_link);
 list_add(&hash_list[pid%HASH_SIZE],&p->hash_link);nr_process++;
 wakeup_proc(p);local_intr_restore(intr_flag);return pid;
bad_stack:
 put_kstack(p);
bad_proc:
 kfree(p);return error;
}
int kernel_thread(int (*fn)(void *),void *arg,uint32_t flags) {
 /* LAB4 function2 2411020: template restored by forkrets -> __trapret. */
 if(!fn)return -E_INVAL;
 struct trapframe tf;memset(&tf,0,sizeof(tf));
 tf.gpr.s0=(uintptr_t)fn;tf.gpr.s1=(uintptr_t)arg;
 tf.status=(read_csr(sstatus)|SSTATUS_SPP|SSTATUS_SPIE)&~SSTATUS_SIE;
 tf.epc=(uintptr_t)kernel_thread_entry;
 return do_fork(flags|CLONE_VM,0,&tf);
}
void proc_run(struct proc_struct *p) {
 /* LAB4 function3 2411020: current, page table, context change atomically. */
 assert(p&&p->state==PROC_RUNNABLE);
 if(p==current)return;
 bool flag;local_intr_save(flag);
 struct proc_struct *prev=current;current=p;lsatp(p->pgdir);
 switch_to(&prev->context,&p->context);
 /* Resumed on the original thread's stack with its saved flag. */
 local_intr_restore(flag);
}
void thread_yield(void) {current->need_resched=1;schedule();}
void do_exit(int code) {
 assert(current!=idleproc);
 bool flag;local_intr_save(flag);(void)flag;
 current->exit_code=code;current->state=PROC_ZOMBIE;
 idleproc->need_resched=1;schedule();panic("zombie resumed");
}
static int worker(void *arg) {
 uintptr_t seed=(uintptr_t)arg;
 assert(current->parent==initproc && current->mm==NULL);
 /* Keep workers runnable across real timer delivery, while probing callee-saved registers. */
 do { assert(context_probe(seed)==0); } while(ticks<3);
 assert(read_csr(sstatus)&SSTATUS_SIE);
 workers_done++;return (int)seed;
}
static int init_main(void *arg) {
 assert(current==initproc && !strcmp(arg,"Hello kernel thread"));
 assert(current->pid==1 && find_proc(1)==current);
 assert(current->parent==idleproc);
 cprintf("initproc pid=%d parent=%d: %s\n",current->pid,current->parent->pid,(char *)arg);
 int a=kernel_thread(worker,(void *)0x120,0),b=kernel_thread(worker,(void *)0x240,0);
 assert(a>1 && b>a);set_proc_name(find_proc(a),"worker A");set_proc_name(find_proc(b),"worker B");
 while(workers_done<2)thread_yield();
 assert(context_probe(0x360)==0);init_done=1;return 0;
}
void proc_init(void) {
 /* LAB4 function2 2411020: idle wraps the boot execution, it is not forked. */
 static_assert(sizeof(struct context)==14*sizeof(uintptr_t));
 static_assert(sizeof(struct trapframe)==36*sizeof(uintptr_t));
 initial_pages=nr_free_pages();list_init(&proc_list);
 for(int i=0;i<HASH_SIZE;i++)list_init(&hash_list[i]);
 idleproc=alloc_proc();assert(idleproc);idleproc->pid=0;idleproc->state=PROC_RUNNABLE;
 idleproc->kstack=(uintptr_t)bootstack;idleproc->need_resched=1;
 set_proc_name(idleproc,"idle");current=idleproc;nr_process=1;
 int pid=kernel_thread(init_main,"Hello kernel thread",0);assert(pid==1);
 initproc=find_proc(pid);assert(initproc);set_proc_name(initproc,"init");
#ifdef LAB4_SELFTEST
 /* A sleeping init cannot be selected: scheduler must fall back to idle. */
 initproc->state=PROC_SLEEPING;schedule();assert(current==idleproc);
 wakeup_proc(initproc);idleproc->need_resched=1;
 size_t before=nr_free_pages();int count=nr_process;
 kmalloc_fail_after(0);
 assert(kernel_thread(worker,NULL,0)==-E_NO_MEM);
 kmalloc_fail_after(-1);
 assert(nr_free_pages()==before && nr_process==count);
 pmm_fail_after(0);
 assert(kernel_thread(worker,NULL,0)==-E_NO_MEM);
 pmm_fail_after(-1);
 assert(nr_free_pages()==before && nr_process==count);
 struct mm_struct dummy;memset(&dummy,0,sizeof(dummy));current->mm=&dummy;
 assert(kernel_thread(worker,NULL,0)==-E_INVAL);current->mm=NULL;
 assert(nr_free_pages()==before && nr_process==count);
 cprintf("fork failure cleanup and idle fallback: PASS\n");
#endif
 cprintf("proc_init(): idle=0 init=1, first-run frame PASS\n");
}
void cpu_idle(void) {
 /* LAB4 function3 2411020: only idle reaps; never free the executing stack. */
 for(;;) {
  if(current->need_resched)schedule();
  bool flag;local_intr_save(flag);
  list_entry_t *l=list_next(&proc_list);
  while(l!=&proc_list) {
   struct proc_struct *p=le2proc(l,list_link);l=list_next(l);
   if(p->state==PROC_ZOMBIE) {
    assert((p==initproc&&p->exit_code==0) || p->exit_code==0x120 || p->exit_code==0x240);
    list_del(&p->list_link);list_del(&p->hash_link);nr_process--;
    if(p==initproc)initproc=NULL;
    put_kstack(p);kfree(p);
   }
  }
  bool done=init_done&&workers_done==2&&nr_process==1;
  local_intr_restore(flag);
  if(done && ticks>=100) {
   assert(current==idleproc && list_empty(&proc_list));
   assert(nr_free_pages()+1==initial_pages); /* idle PCB owns one nonempty slab */
   assert(read_csr(sstatus)&SSTATUS_SIE);
   cprintf("thread context + first return + yields + exit/reap: PASS\n");
   cprintf("resource baseline restored (except idle PCB slab): PASS\n");
   cprintf("timer interrupts during threaded kernel ticks=%d: PASS\n",(int)ticks);
   cprintf("LAB4 SELF CHECKS PASSED\n");sbi_shutdown();panic("shutdown returned");
  }
  asm volatile("wfi");
 }
}
