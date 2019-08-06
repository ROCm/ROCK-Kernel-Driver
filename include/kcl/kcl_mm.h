/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/ipc/util.c
 * Copyright (C) 1992 Krishna Balasubramanian
 *   For kvmalloc/kvzalloc
 */
#ifndef AMDKCL_MM_H
#define AMDKCL_MM_H

#include <linux/mm.h>

#ifndef untagged_addr
/* Copied from include/linux/mm.h */
#define untagged_addr(addr) (addr)
#endif

#ifndef HAVE_MM_ACCESS
extern struct mm_struct * (*_kcl_mm_access)(struct task_struct *task, unsigned int mode);
#endif

#ifndef HAVE_FAULT_FLAG_ALLOW_RETRY_FIRST
static inline bool fault_flag_allow_retry_first(unsigned int flags)
{
	return (flags & FAULT_FLAG_ALLOW_RETRY) &&
	    (!(flags & FAULT_FLAG_TRIED));
}
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

#endif /* AMDKCL_MM_H */
