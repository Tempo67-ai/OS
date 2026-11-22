#include <default_pmm.h>

#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <sync.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);
static void paging_init(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    pmm_manager = &default_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
#ifdef LAB4_SELFTEST
static int alloc_budget = -1;
void pmm_fail_after(int n) { alloc_budget = n; }
#endif
struct Page *alloc_pages(size_t n) {
#ifdef LAB4_SELFTEST
    if (alloc_budget == 0) return NULL;
    if (alloc_budget > 0) alloc_budget--;
#endif
    struct Page *page = NULL;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        page = pmm_manager->alloc_pages(n);
    }
    local_intr_restore(intr_flag);
    return page;
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        pmm_manager->free_pages(base, n);
    }
    local_intr_restore(intr_flag);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    size_t ret;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        ret = pmm_manager->nr_free_pages();
    }
    local_intr_restore(intr_flag);
    return ret;
}

//lab2 请补充你在lab2中的代码
// lab3 中 memlayout.h 删去了 PHYSICAL_MEMORY_END，这里补上与 lab2 一致的定义（QEMU 默认 128MB 内存）
#ifndef PHYSICAL_MEMORY_END
#define PHYSICAL_MEMORY_END         0x88000000
#endif

static void
page_init(void) {
    extern char end[];
    const uintptr_t max_uintptr = ~(uintptr_t)0;
    uintptr_t memory_base = get_memory_base();
    uintptr_t memory_size = get_memory_size();
    uintptr_t memory_end, pages_pa, descriptors_end, free_begin;
    size_t page_count, descriptor_bytes;

    va_pa_offset = PHYSICAL_MEMORY_OFFSET;
    if (memory_base != DRAM_BASE || memory_base % PGSIZE != 0 ||
        memory_size == 0 || memory_size > max_uintptr - memory_base) {
        panic("page_init: invalid DTB memory range");
    }
    memory_end = memory_base + memory_size;
    if (memory_end > PHYSICAL_MEMORY_END) {
        memory_end = PHYSICAL_MEMORY_END;
    }
    memory_end = ROUNDDOWN(memory_end, PGSIZE);
    if (memory_end <= memory_base) {
        panic("page_init: no manageable physical pages");
    }

    npage = PPN(memory_end);
    page_count = npage - nbase;
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);
    pages_pa = PADDR(pages);
    if (pages_pa < memory_base || pages_pa >= memory_end ||
        page_count > (memory_end - pages_pa) / sizeof(struct Page)) {
        panic("page_init: no space for page descriptors");
    }

    descriptor_bytes = page_count * sizeof(struct Page);
    descriptors_end = pages_pa + descriptor_bytes;
    memset(pages, 0, descriptor_bytes);
    for (size_t i = 0; i < page_count; i++) {
        SetPageReserved(pages + i);
    }

    if (descriptors_end > max_uintptr - (PGSIZE - 1)) {
        panic("page_init: descriptor alignment overflow");
    }
    free_begin = ROUNDUP(descriptors_end, PGSIZE);
    if (free_begin < memory_end) {
        init_memmap(pa2page(free_begin), PPN(memory_end - free_begin));
    }

    cprintf("physical memory: [0x%016lx, 0x%016lx)\n",
            memory_base, memory_end);
    cprintf("free memory: [0x%016lx, 0x%016lx)\n",
            free_begin, memory_end);
}


/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    paging_init();
    check_pgdir();
    satp_virtual=boot_pgdir_va;
    satp_physical=boot_pgdir_pa;
    cprintf("active root virtual: 0x%016llx physical: 0x%016llx\n",
            (uintptr_t)boot_pgdir_va,boot_pgdir_pa);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}

pde_t *boot_pgdir_va;
uintptr_t boot_pgdir_pa;

/* LAB4 2411020: allocate missing table pages transactionally. No superpage splitting. */
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create) {
    uintptr_t upper = la >> 39;
    if (upper != ((la & (1UL<<38)) ? ((1UL<<25)-1) : 0)) return NULL;
    pte_t *slot = &pgdir[PDX1(la)], *created[2];
    struct Page *owned[2];
    unsigned n = 0;
    for (int level = 2; level > 0; level--) {
        if (!(*slot & PTE_V)) {
            if (!create) goto fail;
            struct Page *p = alloc_page();
            if (p == NULL) goto fail;
            memset(page2kva(p), 0, PGSIZE);
            set_page_ref(p, 1);
            created[n] = slot; owned[n++] = p;
            /* Sv39 nonleaf U/A/D must be zero; only V is needed. */
            *slot = pte_create(page2ppn(p), PTE_V);
        } else if (*slot & PTE_LEAF) goto fail;
        pte_t *next = KADDR(PTE_ADDR(*slot));
        slot = &next[level == 2 ? PDX0(la) : PTX(la)];
    }
    return slot;
fail:
    while (n) {
        n--; *created[n] = 0; set_page_ref(owned[n], 0); free_page(owned[n]);
    }
    return NULL;
}

void tlb_invalidate(pde_t *pgdir, uintptr_t la) {
    if ((read_csr(satp) & ((1UL<<44)-1)) == PPN(PADDR(pgdir)))
        asm volatile("sfence.vma %0, zero" :: "r"(la) : "memory");
}

struct Page *get_page(pde_t *pgdir, uintptr_t la, pte_t **store) {
    pte_t *pte = get_pte(pgdir, la, 0);
    if (store) *store = pte;
    return pte && (*pte & PTE_V) ? pte2page(*pte) : NULL;
}

void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *pte) {
    if (*pte & PTE_V) {
        struct Page *page = pte2page(*pte);
        *pte = 0;
        tlb_invalidate(pgdir, la);
        if (page_ref_dec(page) == 0) free_page(page);
    }
}

/* LAB4 function1 2411020: same-page reinsertion must not free the page. */
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm) {
    pte_t *pte = get_pte(pgdir, la, 1);
    if (!pte) return -E_NO_MEM;
    page_ref_inc(page);
    if (*pte & PTE_V) {
        if (pte2page(*pte) == page) page_ref_dec(page);
        else page_remove_pte(pgdir, la, pte);
    }
    /* Valid leaves need R or X; write implies read under the base Sv39 rules. */
    perm &= PTE_R | PTE_W | PTE_X | PTE_U;
    if (!(perm & (PTE_R|PTE_X)) || (perm & PTE_W)) perm |= PTE_R;
    *pte = pte_create(page2ppn(page), perm | PTE_V | PTE_A | PTE_D);
    tlb_invalidate(pgdir, la);
    return 0;
}

/* LAB4 function1 2411020: absent mapping is an idempotent no-op. */
void page_remove(pde_t *pgdir, uintptr_t la) {
    pte_t *pte = get_pte(pgdir, la, 0);
    if (pte) page_remove_pte(pgdir, la, pte);
}

/* Destroy only privately allocated test/address-space trees, never the boot tree. */
static void destroy_level(pte_t *table, int level) {
    for (unsigned i=0; i<512; i++) if (table[i]&PTE_V) {
        assert(!(table[i]&PTE_LEAF) || level==0);
        struct Page *page = pte2page(table[i]);
        if (level) destroy_level(page2kva(page), level-1);
        if (page_ref_dec(page)==0) free_page(page);
        table[i]=0;
    }
}
void page_table_destroy(pde_t *pgdir) {
    assert(pgdir != boot_pgdir_va);
    assert((read_csr(satp)&((1UL<<44)-1)) != PPN(PADDR(pgdir)));
    destroy_level(pgdir,2);
    struct Page *root=kva2page(pgdir);set_page_ref(root,0);free_page(root);
}

static void paging_init(void) {
    struct Page *root=alloc_page(); assert(root);
    set_page_ref(root,1);boot_pgdir_va=page2kva(root);boot_pgdir_pa=page2pa(root);
    memset(boot_pgdir_va,0,PGSIZE);
    /* Direct kernel mappings do not own/refcount RAM pages. */
    for (uintptr_t pa=DRAM_BASE; pa<npage*PGSIZE; pa+=PGSIZE) {
        pte_t *pte=get_pte(boot_pgdir_va,pa+PHYSICAL_MEMORY_OFFSET,1);assert(pte);
        *pte=pte_create(PPN(pa),PTE_V|PTE_R|PTE_W|PTE_X|PTE_A|PTE_D);
    }
    lsatp(boot_pgdir_pa);
    cprintf("Sv39 kernel 4K mappings: PASS\n");
}

void check_pgdir(void) {
    size_t before=nr_free_pages();
    struct Page *root=alloc_page();assert(root);set_page_ref(root,1);
    pde_t *pg=page2kva(root);memset(pg,0,PGSIZE);
    struct Page *a=alloc_page(), *b=alloc_page();assert(a&&b);
    assert(get_page(pg,0,NULL)==NULL);
    page_remove(pg,0);
    assert(page_insert(pg,a,0,0)==0 && page_ref(a)==1);
    assert(page_insert(pg,a,0,PTE_R|PTE_U)==0 && page_ref(a)==1);
    assert(page_insert(pg,b,PGSIZE,PTE_W|PTE_U)==0 && page_ref(b)==1);
    assert((*get_pte(pg,PGSIZE,0)&(PTE_R|PTE_W|PTE_U))==(PTE_R|PTE_W|PTE_U));
    assert(!(pg[0]&(PTE_U|PTE_A|PTE_D|PTE_LEAF)));
    assert(page_insert(pg,a,PGSIZE,PTE_R)==0);
    assert(page_ref(a)==2 && page_ref(b)==0);
    page_remove(pg,0); assert(page_ref(a)==1);
    page_remove(pg,PGSIZE); assert(page_ref(a)==0);
    page_remove(pg,PGSIZE);
#ifdef LAB4_SELFTEST
    struct Page *candidate=alloc_page();assert(candidate);
    size_t clean=nr_free_pages();
    for (int fail=0; fail<=1; fail++) {
        pmm_fail_after(fail);
        assert(page_insert(pg,candidate,1UL<<30,PTE_R)==-E_NO_MEM);
        pmm_fail_after(-1);
        assert(pg[1]==0 && nr_free_pages()==clean && page_ref(candidate)==0);
    }
    free_page(candidate);
    assert(get_pte(pg,1UL<<39,1)==NULL); /* noncanonical Sv39 address */
    pg[2]=pte_create(PPN(DRAM_BASE),PTE_V|PTE_R|PTE_X);
    assert(get_pte(pg,2UL<<30,1)==NULL); /* reject a superpage, do not corrupt it */
    pg[2]=0;
    cprintf("Page table allocation failure rollback: PASS\n");
#endif
    page_table_destroy(pg);assert(nr_free_pages()==before);
    /* Exercise hardware translation and TLB replacement on the ACTIVE root. */
    before=nr_free_pages();
    a=alloc_page();b=alloc_page();assert(a&&b);
    *(volatile uintptr_t *)page2kva(a)=0x12345678;
    *(volatile uintptr_t *)page2kva(b)=0xabcdef;
    uintptr_t va=0x40000000;
    assert(page_insert(boot_pgdir_va,a,va,PTE_R|PTE_W)==0);
    assert(*(volatile uintptr_t *)va==0x12345678);
    assert(page_insert(boot_pgdir_va,b,va,PTE_R|PTE_W)==0);
    assert(*(volatile uintptr_t *)va==0xabcdef);
    *(volatile uintptr_t *)va=0x4242;
    assert(*(volatile uintptr_t *)page2kva(b)==0x4242);
    page_remove(boot_pgdir_va,va);
    pte_t *middle=KADDR(PDE_ADDR(boot_pgdir_va[PDX1(va)]));
    struct Page *low=pte2page(middle[PDX0(va)]);
    middle[PDX0(va)]=0;set_page_ref(low,0);free_page(low);
    struct Page *mid=pte2page(boot_pgdir_va[PDX1(va)]);
    boot_pgdir_va[PDX1(va)]=0;set_page_ref(mid,0);free_page(mid);flush_tlb();
    assert(nr_free_pages()==before);
    cprintf("check_pgdir() succeeded! refcounts, replace, remove, hardware TLB: PASS\n");
}
