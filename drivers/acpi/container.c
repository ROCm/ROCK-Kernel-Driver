/*
 * acpi_container.c  - ACPI Generic Container Driver
 * ($Revision: )
 *
 * Copyright (C) 2004 Anil S Keshavamurthy (anil.s.keshavamurthy@intel.com)
 * Copyright (C) 2004 Keiichiro Tokunaga (tokunaga.keiich@jp.fujitsu.com)
 * Copyright (C) 2004 Motoyuki Ito (motoyuki@soft.fujitsu.com)
 * Copyright (C) 2004 Intel Corp.
 * Copyright (C) 2004 FUJITSU LIMITED
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
#include <linux/acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <acpi/container.h>

#define ACPI_CONTAINER_DRIVER_NAME	"ACPI container driver"
#define ACPI_CONTAINER_DEVICE_NAME	"ACPI container device"
#define ACPI_CONTAINER_CLASS		"container"

#define INSTALL_NOTIFY_HANDLER		1
#define UNINSTALL_NOTIFY_HANDLER	2

#define ACPI_CONTAINER_COMPONENT	0x01000000
#define _COMPONENT			ACPI_CONTAINER_COMPONENT
ACPI_MODULE_NAME			("acpi_container")

MODULE_AUTHOR("Anil S Keshavamurthy");
MODULE_DESCRIPTION(ACPI_CONTAINER_DRIVER_NAME);
MODULE_LICENSE("GPL");

#define ACPI_STA_PRESENT		(0x00000001)

static int acpi_container_add(struct acpi_device *device);
static int acpi_container_remove(struct acpi_device *device, int type);

static struct acpi_driver acpi_container_driver = {
	.name =		ACPI_CONTAINER_DRIVER_NAME,
	.class =	ACPI_CONTAINER_CLASS,
	.ids =		"ACPI0004,PNP0A05,PNP0A06",
	.ops =		{
				.add =		acpi_container_add,
				.remove =	acpi_container_remove,
			},
};


/*******************************************************************/

static int
is_device_present(acpi_handle handle)
{
	acpi_handle		temp;
	acpi_status		status;
	unsigned long	sta;

	ACPI_FUNCTION_TRACE("is_device_present");

	status = acpi_get_handle(handle, "_STA", &temp);
	if (ACPI_FAILURE(status))
		return_VALUE(1); /* _STA not found, assmue device present */

	status = acpi_evaluate_integer(handle, "_STA", NULL, &sta);
	if (ACPI_FAILURE(status))
		return_VALUE(0); /* Firmware error */

	return_VALUE((sta & ACPI_STA_PRESENT) == ACPI_STA_PRESENT);
}

/*******************************************************************/
static int
acpi_container_add(struct acpi_device *device)
{
	struct acpi_container *container;

	ACPI_FUNCTION_TRACE("acpi_container_add");

	if (!device) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "device is NULL\n"));
		return_VALUE(-EINVAL);
	}

	container = kmalloc(sizeof(struct acpi_container), GFP_KERNEL);
	if(!container)
		return_VALUE(-ENOMEM);
	
	memset(container, 0, sizeof(struct acpi_container));
	container->handle = device->handle;
	strcpy(acpi_device_name(device), ACPI_CONTAINER_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_CONTAINER_CLASS);
	acpi_driver_data(device) = container;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device <%s> bid <%s>\n",	\
		acpi_device_name(device), acpi_device_bid(device)));


	return_VALUE(0);
}

static int
acpi_container_remove(struct acpi_device *device, int type)
{
	acpi_status		status = AE_OK;
	struct acpi_container	*pc = NULL;
	pc = (struct acpi_container*) acpi_driver_data(device);

	if (pc)
		kfree(pc);

	return status;
}


static int
container_run_sbin_hotplug(struct acpi_device *device, char *action)
{
	char *argv[3], *envp[6], action_str[32];
	int i, ret;
	int len;
	char pathname[ACPI_PATHNAME_MAX] = {0};
	acpi_status status;
	char *container_str;
	struct acpi_buffer buffer = {ACPI_PATHNAME_MAX, pathname};

	ACPI_FUNCTION_TRACE("container_run_sbin_hotplug");


	status = acpi_get_name(device->handle, ACPI_FULL_PATHNAME, &buffer);
	if (ACPI_FAILURE(status)) {
		return(-ENODEV);
	}

	len = strlen("CONTAINER=") + strlen(pathname) + 1;
	container_str = kmalloc(len, GFP_KERNEL);
	if (!container_str)
		return(-ENOMEM);

	sprintf(container_str, "CONTAINER=%s",pathname);
	sprintf(action_str, "ACTION=%s", action);

	i = 0;
	argv[i++] = hotplug_path;
	argv[i++] = "container";
	argv[i] = NULL;

	i = 0;
	envp[i++] = "HOME=/";
	envp[i++] = "PATH=/sbin;/bin;/usr/sbin;/usr/bin";
	envp[i++] = action_str;
	envp[i++] = container_str;
	envp[i++] = "PLATFORM=ACPI";
	envp[i] = NULL;

	ret = call_usermodehelper(argv[0], argv, envp, 0);

	kfree(container_str);
	return_VALUE(ret);
}

static int
container_device_add(struct acpi_device **device, acpi_handle handle)
{
	acpi_handle phandle;
	struct acpi_device *pdev;
	int result;

	ACPI_FUNCTION_TRACE("container_device_add");

	if (acpi_get_parent(handle, &phandle)) {
		return_VALUE(-ENODEV);
	}

	if (acpi_bus_get_device(phandle, &pdev)) {
		return_VALUE(-ENODEV);
	}

	if (acpi_bus_add(device, pdev, handle, ACPI_BUS_TYPE_DEVICE)) {
		return_VALUE(-ENODEV);
	}

	result = acpi_bus_scan(*device);

	return_VALUE(result);
}

static void
container_notify_cb(acpi_handle handle, u32 type, void *context)
{
	struct acpi_device		*device = NULL;
	int result;
	int present;
	acpi_status status;

	ACPI_FUNCTION_TRACE("container_notify_cb");

	present = is_device_present(handle);
	
	switch (type) {
	case ACPI_NOTIFY_BUS_CHECK:
		/* Fall through */
	case ACPI_NOTIFY_DEVICE_CHECK:
		printk("Container driver received %s event\n",
			(type == ACPI_NOTIFY_BUS_CHECK)?
			"ACPI_NOTIFY_BUS_CHECK":"ACPI_NOTIFY_DEVICE_CHECK");
		if (present) {
			status = acpi_bus_get_device(handle, &device);
			if (ACPI_FAILURE(status) || !device) {
				result = container_device_add(&device, handle);
				if (!result)
					container_run_sbin_hotplug(device, "add");
			} else {
				/* device exist and this is a remove request */
				container_run_sbin_hotplug(device, "remove");
			}
		}
		break;
	case ACPI_NOTIFY_EJECT_REQUEST:
		if (!acpi_bus_get_device(handle, &device) && device) {
			container_run_sbin_hotplug(device, "remove");
		}
		break;
	default:
		break;
	}
	return_VOID;
}

static acpi_status
container_walk_namespace_cb(acpi_handle handle,
	u32 lvl,
	void *context,
	void **rv)
{
	char 				*hid = NULL;
	struct acpi_buffer 		buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	struct acpi_device_info 	*info;
	acpi_status 			status;
	int 				*action = context;

	ACPI_FUNCTION_TRACE("container_walk_namespace_cb");

	status = acpi_get_object_info(handle, &buffer);
	if (ACPI_FAILURE(status) || !buffer.pointer) {
		return_ACPI_STATUS(AE_OK);
	}

	info = buffer.pointer;
	if (info->valid & ACPI_VALID_HID)
		hid = info->hardware_id.value;

	if (hid == NULL) {
		goto end;
	}

	if (strcmp(hid, "ACPI0004") && strcmp(hid, "PNP0A05") &&
			strcmp(hid, "PNP0A06")) {
		goto end;
	}

	switch(*action) {
	case INSTALL_NOTIFY_HANDLER:
		acpi_install_notify_handler(handle,
			ACPI_SYSTEM_NOTIFY,
			container_notify_cb,
			NULL);
		break;
	case UNINSTALL_NOTIFY_HANDLER:
		acpi_remove_notify_handler(handle,
			ACPI_SYSTEM_NOTIFY,
			container_notify_cb);
		break;
	default:
		break;
	}

end:
	acpi_os_free(buffer.pointer);

	return_ACPI_STATUS(AE_OK);
}


int __init
acpi_container_init(void)
{
	int	result = 0;
	int	action = INSTALL_NOTIFY_HANDLER;

	result = acpi_bus_register_driver(&acpi_container_driver);
	if (result < 0) {
		return(result);
	}

	/* register notify handler to every container device */
	acpi_walk_namespace(ACPI_TYPE_DEVICE,
				     ACPI_ROOT_OBJECT,
				     ACPI_UINT32_MAX,
				     container_walk_namespace_cb,
				     &action, NULL);

	return(0);
}

void __exit
acpi_container_exit(void)
{
	int			action = UNINSTALL_NOTIFY_HANDLER;

	ACPI_FUNCTION_TRACE("acpi_container_exit");

	acpi_walk_namespace(ACPI_TYPE_DEVICE,
				     ACPI_ROOT_OBJECT,
				     ACPI_UINT32_MAX,
				     container_walk_namespace_cb,
				     &action, NULL);

	acpi_bus_unregister_driver(&acpi_container_driver);

	return_VOID;
}

module_init(acpi_container_init);
module_exit(acpi_container_exit);
