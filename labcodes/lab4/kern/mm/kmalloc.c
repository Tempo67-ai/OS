#include <stdio.h>

#include <kmalloc.h>
#include <pmm.h>
#include <sync.h>
#include <string.h>
#define HEADER 128
#define MAGIC 0x4b4d414c4c4f43UL
struct slab {
    uintptr_t magic;
    list_entry_t link;
    size_t size, count, used, npages;
    void *free;
    uintptr_t bits[4];
};
static list_entry_t classes[7];
void kmalloc_init(void) {
    static_assert(sizeof(struct slab)<=HEADER);
    for (int i=0;i<7;i++) list_init(&classes[i]);
}
/* Small objects use 32..2048 byte slab classes; empty slabs return their page. */
#ifdef LAB4_SELFTEST
static int kmalloc_budget=-1;
void kmalloc_fail_after(int n) { kmalloc_budget=n; }
#endif
void *kmalloc(size_t n) {
#ifdef LAB4_SELFTEST
    if(kmalloc_budget==0)return NULL;
    if(kmalloc_budget>0)kmalloc_budget--;
#endif
    if (!n || n>128*PGSIZE-HEADER) return NULL;
    bool flag;local_intr_save(flag);
    size_t size=32;int c=0;while(size<n && c<6) {size*=2;c++;}
    struct slab *s=NULL;
    if (n<=2048) {
        for (list_entry_t *l=list_next(&classes[c]);l!=&classes[c];l=list_next(l)) {
            struct slab *candidate=to_struct(l,struct slab,link);
            if (candidate->free) {s=candidate;break;}
        }
    }
    if (!s) {
        size_t np=n>2048?(n+HEADER+PGSIZE-1)/PGSIZE:1;
        struct Page *p=alloc_pages(np);if (!p) {local_intr_restore(flag);return NULL;}
        s=page2kva(p);memset(s,0,HEADER);s->magic=MAGIC;s->npages=np;
        s->size=n>2048?n:size;s->count=n>2048?1:(PGSIZE-HEADER)/size;
        for (size_t i=s->count;i>0;i--) {
            void **obj=(void **)((uintptr_t)s+HEADER+(i-1)*s->size);
            *obj=s->free;s->free=obj;
        }
        list_init(&s->link);
        if (n<=2048) list_add(&classes[c],&s->link);
    }
    void *obj=s->free;s->free=*(void **)obj;s->used++;
    size_t i=((uintptr_t)obj-(uintptr_t)s-HEADER)/s->size;
    assert(!(s->bits[i/64]&(1UL<<(i%64))));s->bits[i/64]|=1UL<<(i%64);
    local_intr_restore(flag);return obj;
}
void kfree(void *ptr) {
    if (!ptr) return;
    bool flag;local_intr_save(flag);
    struct slab *s=(void *)ROUNDDOWN((uintptr_t)ptr,PGSIZE);
    assert(s->magic==MAGIC && (uintptr_t)ptr>=(uintptr_t)s+HEADER);
    size_t off=(uintptr_t)ptr-(uintptr_t)s-HEADER,i=off/s->size;
    assert(off%s->size==0 && i<s->count && (s->bits[i/64]&(1UL<<(i%64))));
    s->bits[i/64]&=~(1UL<<(i%64));s->used--;
    *(void **)ptr=s->free;s->free=ptr;
    if (!s->used) {
        size_t np=s->npages;list_del(&s->link);s->magic=0;free_pages(kva2page(s),np);
    }
    local_intr_restore(flag);
}
void check_kmalloc(void) {
    size_t before=nr_free_pages();
    void *p[140];
    for (unsigned i=0;i<140;i++) {p[i]=kmalloc(24);assert(p[i]);memset(p[i],(char)i,24);}
    for (unsigned i=0;i<140;i++) assert(((unsigned char *)p[i])[23]==(unsigned char)i);
    for (unsigned i=0;i<140;i+=2) kfree(p[i]);
    for (unsigned i=1;i<140;i+=2) kfree(p[i]);
    for (size_t n=33;n<5000;n*=2) {void *a=kmalloc(n);assert(a);memset(a,0x5a,n);kfree(a);}
    assert(kmalloc(0)==NULL && nr_free_pages()==before);
    cprintf("check_kmalloc(): slab reuse and reclamation PASS\n");
}
