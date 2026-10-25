#include <slub.h>
#include <pmm.h>
#include <string.h>
#include <stdio.h>

#define MAGIC 0x534c5542UL
#define BIG 0xffff
#define CLASSES 7
struct slab {
    size_t magic, kind, npages, capacity, inuse;
    struct slab *next;
    void *freelist;
    /* At most (4096-header)/32 < 128 objects per slab. */
    uint64_t allocated[2];
};
static const size_t sizes[CLASSES] = {32,64,128,256,512,1024,2048};
static struct slab *heads[CLASSES];
#define HEADER ROUNDUP(sizeof(struct slab), 16)
static void *page_va(struct Page *p) { return (void *)(page2pa(p) + va_pa_offset); }
void *kmalloc(size_t n) {
    if (!n) return NULL;
    unsigned k = 0;
    while (k < CLASSES && sizes[k] < n) k++;
    if (k == CLASSES) {
        if (n > (~(size_t)0) - HEADER - PGSIZE) return NULL;
        size_t np = ROUNDUP(n + HEADER, PGSIZE) / PGSIZE;
        struct Page *p = alloc_pages(np);
        if (!p) return NULL;
        struct slab *s = page_va(p);
        memset(s, 0, HEADER);
        s->magic = MAGIC; s->kind = BIG; s->npages = np;
        return (char *)s + HEADER;
    }
    struct slab *s = heads[k];
    while (s && !s->freelist) s = s->next;
    if (!s) {
        struct Page *p = alloc_page();
        if (!p) return NULL;
        s = page_va(p);
        memset(s, 0, HEADER);
        s->magic = MAGIC; s->kind = k; s->npages = 1;
        s->capacity = (PGSIZE - HEADER) / sizes[k];
        for (size_t i = 0; i < s->capacity; i++) {
            void *obj = (char *)s + HEADER + i * sizes[k];
            *(void **)obj = s->freelist; s->freelist = obj;
        }
        s->next = heads[k]; heads[k] = s;
    }
    void *obj = s->freelist;
    s->freelist = *(void **)obj;
    size_t i = ((char *)obj - (char *)s - HEADER) / sizes[k];
    assert(!(s->allocated[i / 64] & (1UL << (i % 64))));
    s->allocated[i / 64] |= 1UL << (i % 64);
    s->inuse++;
    return obj;
}
void kfree(void *ptr) {
    if (!ptr) return;
    struct slab *s = (void *)ROUNDDOWN((uintptr_t)ptr, PGSIZE);
    assert(s->magic == MAGIC);
    if (s->kind == BIG) {
        assert(ptr == (char *)s + HEADER);
        size_t np = s->npages;
        s->magic = 0;
        free_pages(pa2page(PADDR(s)), np);
        return;
    }
    assert(s->kind < CLASSES);
    size_t stride = sizes[s->kind];
    size_t off = (char *)ptr - (char *)s;
    assert(off >= HEADER && (off - HEADER) % stride == 0);
    size_t i = (off - HEADER) / stride;
    assert(i < s->capacity && (s->allocated[i / 64] & (1UL << (i % 64))));
    s->allocated[i / 64] &= ~(1UL << (i % 64));
    *(void **)ptr = s->freelist; s->freelist = ptr;
    if (--s->inuse == 0) {
        struct slab **link = &heads[s->kind];
        while (*link != s) { assert(*link); link = &(*link)->next; }
        *link = s->next;
        s->magic = 0;
        free_page(pa2page(PADDR(s)));
    }
}
void slub_check(void) {
    size_t before = nr_free_pages();
    const size_t ns[] = {1,31,32,33,63,64,65,127,128,129,255,256,257,
                        511,512,513,1023,1024,1025,2048,2049,4096,8193};
    void *ptrs[sizeof(ns)/sizeof(ns[0])];
    assert(kmalloc(0) == NULL && kmalloc(~(size_t)0) == NULL);
    kfree(NULL);
    for (size_t i = 0; i < sizeof(ns)/sizeof(ns[0]); i++) {
        ptrs[i] = kmalloc(ns[i]); assert(ptrs[i]);
        assert(((uintptr_t)ptrs[i] & 15) == 0);
        memset(ptrs[i], (int)(i + 1), ns[i]);
    }
    for (size_t i = 0; i < sizeof(ns)/sizeof(ns[0]); i++) {
        for (size_t j = 0; j < ns[i]; j++) assert(((uint8_t *)ptrs[i])[j] == i + 1);
        kfree(ptrs[i]);
    }
    void *many[200];
    for (unsigned i = 0; i < 200; i++) { many[i] = kmalloc(32); assert(many[i]); memset(many[i], i, 32); }
    for (unsigned i = 0; i < 200; i += 2) kfree(many[i]);
    for (unsigned i = 0; i < 200; i += 2) { many[i] = kmalloc(32); assert(many[i]); memset(many[i], i, 32); }
    for (unsigned i = 0; i < 200; i++) {
        for (unsigned j = 0; j < 32; j++) assert(((uint8_t *)many[i])[j] == (uint8_t)i);
        kfree(many[i]);
    }
    assert(kmalloc((nr_free_pages() + 1) * PGSIZE) == NULL);
    assert(nr_free_pages() == before);
    cprintf("[SLUB] boundaries/alignment/contents/reuse/large/OOM/reclaim: PASS\n");
}
