/******************************************************************************
 *
 * Module Name: bn_osl.c
 *   $Revision: 16 $
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
#include "bn.h"


MODULE_AUTHOR("Andrew Grover");
MODULE_DESCRIPTION("ACPI Component Architecture (CA) - Button Driver");
MODULE_LICENSE("GPL");


#define BN_PROC_ROOT		"button"
#define BN_PROC_POWER_BUTTON	"power"
#define BN_PROC_SLEEP_BUTTON	"sleep"
#define BN_PROC_LID_SWITCH	"lid"

extern struct proc_dir_entry	*bm_proc_root;
static struct proc_dir_entry	*bn_proc_root = NULL;


#define BN_TYPE_UNKNOWN		0
#define BN_TYPE_FIXED		1
#define BN_TYPE_GENERIC		2

static int bn_power_button = BN_TYPE_UNKNOWN;
static int bn_sleep_button = BN_TYPE_UNKNOWN;
static int bn_lid_switch = BN_TYPE_UNKNOWN;


/****************************************************************************
 *
 * FUNCTION:	bn_osl_add_device
 *
 ****************************************************************************/

acpi_status
bn_osl_add_device(
	BN_CONTEXT		*button)
{
	acpi_status		status = AE_OK;

	if (!button) {
		return(AE_BAD_PARAMETER);
	}

	switch (button->type) {

	case BN_TYPE_POWER_BUTTON_FIXED:
		bn_power_button = BN_TYPE_FIXED;
		printk(KERN_INFO "ACPI: Power Button (FF) found\n");
		if (!proc_mkdir(BN_PROC_POWER_BUTTON, bn_proc_root)) {
			status = AE_ERROR;
		}
		break;

	case BN_TYPE_POWER_BUTTON:
		/* 
		 * Avoid creating multiple /proc entries when (buggy) ACPI
		 * BIOS tables erroneously list both fixed- and generic-
		 * feature buttons.  Note that fixed-feature buttons are 
		 * always enumerated first (and there can only be one) so
		 * we only need to check here.
		 */
		switch (bn_power_button) {
		case BN_TYPE_GENERIC:
			printk(KERN_WARNING "ACPI: Multiple generic-space power buttons detected, using first\n");
			break;
		case BN_TYPE_FIXED:
			printk(KERN_WARNING "ACPI: Multiple power buttons detected, ignoring fixed-feature\n");
		default:
			printk(KERN_INFO "ACPI: Power Button (CM) found\n");
			bn_power_button = BN_TYPE_GENERIC;
			if (!proc_mkdir(BN_PROC_POWER_BUTTON, bn_proc_root)) {
				status = AE_ERROR;
			}
			break;
		}
		break;

	case BN_TYPE_SLEEP_BUTTON_FIXED:
		bn_sleep_button = BN_TYPE_FIXED;
		printk(KERN_INFO "ACPI: Sleep Button (FF) found\n");
		if (!proc_mkdir(BN_PROC_SLEEP_BUTTON, bn_proc_root)) {
			status = AE_ERROR;
		}
		break;

	case BN_TYPE_SLEEP_BUTTON:
		/* 
		 * Avoid creating multiple /proc entries when (buggy) ACPI
		 * BIOS tables erroneously list both fixed- and generic-
		 * feature buttons.  Note that fixed-feature buttons are 
		 * always enumerated first (and there can only be one) so
		 * we only need to check here.
		 */
		switch (bn_sleep_button) {
		case BN_TYPE_GENERIC:
			printk(KERN_WARNING "ACPI: Multiple generic-space sleep buttons detected, using first\n");
			break;
		case BN_TYPE_FIXED:
			printk(KERN_WARNING "ACPI: Multiple sleep buttons detected, ignoring fixed-feature\n");
		default:
			bn_sleep_button = BN_TYPE_GENERIC;
			printk(KERN_INFO "ACPI: Sleep Button (CM) found\n");
			if (!proc_mkdir(BN_PROC_SLEEP_BUTTON, bn_proc_root)) {
				status = AE_ERROR;
			}
			break;
		}
		break;

	case BN_TYPE_LID_SWITCH:
		if (bn_lid_switch) {
			printk(KERN_WARNING "ACPI: Multiple generic-space lid switches detected, using first\n");
			break;
		}
		bn_lid_switch = BN_TYPE_GENERIC;
		printk(KERN_INFO "ACPI: Lid Switch (CM) found\n");
		if (!proc_mkdir(BN_PROC_LID_SWITCH, bn_proc_root)) {
			status = AE_ERROR;
		}
		break;
	}

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:	bn_osl_remove_device
 *
 ****************************************************************************/

acpi_status
bn_osl_remove_device (
	BN_CONTEXT		*button)
{
	if (!button) {
		return(AE_BAD_PARAMETER);
	}

	switch (button->type) {

	case BN_TYPE_POWER_BUTTON:
	case BN_TYPE_POWER_BUTTON_FIXED:
		remove_proc_entry(BN_PROC_POWER_BUTTON, bn_proc_root);
		break;

	case BN_TYPE_SLEEP_BUTTON:
	case BN_TYPE_SLEEP_BUTTON_FIXED:
		remove_proc_entry(BN_PROC_SLEEP_BUTTON, bn_proc_root);
		break;

	case BN_TYPE_LID_SWITCH:
		remove_proc_entry(BN_PROC_LID_SWITCH, bn_proc_root);
		break;
	}

	return(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:	bn_osl_generate_event
 *
 ****************************************************************************/

acpi_status
bn_osl_generate_event (
	u32			event,
	BN_CONTEXT		*button)
{
	acpi_status		status = AE_OK;

	if (!button) {
		return(AE_BAD_PARAMETER);
	}

	switch (event) {

	case BN_NOTIFY_STATUS_CHANGE:

		switch(button->type) {

		case BN_TYPE_POWER_BUTTON:
		case BN_TYPE_POWER_BUTTON_FIXED:
			status = bm_osl_generate_event(button->device_handle,
				BN_PROC_ROOT, BN_PROC_POWER_BUTTON, event, 0);
			break;

		case BN_TYPE_SLEEP_BUTTON:
		case BN_TYPE_SLEEP_BUTTON_FIXED:
			status = bm_osl_generate_event(button->device_handle,
				BN_PROC_ROOT, BN_PROC_SLEEP_BUTTON, event, 0);
			break;

		case BN_TYPE_LID_SWITCH:
			status = bm_osl_generate_event(button->device_handle,
				BN_PROC_ROOT, BN_PROC_LID_SWITCH, event, 0);
			break;

		default:
			status = AE_SUPPORT;
			break;
		}

		break;

	default:
		return(AE_BAD_PARAMETER);
		break;
	}

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:    bn_osl_init
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	0: Success
 *
 * DESCRIPTION: Module initialization.
 *
 ****************************************************************************/

static int __init
bn_osl_init (void)
{
	acpi_status		status = AE_OK;

	/* abort if no busmgr */
	if (!bm_proc_root)
		return -ENODEV;

	bn_proc_root = proc_mkdir(BN_PROC_ROOT, bm_proc_root);
	if (!bn_proc_root) {
		status = AE_ERROR;
	}
	else {
		status = bn_initialize();
		if (ACPI_FAILURE(status)) {
			remove_proc_entry(BN_PROC_ROOT, bm_proc_root);
		}
	}

	return (ACPI_SUCCESS(status)) ? 0 : -ENODEV;
}


/****************************************************************************
 *
 * FUNCTION:    bn_osl_cleanup
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	<none>
 *
 * DESCRIPTION: Module cleanup.
 *
 ****************************************************************************/

static void __exit
bn_osl_cleanup (void)
{
	bn_terminate();

	if (bn_proc_root) {
		remove_proc_entry(BN_PROC_ROOT, bm_proc_root);
	}

	return;
}


module_init(bn_osl_init);
module_exit(bn_osl_cleanup);
