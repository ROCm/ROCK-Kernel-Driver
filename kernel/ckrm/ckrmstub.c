/* ckrmstub.c - Stub for Class-based Kernel Resource Management (CKRM)
 *
 * Copyright (C) Chandra Seetharaman,  IBM Corp. 2003
 * 
 * Provides system call stub for the CKRM system calls.
 *
 * Latest version, more details at http://ckrm.sf.net
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

/* Changes
 * 
 * 06 Nov 2003
 *        Created
 */

#include <linux/linkage.h>
#include <asm/errno.h>

asmlinkage int
sys_res_ctrl(unsigned int op, void *data)
{
	return -ENOSYS;
}

