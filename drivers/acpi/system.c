/*
 *  acpi_system.c - ACPI System Driver ($Revision: 63 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include "acpi_bus.h"
#include "acpi_drivers.h"


#define _COMPONENT		ACPI_SYSTEM_COMPONENT
ACPI_MODULE_NAME		("acpi_system")

extern FADT_DESCRIPTOR		acpi_fadt;

static int acpi_system_add (struct acpi_device *device);
static int acpi_system_remove (struct acpi_device *device, int type);

static struct acpi_driver acpi_system_driver = {
	.name =		ACPI_SYSTEM_DRIVER_NAME,
	.class =	ACPI_SYSTEM_CLASS,
	.ids =		ACPI_SYSTEM_HID,
	.ops =		{
				.add =		acpi_system_add,
				.remove =	acpi_system_remove
			},
};

/* Global vars for handling event proc entry */
static spinlock_t		acpi_system_event_lock = SPIN_LOCK_UNLOCKED;
int				event_is_open = 0;
extern struct list_head		acpi_bus_event_list;
extern wait_queue_head_t	acpi_bus_event_queue;

/* --------------------------------------------------------------------------
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */

static int
acpi_system_read_info (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*data)
{
	char			*p = page;
	int			size = 0;

	ACPI_FUNCTION_TRACE("acpi_system_read_info");

	if (off != 0)
		goto end;

	p += sprintf(p, "version:                 %x\n", ACPI_CA_VERSION);

end:
	size = (p - page);
	if (size <= off+count) *eof = 1;
	*start = page + off;
	size -= off;
	if (size>count) size = count;
	if (size<0) size = 0;

	return_VALUE(size);
}

static int acpi_system_open_event(struct inode *inode, struct file *file);
static ssize_t acpi_system_read_event (struct file*, char*, size_t, loff_t*);
static int acpi_system_close_event(struct inode *inode, struct file *file);
static unsigned int acpi_system_poll_event(struct file *file, poll_table *wait);


static struct file_operations acpi_system_event_ops = {
	.open =		acpi_system_open_event,
	.read =		acpi_system_read_event,
	.release =	acpi_system_close_event,
	.poll =		acpi_system_poll_event,
};

static int
acpi_system_open_event(struct inode *inode, struct file *file)
{
	spin_lock_irq (&acpi_system_event_lock);

	if(event_is_open)
		goto out_busy;

	event_is_open = 1;

	spin_unlock_irq (&acpi_system_event_lock);
	return 0;

out_busy:
	spin_unlock_irq (&acpi_system_event_lock);
	return -EBUSY;
}

static ssize_t
acpi_system_read_event (
	struct file		*file,
	char			*buffer,
	size_t			count,
	loff_t			*ppos)
{
	int			result = 0;
	struct acpi_bus_event	event;
	static char		str[ACPI_MAX_STRING];
	static int		chars_remaining = 0;
	static char		*ptr;


	ACPI_FUNCTION_TRACE("acpi_system_read_event");

	if (!chars_remaining) {
		memset(&event, 0, sizeof(struct acpi_bus_event));

		if ((file->f_flags & O_NONBLOCK)
		    && (list_empty(&acpi_bus_event_list)))
			return_VALUE(-EAGAIN);

		result = acpi_bus_receive_event(&event);
		if (result) {
			return_VALUE(-EIO);
		}

		chars_remaining = sprintf(str, "%s %s %08x %08x\n", 
			event.device_class?event.device_class:"<unknown>",
			event.bus_id?event.bus_id:"<unknown>", 
			event.type, event.data);
		ptr = str;
	}

	if (chars_remaining < count) {
		count = chars_remaining;
	}

	if (copy_to_user(buffer, ptr, count))
		return_VALUE(-EFAULT);

	*ppos += count;
	chars_remaining -= count;
	ptr += count;

	return_VALUE(count);
}

static int
acpi_system_close_event(struct inode *inode, struct file *file)
{
	spin_lock_irq (&acpi_system_event_lock);
	event_is_open = 0;
	spin_unlock_irq (&acpi_system_event_lock);
	return 0;
}

static unsigned int
acpi_system_poll_event(
	struct file		*file,
	poll_table		*wait)
{
	poll_wait(file, &acpi_bus_event_queue, wait);
	if (!list_empty(&acpi_bus_event_list))
		return POLLIN | POLLRDNORM;
	return 0;
}

static ssize_t acpi_system_read_dsdt (struct file*, char*, size_t, loff_t*);

static struct file_operations acpi_system_dsdt_ops = {
	.read =			acpi_system_read_dsdt,
};

static ssize_t
acpi_system_read_dsdt (
	struct file		*file,
	char			*buffer,
	size_t			count,
	loff_t			*ppos)
{
	acpi_status		status = AE_OK;
	acpi_buffer		dsdt = {ACPI_ALLOCATE_BUFFER, NULL};
	void			*data = 0;
	size_t			size = 0;

	ACPI_FUNCTION_TRACE("acpi_system_read_dsdt");

	status = acpi_get_table(ACPI_TABLE_DSDT, 1, &dsdt);
	if (ACPI_FAILURE(status))
		return_VALUE(-ENODEV);

	if (*ppos < dsdt.length) {
		data = dsdt.pointer + file->f_pos;
		size = dsdt.length - file->f_pos;
		if (size > count)
			size = count;
		if (copy_to_user(buffer, data, size)) {
			acpi_os_free(dsdt.pointer);
			return_VALUE(-EFAULT);
		}
	}

	acpi_os_free(dsdt.pointer);

	*ppos += size;

	return_VALUE(size);
}


static ssize_t acpi_system_read_fadt (struct file*, char*, size_t, loff_t*);

static struct file_operations acpi_system_fadt_ops = {
	.read =			acpi_system_read_fadt,
};

static ssize_t
acpi_system_read_fadt (
	struct file		*file,
	char			*buffer,
	size_t			count,
	loff_t			*ppos)
{
	acpi_status		status = AE_OK;
	acpi_buffer		fadt = {ACPI_ALLOCATE_BUFFER, NULL};
	void			*data = 0;
	size_t			size = 0;

	ACPI_FUNCTION_TRACE("acpi_system_read_fadt");

	status = acpi_get_table(ACPI_TABLE_FADT, 1, &fadt);
	if (ACPI_FAILURE(status))
		return_VALUE(-ENODEV);

	if (*ppos < fadt.length) {
		data = fadt.pointer + file->f_pos;
		size = fadt.length - file->f_pos;
		if (size > count)
			size = count;
		if (copy_to_user(buffer, data, size)) {
			acpi_os_free(fadt.pointer);
			return_VALUE(-EFAULT);
		}
	}

	acpi_os_free(fadt.pointer);

	*ppos += size;

	return_VALUE(size);
}


#ifdef ACPI_DEBUG

static int
acpi_system_read_debug (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*data)
{
	char			*p = page;
	int 			size = 0;

	if (off != 0)
		goto end;

	switch ((unsigned long) data) {
	case 0:
		p += sprintf(p, "0x%08x\n", acpi_dbg_layer);
		break;
	case 1:
		p += sprintf(p, "0x%08x\n", acpi_dbg_level);
		break;
	default:
		p += sprintf(p, "Invalid debug option\n");
		break;
	}
	
end:
	size = (p - page);
	if (size <= off+count) *eof = 1;
	*start = page + off;
	size -= off;
	if (size>count) size = count;
	if (size<0) size = 0;

	return size;
}


static int
acpi_system_write_debug (
	struct file             *file,
        const char              *buffer,
	unsigned long           count,
        void                    *data)
{
	char			debug_string[12] = {'\0'};

	ACPI_FUNCTION_TRACE("acpi_system_write_debug");

	if (count > sizeof(debug_string) - 1)
		return_VALUE(-EINVAL);

	if (copy_from_user(debug_string, buffer, count))
		return_VALUE(-EFAULT);

	debug_string[count] = '\0';

	switch ((unsigned long) data) {
	case 0:
		acpi_dbg_layer = simple_strtoul(debug_string, NULL, 0);
		break;
	case 1:
		acpi_dbg_level = simple_strtoul(debug_string, NULL, 0);
		break;
	default:
		return_VALUE(-EINVAL);
	}

	return_VALUE(count);
}

#endif /* ACPI_DEBUG */


static int
acpi_system_add_fs (
	struct acpi_device	*device)
{
	struct proc_dir_entry	*entry = NULL;

	ACPI_FUNCTION_TRACE("acpi_system_add_fs");

	if (!device)
		return_VALUE(-EINVAL);

	/* 'info' [R] */
	entry = create_proc_entry(ACPI_SYSTEM_FILE_INFO,
		S_IRUGO, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_SYSTEM_FILE_INFO));
	else {
		entry->read_proc = acpi_system_read_info;
		entry->data = acpi_driver_data(device);
	}

	/* 'dsdt' [R] */
	entry = create_proc_entry(ACPI_SYSTEM_FILE_DSDT,
		S_IRUSR, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_SYSTEM_FILE_DSDT));
	else
		entry->proc_fops = &acpi_system_dsdt_ops;

	/* 'fadt' [R] */
	entry = create_proc_entry(ACPI_SYSTEM_FILE_FADT,
		S_IRUSR, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_SYSTEM_FILE_FADT));
	else
		entry->proc_fops = &acpi_system_fadt_ops;

	/* 'event' [R] */
	entry = create_proc_entry(ACPI_SYSTEM_FILE_EVENT,
		S_IRUSR, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_SYSTEM_FILE_EVENT));
	else
		entry->proc_fops = &acpi_system_event_ops;

#ifdef ACPI_DEBUG

	/* 'debug_layer' [R/W] */
	entry = create_proc_entry(ACPI_SYSTEM_FILE_DEBUG_LAYER,
		S_IFREG|S_IRUGO|S_IWUSR, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_SYSTEM_FILE_DEBUG_LAYER));
	else {
		entry->read_proc  = acpi_system_read_debug;
		entry->write_proc = acpi_system_write_debug;
		entry->data = (void *) 0;
	}

	/* 'debug_level' [R/W] */
	entry = create_proc_entry(ACPI_SYSTEM_FILE_DEBUG_LEVEL,
		S_IFREG|S_IRUGO|S_IWUSR, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_SYSTEM_FILE_DEBUG_LEVEL));
	else {
		entry->read_proc  = acpi_system_read_debug;
		entry->write_proc = acpi_system_write_debug;
		entry->data = (void *) 1;
	}

#endif /*ACPI_DEBUG*/

	return_VALUE(0);
}


static int
acpi_system_remove_fs (
	struct acpi_device	*device)
{
	ACPI_FUNCTION_TRACE("acpi_system_remove_fs");

	if (!device)
		return_VALUE(-EINVAL);

	remove_proc_entry(ACPI_SYSTEM_FILE_INFO, acpi_device_dir(device));
	remove_proc_entry(ACPI_SYSTEM_FILE_DSDT, acpi_device_dir(device));
	remove_proc_entry(ACPI_SYSTEM_FILE_EVENT, acpi_device_dir(device));
#ifdef ACPI_DEBUG
	remove_proc_entry(ACPI_SYSTEM_FILE_DEBUG_LAYER,
		acpi_device_dir(device));
	remove_proc_entry(ACPI_SYSTEM_FILE_DEBUG_LEVEL,
		acpi_device_dir(device));
#endif

	return_VALUE(0);
}


/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

static int
acpi_system_add (
	struct acpi_device	*device)
{
	int			result = 0;

	ACPI_FUNCTION_TRACE("acpi_system_add");

	if (!device)
		return_VALUE(-EINVAL);

	sprintf(acpi_device_name(device), "%s", ACPI_SYSTEM_DEVICE_NAME);
	sprintf(acpi_device_class(device), "%s", ACPI_SYSTEM_CLASS);

	result = acpi_system_add_fs(device);

	return_VALUE(result);
}


static int
acpi_system_remove (
	struct acpi_device	*device,
	int			type)
{
	struct acpi_system	*system = NULL;

	ACPI_FUNCTION_TRACE("acpi_system_remove");

	if (!device || !acpi_driver_data(device))
		return_VALUE(-EINVAL);

	system = (struct acpi_system *) acpi_driver_data(device);

	acpi_system_remove_fs(device);

	kfree(system);

	return 0;
}


int __init
acpi_system_init (void)
{
	int			result = 0;

	ACPI_FUNCTION_TRACE("acpi_system_init");

	result = acpi_bus_register_driver(&acpi_system_driver);
	if (result < 0)
		return_VALUE(-ENODEV);

	return_VALUE(0);
}


void __exit
acpi_system_exit (void)
{
	ACPI_FUNCTION_TRACE("acpi_system_exit");
	acpi_bus_unregister_driver(&acpi_system_driver);
	return_VOID;
}
