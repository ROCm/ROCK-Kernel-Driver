#ifndef _PPC64_RMAP_H
#define _PPC64_RMAP_H

/* PPC64 calls pte_alloc() before mem_map[] is setup ... */
#define BROKEN_PPC_PTE_ALLOC_ONE

#include <asm-generic/rmap.h>

#endif
