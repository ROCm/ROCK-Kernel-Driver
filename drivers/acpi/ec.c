/*
 *  acpi_ec.c - ACPI Embedded Controller Driver ($Revision: 35 $)
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/compatmac.h>
#include <linux/proc_fs.h>
#include <asm/io.h>
#include "acpi_bus.h"
#include "acpi_drivers.h"


#define _COMPONENT		ACPI_EC_COMPONENT
ACPI_MODULE_NAME		("acpi_ec")

#define PREFIX			"ACPI: "


#define ACPI_EC_FLAG_OBF	0x01	/* Output buffer full */
#define ACPI_EC_FLAG_IBF	0x02	/* Input buffer full */
#define ACPI_EC_FLAG_SCI	0x20	/* EC-SCI occurred */

#define ACPI_EC_EVENT_OBF	0x01	/* Output buffer full */
#define ACPI_EC_EVENT_IBE	0x02	/* Input buffer empty */

#define ACPI_EC_UDELAY		100	/* Poll @ 100us increments */
#define ACPI_EC_UDELAY_COUNT	1000	/* Wait 10ms max. during EC ops */
#define ACPI_EC_UDELAY_GLK	1000	/* Wait 1ms max. to get global lock */

#define ACPI_EC_COMMAND_READ	0x80
#define ACPI_EC_COMMAND_WRITE	0x81
#define ACPI_EC_COMMAND_QUERY	0x84

static int acpi_ec_add (struct acpi_device *device);
static int acpi_ec_remove (struct acpi_device *device, int type);
static int acpi_ec_start (struct acpi_device *device);
static int acpi_ec_stop (struct acpi_device *device, int type);

static struct acpi_driver acpi_ec_driver = {
	name:			ACPI_EC_DRIVER_NAME,
	class:			ACPI_EC_CLASS,
	ids:			ACPI_EC_HID,
	ops:			{
					add:	acpi_ec_add,
					remove:	acpi_ec_remove,
					start:	acpi_ec_start,
					stop:	acpi_ec_stop,
				},
};

struct acpi_ec {
	acpi_handle		handle;
	unsigned long		gpe_bit;
	acpi_generic_address	status_addr;
	acpi_generic_address	command_addr;
	acpi_generic_address	data_addr;
	unsigned long		global_lock;
	spinlock_t		lock;
};

/* If we find an EC via the ECDT, we need to keep a ptr to its context */
static struct acpi_ec	*ec_ecdt;
/* compare this against UIDs in properly enumerated ECs to determine if we
   have a dupe */
static unsigned long		ecdt_uid = 0xFFFFFFFF;

/* --------------------------------------------------------------------------
                             Transaction Management
   -------------------------------------------------------------------------- */

static int
acpi_ec_wait (
	struct acpi_ec		*ec,
	u8			event)
{
	u32			acpi_ec_status = 0;
	u32			i = ACPI_EC_UDELAY_COUNT;

	if (!ec)
		return -EINVAL;

	/* Poll the EC status register waiting for the event to occur. */
	switch (event) {
	case ACPI_EC_EVENT_OBF:
		do {
			acpi_hw_low_level_read(8, &acpi_ec_status, &ec->status_addr, 0);
			if (acpi_ec_status & ACPI_EC_FLAG_OBF)
				return 0;
			udelay(ACPI_EC_UDELAY);
		} while (--i>0);
		break;
	case ACPI_EC_EVENT_IBE:
		do {
			acpi_hw_low_level_read(8, &acpi_ec_status, &ec->status_addr, 0);
			if (!(acpi_ec_status & ACPI_EC_FLAG_IBF))
				return 0;
			udelay(ACPI_EC_UDELAY);
		} while (--i>0);
		break;
	default:
		return -EINVAL;
	}

	return -ETIME;
}


static int
acpi_ec_read (
	struct acpi_ec		*ec,
	u8			address,
	u32			*data)
{
	acpi_status		status = AE_OK;
	int			result = 0;
	unsigned long		flags = 0;
	u32			glk = 0;

	ACPI_FUNCTION_TRACE("acpi_ec_read");

	if (!ec || !data)
		return_VALUE(-EINVAL);

	*data = 0;

	if (ec->global_lock) {
		status = acpi_acquire_global_lock(ACPI_EC_UDELAY_GLK, &glk);
		if (ACPI_FAILURE(status))
			return_VALUE(-ENODEV);
	}
	
	spin_lock_irqsave(&ec->lock, flags);

	acpi_hw_low_level_write(8, ACPI_EC_COMMAND_READ, &ec->command_addr, 0);
	result = acpi_ec_wait(ec, ACPI_EC_EVENT_IBE);
	if (result)
		goto end;

	acpi_hw_low_level_write(8, address, &ec->data_addr, 0);
	result = acpi_ec_wait(ec, ACPI_EC_EVENT_OBF);
	if (result)
		goto end;


	acpi_hw_low_level_read(8, data, &ec->data_addr, 0);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Read [%02x] from address [%02x]\n",
		*data, address));

end:
	spin_unlock_irqrestore(&ec->lock, flags);

	if (ec->global_lock)
		acpi_release_global_lock(glk);

	return_VALUE(result);
}


static int
acpi_ec_write (
	struct acpi_ec		*ec,
	u8			address,
	u8			data)
{
	int			result = 0;
	acpi_status		status = AE_OK;
	unsigned long		flags = 0;
	u32			glk = 0;

	ACPI_FUNCTION_TRACE("acpi_ec_write");

	if (!ec)
		return_VALUE(-EINVAL);

	if (ec->global_lock) {
		status = acpi_acquire_global_lock(ACPI_EC_UDELAY_GLK, &glk);
		if (ACPI_FAILURE(status))
			return_VALUE(-ENODEV);
	}

	spin_lock_irqsave(&ec->lock, flags);

	acpi_hw_low_level_write(8, ACPI_EC_COMMAND_WRITE, &ec->command_addr, 0);
	result = acpi_ec_wait(ec, ACPI_EC_EVENT_IBE);
	if (result)
		goto end;

	acpi_hw_low_level_write(8, address, &ec->data_addr, 0);
	result = acpi_ec_wait(ec, ACPI_EC_EVENT_IBE);
	if (result)
		goto end;

	acpi_hw_low_level_write(8, data, &ec->data_addr, 0);
	result = acpi_ec_wait(ec, ACPI_EC_EVENT_IBE);
	if (result)
		goto end;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Wrote [%02x] to address [%02x]\n",
		data, address));

end:
	spin_unlock_irqrestore(&ec->lock, flags);

	if (ec->global_lock)
		acpi_release_global_lock(glk);

	return_VALUE(result);
}


static int
acpi_ec_query (
	struct acpi_ec		*ec,
	u32			*data)
{
	int			result = 0;
	acpi_status		status = AE_OK;
	unsigned long		flags = 0;
	u32			glk = 0;

	ACPI_FUNCTION_TRACE("acpi_ec_query");

	if (!ec || !data)
		return_VALUE(-EINVAL);

	*data = 0;

	if (ec->global_lock) {
		status = acpi_acquire_global_lock(ACPI_EC_UDELAY_GLK, &glk);
		if (ACPI_FAILURE(status))
			return_VALUE(-ENODEV);
	}

	/*
	 * Query the EC to find out which _Qxx method we need to evaluate.
	 * Note that successful completion of the query causes the ACPI_EC_SCI
	 * bit to be cleared (and thus clearing the interrupt source).
	 */
	spin_lock_irqsave(&ec->lock, flags);

	acpi_hw_low_level_write(8, ACPI_EC_COMMAND_QUERY, &ec->command_addr, 0);
	result = acpi_ec_wait(ec, ACPI_EC_EVENT_OBF);
	if (result)
		goto end;
	
	acpi_hw_low_level_read(8, data, &ec->data_addr, 0);
	if (!*data)
		result = -ENODATA;

end:
	spin_unlock_irqrestore(&ec->lock, flags);

	if (ec->global_lock)
		acpi_release_global_lock(glk);

	return_VALUE(result);
}


/* --------------------------------------------------------------------------
                                Event Management
   -------------------------------------------------------------------------- */

struct acpi_ec_query_data {
	acpi_handle		handle;
	u8			data;
};


static void
acpi_ec_gpe_query (
	void			*data)
{
	struct acpi_ec_query_data *query_data = NULL;
	static char		object_name[5] = {'_','Q','0','0','\0'};
	const char		hex[] = {'0','1','2','3','4','5','6','7',
				         '8','9','A','B','C','D','E','F'};

	ACPI_FUNCTION_TRACE("acpi_ec_gpe_query");

	if (!data)
		return;

	query_data = (struct acpi_ec_query_data *) data;

	object_name[2] = hex[((query_data->data >> 4) & 0x0F)];
	object_name[3] = hex[(query_data->data & 0x0F)];

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Evaluating %s\n", object_name));

	acpi_evaluate_object(query_data->handle, object_name, NULL, NULL);

	kfree(query_data);

	return;
}


static void
acpi_ec_gpe_handler (
	void			*data)
{
	acpi_status		status = AE_OK;
	struct acpi_ec		*ec = (struct acpi_ec *) data;
	u32			value = 0;
	unsigned long		flags = 0;
	struct acpi_ec_query_data *query_data = NULL;

	if (!ec)
		return;

	spin_lock_irqsave(&ec->lock, flags);
	acpi_hw_low_level_read(8, &value, &ec->command_addr, 0);
	spin_unlock_irqrestore(&ec->lock, flags);

	/* TBD: Implement asynch events!
	 * NOTE: All we care about are EC-SCI's.  Other EC events are
	 *       handled via polling (yuck!).  This is because some systems
	 *       treat EC-SCIs as level (versus EDGE!) triggered, preventing
	 *       a purely interrupt-driven approach (grumble, grumble).
	 */
	if (!(value & ACPI_EC_FLAG_SCI))
		return;

	if (acpi_ec_query(ec, &value))
		return;

	query_data = kmalloc(sizeof(struct acpi_ec_query_data), GFP_ATOMIC);
	if (!query_data)
		return;
	query_data->handle = ec->handle;
	query_data->data = value;

	status = acpi_os_queue_for_execution(OSD_PRIORITY_GPE,
		acpi_ec_gpe_query, query_data);
	if (ACPI_FAILURE(status))
		kfree(query_data);

	return;
}


/* --------------------------------------------------------------------------
                             Address Space Management
   -------------------------------------------------------------------------- */

static acpi_status
acpi_ec_space_setup (
	acpi_handle		region_handle,
	u32			function,
	void			*handler_context,
	void			**return_context)
{
	/*
	 * The EC object is in the handler context and is needed
	 * when calling the acpi_ec_space_handler.
	 */
	*return_context = handler_context;

	return AE_OK;
}


static acpi_status
acpi_ec_space_handler (
	u32			function,
	ACPI_PHYSICAL_ADDRESS	address,
	u32			bit_width,
	acpi_integer		*value,
	void			*handler_context,
	void			*region_context)
{
	int			result = 0;
	struct acpi_ec		*ec = NULL;
	u32          		tmp = 0;

	ACPI_FUNCTION_TRACE("acpi_ec_space_handler");

	if ((address > 0xFF) || (bit_width != 8) || !value || !handler_context)
		return_VALUE(AE_BAD_PARAMETER);

	ec = (struct acpi_ec *) handler_context;

	switch (function) {
	case ACPI_READ:
		result = acpi_ec_read(ec, (u8) address, &tmp);
		*value = (acpi_integer) tmp;
		break;
	case ACPI_WRITE:
		result = acpi_ec_write(ec, (u8) address, (u8) *value);
		break;
	default:
		result = -EINVAL;
		break;
	}

	switch (result) {
	case -EINVAL:
		return_VALUE(AE_BAD_PARAMETER);
		break;
	case -ENODEV:
		return_VALUE(AE_NOT_FOUND);
		break;
	case -ETIME:
		return_VALUE(AE_TIME);
		break;
	default:
		return_VALUE(AE_OK);
	}

}


/* --------------------------------------------------------------------------
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */

struct proc_dir_entry		*acpi_ec_dir = NULL;


static int
acpi_ec_read_info (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*data)
{
	struct acpi_ec		*ec = (struct acpi_ec *) data;
	char			*p = page;
	int			len = 0;

	ACPI_FUNCTION_TRACE("acpi_ec_read_info");

	if (!ec || (off != 0))
		goto end;

	p += sprintf(p, "gpe bit:                 0x%02x\n",
		(u32) ec->gpe_bit);
	p += sprintf(p, "ports:                   0x%02x, 0x%02x\n",
		(u32) ec->status_addr.address, (u32) ec->data_addr.address);
	p += sprintf(p, "use global lock:         %s\n",
		ec->global_lock?"yes":"no");

end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;

	return_VALUE(len);
}


static int
acpi_ec_add_fs (
	struct acpi_device	*device)
{
	struct proc_dir_entry	*entry = NULL;

	ACPI_FUNCTION_TRACE("acpi_ec_add_fs");

	if (!acpi_ec_dir) {
		acpi_ec_dir = proc_mkdir(ACPI_EC_CLASS, acpi_root_dir);
		if (!acpi_ec_dir)
			return_VALUE(-ENODEV);
	}

	if (!acpi_device_dir(device)) {
		acpi_device_dir(device) = proc_mkdir(acpi_device_bid(device),
			acpi_ec_dir);
		if (!acpi_device_dir(device))
			return_VALUE(-ENODEV);
	}

	entry = create_proc_read_entry(ACPI_EC_FILE_INFO, S_IRUGO,
		acpi_device_dir(device), acpi_ec_read_info,
		acpi_driver_data(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_WARN,
			"Unable to create '%s' fs entry\n",
			ACPI_EC_FILE_INFO));

	return_VALUE(0);
}


static int
acpi_ec_remove_fs (
	struct acpi_device	*device)
{
	ACPI_FUNCTION_TRACE("acpi_ec_remove_fs");

	if (!acpi_ec_dir)
		return_VALUE(-ENODEV);

	if (acpi_device_dir(device))
		remove_proc_entry(acpi_device_bid(device), acpi_ec_dir);

	return_VALUE(0);
}


/* --------------------------------------------------------------------------
                               Driver Interface
   -------------------------------------------------------------------------- */

static int
acpi_ec_add (
	struct acpi_device	*device)
{
	int			result = 0;
	acpi_status		status = AE_OK;
	struct acpi_ec		*ec = NULL;
	unsigned long		uid;

	ACPI_FUNCTION_TRACE("acpi_ec_add");

	if (!device)
		return_VALUE(-EINVAL);

	ec = kmalloc(sizeof(struct acpi_ec), GFP_KERNEL);
	if (!ec)
		return_VALUE(-ENOMEM);
	memset(ec, 0, sizeof(struct acpi_ec));

	ec->handle = device->handle;
	ec->lock = SPIN_LOCK_UNLOCKED;
	sprintf(acpi_device_name(device), "%s", ACPI_EC_DEVICE_NAME);
	sprintf(acpi_device_class(device), "%s", ACPI_EC_CLASS);
	acpi_driver_data(device) = ec;

	/* Use the global lock for all EC transactions? */
	acpi_evaluate_integer(ec->handle, "_GLK", NULL, &ec->global_lock);

	/* If our UID matches ecdt_uid, we already found this EC via the
	   ECDT. Abort. */
	acpi_evaluate_integer(ec->handle, "_UID", NULL, &uid);
	if (ecdt_uid == uid) {
		result = -ENODEV;
		goto end;
	}

	/* Get GPE bit assignment (EC events). */
	status = acpi_evaluate_integer(ec->handle, "_GPE", NULL, &ec->gpe_bit);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Error obtaining GPE bit assignment\n"));
		result = -ENODEV;
		goto end;
	}

	result = acpi_ec_add_fs(device);
	if (result)
		goto end;

	printk(KERN_INFO PREFIX "%s [%s] (gpe %d)\n",
		acpi_device_name(device), acpi_device_bid(device),
		(u32) ec->gpe_bit);

end:
	if (result)
		kfree(ec);

	return_VALUE(result);
}


static int
acpi_ec_remove (
	struct acpi_device	*device,
	int			type)
{
	struct acpi_ec		*ec = NULL;

	ACPI_FUNCTION_TRACE("acpi_ec_remove");

	if (!device)
		return_VALUE(-EINVAL);

	ec = (struct acpi_ec *) acpi_driver_data(device);

	acpi_ec_remove_fs(device);

	kfree(ec);

	return_VALUE(0);
}


static int
acpi_ec_start (
	struct acpi_device	*device)
{
	int			result = 0;
	acpi_status		status = AE_OK;
	struct acpi_ec		*ec = NULL;
	acpi_buffer		buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	acpi_resource		*resource = NULL;

	ACPI_FUNCTION_TRACE("acpi_ec_start");

	if (!device)
		return_VALUE(-EINVAL);

	ec = (struct acpi_ec *) acpi_driver_data(device);

	if (!ec)
		return_VALUE(-EINVAL);

	/*
	 * Get I/O port addresses. Convert to GAS format.
	 */
	status = acpi_get_current_resources(ec->handle, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error getting I/O port addresses"));
		return_VALUE(-ENODEV);
	}

	resource = (acpi_resource *) buffer.pointer;
	if (!resource || (resource->id != ACPI_RSTYPE_IO)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid or missing resource\n"));
		result = -ENODEV;
		goto end;
	}
	ec->data_addr.address_space_id = ACPI_ADR_SPACE_SYSTEM_IO;
	ec->data_addr.register_bit_width = 8;
	ec->data_addr.register_bit_offset = 0;
	ec->data_addr.address = resource->data.io.min_base_address;

	resource = ACPI_NEXT_RESOURCE(resource);
	if (!resource || (resource->id != ACPI_RSTYPE_IO)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid or missing resource\n"));
		result = -ENODEV;
		goto end;
	}
	ec->command_addr.address_space_id = ACPI_ADR_SPACE_SYSTEM_IO;
	ec->command_addr.register_bit_width = 8;
	ec->command_addr.register_bit_offset = 0;
	ec->command_addr.address = resource->data.io.min_base_address;
	ec->status_addr = ec->command_addr;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "gpe=0x%02x, ports=0x%2x,0x%2x\n",
		(u32) ec->gpe_bit, (u32) ec->command_addr.address,
		(u32) ec->data_addr.address));

	/*
	 * Install GPE handler
	 */
	status = acpi_install_gpe_handler(ec->gpe_bit,
		ACPI_EVENT_EDGE_TRIGGERED, &acpi_ec_gpe_handler, ec);
	if (ACPI_FAILURE(status)) {
		result = -ENODEV;
		goto end;
	}

	status = acpi_install_address_space_handler (ec->handle,
			ACPI_ADR_SPACE_EC, &acpi_ec_space_handler,
			&acpi_ec_space_setup, ec);
	if (ACPI_FAILURE(status)) {
		acpi_remove_gpe_handler(ec->gpe_bit, &acpi_ec_gpe_handler);
		result = -ENODEV;
		goto end;
	}
end:
	acpi_os_free(buffer.pointer);

	return_VALUE(result);
}


static int
acpi_ec_stop (
	struct acpi_device	*device,
	int			type)
{
	acpi_status		status = AE_OK;
	struct acpi_ec		*ec = NULL;

	ACPI_FUNCTION_TRACE("acpi_ec_stop");

	if (!device)
		return_VALUE(-EINVAL);

	ec = (struct acpi_ec *) acpi_driver_data(device);

	status = acpi_remove_address_space_handler(ec->handle,
		ACPI_ADR_SPACE_EC, &acpi_ec_space_handler);
	if (ACPI_FAILURE(status))
		return_VALUE(-ENODEV);

	status = acpi_remove_gpe_handler(ec->gpe_bit, &acpi_ec_gpe_handler);
	if (ACPI_FAILURE(status))
		return_VALUE(-ENODEV);

	return_VALUE(0);
}


int __init
acpi_ec_ecdt_probe (void)
{
	acpi_status		status;
	struct acpi_table_ecdt 	*ecdt_ptr;

	status = acpi_get_firmware_table("ECDT", 1, ACPI_LOGICAL_ADDRESSING, 
		(acpi_table_header **) &ecdt_ptr);
	if (ACPI_SUCCESS(status)) {
		printk(KERN_INFO PREFIX "Found ECDT\n");

		/*
		 * TODO: When the new driver model allows it, simply tell the
		 * EC driver it has a new device via that, instead if this.
		 */
		ec_ecdt = kmalloc(sizeof(struct acpi_ec), GFP_KERNEL);
		if (!ec_ecdt)
			return -ENOMEM;
		memset(ec_ecdt, 0, sizeof(struct acpi_ec));
		
		ec_ecdt->command_addr = ecdt_ptr->ec_control;
		ec_ecdt->status_addr = ecdt_ptr->ec_control;
		ec_ecdt->data_addr = ecdt_ptr->ec_data;
		ec_ecdt->gpe_bit = ecdt_ptr->gpe_bit;
		ec_ecdt->lock = SPIN_LOCK_UNLOCKED;
		/* use the GL just to be safe */
		ec_ecdt->global_lock = TRUE;
		ecdt_uid = ecdt_ptr->uid;

		/*
		 * Install GPE handler
		 */
		status = acpi_install_gpe_handler(ec_ecdt->gpe_bit,
			ACPI_EVENT_EDGE_TRIGGERED, &acpi_ec_gpe_handler,
			ec_ecdt);
		if (ACPI_FAILURE(status)) {
			goto error;
		}

		status = acpi_install_address_space_handler (ACPI_ROOT_OBJECT,
				ACPI_ADR_SPACE_EC, &acpi_ec_space_handler,
				&acpi_ec_space_setup, ec_ecdt);
		if (ACPI_FAILURE(status)) {
			acpi_remove_gpe_handler(ec_ecdt->gpe_bit,
				&acpi_ec_gpe_handler);
			goto error;
		}
	}

	return 0;

error:
	kfree(ec_ecdt);

	return -ENODEV;
}


int __init
acpi_ec_init (void)
{
	int			result = 0;

	ACPI_FUNCTION_TRACE("acpi_ec_init");

	result = acpi_bus_register_driver(&acpi_ec_driver);
	if (result < 0) {
		remove_proc_entry(ACPI_EC_CLASS, acpi_root_dir);
		return_VALUE(-ENODEV);
	}

	return_VALUE(0);
}

void __exit
acpi_ec_ecdt_exit (void)
{
	if (!ec_ecdt)
		return;

	acpi_remove_address_space_handler(ACPI_ROOT_OBJECT,
		ACPI_ADR_SPACE_EC, &acpi_ec_space_handler);
	
	acpi_remove_gpe_handler(ec_ecdt->gpe_bit, &acpi_ec_gpe_handler);

	kfree(ec_ecdt);
}

void __exit
acpi_ec_exit (void)
{
	int			result = 0;

	ACPI_FUNCTION_TRACE("acpi_ec_exit");

	result = acpi_bus_unregister_driver(&acpi_ec_driver);
	if (!result)
		remove_proc_entry(ACPI_EC_CLASS, acpi_root_dir);


	acpi_ec_ecdt_exit();

	return_VOID;
}
