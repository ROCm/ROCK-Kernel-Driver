/*
 *  arch/mips/vm86.c
 *
 *  Copyright (C) 1994  Waldorf GMBH,
 *  written by Ralf Baechle
 */
#include <linux/linkage.h>
#include <linux/errno.h>

asmlinkage int sys_vm86(void *v86)
{
	return -ENOSYS;
}
