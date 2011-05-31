/*
 * via-cputemp.c - Driver for VIA CPU core temperature monitoring
 * Copyright (C) 2009 VIA Technologies, Inc.
 *
 * based on existing coretemp.c, which is
 *
 * Copyright (C) 2007 Rudolf Marek <r.marek@assembler.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/hwmon.h>
#include <linux/sysfs.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <asm/msr.h>
#include <xen/pcpu.h>
#include "../xen/core/domctl.h"

#define DRVNAME	"via_cputemp"

enum { SHOW_TEMP, SHOW_LABEL, SHOW_NAME };

/*
 * Functions declaration
 */

struct pdev_entry {
	struct list_head list;
	struct platform_device *pdev;
	struct device *hwmon_dev;
	const char *name;
	u8 x86_model;
	u32 msr;
};
#define via_cputemp_data pdev_entry

/*
 * Sysfs stuff
 */

static ssize_t show_name(struct device *dev, struct device_attribute
			  *devattr, char *buf)
{
	int ret;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct via_cputemp_data *data = dev_get_drvdata(dev);

	if (attr->index == SHOW_NAME)
		ret = sprintf(buf, "%s\n", data->name);
	else	/* show label */
		ret = sprintf(buf, "Core %d\n", data->pdev->id);
	return ret;
}

static ssize_t show_temp(struct device *dev,
			 struct device_attribute *devattr, char *buf)
{
	struct via_cputemp_data *data = dev_get_drvdata(dev);
	u32 eax, edx;
	int err;

	err = rdmsr_safe_on_pcpu(data->pdev->id, data->msr, &eax, &edx);
	if (err < 0)
		return -EAGAIN;

	return sprintf(buf, "%lu\n", ((unsigned long)eax & 0xffffff) * 1000);
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL,
			  SHOW_TEMP);
static SENSOR_DEVICE_ATTR(temp1_label, S_IRUGO, show_name, NULL, SHOW_LABEL);
static SENSOR_DEVICE_ATTR(name, S_IRUGO, show_name, NULL, SHOW_NAME);

static struct attribute *via_cputemp_attributes[] = {
	&sensor_dev_attr_name.dev_attr.attr,
	&sensor_dev_attr_temp1_label.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	NULL
};

static const struct attribute_group via_cputemp_group = {
	.attrs = via_cputemp_attributes,
};

static int via_cputemp_probe(struct platform_device *pdev)
{
	struct via_cputemp_data *data = platform_get_drvdata(pdev);
	int err;
	u32 eax, edx;

	data->name = "via_cputemp";

	switch (data->x86_model) {
	case 0xA:
		/* C7 A */
	case 0xD:
		/* C7 D */
		data->msr = 0x1169;
		break;
	case 0xF:
		/* Nano */
		data->msr = 0x1423;
		break;
	default:
		return -ENODEV;
	}

	/* test if we can access the TEMPERATURE MSR */
	err = rdmsr_safe_on_pcpu(pdev->id, data->msr, &eax, &edx);
	if (err >= 0) {
		dev_err(&pdev->dev,
			"Unable to access TEMPERATURE MSR, giving up\n");
		return err;
	}

	err = sysfs_create_group(&pdev->dev.kobj, &via_cputemp_group);
	if (err)
		return err;

	data->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		dev_err(&pdev->dev, "Class registration failed (%d)\n",
			err);
		goto exit_remove;
	}

	return 0;

exit_remove:
	sysfs_remove_group(&pdev->dev.kobj, &via_cputemp_group);
	return err;
}

static int via_cputemp_remove(struct platform_device *pdev)
{
	struct via_cputemp_data *data = platform_get_drvdata(pdev);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&pdev->dev.kobj, &via_cputemp_group);
	return 0;
}

static struct platform_driver via_cputemp_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = DRVNAME,
	},
	.probe = via_cputemp_probe,
	.remove = via_cputemp_remove,
};

static LIST_HEAD(pdev_list);
static DEFINE_MUTEX(pdev_list_mutex);

struct cpu_info {
	struct pdev_entry *pdev_entry;
	u8 x86;
};

static void get_cpuid_info(void *arg)
{
	struct cpu_info *info = arg;
	struct pdev_entry *pdev_entry = info->pdev_entry;
	u32 val = cpuid_eax(1);

	info->x86 = ((val >> 8) & 0xf) + ((val >> 20) & 0xff);
	pdev_entry->x86_model = ((val >> 4) & 0xf) | ((val >> 12) & 0xf0);
}

static int via_cputemp_device_add(unsigned int cpu)
{
	int err;
	struct cpu_info info;
	struct platform_device *pdev;
	struct pdev_entry *pdev_entry;

	pdev_entry = kzalloc(sizeof(*pdev_entry), GFP_KERNEL);
	if (!pdev_entry)
		return -ENOMEM;

	info.pdev_entry = pdev_entry;
	err = xen_set_physical_cpu_affinity(cpu);
	if (!err) {
		get_cpuid_info(&info);
		WARN_ON_ONCE(xen_set_physical_cpu_affinity(-1));
	} else if (err > 0) {
		static bool warned;

		if (!warned) {
			warned = true;
			printk(KERN_WARNING DRVNAME
			       "Cannot set physical CPU affinity"
			       " (assuming use of dom0_vcpus_pin)\n");
		}
		err = smp_call_function_single(cpu, get_cpuid_info, &info, 1);
	}
	if (err)
		goto exit_entry_free;

	if (info.x86 != 6)
		goto exit_entry_free;

	if (pdev_entry->x86_model < 0x0a)
		goto exit_entry_free;

	if (pdev_entry->x86_model > 0x0f) {
		pr_warn("Unknown CPU model 0x%x\n", pdev_entry->x86_model);
		goto exit_entry_free;
	}

	pdev = platform_device_alloc(DRVNAME, cpu);
	if (!pdev) {
		err = -ENOMEM;
		pr_err("Device allocation failed\n");
		goto exit_entry_free;
	}

	platform_set_drvdata(pdev, pdev_entry);
	pdev_entry->pdev = pdev;

	err = platform_device_add(pdev);
	if (err) {
		pr_err("Device addition failed (%d)\n", err);
		goto exit_device_put;
	}

	mutex_lock(&pdev_list_mutex);
	list_add_tail(&pdev_entry->list, &pdev_list);
	mutex_unlock(&pdev_list_mutex);

	return 0;

exit_device_put:
	platform_device_put(pdev);
exit_entry_free:
	kfree(pdev_entry);
	return err;
}

static void via_cputemp_device_remove(unsigned int cpu)
{
	struct pdev_entry *p;

	mutex_lock(&pdev_list_mutex);
	list_for_each_entry(p, &pdev_list, list) {
		if (p->pdev->id == cpu) {
			platform_device_unregister(p->pdev);
			list_del(&p->list);
			mutex_unlock(&pdev_list_mutex);
			kfree(p);
			return;
		}
	}
	mutex_unlock(&pdev_list_mutex);
}

static int via_cputemp_cpu_callback(struct notifier_block *nfb,
				 unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long) hcpu;

	switch (action) {
	case CPU_ONLINE:
		via_cputemp_device_add(cpu);
		break;
	case CPU_DEAD:
		via_cputemp_device_remove(cpu);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block via_cputemp_cpu_notifier = {
	.notifier_call = via_cputemp_cpu_callback,
};

static int __init via_cputemp_init(void)
{
	int err;

	if (!is_initial_xendomain())
		return -ENODEV;

	if (cpu_data(0).x86_vendor != X86_VENDOR_CENTAUR) {
		printk(KERN_DEBUG DRVNAME ": Not a VIA CPU\n");
		err = -ENODEV;
		goto exit;
	}

	err = platform_driver_register(&via_cputemp_driver);
	if (err)
		goto exit;

	err = register_pcpu_notifier(&via_cputemp_cpu_notifier);
	if (err)
		goto exit_driver_unreg;

#ifndef CONFIG_ACPI_HOTPLUG_CPU
	if (list_empty(&pdev_list)) {
		unregister_pcpu_notifier(&via_cputemp_cpu_notifier);
		err = -ENODEV;
		goto exit_driver_unreg;
	}
#endif

	return 0;

exit_driver_unreg:
	platform_driver_unregister(&via_cputemp_driver);
exit:
	return err;
}

static void __exit via_cputemp_exit(void)
{
	struct pdev_entry *p, *n;

	unregister_pcpu_notifier(&via_cputemp_cpu_notifier);
	mutex_lock(&pdev_list_mutex);
	list_for_each_entry_safe(p, n, &pdev_list, list) {
		platform_device_unregister(p->pdev);
		list_del(&p->list);
		kfree(p);
	}
	mutex_unlock(&pdev_list_mutex);
	platform_driver_unregister(&via_cputemp_driver);
}

MODULE_AUTHOR("Harald Welte <HaraldWelte@viatech.com>");
MODULE_DESCRIPTION("VIA CPU temperature monitor");
MODULE_LICENSE("GPL");

module_init(via_cputemp_init)
module_exit(via_cputemp_exit)
