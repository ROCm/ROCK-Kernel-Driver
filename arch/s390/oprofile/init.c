/**
 * arch/s390/oprofile/init.c
 *
 * S390 Version
 *   Copyright (C) 2003 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *   Author(s): Thomas Spatzier (tspat@de.ibm.com)
 *
 * @remark Copyright 2002 OProfile authors
 */

#include <linux/oprofile.h>
#include <linux/init.h>
#include <linux/errno.h>

//extern int irq_init(struct oprofile_operations** ops);
extern void timer_init(struct oprofile_operations** ops);

int __init oprofile_arch_init(struct oprofile_operations** ops)
{
	timer_init(ops);
	return 0;
}

void oprofile_arch_exit(void)
{
}
