/*
 * coretemp.c - Linux kernel module for hardware monitoring
 *
 * Copyright (C) 2007 Rudolf Marek <r.marek@assembler.cz>
 *
 * Inspired from many hwmon drivers
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
#include <linux/jiffies.h>
#include <linux/hwmon.h>
#include <linux/sysfs.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <asm/msr.h>
#include <xen/pcpu.h>
#include "../xen/core/domctl.h"

#define DRVNAME	"coretemp"
#define coretemp_data pdev_entry

typedef enum { SHOW_TEMP, SHOW_TJMAX, SHOW_TTARGET, SHOW_LABEL,
		SHOW_NAME } SHOW;

/*
 * Functions declaration
 */

static struct coretemp_data *coretemp_update_device(struct device *dev);

struct pdev_entry {
	struct list_head list;
	struct platform_device *pdev;
	struct device *hwmon_dev;
	struct mutex update_lock;
	const char *name;
	u32 cpu_core_id, phys_proc_id;
	u8 x86_model, x86_mask;
	u32 ucode_rev;
	char valid;		/* zero until following fields are valid */
	unsigned long last_updated;	/* in jiffies */
	int temp;
	int tjmax;
	int ttarget;
	u8 alarm;
};

/*
 * Sysfs stuff
 */

static ssize_t show_name(struct device *dev, struct device_attribute
			  *devattr, char *buf)
{
	int ret;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct coretemp_data *data = dev_get_drvdata(dev);

	if (attr->index == SHOW_NAME)
		ret = sprintf(buf, "%s\n", data->name);
	else	/* show label */
		ret = sprintf(buf, "Core %d\n", data->cpu_core_id);
	return ret;
}

static ssize_t show_alarm(struct device *dev, struct device_attribute
			  *devattr, char *buf)
{
	struct coretemp_data *data = coretemp_update_device(dev);
	/* read the Out-of-spec log, never clear */
	return sprintf(buf, "%d\n", data->alarm);
}

static ssize_t show_temp(struct device *dev,
			 struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct coretemp_data *data = coretemp_update_device(dev);
	int err;

	if (attr->index == SHOW_TEMP)
		err = data->valid ? sprintf(buf, "%d\n", data->temp) : -EAGAIN;
	else if (attr->index == SHOW_TJMAX)
		err = sprintf(buf, "%d\n", data->tjmax);
	else
		err = sprintf(buf, "%d\n", data->ttarget);
	return err;
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL,
			  SHOW_TEMP);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO, show_temp, NULL,
			  SHOW_TJMAX);
static SENSOR_DEVICE_ATTR(temp1_max, S_IRUGO, show_temp, NULL,
			  SHOW_TTARGET);
static DEVICE_ATTR(temp1_crit_alarm, S_IRUGO, show_alarm, NULL);
static SENSOR_DEVICE_ATTR(temp1_label, S_IRUGO, show_name, NULL, SHOW_LABEL);
static SENSOR_DEVICE_ATTR(name, S_IRUGO, show_name, NULL, SHOW_NAME);

static struct attribute *coretemp_attributes[] = {
	&sensor_dev_attr_name.dev_attr.attr,
	&sensor_dev_attr_temp1_label.dev_attr.attr,
	&dev_attr_temp1_crit_alarm.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	NULL
};

static const struct attribute_group coretemp_group = {
	.attrs = coretemp_attributes,
};

static struct coretemp_data *coretemp_update_device(struct device *dev)
{
	struct coretemp_data *data = dev_get_drvdata(dev);

	mutex_lock(&data->update_lock);

	if (!data->valid || time_after(jiffies, data->last_updated + HZ)) {
		u32 eax, edx;

		data->valid = 0;
		if (rdmsr_safe_on_pcpu(data->pdev->id, MSR_IA32_THERM_STATUS,
				       &eax, &edx) < 0)
			eax = ~0;
		data->alarm = (eax >> 5) & 1;
		/* update only if data has been valid */
		if (eax & 0x80000000) {
			data->temp = data->tjmax - (((eax >> 16)
							& 0x7f) * 1000);
			data->valid = 1;
		} else {
			dev_dbg(dev, "Temperature data invalid (0x%x)\n", eax);
		}
		data->last_updated = jiffies;
	}

	mutex_unlock(&data->update_lock);
	return data;
}

static int adjust_tjmax(struct coretemp_data *c, u32 id, struct device *dev)
{
	/* The 100C is default for both mobile and non mobile CPUs */

	int tjmax = 100000;
	int tjmax_ee = 85000;
	int usemsr_ee = 1;
	int err;
	u32 eax, edx;
	struct pci_dev *host_bridge;

	/* Early chips have no MSR for TjMax */

	if ((c->x86_model == 0xf) && (c->x86_mask < 4)) {
		usemsr_ee = 0;
	}

	/* Atom CPUs */

	if (c->x86_model == 0x1c) {
		usemsr_ee = 0;

		host_bridge = pci_get_bus_and_slot(0, PCI_DEVFN(0, 0));

		if (host_bridge && host_bridge->vendor == PCI_VENDOR_ID_INTEL
		    && (host_bridge->device == 0xa000	/* NM10 based nettop */
		    || host_bridge->device == 0xa010))	/* NM10 based netbook */
			tjmax = 100000;
		else
			tjmax = 90000;

		pci_dev_put(host_bridge);
	}

	if ((c->x86_model > 0xe) && (usemsr_ee)) {
		u8 platform_id;

		/* Now we can detect the mobile CPU using Intel provided table
		   http://softwarecommunity.intel.com/Wiki/Mobility/720.htm
		   For Core2 cores, check MSR 0x17, bit 28 1 = Mobile CPU
		*/

		err = rdmsr_safe_on_pcpu(id, 0x17, &eax, &edx);
		if (err < 0) {
			dev_warn(dev,
				 "Unable to access MSR 0x17, assuming desktop"
				 " CPU\n");
			usemsr_ee = 0;
		} else if (c->x86_model < 0x17 && !(eax & 0x10000000)) {
			/* Trust bit 28 up to Penryn, I could not find any
			   documentation on that; if you happen to know
			   someone at Intel please ask */
			usemsr_ee = 0;
		} else {
			/* Platform ID bits 52:50 (EDX starts at bit 32) */
			platform_id = (edx >> 18) & 0x7;

			/* Mobile Penryn CPU seems to be platform ID 7 or 5
			  (guesswork) */
			if ((c->x86_model == 0x17) &&
			    ((platform_id == 5) || (platform_id == 7))) {
				/* If MSR EE bit is set, set it to 90 degrees C,
				   otherwise 105 degrees C */
				tjmax_ee = 90000;
				tjmax = 105000;
			}
		}
	}

	if (usemsr_ee) {

		err = rdmsr_safe_on_pcpu(id, 0xee, &eax, &edx);
		if (err < 0) {
			dev_warn(dev,
				 "Unable to access MSR 0xEE, for Tjmax, left"
				 " at default\n");
		} else if (eax & 0x40000000) {
			tjmax = tjmax_ee;
		}
	/* if we dont use msr EE it means we are desktop CPU (with exeception
	   of Atom) */
	} else if (tjmax == 100000) {
		dev_warn(dev, "Using relative temperature scale!\n");
	}

	return tjmax;
}

static int get_tjmax(struct coretemp_data *c, u32 id, struct device *dev)
{
	/* The 100C is default for both mobile and non mobile CPUs */
	int err;
	u32 eax, edx;
	u32 val;

	/* A new feature of current Intel(R) processors, the
	   IA32_TEMPERATURE_TARGET contains the TjMax value */
	err = rdmsr_safe_on_pcpu(id, MSR_IA32_TEMPERATURE_TARGET, &eax, &edx);
	if (err < 0) {
		dev_warn(dev, "Unable to read TjMax from CPU.\n");
	} else {
		val = (eax >> 16) & 0xff;
		/*
		 * If the TjMax is not plausible, an assumption
		 * will be used
		 */
		if ((val > 80) && (val < 120)) {
			dev_info(dev, "TjMax is %d C.\n", val);
			return val * 1000;
		}
	}

	/*
	 * An assumption is made for early CPUs and unreadable MSR.
	 * NOTE: the given value may not be correct.
	 */

	switch (c->x86_model) {
	case 0xe:
	case 0xf:
	case 0x16:
	case 0x1a:
		dev_warn(dev, "TjMax is assumed as 100 C!\n");
		return 100000;
	case 0x17:
	case 0x1c:		/* Atom CPUs */
		return adjust_tjmax(c, id, dev);
	default:
		dev_warn(dev, "CPU (model=0x%x) is not supported yet,"
			" using default TjMax of 100C.\n", c->x86_model);
		return 100000;
	}
}

static int coretemp_probe(struct platform_device *pdev)
{
	struct coretemp_data *data = platform_get_drvdata(pdev);
	int err;
	u32 eax, edx;

	data->name = "coretemp";
	mutex_init(&data->update_lock);

	/* test if we can access the THERM_STATUS MSR */
	err = rdmsr_safe_on_pcpu(pdev->id, MSR_IA32_THERM_STATUS, &eax, &edx);
	if (err < 0) {
		dev_err(&pdev->dev,
			"Unable to access THERM_STATUS MSR, giving up\n");
		return err;
	}

	/* Check if we have problem with errata AE18 of Core processors:
	   Readings might stop update when processor visited too deep sleep,
	   fixed for stepping D0 (6EC).
	*/

	if ((data->x86_model == 0xe) && (data->x86_mask < 0xc)) {
		/* check for microcode update */
		if (!(data->ucode_rev + 1))
			dev_warn(&pdev->dev,
				 "Cannot read microcode revision of CPU\n");
		else if (data->ucode_rev < 0x39) {
			err = -ENODEV;
			dev_err(&pdev->dev,
				"Errata AE18 not fixed, update BIOS or "
				"microcode of the CPU!\n");
			return err;
		}
	}

	data->tjmax = get_tjmax(data, pdev->id, &pdev->dev);

	/*
	 * read the still undocumented IA32_TEMPERATURE_TARGET. It exists
	 * on older CPUs but not in this register,
	 * Atoms don't have it either.
	 */

	if ((data->x86_model > 0xe) && (data->x86_model != 0x1c)) {
		err = rdmsr_safe_on_pcpu(pdev->id, MSR_IA32_TEMPERATURE_TARGET,
					 &eax, &edx);
		if (err < 0) {
			dev_warn(&pdev->dev, "Unable to read"
					" IA32_TEMPERATURE_TARGET MSR\n");
		} else {
			data->ttarget = data->tjmax -
					(((eax >> 8) & 0xff) * 1000);
			err = device_create_file(&pdev->dev,
					&sensor_dev_attr_temp1_max.dev_attr);
			if (err)
				return err;
		}
	}

	if ((err = sysfs_create_group(&pdev->dev.kobj, &coretemp_group)))
		goto exit_dev;

	data->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		dev_err(&pdev->dev, "Class registration failed (%d)\n",
			err);
		goto exit_class;
	}

	return 0;

exit_class:
	sysfs_remove_group(&pdev->dev.kobj, &coretemp_group);
exit_dev:
	device_remove_file(&pdev->dev, &sensor_dev_attr_temp1_max.dev_attr);
	return err;
}

static int coretemp_remove(struct platform_device *pdev)
{
	struct coretemp_data *data = platform_get_drvdata(pdev);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&pdev->dev.kobj, &coretemp_group);
	device_remove_file(&pdev->dev, &sensor_dev_attr_temp1_max.dev_attr);
	return 0;
}

static struct platform_driver coretemp_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = DRVNAME,
	},
	.probe = coretemp_probe,
	.remove = coretemp_remove,
};

static LIST_HEAD(pdev_list);
static DEFINE_MUTEX(pdev_list_mutex);

struct cpu_info {
	struct pdev_entry *pdev_entry;
	u32 cpuid_6_eax;
};

static void get_cpuid_info(void *arg)
{
	struct cpu_info *info = arg;
	struct pdev_entry *pdev_entry = info->pdev_entry;
	u32 val = cpuid_eax(1);

	pdev_entry->x86_model = ((val >> 4) & 0xf) | ((val >> 12) & 0xf0);
	pdev_entry->x86_mask = val & 0xf;

	if (((val >> 8) & 0xf) != 6 || ((val >> 20) & 0xff)
	    || !pdev_entry->x86_model
	    || wrmsr_safe(MSR_IA32_UCODE_REV, 0, 0) < 0
	    || (sync_core(), rdmsr_safe(MSR_IA32_UCODE_REV,
					&val, &pdev_entry->ucode_rev)) < 0)
		pdev_entry->ucode_rev = ~0;

	info->cpuid_6_eax = cpuid_eax(0) >= 6 ? cpuid_eax(6) : 0;
}

static int coretemp_device_add(unsigned int cpu)
{
	int err;
	struct cpu_info info;
	struct platform_device *pdev;
	struct pdev_entry *pdev_entry;

	info.pdev_entry = kzalloc(sizeof(*pdev_entry), GFP_KERNEL);
	if (!info.pdev_entry)
		return -ENOMEM;

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

	/*
	 * CPUID.06H.EAX[0] indicates whether the CPU has thermal
	 * sensors. We check this bit only, all the early CPUs
	 * without thermal sensors will be filtered out.
	 */
	if (!(info.cpuid_6_eax & 0x1)) {
		pr_info("CPU (model=0x%x) has no thermal sensor\n",
			info.pdev_entry->x86_model);
		goto exit_entry_free;
	}

	err = xen_get_topology_info(cpu, &info.pdev_entry->cpu_core_id,
				    &info.pdev_entry->phys_proc_id, NULL);
	if (err)
		goto exit_entry_free;

	mutex_lock(&pdev_list_mutex);

	/* Skip second HT entry of each core */
	list_for_each_entry(pdev_entry, &pdev_list, list) {
		if (info.pdev_entry->phys_proc_id == pdev_entry->phys_proc_id &&
		    info.pdev_entry->cpu_core_id == pdev_entry->cpu_core_id) {
			err = 0;	/* Not an error */
			goto exit;
		}
	}

	pdev = platform_device_alloc(DRVNAME, cpu);
	if (!pdev) {
		err = -ENOMEM;
		pr_err("Device allocation failed\n");
		goto exit;
	}

	pdev_entry = info.pdev_entry;
	platform_set_drvdata(pdev, pdev_entry);
	pdev_entry->pdev = pdev;

	err = platform_device_add(pdev);
	if (err) {
		pr_err("Device addition failed (%d)\n", err);
		goto exit_device_put;
	}

	list_add_tail(&pdev_entry->list, &pdev_list);
	mutex_unlock(&pdev_list_mutex);

	return 0;

exit_device_put:
	platform_device_put(pdev);
exit:
	mutex_unlock(&pdev_list_mutex);
exit_entry_free:
	kfree(info.pdev_entry);
	return err;
}

static void coretemp_device_remove(unsigned int cpu)
{
	struct pdev_entry *p;
	unsigned int i;

	mutex_lock(&pdev_list_mutex);
	list_for_each_entry(p, &pdev_list, list) {
		if (p->pdev->id != cpu)
			continue;

		platform_device_unregister(p->pdev);
		list_del(&p->list);
		mutex_unlock(&pdev_list_mutex);
		for (i = 0; ; ++i) {
			u32 cpu_core_id, phys_proc_id;
			int err;

			if (i == cpu)
				continue;
			err = xen_get_topology_info(i, &cpu_core_id,
						    &phys_proc_id, NULL);
			if (err == -ENOENT)
				continue;
			if (err)
				break;
			if (phys_proc_id != p->phys_proc_id ||
			    cpu_core_id != p->cpu_core_id)
				continue;
			if (!coretemp_device_add(i))
				break;
		}
		kfree(p);
		return;
	}
	mutex_unlock(&pdev_list_mutex);
}

static int coretemp_cpu_callback(struct notifier_block *nfb,
				 unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long) hcpu;

	switch (action) {
	case CPU_ONLINE:
		coretemp_device_add(cpu);
		break;
	case CPU_DEAD:
		coretemp_device_remove(cpu);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block coretemp_cpu_notifier = {
	.notifier_call = coretemp_cpu_callback,
};

static int __init coretemp_init(void)
{
	int err = -ENODEV;

	if (!is_initial_xendomain())
		goto exit;

	/* quick check if we run Intel */
	if (cpu_data(0).x86_vendor != X86_VENDOR_INTEL)
		goto exit;

	err = platform_driver_register(&coretemp_driver);
	if (err)
		goto exit;

	err = register_pcpu_notifier(&coretemp_cpu_notifier);
	if (err)
		goto exit_driver_unreg;

#ifndef CONFIG_ACPI_HOTPLUG_CPU
	if (list_empty(&pdev_list)) {
		unregister_pcpu_notifier(&coretemp_cpu_notifier);
		err = -ENODEV;
		goto exit_driver_unreg;
	}
#endif

	return 0;

exit_driver_unreg:
	platform_driver_unregister(&coretemp_driver);
exit:
	return err;
}

static void __exit coretemp_exit(void)
{
	struct pdev_entry *p, *n;

	unregister_pcpu_notifier(&coretemp_cpu_notifier);
	mutex_lock(&pdev_list_mutex);
	list_for_each_entry_safe(p, n, &pdev_list, list) {
		platform_device_unregister(p->pdev);
		list_del(&p->list);
		kfree(p);
	}
	mutex_unlock(&pdev_list_mutex);
	platform_driver_unregister(&coretemp_driver);
}

MODULE_AUTHOR("Rudolf Marek <r.marek@assembler.cz>");
MODULE_DESCRIPTION("Intel Core temperature monitor");
MODULE_LICENSE("GPL");

module_init(coretemp_init)
module_exit(coretemp_exit)
