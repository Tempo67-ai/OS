#include <vmm.h>
#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
struct mm_struct *mm_create(void) {
 struct mm_struct *mm=kmalloc(sizeof(*mm));if(!mm)return NULL;
 memset(mm,0,sizeof(*mm));list_init(&mm->mmap_list);return mm;
}
struct vma_struct *vma_create(uintptr_t start,uintptr_t end,uint32_t flags) {
 if(start>=end)return NULL;
 struct vma_struct *vma=kmalloc(sizeof(*vma));if(!vma)return NULL;
 memset(vma,0,sizeof(*vma));vma->vm_start=start;vma->vm_end=end;vma->vm_flags=flags;list_init(&vma->list_link);return vma;
}
void insert_vma_struct(struct mm_struct *mm,struct vma_struct *vma) {
 assert(mm&&vma&&!vma->vm_mm&&vma->vm_start<vma->vm_end);
 list_entry_t *l=list_next(&mm->mmap_list);
 for(;l!=&mm->mmap_list;l=list_next(l)) {
  struct vma_struct *cur=to_struct(l,struct vma_struct,list_link);
  if(vma->vm_end<=cur->vm_start)break;
  assert(vma->vm_start>=cur->vm_end);
 }
 list_add_before(l,&vma->list_link);vma->vm_mm=mm;mm->map_count++;mm->mmap_cache=NULL;
}
struct vma_struct *find_vma(struct mm_struct *mm,uintptr_t addr) {
 if(!mm)return NULL;
 struct vma_struct *v=mm->mmap_cache;
 if(v&&v->vm_start<=addr&&addr<v->vm_end)return v;
 for(list_entry_t *l=list_next(&mm->mmap_list);l!=&mm->mmap_list;l=list_next(l)) {
  v=to_struct(l,struct vma_struct,list_link);
  if(addr<v->vm_start)break;
  if(addr<v->vm_end){mm->mmap_cache=v;return v;}
 }
 return NULL;
}
void mm_destroy(struct mm_struct *mm) {
 if(!mm)return;
 while(!list_empty(&mm->mmap_list)) {
  struct vma_struct *v=to_struct(list_next(&mm->mmap_list),struct vma_struct,list_link);
  list_del(&v->list_link);kfree(v);
 }
 /* This lab's kernel threads share boot_pgdir; mm owns VMA descriptors only. */
 kfree(mm);
}
void vmm_init(void) {
 size_t before=nr_free_pages();struct mm_struct *mm=mm_create();assert(mm);
 for(int i=9;i>=0;i--){struct vma_struct *v=vma_create(i*0x2000,i*0x2000+PGSIZE,VM_READ|VM_WRITE);assert(v);insert_vma_struct(mm,v);}
 assert(mm->map_count==10);
 for(int i=0;i<10;i++){assert(find_vma(mm,i*0x2000));assert(find_vma(mm,i*0x2000+PGSIZE-1));assert(!find_vma(mm,i*0x2000+PGSIZE));}
 assert(!vma_create(10,10,VM_READ));mm_destroy(mm);assert(nr_free_pages()==before);
 cprintf("vmm_init(): ordered VMA, cache, gaps, destruction PASS\n");
}
