/******************************************************************************
 *
 * Module Name: pr_osl.c
 *   $Revision: 21 $
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
#include <linux/pci.h>
#include <acpi.h>
#include <bm.h>
#include "pr.h"


MODULE_AUTHOR("Andrew Grover");
MODULE_DESCRIPTION("ACPI Component Architecture (CA) - IA32 Processor Driver");
MODULE_LICENSE("GPL");


#define PR_PROC_ROOT		"processor"
#define PR_PROC_STATUS		"status"
#define PR_PROC_INFO		"info"

extern struct proc_dir_entry	*bm_proc_root;
static struct proc_dir_entry	*pr_proc_root = NULL;
extern unsigned short		acpi_piix4_bmisx;


/****************************************************************************
 *
 * FUNCTION:	pr_osl_proc_read_status
 *
 ****************************************************************************/

static int
pr_osl_proc_read_status (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*context)
{
	PR_CONTEXT		*processor = NULL;
	char			*p = page;
	int 			len = 0;

	if (!context || (off != 0)) {
		goto end;
	}

	processor = (PR_CONTEXT*)context;

	p += sprintf(p, "Bus Mastering Activity:  %08x\n",
		processor->power.bm_activity);

	p += sprintf(p, "C-State Utilization:     C1[%d] C2[%d] C3[%d]\n",
		processor->power.state[PR_C1].utilization,
		processor->power.state[PR_C2].utilization,
		processor->power.state[PR_C3].utilization);

end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;

	return(len);
}


/****************************************************************************
 *
 * FUNCTION:	pr_osl_proc_read_info
 *
 ****************************************************************************/

static int
pr_osl_proc_read_info (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*context)
{
	PR_CONTEXT		*processor = NULL;
	char			*p = page;
	int 			len = 0;

	if (!context || (off != 0)) {
		goto end;
	}

	processor = (PR_CONTEXT*)context;

	p += sprintf(p, "<TBD>\n");

end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;

	return(len);
}


/****************************************************************************
 *
 * FUNCTION:	pr_osl_add_device
 *
 ****************************************************************************/

acpi_status
pr_osl_add_device(
	PR_CONTEXT		*processor)
{
	u32			i = 0;
	struct proc_dir_entry	*proc_entry = NULL, *proc;
	char			processor_uid[16];

	if (!processor) {
		return(AE_BAD_PARAMETER);
	}

	printk("Processor[%x]:", processor->uid);
	for (i=0; i<processor->power.state_count; i++) {
		if (processor->power.state[i].is_valid) {
			printk(" C%d", i);
		}
	}
	if (processor->performance.state_count > 1)
		printk(", %d throttling states", processor->performance.state_count);
	if (acpi_piix4_bmisx && processor->power.state[3].is_valid)
		printk(" (PIIX errata enabled)");
	printk("\n");

	sprintf(processor_uid, "%d", processor->uid);

	proc_entry = proc_mkdir(processor_uid, pr_proc_root);
	if (!proc_entry)
		return(AE_ERROR);

	proc = create_proc_read_entry(PR_PROC_STATUS, S_IFREG | S_IRUGO, 
				      proc_entry, pr_osl_proc_read_status, (void*)processor);
	if (!proc_entry)
		return(AE_ERROR);

	proc = create_proc_read_entry(PR_PROC_INFO, S_IFREG | S_IRUGO, 
				      proc_entry, pr_osl_proc_read_info, (void*)processor);
	if (!proc_entry)
		return(AE_ERROR);

	return(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:	pr_osl_remove_device
 *
 ****************************************************************************/

acpi_status
pr_osl_remove_device (
	PR_CONTEXT		*processor)
{
	char			proc_entry[64];

	if (!processor) {
		return(AE_BAD_PARAMETER);
	}

	sprintf(proc_entry, "%d/%s", processor->uid, PR_PROC_INFO);
	remove_proc_entry(proc_entry, pr_proc_root);

	sprintf(proc_entry, "%d/%s", processor->uid, PR_PROC_STATUS);
	remove_proc_entry(proc_entry, pr_proc_root);

	sprintf(proc_entry, "%d", processor->uid);
	remove_proc_entry(proc_entry, pr_proc_root);

	return(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:	pr_osl_generate_event
 *
 ****************************************************************************/

acpi_status
pr_osl_generate_event (
	u32			event,
	PR_CONTEXT		*processor)
{
	acpi_status		status = AE_OK;
	char			processor_uid[16];

	if (!processor) {
		return(AE_BAD_PARAMETER);
	}

	switch (event) {

	case PR_NOTIFY_PERF_STATES:
	case PR_NOTIFY_POWER_STATES:
		sprintf(processor_uid, "%d", processor->uid);
		status = bm_osl_generate_event(processor->device_handle,
			PR_PROC_ROOT, processor_uid, event, 0);
		break;

	default:
		return(AE_BAD_PARAMETER);
		break;
	}

	return(status);
}


/****************************************************************************
 *                              Errata Handling
 ****************************************************************************/

void acpi_pr_errata (void)
{
	struct pci_dev		*dev = NULL;

	while ((dev = pci_find_subsys(PCI_VENDOR_ID_INTEL, PCI_ANY_ID, 
		PCI_ANY_ID, PCI_ANY_ID, dev))) {
		switch (dev->device) {
		case PCI_DEVICE_ID_INTEL_82801BA_8:	/* PIIX4U4 */
		case PCI_DEVICE_ID_INTEL_82801BA_9:	/* PIIX4U3 */
		case PCI_DEVICE_ID_INTEL_82451NX:	/* PIIX4NX */
		case PCI_DEVICE_ID_INTEL_82372FB_1:	/* PIIX4U2 */
		case PCI_DEVICE_ID_INTEL_82801AA_1:	/* PIIX4U */
		case PCI_DEVICE_ID_INTEL_82443MX_1:	/* PIIX4E2 */
		case PCI_DEVICE_ID_INTEL_82801AB_1:	/* PIIX4E */
		case PCI_DEVICE_ID_INTEL_82371AB:	/* PIIX4 */
			acpi_piix4_bmisx = pci_resource_start(dev, 4);
			return;
		}
	}

	return;
}


/****************************************************************************
 *
 * FUNCTION:	pr_osl_init
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	0: Success
 *
 * DESCRIPTION: Module initialization.
 *
 ****************************************************************************/

static int __init
pr_osl_init (void)
{
	acpi_status		status = AE_OK;

	/* abort if no busmgr */
	if (!bm_proc_root)
		return -ENODEV;

	acpi_pr_errata();

	pr_proc_root = proc_mkdir(PR_PROC_ROOT, bm_proc_root);
	if (!pr_proc_root) {
		status = AE_ERROR;
	}
	else {
		status = pr_initialize();
		if (ACPI_FAILURE(status)) {
			remove_proc_entry(PR_PROC_ROOT, bm_proc_root);
		}

	}

	return (ACPI_SUCCESS(status)) ? 0 : -ENODEV;
}


/****************************************************************************
 *
 * FUNCTION:    pr_osl_cleanup
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	<none>
 *
 * DESCRIPTION: Module cleanup.
 *
 ****************************************************************************/

static void __exit
pr_osl_cleanup (void)
{
	pr_terminate();

	if (pr_proc_root) {
		remove_proc_entry(PR_PROC_ROOT, bm_proc_root);
	}

	return;
}


module_init(pr_osl_init);
module_exit(pr_osl_cleanup);
