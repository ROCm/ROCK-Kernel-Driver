#include <linux/version.h>
#include <linux/sched.h>
#include "kcl_common.h"

struct mm_struct* (*_kcl_mm_access)(struct task_struct *task, unsigned int mode);
EXPORT_SYMBOL(_kcl_mm_access);

static struct mm_struct * __kcl_mm_access_stub(struct task_struct *task, unsigned int mode)
{
	printk_once(KERN_WARNING "This kernel version not support API: mm_access !\n");
	return NULL;
}

void amdkcl_mm_init(void)
{
	_kcl_mm_access = amdkcl_fp_setup("mm_access", __kcl_mm_access_stub);
}
