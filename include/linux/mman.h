#ifndef _LINUX_MMAN_H
#define _LINUX_MMAN_H

#include <linux/config.h>

#include <asm/atomic.h>
#include <asm/mman.h>

#define MREMAP_MAYMOVE	1
#define MREMAP_FIXED	2

extern int vm_enough_memory(long pages);
extern atomic_t vm_committed_space;

#ifdef CONFIG_SMP
extern void vm_acct_memory(long pages);
#else
static inline void vm_acct_memory(long pages)
{
	atomic_add(pages, &vm_committed_space);
}
#endif

static inline void vm_unacct_memory(long pages)
{
	vm_acct_memory(-pages);
}

#endif /* _LINUX_MMAN_H */
