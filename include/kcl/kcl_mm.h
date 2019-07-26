#ifndef AMDKCL_MM_H
#define AMDKCL_MM_H

#include <linux/mm.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 11, 0)
#include <linux/sched/mm.h>
#else
#include <linux/sched.h>
#endif
#include <linux/gfp.h>

static inline int kcl_get_user_pages(struct task_struct *tsk, struct mm_struct *mm,
				unsigned long start, unsigned long nr_pages,
				int write, int force, struct page **pages,
				struct vm_area_struct **vmas, int *locked)
{
#if	LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0) || \
	defined(OS_NAME_SLE_12_3) || \
	defined(OS_NAME_SUSE_42_3) || \
	defined(OS_NAME_RHEL_7_X)
	if (mm == current->mm)
		return get_user_pages(start, nr_pages, write, pages, vmas);
	else
#if	LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0) || \
	defined(OS_NAME_SLE_12_3) || \
	defined(OS_NAME_SUSE_42_3) || \
	defined(OS_NAME_RHEL_7_X)
		return get_user_pages_remote(tsk, mm, start, nr_pages,
				write, pages, vmas, locked);
#else
		return get_user_pages_remote(tsk, mm, start, nr_pages,
				write, pages, vmas);
#endif
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0) || \
    defined(OS_NAME_SLE_12_3) || \
    defined(OS_NAME_SUSE_42_3)
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


#if !defined(HAVE_MEMALLOC_NOFS_SAVE)
static inline unsigned int memalloc_nofs_save(void)
{
		return current->flags;
}

static inline void memalloc_nofs_restore(unsigned int flags)
{
}
#endif

#if !defined(HAVE_KVZALLOC_KVMALLOC)
#define kvzalloc kzalloc
#define kvmalloc kzalloc
#endif

#if !defined(HAVE_KVFREE)
#define kvfree kfree
#endif

#if !defined(HAVE_KVMALLOC_ARRAY)
#define kvmalloc_array kmalloc_array
#endif

#if !defined(HAVE_KVCALLOC)
static inline void *kvcalloc(size_t n, size_t size, gfp_t flags)
{
	return kvmalloc_array(n, size, flags | __GFP_ZERO);
}
#endif

#if !defined(GFP_TRANSHUGE_LIGHT)
#define GFP_TRANSHUGE_LIGHT	((GFP_HIGHUSER_MOVABLE | __GFP_COMP | \
			 __GFP_NOMEMALLOC | __GFP_NOWARN) & ~__GFP_RECLAIM)
#endif

#if defined(BUILD_AS_DKMS)
extern struct mm_struct * (*_kcl_mm_access)(struct task_struct *task, unsigned int mode);
#endif

static inline struct mm_struct * kcl_mm_access(struct task_struct *task, unsigned int mode)
{
#if defined(BUILD_AS_DKMS)
	return _kcl_mm_access(task, mode);
#else
	return mm_access(task, mode);
#endif

}

#endif /* AMDKCL_MM_H */
