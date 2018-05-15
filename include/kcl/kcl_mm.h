#ifndef AMDKCL_MM_H
#define AMDKCL_MM_H

#include <linux/mm.h>

static inline int kcl_get_user_pages(struct task_struct *tsk, struct mm_struct *mm,
				unsigned long start, unsigned long nr_pages,
				int write, int force, struct page **pages,
				struct vm_area_struct **vmas, int *locked)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0) || \
	defined(OS_NAME_RHEL_7_4_5) || \
	defined(OS_NAME_SLE_12_3) || \
	defined(OS_NAME_SUSE_42_3)
	if (mm == current->mm)
		return get_user_pages(start, nr_pages, write, pages, vmas);
	else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0) || \
	defined(OS_NAME_RHEL_7_4_5) || \
	defined(OS_NAME_SLE_12_3) || \
	defined(OS_NAME_SUSE_42_3)
		return get_user_pages_remote(tsk, mm, start, nr_pages,
				write, pages, vmas, locked);
#else
		return get_user_pages_remote(tsk, mm, start, nr_pages,
				write, pages, vmas);
#endif
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0) || defined(OS_NAME_SLE_12_3) || defined(OS_NAME_SUSE_42_3)
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0)
#define kvzalloc kzalloc
#define kvfree kfree
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
#define ___GFP_DIRECT_RECLAIM	0x400000u
#define ___GFP_KSWAPD_RECLAIM	0x1000000u
#define __GFP_RECLAIM ((__force gfp_t)(___GFP_DIRECT_RECLAIM|___GFP_KSWAPD_RECLAIM))
#define __GFP_DIRECT_RECLAIM	((__force gfp_t)___GFP_DIRECT_RECLAIM) /* Caller can reclaim */
#define __GFP_KSWAPD_RECLAIM	((__force gfp_t)___GFP_KSWAPD_RECLAIM) /* kswapd can wake */
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 8, 0)
#define GFP_TRANSHUGE_LIGHT	((GFP_HIGHUSER_MOVABLE | __GFP_COMP | \
			 __GFP_NOMEMALLOC | __GFP_NOWARN) & ~__GFP_RECLAIM)
#define GFP_TRANSHUGE	(GFP_TRANSHUGE_LIGHT | __GFP_DIRECT_RECLAIM)
#endif


#endif /* AMDKCL_MM_H */
