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
 
extern void timer_init(struct oprofile_operations ** ops, enum oprofile_cpu * cpu);

int __init oprofile_arch_init(struct oprofile_operations ** ops, enum oprofile_cpu * cpu)
{
	timer_init(ops, cpu);
	return 0;
}
