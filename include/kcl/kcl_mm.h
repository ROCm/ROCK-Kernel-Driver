#ifndef AMDKCL_MM_H
#define AMDKCL_MM_H

#if defined(HAVE_MM_H)
#include <linux/sched/mm.h>
#else
#include <linux/sched.h>
#endif
#include <linux/mm.h>
#include <linux/gfp.h>
#include <kcl/kcl_overflow.h>

static inline int kcl_get_user_pages(struct task_struct *tsk, struct mm_struct *mm,
				unsigned long start, unsigned long nr_pages,
				int write, int force, struct page **pages,
				struct vm_area_struct **vmas, int *locked)
{
#if	LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0) || \
	defined(OS_NAME_SLE_12_3) || \
	defined(OS_NAME_SUSE_42_3) || \
	defined(OS_NAME_RHEL_7_4_5)
	if (mm == current->mm)
		return get_user_pages(start, nr_pages, write, pages, vmas);
	else
#if	LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0) || \
	defined(OS_NAME_SLE_12_3) || \
	defined(OS_NAME_SUSE_42_3) || \
	defined(OS_NAME_RHEL_7_4_5)
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

#ifndef HAVE_KVFREE
#ifdef HAVE_DRM_FREE_LARGE
#define kvfree drm_free_large
#else
static inline void kvfree(const void *addr)
{
	if (is_vmalloc_addr(addr))
		vfree(addr);
	else
		kfree(addr);
}
#endif /* HAVE_DRM_FREE_LARGE */
#endif /* HAVE_KVFREE */

#ifndef HAVE_KVMALLOC_ARRAY
#if defined(HAVE_DRM_MALLOC_AB) && defined(HAVE_DRM_CALLOC_LARGE)
static inline void *kvmalloc_array(size_t n, size_t size, gfp_t flags)
{
	if (flags & __GFP_ZERO)
		return drm_calloc_large(n, size);
	else
		return drm_malloc_ab(n, size);
}
#else
static inline void *kvmalloc_array(size_t n, size_t size, gfp_t flags)
{
	size_t bytes;

	if (unlikely(check_mul_overflow(n, size, &bytes)))
		return NULL;

	return kvmalloc(bytes, flags);
}
#endif /* HAVE_DRM_MALLOC_AB && HAVE_DRM_CALLOC_LARGE */
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

static inline struct mm_struct * kcl_mm_access(struct task_struct *task, unsigned int mode)
{
#ifndef HAVE_MM_ACCESS
	return _kcl_mm_access(task, mode);
#else
	return mm_access(task, mode);
#endif

}
#endif /* AMDKCL_MM_H */
