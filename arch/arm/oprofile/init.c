/**
 * @file init.c
 *
 * @remark Copyright 2004 Oprofile Authors
 * @remark Read the file COPYING
 *
 * @author Zwane Mwaikambo
 */

#include <linux/oprofile.h>
#include <linux/init.h>
#include <linux/errno.h>
#include "op_arm_model.h"

void __init oprofile_arch_init(struct oprofile_operations *ops)
{
#ifdef CONFIG_CPU_XSCALE
	pmu_init(ops, &op_xscale_spec);
#endif
}

void oprofile_arch_exit(void)
{
#ifdef CONFIG_CPU_XSCALE
	pmu_exit();
#endif
}
