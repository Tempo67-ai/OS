#ifndef LAB4_PMM_H
#define LAB4_PMM_H
#include <assert.h>
#include <memlayout.h>
#include <mmu.h>
#include <riscv.h>
struct pmm_manager {
 const char *name;
 void (*init)(void);
 void (*init_memmap)(struct Page *,size_t);
 struct Page *(*alloc_pages)(size_t);
 void (*free_pages)(struct Page *,size_t);
 size_t (*nr_free_pages)(void);
 void (*check)(void);
};
extern const struct pmm_manager *pmm_manager;
extern struct Page *pages;
extern size_t npage;
extern const size_t nbase;
extern uint64_t va_pa_offset;
extern pde_t *boot_pgdir_va;
extern uintptr_t boot_pgdir_pa;
#define boot_pgdir boot_pgdir_va
extern char bootstack[],bootstacktop[];
void pmm_init(void);
struct Page *alloc_pages(size_t n);
void free_pages(struct Page *p,size_t n);
size_t nr_free_pages(void);
#define alloc_page() alloc_pages(1)
#define free_page(p) free_pages(p,1)
static inline ppn_t page2ppn(struct Page *p) { return p-pages+nbase; }
static inline uintptr_t page2pa(struct Page *p) { return page2ppn(p)<<PGSHIFT; }
static inline int page_ref(struct Page *p) { return p->ref; }
static inline void set_page_ref(struct Page *p,int n) { p->ref=n; }
static inline int page_ref_inc(struct Page *p) { return ++p->ref; }
static inline int page_ref_dec(struct Page *p) { assert(p->ref>0);return --p->ref; }
static inline struct Page *pa2page(uintptr_t pa) {
 assert(PPN(pa)>=nbase && PPN(pa)<npage);return pages+(PPN(pa)-nbase);
}
static inline uintptr_t paddr(uintptr_t va) { assert(va>=PHYSICAL_MEMORY_OFFSET+DRAM_BASE);return va-PHYSICAL_MEMORY_OFFSET; }
#define PADDR(va) paddr((uintptr_t)(va))
static inline void *kaddr(uintptr_t pa) { assert(PPN(pa)>=nbase && PPN(pa)<npage);return (void *)(pa+PHYSICAL_MEMORY_OFFSET); }
#define KADDR(pa) kaddr((uintptr_t)(pa))
static inline void *page2kva(struct Page *p) { return KADDR(page2pa(p)); }
static inline struct Page *kva2page(void *va) { return pa2page(PADDR(va)); }
static inline struct Page *pte2page(pte_t pte) { assert(pte&PTE_V);return pa2page(PTE_ADDR(pte)); }
#define pde2page(pde) pte2page(pde)
static inline pte_t pte_create(ppn_t ppn,uint32_t perm) { return (ppn<<10)|perm; }
static inline void flush_tlb(void) { asm volatile("sfence.vma" ::: "memory"); }
static inline void lsatp(uintptr_t pa) { write_csr(satp,(8UL<<60)|(pa>>12));flush_tlb(); }
void tlb_invalidate(pde_t *pgdir,uintptr_t la);
pte_t *get_pte(pde_t *pgdir,uintptr_t la,bool create);
struct Page *get_page(pde_t *pgdir,uintptr_t la,pte_t **ptep);
int page_insert(pde_t *pgdir,struct Page *page,uintptr_t la,uint32_t perm);
void page_remove(pde_t *pgdir,uintptr_t la);
void page_remove_pte(pde_t *pgdir,uintptr_t la,pte_t *ptep);
void page_table_destroy(pde_t *pgdir);
void check_pgdir(void);
#ifdef LAB4_SELFTEST
void pmm_fail_after(int count);
#endif
#endif
