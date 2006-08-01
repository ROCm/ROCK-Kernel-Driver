/*
 * Kernel Debugger Architecture Dependent FRU functions.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
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
