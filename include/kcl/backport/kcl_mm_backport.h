/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_MM_BACKPORT_H
#define AMDKCL_MM_BACKPORT_H
#include <kcl/kcl_mm.h>
#include <kcl/header/kcl_sched_mm_h.h>
#include <linux/mm.h>

#ifndef HAVE_MM_ACCESS
#define mm_access _kcl_mm_access
#endif

#ifdef get_user_pages_remote
#undef get_user_pages_remote
#endif
#ifdef get_user_pages
#undef get_user_pages
#endif
#ifndef HAVE_GET_USER_PAGES_REMOTE_LOCKED
static inline
long _kcl_get_user_pages_remote(struct task_struct *tsk, struct mm_struct *mm,
		unsigned long start, unsigned long nr_pages,
		unsigned int gup_flags, struct page **pages,
		struct vm_area_struct **vmas, int *locked)
{
#if defined(HAVE_GET_USER_PAGES_REMOTE_GUP_FLAGS)
	return get_user_pages_remote(tsk, mm, start, nr_pages, gup_flags, pages, vmas, NULL);
#elif defined(HAVE_GET_USER_PAGES_REMOTE_INTRODUCED)
	return get_user_pages_remote(tsk, mm, start, nr_pages, !!(gup_flags & FOLL_WRITE),
				     !!(gup_flags & FOLL_FORCE), pages, vmas, NULL);
#else
	return get_user_pages(tsk, mm, start, nr_pages, !!(gup_flags & FOLL_WRITE),
			      !!(gup_flags & FOLL_FORCE), pages, vmas);
#endif
}
#define get_user_pages_remote _kcl_get_user_pages_remote
#endif /* HAVE_GET_USER_PAGES_REMOTE_LOCKED */

#ifndef HAVE_GET_USER_PAGES_GUP_FLAGS
static inline
long _kcl_get_user_pages(unsigned long start, unsigned long nr_pages,
		unsigned int gup_flags, struct page **pages,
		struct vm_area_struct **vmas)
{
#if defined(HAVE_GET_USER_PAGES_6ARGS)
	return get_user_pages(start, nr_pages, !!(gup_flags & FOLL_WRITE),
			      !!(gup_flags & FOLL_FORCE), pages, vmas);
#else
	return get_user_pages(current, current->mm, start, nr_pages, !!(gup_flags & FOLL_WRITE),
			      !!(gup_flags & FOLL_FORCE), pages, vmas);
#endif
}
#define get_user_pages _kcl_get_user_pages
#endif /* HAVE_GET_USER_PAGES_GUP_FLAGS */

#endif
