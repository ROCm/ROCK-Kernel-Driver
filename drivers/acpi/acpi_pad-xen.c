/*
 * acpi_pad-xen.c - Xen pad interface
 *
 * Copyright (c) 2012, Intel Corporation.
 *    Author: Liu, Jinsong <jinsong.liu@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <asm/hypervisor.h>

#define ACPI_PROCESSOR_AGGREGATOR_CLASS	"acpi_pad"
#define ACPI_PROCESSOR_AGGREGATOR_DEVICE_NAME "Xen Processor Aggregator"
#define ACPI_PROCESSOR_AGGREGATOR_NOTIFY 0x80
static DEFINE_MUTEX(xen_cpu_lock);

static int xen_acpi_pad_idle_cpus(unsigned int num_cpus)
{
	struct xen_platform_op op;

	op.cmd = XENPF_core_parking;
	op.u.core_parking.type = XEN_CORE_PARKING_SET;
	op.u.core_parking.idle_nums = num_cpus;

	return HYPERVISOR_platform_op(&op);
}

static long xen_acpi_pad_idle_cpus_num(void)
{
	struct xen_platform_op op;

	op.cmd = XENPF_core_parking;
	op.u.core_parking.type = XEN_CORE_PARKING_GET;

	return (long)HYPERVISOR_platform_op(&op)
	       ?: op.u.core_parking.idle_nums;
}

static ssize_t acpi_pad_idlecpus_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long num;
	int err;

	if (strict_strtoul(buf, 0, &num))
		return -EINVAL;
	mutex_lock(&xen_cpu_lock);
	err = xen_acpi_pad_idle_cpus(num);
	mutex_unlock(&xen_cpu_lock);
	return err ?: count;
}

static ssize_t acpi_pad_idlecpus_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	long num = xen_acpi_pad_idle_cpus_num();

	return num < 0 ? num : snprintf(buf, PAGE_SIZE, "%ld\n", num);
}
static DEVICE_ATTR(idlecpus, S_IRUGO|S_IWUSR,
	acpi_pad_idlecpus_show,
	acpi_pad_idlecpus_store);

static int acpi_pad_add_sysfs(struct acpi_device *device)
{
	return device_create_file(&device->dev, &dev_attr_idlecpus);
}

static void acpi_pad_remove_sysfs(struct acpi_device *device)
{
	device_remove_file(&device->dev, &dev_attr_idlecpus);
}

/*
 * Query firmware how many CPUs should be idle
 * return -1 on failure
 */
static int acpi_pad_pur(acpi_handle handle)
{
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *package;
	int num = -1;

	if (ACPI_FAILURE(acpi_evaluate_object(handle, "_PUR", NULL, &buffer)))
		return num;

	if (!buffer.length || !buffer.pointer)
		return num;

	package = buffer.pointer;

	if (package->type == ACPI_TYPE_PACKAGE &&
		package->package.count == 2 &&
		package->package.elements[0].integer.value == 1) /* rev 1 */

		num = package->package.elements[1].integer.value;

	kfree(buffer.pointer);
	return num;
}

/* Notify firmware how many CPUs are idle */
static void acpi_pad_ost(acpi_handle handle, int stat,
	uint32_t idle_cpus)
{
	union acpi_object params[3] = {
		{.type = ACPI_TYPE_INTEGER,},
		{.type = ACPI_TYPE_INTEGER,},
		{.type = ACPI_TYPE_BUFFER,},
	};
	struct acpi_object_list arg_list = {3, params};

	params[0].integer.value = ACPI_PROCESSOR_AGGREGATOR_NOTIFY;
	params[1].integer.value =  stat;
	params[2].buffer.length = 4;
	params[2].buffer.pointer = (void *)&idle_cpus;
	acpi_evaluate_object(handle, "_OST", &arg_list, NULL);
}

static void acpi_pad_handle_notify(acpi_handle handle)
{
	int num_cpus;
	long idle_cpus;

	mutex_lock(&xen_cpu_lock);
	num_cpus = acpi_pad_pur(handle);
	if (num_cpus < 0) {
		mutex_unlock(&xen_cpu_lock);
		return;
	}

	idle_cpus = xen_acpi_pad_idle_cpus(num_cpus)
		    ?: xen_acpi_pad_idle_cpus_num();
	if (idle_cpus >= 0)
		acpi_pad_ost(handle, 0, idle_cpus);
	mutex_unlock(&xen_cpu_lock);
}

static void acpi_pad_notify(acpi_handle handle, u32 event,
	void *data)
{
	switch (event) {
	case ACPI_PROCESSOR_AGGREGATOR_NOTIFY:
		acpi_pad_handle_notify(handle);
		break;
	default:
		printk(KERN_WARNING "Unsupported event [0x%x]\n", event);
		break;
	}
}

static int acpi_pad_add(struct acpi_device *device)
{
	acpi_status status;

	strcpy(acpi_device_name(device), ACPI_PROCESSOR_AGGREGATOR_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_PROCESSOR_AGGREGATOR_CLASS);

	if (acpi_pad_add_sysfs(device))
		return -ENODEV;

	status = acpi_install_notify_handler(device->handle,
		ACPI_DEVICE_NOTIFY, acpi_pad_notify, device);
	if (ACPI_FAILURE(status)) {
		acpi_pad_remove_sysfs(device);
		return -ENODEV;
	}

	return 0;
}

static int acpi_pad_remove(struct acpi_device *device,
	int type)
{
	mutex_lock(&xen_cpu_lock);
	xen_acpi_pad_idle_cpus(0);
	mutex_unlock(&xen_cpu_lock);

	acpi_remove_notify_handler(device->handle,
		ACPI_DEVICE_NOTIFY, acpi_pad_notify);
	acpi_pad_remove_sysfs(device);
	return 0;
}

static const struct acpi_device_id pad_device_ids[] = {
	{"ACPI000C", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, pad_device_ids);

static struct acpi_driver acpi_pad_driver = {
	.name = "processor_aggregator",
	.class = ACPI_PROCESSOR_AGGREGATOR_CLASS,
	.ids = pad_device_ids,
	.ops = {
		.add = acpi_pad_add,
		.remove = acpi_pad_remove,
	},
};

static int __init acpi_pad_init(void)
{
	if (xen_acpi_pad_idle_cpus_num() < 0)
		return -ENODEV;
	return acpi_bus_register_driver(&acpi_pad_driver);
}

static void __exit acpi_pad_exit(void)
{
	acpi_bus_unregister_driver(&acpi_pad_driver);
}

module_init(acpi_pad_init);
module_exit(acpi_pad_exit);
MODULE_AUTHOR("Liu, Jinsong <jinsong.liu@intel.com>");
MODULE_DESCRIPTION("ACPI Processor Aggregator Driver");
MODULE_LICENSE("GPL");
