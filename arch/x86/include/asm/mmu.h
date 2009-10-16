#ifndef _ASM_X86_MMU_H
#define _ASM_X86_MMU_H

#include <linux/spinlock.h>
#include <linux/mutex.h>

/*
 * The x86 doesn't have a mmu context, but
 * we put the segment information here.
 */
typedef struct {
	void *ldt;
	int size;
#ifdef CONFIG_XEN
	unsigned has_foreign_mappings:1;
#endif
	struct mutex lock;
	void *vdso;
} mm_context_t;

#if defined(CONFIG_SMP) && !defined(CONFIG_XEN)
void leave_mm(int cpu);
#else
static inline void leave_mm(int cpu)
{
}
#endif

#endif /* _ASM_X86_MMU_H */
