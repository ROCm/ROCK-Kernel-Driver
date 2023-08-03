// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/kernel/fork.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */
#include <linux/sched.h>
#include <linux/pagemap.h>

#ifndef HAVE_MMPUT_ASYNC
void (*_kcl_mmput_async)(struct mm_struct *mm);
EXPORT_SYMBOL(_kcl_mmput_async);

void __kcl_mmput_async(struct mm_struct *mm)
{
	pr_warn_once("This kernel version not support API: mmput_async !\n");
}
#endif

#ifndef HAVE_ZONE_DEVICE_PAGE_INIT
/* copied from v6.0-rc3-597-g0dc45ca1ce18 mm/memremap.c and modified for kcl usage */
void zone_device_page_init(struct page *page)
{
/* v5.17-rc4-75-g27674ef6c73f mm: remove the extra ZONE_DEVICE struct page refcount */
#if IS_ENABLED(CONFIG_DEV_PAGEMAP_OPS)
	get_page(page);
#endif
	lock_page(page);
}
EXPORT_SYMBOL_GPL(zone_device_page_init);
#endif

#ifndef HAVE_KMALLOC_SIZE_ROUNDUP
#ifndef CONFIG_SLOB
extern struct kmem_cache *(*_kcl_kmalloc_slab)(size_t size, gfp_t flags);
#endif
#endif /* HAVE_KMALLOC_SIZE_ROUNDUP */

void amdkcl_mm_init(void)
{

#ifndef HAVE_MMPUT_ASYNC
	_kcl_mmput_async = amdkcl_fp_setup("mmput_async", __kcl_mmput_async);
#endif

#ifndef HAVE_KMALLOC_SIZE_ROUNDUP
#ifndef CONFIG_SLOB
	_kcl_kmalloc_slab = amdkcl_fp_setup("kmalloc_slab", NULL);
#endif
#endif
}
