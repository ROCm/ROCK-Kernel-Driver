/*
 * Kernel Debugger Architecture Dependent FRU functions.
 *
 * Copyright (C) 1999-2003 Silicon Graphics, Inc.  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information:  Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
 */

#include <linux/types.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <linux/module.h>
#include <asm/sal.h>
#include <asm/sn/sn_sal.h>

MODULE_AUTHOR("Jesse Barnes");
MODULE_DESCRIPTION("Capture FRU data");
MODULE_LICENSE("GPL");

/**
 * kdba_fru - capture FRU data
 * @argc: arg count
 * @argv: arg values
 * @envp: kdb env. vars
 * @regs: current register state
 *
 * Tell the system contollers to capture FRU data
 */
static int
kdba_fru(int argc, const char **argv, const char **envp, struct pt_regs *regs)
{
	u64 ret;

	kdb_printf("Capturing FRU data...");
	ret = ia64_sn_fru_capture();
	kdb_printf("done.\n");
	return ret;
}

/**
 * kdba_fru_init - register 'fru' command with kdb
 *
 * Register the 'fru' command with kdb at load time.
 */
static int __init
kdba_fru_init(void)
{
	kdb_register("fru", kdba_fru, 0, "Capture FRU data", 0);

	return 0;
}

/**
 * kdba_fru_exit - unregister the 'fru' command
 *
 * Tell kdb that the 'fru' command is no longer available.
 */
static void __exit
kdba_fru_exit(void)
{
	kdb_unregister("fru");
}

kdb_module_init(kdba_fru_init)
kdb_module_exit(kdba_fru_exit)

