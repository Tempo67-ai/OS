#ifndef __KERN_MM_MMU_H__
#define __KERN_MM_MMU_H__
#ifndef __ASSEMBLER__
#include <defs.h>
#endif
#define PGSIZE 4096
#define PGSHIFT 12
#define PPN(la) ((uintptr_t)(la) >> PGSHIFT)
#define PGOFF(la) ((uintptr_t)(la) & 0xfff)
#define PDX1SHIFT 30
#define PDX0SHIFT 21
#define PTXSHIFT 12
#define PDX1(la) (((uintptr_t)(la) >> 30) & 511)
#define PDX0(la) (((uintptr_t)(la) >> 21) & 511)
#define PTX(la) (((uintptr_t)(la) >> 12) & 511)
#define NPDEENTRY 512
#define NPTEENTRY 512
#define PTSIZE (PGSIZE * NPTEENTRY)
#define PGADDR(d1,d0,t,o) (((uintptr_t)(d1)<<30)|((uintptr_t)(d0)<<21)|((uintptr_t)(t)<<12)|(o))
#define PTE_PPN_SHIFT 10
#define PTE_ADDR(pte) ((((uintptr_t)(pte) >> 10) & ((1UL<<44)-1)) << 12)
#define PDE_ADDR(pde) PTE_ADDR(pde)
#define PTE_V 0x001
#define PTE_R 0x002
#define PTE_W 0x004
#define PTE_X 0x008
#define PTE_U 0x010
#define PTE_G 0x020
#define PTE_A 0x040
#define PTE_D 0x080
#define PTE_LEAF (PTE_R|PTE_W|PTE_X)
#endif
