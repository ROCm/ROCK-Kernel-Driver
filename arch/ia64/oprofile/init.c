/**
 * @file init.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/kernel.h>
#include <linux/oprofile.h>
#include <linux/init.h>
#include <linux/errno.h>
 
extern void timer_init(struct oprofile_operations ** ops);

int __init oprofile_arch_init(struct oprofile_operations ** ops)
{
	return -ENODEV;
}


void oprofile_arch_exit(void)
{
}
