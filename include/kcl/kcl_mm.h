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
#include <linux/swap.h>
#include <linux/slab.h>
#include <kcl/kcl_mmap_lock.h>
#include <kcl/kcl_mm_types.h>
#include <kcl/kcl_memory.h>

#ifndef untagged_addr
/* Copied from include/linux/mm.h */
#define untagged_addr(addr) (addr)
#endif

#ifndef HAVE_MMPUT_ASYNC
extern void (*_kcl_mmput_async)(struct mm_struct *mm);
#endif

#ifndef HAVE_ZONE_DEVICE_PAGE_INIT
void zone_device_page_init(struct page *page);
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
/* Copied from v4.20-6505-g9705bea5f833 include/linux/mmzone.h and modified for KCL */
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

#ifndef HAVE_VMA_LOOKUP
/**
 * vma_lookup() - Find a VMA at a specific address
 * @mm: The process address space.
 * @addr: The user address.
 *
 * Return: The vm_area_struct at the given address, %NULL otherwise.
 */
static inline
struct vm_area_struct *vma_lookup(struct mm_struct *mm, unsigned long addr)
{
        struct vm_area_struct *vma = find_vma(mm, addr);

        if (vma && addr < vma->vm_start)
                vma = NULL;

        return vma;
}
#endif /* HAVE_VMA_LOOKUP */

#ifndef VM_ACCESS_FLAGS
/* Copied from v5.6-12367-g6cb4d9a2870d mm/vma: introduce VM_ACCESS_FLAGS*/
/* VMA basic access permission flags */
#define VM_ACCESS_FLAGS (VM_READ | VM_WRITE | VM_EXEC)
#endif

#ifndef page_to_virt
#define page_to_virt(x) __va(PFN_PHYS(page_to_pfn(x)))
#endif

#ifndef HAVE_VM_FLAGS_SET
static inline void vm_flags_set(struct vm_area_struct *vma,
                                vm_flags_t flags)
{
#ifdef HAVE_MMAP_ASSERT_WRITE_LOCKED
        mmap_assert_write_locked(vma->vm_mm);
#endif
        vma->vm_flags |= flags;
}

static inline void vm_flags_clear(struct vm_area_struct *vma,
                                  vm_flags_t flags)
{
#ifdef HAVE_MMAP_ASSERT_WRITE_LOCKED
        mmap_assert_write_locked(vma->vm_mm);
#endif
        vma->vm_flags &= ~flags;
}
#endif

#ifndef HAVE_WANT_INIT_ON_FREE
static inline bool want_init_on_free(void)
{
	pr_warn_once("legacy kernel without want_init_on_free()\n");
	return false;
}
#endif

#ifndef HAVE_TOTALRAM_PAGES
extern unsigned long totalram_pages;
static inline unsigned long _kcl_totalram_pages(void)
{
       return totalram_pages;
}
#define totalram_pages _kcl_totalram_pages
#endif /* HAVE_TOTALRAM_PAGES */

#endif /* AMDKCL_MM_H */
