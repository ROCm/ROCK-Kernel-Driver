/*
 * ACPI Sony Notebook Control Driver (SNC)
 *
 * Copyright (C) 2004 Stelian Pop <stelian@popies.net>
 * 
 * Parts of this driver inspired from asus_acpi.c, which is 
 * Copyright (C) 2002, 2003, 2004 Julien Lerouge, Karol Kozimor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/moduleparam.h>
#include <acpi/acpi_drivers.h>
#include <acpi/acpi_bus.h>
#include <asm/uaccess.h>

#define ACPI_SNC_CLASS		"sony"
#define ACPI_SNC_HID		"SNY5001"
#define ACPI_SNC_DRIVER_NAME	"ACPI Sony Notebook Control Driver v0.1"

MODULE_AUTHOR("Stelian Pop");
MODULE_DESCRIPTION(ACPI_SNC_DRIVER_NAME);
MODULE_LICENSE("GPL");

static int debug = 0;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug,"set this to 1 (and RTFM) if you want to help the development of this driver");

static int sony_acpi_add (struct acpi_device *device);
static int sony_acpi_remove (struct acpi_device *device, int type);

static struct acpi_driver sony_acpi_driver = {
	name:	ACPI_SNC_DRIVER_NAME,
	class:	ACPI_SNC_CLASS,
	ids:	ACPI_SNC_HID,
	ops:	{
			add:	sony_acpi_add,
			remove:	sony_acpi_remove,
		},
};

struct sony_snc {
	acpi_handle		handle;
	int			brt;		/* brightness */
	struct proc_dir_entry	*proc_brt;
	int			cmi;		/* ??? ? */
	struct proc_dir_entry	*proc_cmi;
	int			csxb;		/* ??? */
	struct proc_dir_entry	*proc_csxb;
	int			ctr;		/* contrast ? */
	struct proc_dir_entry	*proc_ctr;
	int			pbr;		/* ??? */
	struct proc_dir_entry	*proc_pbr;
};

static struct proc_dir_entry *sony_acpi_dir;

static int acpi_callgetfunc(acpi_handle handle, char *name, int *result)
{
	struct acpi_buffer output;
	union acpi_object out_obj;
	acpi_status status;

	output.length = sizeof(out_obj);
	output.pointer = &out_obj;

	status = acpi_evaluate_object(handle, name, NULL, &output);
	if ((status == AE_OK) && (out_obj.type == ACPI_TYPE_INTEGER)) {
		*result = out_obj.integer.value;
		return 0;
	}

	printk(KERN_WARNING "acpi_callreadfunc failed\n");

	return -1;
}

static int acpi_callsetfunc(acpi_handle handle, char *name, int value, int *result)
{
	struct acpi_object_list params;
	union acpi_object in_obj;
	struct acpi_buffer output;
	union acpi_object out_obj;
	acpi_status status;

	params.count = 1;
	params.pointer = &in_obj;
	in_obj.type = ACPI_TYPE_INTEGER;
	in_obj.integer.value = value;

	output.length = sizeof(out_obj);
	output.pointer = &out_obj;

	status = acpi_evaluate_object(handle, name, &params, &output);
	if (status == AE_OK) {
		if (result != NULL) {
			if (out_obj.type != ACPI_TYPE_INTEGER) {
				printk(KERN_WARNING "acpi_evaluate_object bad return type\n");
				return -1;
			}
			*result = out_obj.integer.value;
		}
		return 0;
	}
	
	printk(KERN_WARNING "acpi_evaluate_object failed\n");

	return -1;
}

static int parse_buffer(const char __user *buffer, unsigned long count, int *val) {
	char s[32];
	
	if (count > 31)
		return -EINVAL;
	if (copy_from_user(s, buffer, count))
		return -EFAULT;
	s[count] = 0;
	if (sscanf(s, "%i", val) != 1)
		return -EINVAL;
	return 0;
}

static int sony_acpi_write_brt(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	struct sony_snc *snc = (struct sony_snc *) data;
	int result;

	if ((result = parse_buffer(buffer, count, &snc->brt)) < 0)
		return result;
	
	if (acpi_callsetfunc(snc->handle, "SBRT", snc->brt, NULL) < 0)
		return -EIO;

	return count;
}

static int sony_acpi_read_brt(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	struct sony_snc *snc = (struct sony_snc *) data;

	if (acpi_callgetfunc(snc->handle, "GBRT", &snc->brt) < 0)
		return -EIO;
	
	return sprintf(page, "%d\n", snc->brt);
}

static int sony_acpi_write_cmi(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	struct sony_snc *snc = (struct sony_snc *) data;
	int result;

	if ((result = parse_buffer(buffer, count, &snc->cmi)) < 0)
		return result;
	
	if (acpi_callsetfunc(snc->handle, "SCMI", snc->cmi, &snc->cmi) < 0)
		return -EIO;

	return count;
}

static int sony_acpi_read_cmi(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	struct sony_snc *snc = (struct sony_snc *) data;
	return sprintf(page, "%d\n", snc->cmi);
}

static int sony_acpi_write_csxb(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	struct sony_snc *snc = (struct sony_snc *) data;
	int result;

	if ((result = parse_buffer(buffer, count, &snc->csxb)) < 0)
		return result;
	
	if (acpi_callsetfunc(snc->handle, "CSXB", snc->csxb, &snc->csxb) < 0)
		return -EIO;

	return count;
}

static int sony_acpi_read_csxb(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	struct sony_snc *snc = (struct sony_snc *) data;
	return sprintf(page, "%d\n", snc->csxb);
}

static int sony_acpi_write_ctr(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	struct sony_snc *snc = (struct sony_snc *) data;
	int result;

	if ((result = parse_buffer(buffer, count, &snc->ctr)) < 0)
		return result;
	
	if (acpi_callsetfunc(snc->handle, "SCTR", snc->ctr, NULL) < 0)
		return -EIO;

	return count;
}

static int sony_acpi_read_ctr(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	struct sony_snc *snc = (struct sony_snc *) data;

	if (acpi_callgetfunc(snc->handle, "GCTR", &snc->ctr) < 0)
		return -EIO;
	
	return sprintf(page, "%d\n", snc->ctr);
}

static int sony_acpi_write_pbr(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	struct sony_snc *snc = (struct sony_snc *) data;
	int result;

	if ((result = parse_buffer(buffer, count, &snc->pbr)) < 0)
		return result;
	
	if (acpi_callsetfunc(snc->handle, "SPBR", snc->pbr, NULL) < 0)
		return -EIO;

	return count;
}

static int sony_acpi_read_pbr(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	struct sony_snc *snc = (struct sony_snc *) data;

	if (acpi_callgetfunc(snc->handle, "GPBR", &snc->pbr) < 0)
		return -EIO;
	
	return sprintf(page, "%d\n", snc->pbr);
}
		 
static void sony_acpi_notify(acpi_handle handle, u32 event, void *data)
{
	/* struct sony_snc *snc = (struct sony_snc *) data; */

	printk("sony_snc_notify\n");
}

static acpi_status sony_walk_callback(acpi_handle handle, u32 level, void *context, void **return_value)
{
	struct acpi_namespace_node *node = (struct acpi_namespace_node *) handle;
	union acpi_operand_object *operand = (union acpi_operand_object *) node->object;

	printk("sony_acpi method: name: %4.4s, args %X\n", 
		node->name.ascii,
		(u32) operand->method.param_count);

	return AE_OK;
}

static int __init sony_acpi_add(struct acpi_device *device)
{
	acpi_status status = AE_OK;
	struct sony_snc *snc = NULL;
	int result;

	snc = kmalloc(sizeof(struct sony_snc), GFP_KERNEL);
	if (!snc)
		return -ENOMEM;
	memset(snc, 0, sizeof(struct sony_snc));

	snc->handle = device->handle;

	acpi_driver_data(device) = snc;
	acpi_device_dir(device) = sony_acpi_dir;

	if (debug) {
		status = acpi_walk_namespace(ACPI_TYPE_METHOD, snc->handle, 1, sony_walk_callback, NULL, NULL);
		if (ACPI_FAILURE(status)) {
			printk(KERN_WARNING "Unable to walk acpi resources\n");
		}
	}

	snc->proc_brt = create_proc_entry("brt", 0666, acpi_device_dir(device));
	if (!snc->proc_brt) {
		printk(KERN_WARNING "Unable to create proc entry\n");
		result = -EIO;
		goto outbrt;
	}

	snc->proc_brt->write_proc = sony_acpi_write_brt;
	snc->proc_brt->read_proc = sony_acpi_read_brt;
	snc->proc_brt->data = acpi_driver_data(device);
	snc->proc_brt->owner = THIS_MODULE;

	if (debug) {
		snc->proc_cmi = create_proc_entry("cmi", 0666, acpi_device_dir(device));
		if (!snc->proc_cmi) {
			printk(KERN_WARNING "Unable to create proc entry\n");
			result = -EIO;
			goto outcmi;
		}

		snc->proc_cmi->write_proc = sony_acpi_write_cmi;
		snc->proc_cmi->read_proc = sony_acpi_read_cmi;
		snc->proc_cmi->data = acpi_driver_data(device);
		snc->proc_cmi->owner = THIS_MODULE;

		snc->proc_csxb = create_proc_entry("csxb", 0666, acpi_device_dir(device));
		if (!snc->proc_csxb) {
			printk(KERN_WARNING "Unable to create proc entry\n");
			result = -EIO;
			goto outcsxb;
		}

		snc->proc_csxb->write_proc = sony_acpi_write_csxb;
		snc->proc_csxb->read_proc = sony_acpi_read_csxb;
		snc->proc_csxb->data = acpi_driver_data(device);
		snc->proc_csxb->owner = THIS_MODULE;

		snc->proc_ctr = create_proc_entry("ctr", 0666, acpi_device_dir(device));
		if (!snc->proc_ctr) {
			printk(KERN_WARNING "Unable to create proc entry\n");
			result = -EIO;
			goto outctr;
		}

		snc->proc_ctr->write_proc = sony_acpi_write_ctr;
		snc->proc_ctr->read_proc = sony_acpi_read_ctr;
		snc->proc_ctr->data = acpi_driver_data(device);
		snc->proc_ctr->owner = THIS_MODULE;

		snc->proc_pbr = create_proc_entry("pbr", 0666, acpi_device_dir(device));
		if (!snc->proc_pbr) {
			printk(KERN_WARNING "Unable to create proc entry\n");
			result = -EIO;
			goto outpbr;
		}

		snc->proc_pbr->write_proc = sony_acpi_write_pbr;
		snc->proc_pbr->read_proc = sony_acpi_read_pbr;
		snc->proc_pbr->data = acpi_driver_data(device);
		snc->proc_pbr->owner = THIS_MODULE;

		status = acpi_install_notify_handler(snc->handle,
			ACPI_DEVICE_NOTIFY, sony_acpi_notify, snc);
			if (ACPI_FAILURE(status)) {
				printk(KERN_WARNING "Unable to install notify handler\n");
				result = -ENODEV;
				goto outnotify;
		}
	}

	printk(KERN_INFO ACPI_SNC_DRIVER_NAME " successfully installed\n");

	return 0;

outnotify:
	remove_proc_entry("pbr", acpi_device_dir(device));
outpbr:
	remove_proc_entry("ctr", acpi_device_dir(device));
outctr:
	remove_proc_entry("csxb", acpi_device_dir(device));
outcsxb:
	remove_proc_entry("cmi", acpi_device_dir(device));
outcmi:
	remove_proc_entry("brt", acpi_device_dir(device));
outbrt:
	kfree(snc);
	return result;
}


static int __exit sony_acpi_remove(struct acpi_device *device, int type)
{
	acpi_status status = AE_OK;
	struct sony_snc *snc = NULL;

	snc = (struct sony_snc *) acpi_driver_data(device);

	if (debug) {
		status = acpi_remove_notify_handler(snc->handle, ACPI_DEVICE_NOTIFY, sony_acpi_notify);
		if (ACPI_FAILURE(status))
			printk(KERN_WARNING "Unable to remove notify handler\n");

		remove_proc_entry("pbr", acpi_device_dir(device));
		remove_proc_entry("ctr", acpi_device_dir(device));
		remove_proc_entry("csxb", acpi_device_dir(device));
		remove_proc_entry("cmi", acpi_device_dir(device));
	}
	remove_proc_entry("brt", acpi_device_dir(device));

	kfree(snc);

	printk(KERN_INFO ACPI_SNC_DRIVER_NAME " successfully removed\n");

	return 0;
}

static int __init sony_acpi_init(void)
{
	int result;

	sony_acpi_dir = proc_mkdir("sony", acpi_root_dir);
	if (!sony_acpi_dir) {
		printk(KERN_WARNING "Unable to create /proc entry\n");
		return -ENODEV;
	}
	sony_acpi_dir->owner = THIS_MODULE;

	result = acpi_bus_register_driver(&sony_acpi_driver);
	if (result < 0) {
		remove_proc_entry("sony", acpi_root_dir);
		return -ENODEV;
	}
	return 0;
}


static void __exit sony_acpi_exit(void)
{
	acpi_bus_unregister_driver(&sony_acpi_driver);
	remove_proc_entry("sony", acpi_root_dir);
}

module_init(sony_acpi_init);
