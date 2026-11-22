#ifndef LAB4_VMM_H
#define LAB4_VMM_H
#include <pmm.h>
#define VM_READ 1
#define VM_WRITE 2
#define VM_EXEC 4
struct mm_struct {
 list_entry_t mmap_list;
 struct vma_struct *mmap_cache;
 pde_t *pgdir;
 int map_count;
};
struct vma_struct {
 struct mm_struct *vm_mm;
 uintptr_t vm_start,vm_end;
 uint32_t vm_flags;
 list_entry_t list_link;
};
struct mm_struct *mm_create(void);
struct vma_struct *vma_create(uintptr_t start,uintptr_t end,uint32_t flags);
void insert_vma_struct(struct mm_struct *mm,struct vma_struct *vma);
struct vma_struct *find_vma(struct mm_struct *mm,uintptr_t addr);
void mm_destroy(struct mm_struct *mm);
void vmm_init(void);
#endif
