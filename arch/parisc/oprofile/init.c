/**
 * @file init.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/oprofile.h>

void __init oprofile_arch_init(struct oprofile_operations * ops)
{
}


void oprofile_arch_exit()
{
}
