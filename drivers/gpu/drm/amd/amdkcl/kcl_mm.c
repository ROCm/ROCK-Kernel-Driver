// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/kernel/fork.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */
#include <linux/sched.h>
#include <linux/pagemap.h>

#ifndef HAVE_MM_ACCESS
struct mm_struct* (*_kcl_mm_access)(struct task_struct *task, unsigned int mode);
EXPORT_SYMBOL(_kcl_mm_access);

static struct mm_struct * __kcl_mm_access_stub(struct task_struct *task, unsigned int mode)
{
	pr_warn_once("This kernel version not support API: mm_access !\n");
	return NULL;
}
#endif

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

void amdkcl_mm_init(void)
{
#ifndef HAVE_MM_ACCESS
	_kcl_mm_access = amdkcl_fp_setup("mm_access", __kcl_mm_access_stub);
#endif

#ifndef HAVE_MMPUT_ASYNC
	_kcl_mmput_async = amdkcl_fp_setup("mmput_async", __kcl_mmput_async);
#endif
}
