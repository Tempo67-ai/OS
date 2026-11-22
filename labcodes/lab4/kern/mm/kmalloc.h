#ifndef LAB4_KMALLOC_H
#define LAB4_KMALLOC_H
#include <defs.h>
void kmalloc_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void check_kmalloc(void);
#ifdef LAB4_SELFTEST
void kmalloc_fail_after(int n);
#endif
#endif
