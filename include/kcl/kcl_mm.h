#ifndef AMDKCL_MM_H
#define AMDKCL_MM_H

#include <kcl/kcl_sched_mm_h.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <kcl/kcl_overflow.h>

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
static inline void kvfree(const void *addr)
{
#ifdef HAVE_DRM_FREE_LARGE
	return drm_free_large(addr);
#else
	if (is_vmalloc_addr(addr))
		vfree(addr);
	else
		kfree(addr);
#endif /* HAVE_DRM_FREE_LARGE */
}
#endif /* HAVE_KVFREE */

#ifndef HAVE_KVMALLOC_ARRAY
static inline void *kvmalloc_array(size_t n, size_t size, gfp_t flags)
{
#if defined(HAVE_DRM_MALLOC_AB) && defined(HAVE_DRM_CALLOC_LARGE)
	if (flags & __GFP_ZERO)
		return drm_calloc_large(n, size);
	else
		return drm_malloc_ab(n, size);
#else
	size_t bytes;

	if (unlikely(check_mul_overflow(n, size, &bytes)))
		return NULL;

	return kvmalloc(bytes, flags);
#endif /* HAVE_DRM_MALLOC_AB && HAVE_DRM_CALLOC_LARGE */
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

#endif /* AMDKCL_MM_H */
