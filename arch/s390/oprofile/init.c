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

void __init oprofile_arch_init(struct oprofile_operations* ops)
{
}

void oprofile_arch_exit(void)
{
}
