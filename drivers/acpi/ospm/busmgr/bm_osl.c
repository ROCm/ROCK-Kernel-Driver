/*****************************************************************************
 *
 * Module Name: bm_osl.c
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
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <acpi.h>
#include "bm.h"


MODULE_AUTHOR("Andrew Grover");
MODULE_DESCRIPTION("ACPI Component Architecture (CA) - ACPI Bus Manager");
MODULE_LICENSE("GPL");


#ifdef ACPI_DEBUG

static int dbg_layer = ACPI_COMPONENT_DEFAULT;
MODULE_PARM(dbg_layer, "i");
MODULE_PARM_DESC(dbg_layer, "Controls debug output (see acpi_dbg_layer).\n");

static int dbg_level = DEBUG_DEFAULT;
MODULE_PARM(dbg_level, "i");
MODULE_PARM_DESC(dbg_level, "Controls debug output (see acpi_dbg_level).\n");

#endif /*ACPI_DEBUG*/


/*****************************************************************************
 *                               Types & Defines
 *****************************************************************************/

typedef struct
{
	BM_HANDLE		device_handle;
	char			*device_type;
	char			*device_instance;
	u32			event_type;
	u32			event_data;
	struct list_head	list;
} BM_OSL_EVENT;


#define BM_PROC_ROOT		"acpi"
#define BM_PROC_EVENT		"event"
#define BM_PROC_DEVICES		"devices"

#define BM_MAX_STRING_LENGTH	80


/****************************************************************************
 *                                  Globals
 ****************************************************************************/

struct proc_dir_entry		*bm_proc_root = NULL;
static struct proc_dir_entry	*bm_proc_event = NULL;

#ifdef ACPI_DEBUG
static u32			save_dbg_layer;
static u32			save_dbg_level;
#endif /*ACPI_DEBUG*/

extern BM_NODE_LIST		node_list;

static spinlock_t		bm_osl_event_lock = SPIN_LOCK_UNLOCKED;

static LIST_HEAD(bm_event_list);

static DECLARE_WAIT_QUEUE_HEAD(bm_event_wait_queue);

static int event_is_open = 0;


/****************************************************************************
 *                                 Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:	bm_osl_generate_event
 *
 * DESCRIPTION: Generates an event for user-space consumption by writing
 *              the event data to the 'event' file.
 *
 ****************************************************************************/

ACPI_STATUS
bm_osl_generate_event (
	BM_HANDLE		device_handle,
	char			*device_type,
	char			*device_instance,
	u32			event_type,
	u32			event_data)
{
	BM_OSL_EVENT		*event = NULL;
	u32			flags = 0;

	/* drop event on the floor if no one's listening */
	if (!event_is_open)
		return (AE_OK);

	/*
	 * Allocate a new event structure.
	 */
	event = acpi_os_callocate(sizeof(BM_OSL_EVENT));
	if (!event)
		goto alloc_error;

	event->device_type = acpi_os_callocate(strlen(device_type) 
		+ sizeof(char));
	if (!event->device_type)
		goto alloc_error;

	event->device_instance = acpi_os_callocate(strlen(device_instance) 
		+ sizeof(char));
	if (!event->device_instance)
		goto alloc_error;

	/*
	 * Set event data.
	 */
	event->device_handle = device_handle;
	strcpy(event->device_type, device_type);
	strcpy(event->device_instance, device_instance);
	event->event_type = event_type;
	event->event_data = event_data;

	/*
	 * Add to the end of our event list.
	 */
	spin_lock_irqsave(&bm_osl_event_lock, flags);
	list_add_tail(&event->list, &bm_event_list);
	spin_unlock_irqrestore(&bm_osl_event_lock, flags);

	/*
	 * Signal waiting threads (if any).
	 */
	wake_up_interruptible(&bm_event_wait_queue);

	return(AE_OK);

alloc_error:
	if (event->device_instance)
		acpi_os_free(event->device_instance);

	if (event->device_type)
		acpi_os_free(event->device_type);

	if (event)
		acpi_os_free(event);		

	return (AE_NO_MEMORY);
}

static int bm_osl_open_event(struct inode *inode, struct file *file)
{
	spin_lock_irq (&bm_osl_event_lock);

	if(event_is_open)
		goto out_busy;

	event_is_open = 1;

	spin_unlock_irq (&bm_osl_event_lock);
	return 0;

out_busy:
	spin_unlock_irq (&bm_osl_event_lock);
	return -EBUSY;
}


static int bm_osl_close_event(struct inode *inode, struct file *file)
{
	event_is_open = 0;
	return 0;
}

/****************************************************************************
 *
 * FUNCTION:	bm_osl_read_event
 *
 * DESCRIPTION: Handles reads to the 'event' file by blocking user-mode 
 *              threads until data (an event) is generated.
 *
 ****************************************************************************/
static ssize_t
bm_osl_read_event(
	struct file		*file, 
	char			*buf, 
	size_t			count, 
	loff_t			*ppos)
{
	char			str[BM_MAX_STRING_LENGTH];
	int			size;
	BM_OSL_EVENT		*event = NULL;
	unsigned long		flags = 0;

	DECLARE_WAITQUEUE(wait, current);

	if (count < BM_MAX_STRING_LENGTH) {
		return 0;
	}

	if (list_empty(&bm_event_list)) {

		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&bm_event_wait_queue, &wait);

		if (list_empty(&bm_event_list)) {
			schedule();
		}

		remove_wait_queue(&bm_event_wait_queue, &wait);
		set_current_state(TASK_RUNNING);

		if (signal_pending(current)) {
			return -ERESTARTSYS;
		}
	}

	spin_lock_irqsave(&bm_osl_event_lock, flags);
	event = list_entry(bm_event_list.next, BM_OSL_EVENT, list);
	list_del(&event->list);
	spin_unlock_irqrestore(&bm_osl_event_lock, flags);

	/* BUG: buffer overrun? */
	size = sprintf(str, "%s %s %08x %08x\n",
		event->device_type, event->device_instance,
		event->event_type, event->event_data);
	
	acpi_os_free(event->device_type);
	acpi_os_free(event->device_instance);
	acpi_os_free(event);

	if (copy_to_user(buf, str, size))
		return -EFAULT;

	*ppos += size;

	return size;
}

struct file_operations proc_event_operations = {
	open:		bm_osl_open_event,
	read:		bm_osl_read_event,
	release:	bm_osl_close_event,
};

/****************************************************************************
 *
 * FUNCTION:    bm_osl_init
 *
 ****************************************************************************/

int
bm_osl_init(void)
{
	ACPI_STATUS		status = AE_OK;

#ifdef ACPI_DEBUG
	save_dbg_layer = acpi_dbg_layer;
	acpi_dbg_layer = dbg_layer;

	save_dbg_level = acpi_dbg_level;
	acpi_dbg_level = dbg_level;
#endif /*ACPI_DEBUG*/

	bm_proc_root = proc_mkdir(BM_PROC_ROOT, NULL);
	if (!bm_proc_root) {
		return(AE_ERROR);
	}

	bm_proc_event = create_proc_entry(BM_PROC_EVENT, S_IRUSR, bm_proc_root);
	if (bm_proc_event) {
		bm_proc_event->proc_fops = &proc_event_operations;
	}

	status = bm_initialize();

	return (ACPI_SUCCESS(status)) ? 0 : -ENODEV;
}


/****************************************************************************
 *
 * FUNCTION:    bm_osl_cleanup
 *
 ****************************************************************************/

void
bm_osl_cleanup(void)
{
	bm_terminate();

	if (bm_proc_event) {
		remove_proc_entry(BM_PROC_EVENT, bm_proc_root);
		bm_proc_event = NULL;
	}

	if (bm_proc_root) {
		remove_proc_entry(BM_PROC_ROOT, NULL);
		bm_proc_root = NULL;
	}

#ifdef ACPI_DEBUG
	acpi_dbg_layer = save_dbg_layer;
	acpi_dbg_level = save_dbg_level;
#endif /*ACPI_DEBUG*/

	return;
}


module_init(bm_osl_init);
module_exit(bm_osl_cleanup);


/****************************************************************************
 *                                  Symbols
 ****************************************************************************/

/* bm.c */

EXPORT_SYMBOL(bm_get_node);

/* bmdriver.c */

EXPORT_SYMBOL(bm_get_device_power_state);
EXPORT_SYMBOL(bm_set_device_power_state);
EXPORT_SYMBOL(bm_get_device_info);
EXPORT_SYMBOL(bm_get_device_status);
EXPORT_SYMBOL(bm_get_device_context);
EXPORT_SYMBOL(bm_register_driver);
EXPORT_SYMBOL(bm_unregister_driver);

/* bmsearch.c */

EXPORT_SYMBOL(bm_search);

/* bmrequest.c */

EXPORT_SYMBOL(bm_request);

/* bmutils.c */

EXPORT_SYMBOL(bm_extract_package_data);
EXPORT_SYMBOL(bm_evaluate_object);
EXPORT_SYMBOL(bm_evaluate_simple_integer);
EXPORT_SYMBOL(bm_evaluate_reference_list);
EXPORT_SYMBOL(bm_copy_to_buffer);
EXPORT_SYMBOL(bm_cast_buffer);

/* bm_proc.c */

EXPORT_SYMBOL(bm_osl_generate_event);
EXPORT_SYMBOL(bm_proc_root);
