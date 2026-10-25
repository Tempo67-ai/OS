#include <buddy_pmm.h>
#include <stdio.h>
#include <slub.h>

#define MAX_ORDER 15
static list_entry_t bins[MAX_ORDER + 1];
static size_t free_count;

static unsigned order_for(size_t n) {
    unsigned o = 0;
    while (o < MAX_ORDER && (1UL << o) < n) o++;
    return o;
}
static void put(struct Page *p, unsigned o) {
    p->property = 1UL << o;
    SetPageProperty(p);
    list_add(&bins[o], &p->page_link);
}
static void buddy_init(void) {
    for (unsigned o = 0; o <= MAX_ORDER; o++) list_init(&bins[o]);
    free_count = 0;
}
static void buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    for (struct Page *p = base; p < base + n; p++) {
        assert(PageReserved(p));
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }
    free_count += n;
    while (n) {
        unsigned o = MAX_ORDER;
        while ((1UL << o) > n || (page2ppn(base) & ((1UL << o) - 1))) o--;
        put(base, o);
        base += 1UL << o;
        n -= 1UL << o;
    }
}
static struct Page *buddy_alloc(size_t n) {
    if (!n || n > (1UL << MAX_ORDER)) return NULL;
    unsigned want = order_for(n), o = want;
    while (o <= MAX_ORDER && list_empty(&bins[o])) o++;
    if (o > MAX_ORDER) return NULL;
    struct Page *p = le2page(list_next(&bins[o]), page_link);
    list_del(&p->page_link);
    ClearPageProperty(p);
    while (o > want) {
        o--;
        put(p + (1UL << o), o);
    }
    p->property = 1UL << want;
    /* ref=-1 marks the allocated head, so invalid/double frees are rejected. */
    set_page_ref(p, -1);
    free_count -= p->property;
    return p;
}
static void buddy_free(struct Page *p, size_t n) {
    assert(n && n <= (1UL << MAX_ORDER));
    unsigned o = order_for(n);
    assert(p >= pages && p < pages + npage - nbase);
    assert(!PageReserved(p) && !PageProperty(p) && page_ref(p) == -1);
    assert(p->property == (1UL << o));
    set_page_ref(p, 0);
    free_count += 1UL << o;
    while (o < MAX_ORDER) {
        ppn_t ppn = page2ppn(p) ^ (1UL << o);
        if (ppn < nbase || ppn >= npage) break;
        struct Page *b = pages + ppn - nbase;
        if (!PageProperty(b) || b->property != (1UL << o)) break;
        list_del(&b->page_link);
        ClearPageProperty(b);
        b->property = 0;
        if (b < p) p = b;
        o++;
    }
    put(p, o);
}
static size_t buddy_nr_free(void) { return free_count; }
static void buddy_check(void) {
    size_t before = free_count;
    assert(buddy_alloc(0) == NULL && buddy_alloc((1UL << MAX_ORDER) + 1) == NULL);
    struct Page *a = buddy_alloc(3), *b = buddy_alloc(1);
    assert(a && b && a != b && (page2ppn(a) & 3) == 0);
    assert(free_count == before - 5);
    buddy_free(b, 1);
    buddy_free(a, 3);
    assert(free_count == before);
    /* Exhaustion, alignment, non-overlap, and restoration across every order. */
    struct Page *held[64];
    size_t sizes[64];
    unsigned used = 0;
    for (int o = MAX_ORDER; o >= 0; o--) {
        struct Page *p;
        while ((p = buddy_alloc(1UL << o)) != NULL) {
            assert(used < 64);
            assert((page2ppn(p) & ((1UL << o) - 1)) == 0);
            for (unsigned j = 0; j < used; j++)
                assert(p + (1UL << o) <= held[j] || held[j] + sizes[j] <= p);
            held[used] = p; sizes[used++] = 1UL << o;
        }
    }
    assert(free_count == 0 && buddy_alloc(1) == NULL);
    while (used) { used--; buddy_free(held[used], sizes[used]); }
    assert(free_count == before);
    a = buddy_alloc(before);
    assert(a && free_count == 0);
    buddy_free(a, before);
    struct Page *slots[32] = {0};
    size_t lens[32];
    unsigned seed = 2026;
    for (unsigned i = 0; i < 1000; i++) {
        seed = seed * 1664525U + 1013904223U;
        unsigned j = (seed >> 16) % 32;
        if (slots[j]) { buddy_free(slots[j], lens[j]); slots[j] = NULL; }
        else {
            lens[j] = 1 + (seed >> 24) % 17;
            slots[j] = buddy_alloc(lens[j]);
            /* A bounded arena may legitimately run out during mixed requests. */
            if (!slots[j]) continue;
            for (unsigned k = 0; k < 32; k++) if (k != j && slots[k])
                assert(slots[j] + (1UL << order_for(lens[j])) <= slots[k] ||
                       slots[k] + (1UL << order_for(lens[k])) <= slots[j]);
        }
    }
    for (unsigned j = 0; j < 32; j++) if (slots[j]) buddy_free(slots[j], lens[j]);
    assert(free_count == before);
    cprintf("[Buddy] split/coalesce/rounding/exhaustion/1000 operations: PASS\n");
}
const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager", .init = buddy_init, .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc, .free_pages = buddy_free,
    .nr_free_pages = buddy_nr_free, .check = buddy_check,
};
void buddy_demo_check(void) {
    const struct pmm_manager *saved = pmm_manager;
    size_t before = nr_free_pages();
    /* Isolate one aligned 256-page arena; never overwrite Best-Fit's metadata. */
    struct Page *arena = alloc_pages(511);
    assert(arena);
    struct Page *base = arena + ((256 - (page2ppn(arena) & 255)) & 255);
    for (unsigned i = 0; i < 256; i++) SetPageReserved(base + i);
    pmm_manager = &buddy_pmm_manager;
    buddy_init();
    buddy_init_memmap(base, 256);
    buddy_check();
    slub_check();
    /* Reinitialize a free arena as a deliberately unaligned 253-page range. */
    buddy_init();
    for (unsigned i = 0; i < 256; i++) { base[i].flags = base[i].property = 0; }
    for (unsigned i = 1; i < 254; i++) SetPageReserved(base + i);
    buddy_init_memmap(base + 1, 253);
    struct Page *blocks[256];
    for (unsigned i = 0; i < 253; i++) {
        blocks[i] = buddy_alloc(1);
        assert(blocks[i] >= base + 1 && blocks[i] < base + 254);
        for (unsigned j = 0; j < i; j++) assert(blocks[i] != blocks[j]);
    }
    assert(buddy_alloc(1) == NULL && free_count == 0);
    for (unsigned i = 0; i < 253; i++) buddy_free(blocks[i], 1);
    assert(free_count == 253 && buddy_alloc(256) == NULL);
    cprintf("[Buddy] unaligned 253-page interval boundaries: PASS\n");
    for (unsigned i = 0; i < 256; i++) { base[i].flags = base[i].property = 0; set_page_ref(base + i, 0); }
    pmm_manager = saved;
    free_pages(arena, 511);
    assert(nr_free_pages() == before);
}
