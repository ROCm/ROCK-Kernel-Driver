/**
 * @file init.c
 *
 * @remark Copyright 2004 Oprofile Authors
 *
 * @author Zwane Mwaikambo
 */

#include <linux/oprofile.h>
#include <linux/init.h>
#include <linux/errno.h>

int oprofile_arch_init(struct oprofile_operations **ops)
{
	int ret = -ENODEV;

	return ret;
}

void oprofile_arch_exit(void)
{
}
