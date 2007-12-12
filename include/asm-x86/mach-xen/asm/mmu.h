#ifndef _ASM_X86_MMU_H
#define _ASM_X86_MMU_H

#include <linux/spinlock.h>
#include <linux/mutex.h>

/*
 * The x86 doesn't have a mmu context, but
 * we put the segment information here.
 *
 * cpu_vm_mask is used to optimize ldt flushing.
 */
typedef struct { 
	void *ldt;
#ifdef CONFIG_X86_64
	rwlock_t ldtlock;
#endif
	int size;
	struct mutex lock;
	void *vdso;
#ifdef CONFIG_XEN
	unsigned has_foreign_mappings:1;
#ifdef CONFIG_X86_64
	unsigned pinned:1;
	struct list_head unpinned;
#endif
#endif
} mm_context_t;

#ifdef CONFIG_X86_64_XEN
extern struct list_head mm_unpinned;
extern spinlock_t mm_unpinned_lock;
#endif

#endif /* _ASM_X86_MMU_H */
