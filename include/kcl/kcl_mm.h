/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_MM_H
#define AMDKCL_MM_H

#include <kcl/header/kcl_sched_mm_h.h>
#include <asm/page.h>
#include <linux/mm_types.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <kcl/kcl_mm_types.h>
#include <kcl/kcl_memory.h>

#ifndef untagged_addr
#define untagged_addr(addr) (addr)
#endif

#ifndef HAVE_KVFREE
static inline void kvfree(const void *addr)
{
	if (is_vmalloc_addr(addr))
		vfree(addr);
	else
		kfree(addr);
}
#endif

#ifndef HAVE_KVZALLOC_KVMALLOC
static inline void *kvmalloc(size_t size, gfp_t flags)
{
	void *out;

	if (size > PAGE_SIZE)
		out = __vmalloc(size, flags, PAGE_KERNEL);
	else
		out = kmalloc(size, flags);
	return out;
}
static inline void *kvzalloc(size_t size, gfp_t flags)
{
	return kvmalloc(size, flags | __GFP_ZERO);
}
#endif /* HAVE_KVZALLOC_KVMALLOC */

#ifndef HAVE_KVMALLOC_ARRAY
static inline void *kvmalloc_array(size_t n, size_t size, gfp_t flags)
{
	if (size != 0 && n > SIZE_MAX / size)
		return NULL;

	return kvmalloc(n * size, flags);
}
#endif /* HAVE_KVMALLOC_ARRAY */

#ifndef HAVE_KVCALLOC
static inline void *kvcalloc(size_t n, size_t size, gfp_t flags)
{
	return kvmalloc_array(n, size, flags | __GFP_ZERO);
}
#endif /* HAVE_KVCALLOC */

#if !defined(HAVE_MMGRAB)
static inline void mmgrab(struct mm_struct *mm)
{
	atomic_inc(&mm->mm_count);
}
#endif

#ifndef HAVE_MM_ACCESS
extern struct mm_struct * (*_kcl_mm_access)(struct task_struct *task, unsigned int mode);
#endif

#if !defined(HAVE_MEMALLOC_NOFS_SAVE)
static inline unsigned int memalloc_nofs_save(void)
{
	return current->flags;
}

static inline void memalloc_nofs_restore(unsigned int flags)
{
}
#endif

static inline int kcl_get_user_pages(struct task_struct *tsk, struct mm_struct *mm,
				unsigned long start, unsigned long nr_pages,
				int write, int force, struct page **pages,
				struct vm_area_struct **vmas, int *locked)
{
#if !defined(HAVE_8ARGS_GET_USER_PAGES)
	if (mm == current->mm) {
#if defined(HAVE_5ARGS_GET_USER_PAGES)
		write = ((!!write) & FOLL_WRITE) | ((!!force) & FOLL_FORCE);
		return get_user_pages(start, nr_pages, write, pages, vmas);
#else
		return get_user_pages(start, nr_pages, write, force, pages,
				vmas);
#endif
	} else {
#if defined(HAVE_8ARGS_GET_USER_PAGES_REMOTE)
		write = ((!!write) & FOLL_WRITE) | ((!!force) & FOLL_FORCE);
		return get_user_pages_remote(tsk, mm, start, nr_pages,
				write, pages, vmas, locked);
#elif defined(HAVE_7ARGS_GET_USER_PAGES_REMOTE)
		write = ((!!write) & FOLL_WRITE) | ((!!force) & FOLL_FORCE);
		return get_user_pages_remote(tsk, mm, start, nr_pages,
				write, pages, vmas);
#else
		return get_user_pages_remote(tsk, mm, start, nr_pages,
				write, force, pages, vmas);
#endif
	}
#else
	write = !!(write & FOLL_WRITE);
	return get_user_pages(tsk, mm, start, nr_pages,
			write, force, pages, vmas);
#endif
}

#if !defined(HAVE_ZONE_MANAGED_PAGES)
static inline unsigned long zone_managed_pages(struct zone *zone)
{
#if defined(HAVE_STRUCT_ZONE_MANAGED_PAGES)
	return (unsigned long)zone->managed_pages;
#else
	/* zone->managed_pages is introduced in v3.7-4152-g9feedc9d831e */
	WARN_ONCE(1, "struct zone->managed_pages don't exist. kernel is a bit old...");
	return 0;
#endif
}
#endif /* HAVE_ZONE_MANAGED_PAGES */

#endif /* AMDKCL_MM_H */
