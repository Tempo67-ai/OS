// LAB2 2312418: simplified single-CPU SLUB-style size-class allocator.
#include <slub.h>
#include <pmm.h>
#include <stdio.h>
#include <string.h>
#define CLASSES 8
#define MAGIC 0x534c5542
struct slab {
    unsigned magic, class_id, used, capacity;
    size_t pages_count;
    uint64_t bitmap[4];
    list_entry_t link;
};
#define HEADER ROUNDUP(sizeof(struct slab), 16)
static list_entry_t caches[CLASSES];
static const size_t sizes[CLASSES] = {16,32,64,128,256,512,1024,2048};
void slub_init(void) {
    for (unsigned i = 0; i < CLASSES; i++) list_init(&caches[i]);
}
void *kmalloc(size_t size) {
    if (!size) return NULL;
    unsigned c = 0;
    while (c < CLASSES && sizes[c] < size) c++;
    if (c == CLASSES) {
        if (size > (size_t)-1 - HEADER - PGSIZE) return NULL;
        size_t n = (size + HEADER + PGSIZE - 1) / PGSIZE;
        struct Page *p = alloc_pages(n);
        if (!p) return NULL;
        struct slab *s = (void *)(page2pa(p) + va_pa_offset);
        memset(s, 0, HEADER);
        s->magic = MAGIC; s->class_id = CLASSES; s->pages_count = n;
        return (char *)s + HEADER;
    }
    list_entry_t *le;
    struct slab *s = NULL;
    for (le = list_next(&caches[c]); le != &caches[c]; le = list_next(le)) {
        struct slab *candidate = to_struct(le, struct slab, link);
        if (candidate->used < candidate->capacity) { s = candidate; break; }
    }
    if (!s) {
        struct Page *p = alloc_page();
        if (!p) return NULL;
        s = (void *)(page2pa(p) + va_pa_offset);
        memset(s, 0, HEADER);
        s->magic = MAGIC; s->class_id = c; s->pages_count = 1;
        s->capacity = (PGSIZE - HEADER) / sizes[c];
        assert(s->capacity <= 256);
        list_add(&caches[c], &s->link);
    }
    for (unsigned i = 0; i < s->capacity; i++) {
        uint64_t bit = (uint64_t)1 << (i % 64);
        if (!(s->bitmap[i / 64] & bit)) {
            s->bitmap[i / 64] |= bit; s->used++;
            return (char *)s + HEADER + i * sizes[c];
        }
    }
    panic("slub free-slot accounting");
}
void kfree(void *ptr) {
    if (!ptr) return;
    struct slab *s = (void *)ROUNDDOWN((uintptr_t)ptr, PGSIZE);
    assert(s->magic == MAGIC);
    if (s->class_id == CLASSES) {
        assert(ptr == (char *)s + HEADER);
        size_t n = s->pages_count;
        s->magic = 0;
        free_pages(pa2page(PADDR(s)), n);
        return;
    }
    assert(s->class_id < CLASSES);
    size_t offset = (char *)ptr - ((char *)s + HEADER);
    size_t slot = sizes[s->class_id], index = offset / slot;
    assert(offset % slot == 0 && index < s->capacity);
    uint64_t bit = (uint64_t)1 << (index % 64);
    assert(s->bitmap[index / 64] & bit);
    s->bitmap[index / 64] &= ~bit;
    assert(s->used > 0);
    if (--s->used == 0) {
        list_del(&s->link);
        s->magic = 0;
        free_page(pa2page(PADDR(s)));
    }
}
void slub_check(void) {
    size_t before = nr_free_pages();
    assert(kmalloc(0) == NULL);
    assert(kmalloc((size_t)-1) == NULL);
    kfree(NULL);
    // Independent byte patterns expose overlap/corruption between size classes.
    const size_t requests[] = {1,16,17,33,65,129,257,513,1025,2048,2049,5000};
    void *objects[12];
    for (unsigned i = 0; i < 12; i++) {
        objects[i] = kmalloc(requests[i]);
        assert(objects[i] && ((uintptr_t)objects[i] & 15) == 0);
        memset(objects[i], i + 1, requests[i]);
    }
    for (unsigned i = 0; i < 12; i++)
        for (size_t j = 0; j < requests[i]; j++)
            assert(((unsigned char *)objects[i])[j] == i + 1);
    for (unsigned i = 12; i > 0; i--) kfree(objects[i - 1]);
    assert(nr_free_pages() == before);
    void *one = kmalloc(64), *two = kmalloc(64);
    assert(one && two);
    kfree(one);
    void *again = kmalloc(64);
    assert(again == one);
    kfree(again);
    kfree(two);
    assert(nr_free_pages() == before);
    void *many[300];
    for (unsigned i = 0; i < 300; i++) {
        many[i] = kmalloc(16);
        assert(many[i]);
        memset(many[i], i & 255, 16);
    }
    for (unsigned i = 0; i < 300; i++)
        for (unsigned j = 0; j < 16; j++)
            assert(((unsigned char *)many[i])[j] == (i & 255));
    size_t held_pages = nr_free_pages();
    void *released = many[0];
    kfree(released);
    many[0] = kmalloc(16);
    assert(many[0] != NULL && nr_free_pages() == held_pages);
    for (unsigned i = 0; i < 300; i += 2) kfree(many[i]);
    for (unsigned i = 1; i < 300; i += 2) kfree(many[i]);
    assert(nr_free_pages() == before);
    cprintf("[SLUB] size classes, alignment, isolation, reuse, reclaim: PASS\n");
}
