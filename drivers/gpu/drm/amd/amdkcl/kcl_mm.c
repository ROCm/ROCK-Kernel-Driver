// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/kernel/fork.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */
#include <linux/sched.h>

#ifndef HAVE_MM_ACCESS
struct mm_struct* (*_kcl_mm_access)(struct task_struct *task, unsigned int mode);
EXPORT_SYMBOL(_kcl_mm_access);

static struct mm_struct * __kcl_mm_access_stub(struct task_struct *task, unsigned int mode)
{
	pr_warn_once("This kernel version not support API: mm_access !\n");
	return NULL;
}
#endif

void amdkcl_mm_init(void)
{
#ifndef HAVE_MM_ACCESS
	_kcl_mm_access = amdkcl_fp_setup("mm_access", __kcl_mm_access_stub);
#endif
}
