// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/kernel/fork.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */
#include <linux/sched.h>

#ifndef HAVE_MMPUT_ASYNC
void (*_kcl_mmput_async)(struct mm_struct *mm);
EXPORT_SYMBOL(_kcl_mmput_async);

void __kcl_mmput_async(struct mm_struct *mm)
{
	pr_warn_once("This kernel version not support API: mmput_async !\n");
}
#endif

void amdkcl_mm_init(void)
{
#ifndef HAVE_MMPUT_ASYNC
	_kcl_mmput_async = amdkcl_fp_setup("mmput_async", __kcl_mmput_async);
#endif
}
