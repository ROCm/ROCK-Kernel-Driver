/*****************************************************************************
 *
 * Module Name: ec_osl.c
 *   $Revision: 11 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000, 2001 Andrew Grover
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <acpi.h>
#include <bm.h>
#include "ec.h"


MODULE_AUTHOR("Andrew Grover");
MODULE_DESCRIPTION("ACPI Component Architecture (CA) - Embedded Controller Driver");
MODULE_LICENSE("GPL");

extern struct proc_dir_entry	*bm_proc_root;


/****************************************************************************
 *
 * FUNCTION:    ec_osl_init
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	0: Success
 *
 * DESCRIPTION: Module initialization.
 *
 ****************************************************************************/

static int __init
ec_osl_init (void)
{
	acpi_status		status = AE_OK;

	/* abort if no busmgr */
	if (!bm_proc_root)
		return -ENODEV;

	status = ec_initialize();

	return (ACPI_SUCCESS(status)) ? 0 : -ENODEV;
}

/****************************************************************************
 *
 * FUNCTION:    ec_osl_cleanup
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	<none>
 *
 * DESCRIPTION: Module cleanup.
 *
 ****************************************************************************/

static void __exit
ec_osl_cleanup(void)
{
	ec_terminate();

	return;
}

module_init(ec_osl_init);
module_exit(ec_osl_cleanup);
