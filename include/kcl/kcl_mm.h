/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/ipc/util.c
 * Copyright (C) 1992 Krishna Balasubramanian
 *   For kvmalloc/kvzalloc
 */
#ifndef AMDKCL_MM_H
#define AMDKCL_MM_H

#include <linux/sched/mm.h>
#include <asm/page.h>
#include <linux/mm_types.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <kcl/kcl_mmap_lock.h>
#include <kcl/kcl_mm_types.h>
#include <kcl/kcl_memory.h>

#ifndef untagged_addr
/* Copied from include/linux/mm.h */
#define untagged_addr(addr) (addr)
#endif

#ifndef HAVE_FAULT_FLAG_ALLOW_RETRY_FIRST
static inline bool fault_flag_allow_retry_first(unsigned int flags)
{
	return (flags & FAULT_FLAG_ALLOW_RETRY) &&
	    (!(flags & FAULT_FLAG_TRIED));
}
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

#ifndef HAVE_IS_COW_MAPPING
static inline bool is_cow_mapping(vm_flags_t flags)
{
        return (flags & (VM_SHARED | VM_MAYWRITE)) == VM_MAYWRITE;
}
#endif /* HAVE_IS_COW_MAPPING */

#endif /* AMDKCL_MM_H */
