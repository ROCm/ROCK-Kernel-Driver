/*****************************************************************************
 *
 * Module Name: ec_osl.c
 *   $Revision: 6 $
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

#ifdef ACPI_DEBUG

static int dbg_layer = ACPI_COMPONENT_DEFAULT;
MODULE_PARM(dbg_layer, "i");
MODULE_PARM_DESC(dbg_layer, "Controls debug output (see acpi_dbg_layer).\n");

static int dbg_level = DEBUG_DEFAULT;
MODULE_PARM(dbg_level, "i");
MODULE_PARM_DESC(dbg_level, "Controls debug output (see acpi_dbg_level).\n");

#endif /*ACPI_DEBUG*/


#ifdef ACPI_DEBUG
static u32			save_dbg_layer;
static u32			save_dbg_level;
#endif /*ACPI_DEBUG*/


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
	ACPI_STATUS		status = AE_OK;

#ifdef ACPI_DEBUG
	save_dbg_layer = acpi_dbg_layer;
	acpi_dbg_layer = dbg_layer;

	save_dbg_level = acpi_dbg_level;
	acpi_dbg_level = dbg_level;
#endif /*ACPI_DEBUG*/

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

#ifdef ACPI_DEBUG
	acpi_dbg_layer = save_dbg_layer;
	acpi_dbg_level = save_dbg_level;
#endif /*ACPI_DEBUG*/

	return;
}

module_init(ec_osl_init);
module_exit(ec_osl_cleanup);
