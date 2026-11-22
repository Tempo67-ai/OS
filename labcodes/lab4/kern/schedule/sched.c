#include <sched.h>
#include <sync.h>
/* LAB4 function3 2411020: cooperative circular scan, idle fallback. */
void schedule(void) {
 bool flag;local_intr_save(flag);
 current->need_resched=0;
 list_entry_t *start=current==idleproc?&proc_list:&current->list_link;
 list_entry_t *l=start;struct proc_struct *next=NULL;
 do {
  l=list_next(l);
  if(l!=&proc_list) {
   struct proc_struct *p=le2proc(l,list_link);
   if(p->state==PROC_RUNNABLE){next=p;break;}
  }
 } while(l!=start);
 if(!next)next=idleproc;
 next->runs++;
 proc_run(next);
 local_intr_restore(flag);
}
void wakeup_proc(struct proc_struct *p) {
 bool flag;local_intr_save(flag);
 assert(p&&p->state!=PROC_ZOMBIE);p->state=PROC_RUNNABLE;
 local_intr_restore(flag);
}
