#ifndef AMDKCL_MM_H
#define AMDKCL_MM_H

#include <linux/mm.h>

static inline int kcl_get_user_pages(struct task_struct *tsk, struct mm_struct *mm,
				unsigned long start, unsigned long nr_pages,
				int write, int force, struct page **pages,
				struct vm_area_struct **vmas)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
	if (mm == current->mm)
		return get_user_pages(start, nr_pages, write, pages, vmas);
	else
		return get_user_pages_remote(tsk, mm, start, nr_pages,
				write, pages, vmas);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
	if (mm == current->mm)
		return get_user_pages(start, nr_pages, write, force, pages,
				vmas);
	else
		return get_user_pages_remote(tsk, mm, start, nr_pages,
				write, force, pages, vmas);
#else
	write = !!(write & FOLL_WRITE);
	return get_user_pages(tsk, mm, start, nr_pages,
			write, force, pages, vmas);
#endif
}

#endif /* AMDKCL_MM_H */
