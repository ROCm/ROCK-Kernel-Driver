#ifndef _LINUX_MMAN_H
#define _LINUX_MMAN_H

#include <asm/mman.h>

#define MREMAP_MAYMOVE	1
#define MREMAP_FIXED	2

extern int vm_enough_memory(long pages);
extern void vm_unacct_memory(long pages);

#endif /* _LINUX_MMAN_H */
