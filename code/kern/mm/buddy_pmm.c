// LAB2 2312418: Buddy implementation; orders use DRAM-relative page indices.
#include <buddy_pmm.h>
#include <stdio.h>
#define MAX_ORDER 20
static list_entry_t areas[MAX_ORDER + 1];
static size_t available;
static size_t managed_start, managed_end;

static unsigned order_for(size_t n) {
    unsigned order = 0;
    while (((size_t)1 << order) < n && order < MAX_ORDER) order++;
    return order;
}
static void put(struct Page *p, unsigned order) {
    p->property = (size_t)1 << order;
    SetPageProperty(p);
    list_add(&areas[order], &p->page_link);
}
static void buddy_init(void) {
    for (unsigned i = 0; i <= MAX_ORDER; i++) list_init(&areas[i]);
    available = 0;
    managed_start = npage - nbase; managed_end = 0;
}
static void release(struct Page *base, unsigned order) {
    size_t index = base - pages;
    while (order < MAX_ORDER) {
        size_t other = index ^ ((size_t)1 << order);
        if (other < managed_start || other >= managed_end) break;
        struct Page *p = pages + other;
        if (!PageProperty(p) || p->property != ((size_t)1 << order)) break;
        list_del(&p->page_link);
        ClearPageProperty(p);
        p->property = 0;
        if (other < index) index = other;
        order++;
    }
    put(pages + index, order);
}
static void buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    for (size_t i = 0; i < n; i++) {
        assert(PageReserved(base + i));
        base[i].flags = 0;
        base[i].property = 0;
        base[i].ref = 0;
    }
    // This teaching manager accepts one contiguous range per initialization.
    assert(managed_end == 0);
    managed_start = base - pages;
    managed_end = managed_start + n;
    available += n;
    while (n) {
        size_t index = base - pages;
        unsigned order = MAX_ORDER;
        while (((size_t)1 << order) > n ||
               (index & (((size_t)1 << order) - 1))) order--;
        release(base, order);
        base += (size_t)1 << order;
        n -= (size_t)1 << order;
    }
}
static struct Page *buddy_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > ((size_t)1 << MAX_ORDER)) return NULL;
    unsigned wanted = order_for(n), order = wanted;
    if (((size_t)1 << wanted) > available) return NULL;
    while (order <= MAX_ORDER && list_empty(&areas[order])) order++;
    if (order > MAX_ORDER) return NULL;
    struct Page *p = le2page(list_next(&areas[order]), page_link);
    list_del(&p->page_link);
    ClearPageProperty(p);
    p->property = 0;
    while (order > wanted) {
        order--;
        put(p + ((size_t)1 << order), order);
    }
    size_t count = (size_t)1 << wanted;
    for (size_t i = 0; i < count; i++) {
        p[i].property = 0;
        // ref is kept available for normal page reference counting.
        p[i].ref = 0;
    }
    // Remember allocated extent at its head, without marking it as free.
    p->property = count;
    available -= count;
    return p;
}
static void buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0 && n <= ((size_t)1 << MAX_ORDER));
    unsigned order = order_for(n);
    size_t count = (size_t)1 << order;
    assert(base >= pages && base + count <= pages + npage - nbase);
    assert(((base - pages) & (count - 1)) == 0);
    assert(!PageProperty(base) && base->property == count);
    for (size_t i = 0; i < count; i++) {
        assert(!PageReserved(base + i) && !PageProperty(base + i));
        base[i].ref = 0;
        base[i].property = 0;
    }
    available += count;
    release(base, order);
}
static size_t buddy_nr_free_pages(void) { return available; }
static void invariant(void) {
    size_t total = 0;
    for (unsigned order = 0; order <= MAX_ORDER; order++) {
        list_entry_t *le;
        for (le = list_next(&areas[order]); le != &areas[order]; le = list_next(le)) {
            struct Page *p = le2page(le, page_link);
            assert(PageProperty(p) && p->property == ((size_t)1 << order));
            assert(((p - pages) & (p->property - 1)) == 0);
            size_t other = (p - pages) ^ p->property;
            if (order < MAX_ORDER && other >= managed_start && other < managed_end)
                assert(!PageProperty(pages + other) ||
                       pages[other].property != p->property);
            total += p->property;
        }
    }
    assert(total == available);
}
static void buddy_check(void) {
    size_t before = available;
    invariant();
    struct Page *a = alloc_pages(3), *b = alloc_pages(1);
    assert(a && b && a->property == 4 && b->property == 1);
    assert(b < a || b >= a + 4);
    assert(available == before - 5);
    free_pages(a, 3);
    free_pages(b, 1);
    assert(available == before);
    invariant();
    cprintf("[Buddy] rounding, split, merge and accounting: PASS\n");
}
const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager", .init = buddy_init,
    .init_memmap = buddy_init_memmap, .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages, .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};

// Independent 64-page pool: tests never overwrite the active PMM's free lists.
void buddy_check_pool(void) {
    const struct pmm_manager *saved = pmm_manager;
    assert(saved != &buddy_pmm_manager);
    size_t original = nr_free_pages();
    struct Page *pool = alloc_pages(64);
    assert(pool);
    for (size_t i = 0; i < 64; i++) SetPageReserved(pool + i);
    pmm_manager = &buddy_pmm_manager;
    buddy_init();
    buddy_init_memmap(pool, 64);
    buddy_check();
    struct Page *items[64];
    for (size_t i = 0; i < 64; i++) {
        items[i] = alloc_page();
        assert(items[i] >= pool && items[i] < pool + 64);
        for (size_t j = 0; j < i; j++) assert(items[i] != items[j]);
    }
    assert(alloc_page() == NULL && available == 0);
    // Two adjacent pages straddling a buddy-pair boundary must NOT coalesce.
    size_t odd = ((pool - pages) & 1) ? 0 : 1;
    free_page(pool + odd);
    free_page(pool + odd + 1);
    assert(available == 2 && alloc_pages(2) == NULL);
    assert(alloc_page() != NULL);
    assert(alloc_page() != NULL);
    assert(available == 0);
    // All pool pages are allocated again; use their physical order for recovery.
    for (size_t i = 0; i < 64; i++) items[i] = pool + i;
    // Reverse/alternating frees exercise recursively reconstructed partners.
    for (size_t i = 0; i < 64; i += 2) free_page(items[i]);
    invariant();
    for (size_t i = 1; i < 64; i += 2) free_page(items[i]);
    invariant();
    assert(available == 64);
    assert(alloc_pages(65) == NULL);
    // Drain again to return all descriptors to allocated state before restoring.
    for (size_t i = 0; i < 64; i++) assert(alloc_page() != NULL);
    pmm_manager = saved;
    if (saved == &buddy_pmm_manager) {
        // This helper is only called when a different allocator owns the pool.
        panic("buddy_check_pool called with buddy active");
    }
    free_pages(pool, 64);
    assert(nr_free_pages() == original);
    cprintf("[Buddy] exhaustion, non-overlap, boundaries, recovery: PASS\n");
}
