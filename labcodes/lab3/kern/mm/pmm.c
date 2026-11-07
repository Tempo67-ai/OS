#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <../sync/sync.h>
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
struct Page *alloc_pages(size_t n) {
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

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
