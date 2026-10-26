#ifndef SLUB_H
#define SLUB_H
#include <defs.h>
void slub_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void slub_check(void);
#endif
