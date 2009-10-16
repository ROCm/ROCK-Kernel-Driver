/*
 *	Intel CPU Microcode Update Driver for Linux
 *
 *	Copyright (C) 2000-2006 Tigran Aivazian <tigran@aivazian.fsnet.co.uk>
 *		      2006	Shaohua Li <shaohua.li@intel.com>
 *
 *	This driver allows to upgrade microcode on Intel processors
 *	belonging to IA-32 family - PentiumPro, Pentium II,
 *	Pentium III, Xeon, Pentium 4, etc.
 *
 *	Reference: Section 8.11 of Volume 3a, IA-32 Intel? Architecture
 *	Software Developer's Manual
 *	Order Number 253668 or free download from:
 *
 *	http://developer.intel.com/design/pentium4/manuals/253668.htm
 *
 *	For more information, go to http://www.urbanmyth.org/microcode
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/capability.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/cpu.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/firmware.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include <asm/microcode.h>
#include <asm/processor.h>

MODULE_DESCRIPTION("Microcode Update Driver");
MODULE_AUTHOR("Tigran Aivazian <tigran@aivazian.fsnet.co.uk>");
MODULE_LICENSE("GPL");

static int verbose;
module_param(verbose, int, 0644);

#define MICROCODE_VERSION	"2.00-xen"

/*
 * Synchronization.
 *
 * All non cpu-hotplug-callback call sites use:
 *
 * - microcode_mutex to synchronize with each other;
 * - get/put_online_cpus() to synchronize with
 *   the cpu-hotplug-callback call sites.
 *
 * We guarantee that only a single cpu is being
 * updated at any particular moment of time.
 */
static DEFINE_MUTEX(microcode_mutex);

#ifdef CONFIG_MICROCODE_OLD_INTERFACE
static int do_microcode_update(const void __user *ubuf, size_t len)
{
	int err;
	void *kbuf;

	kbuf = vmalloc(len);
	if (!kbuf)
		return -ENOMEM;

	if (copy_from_user(kbuf, ubuf, len) == 0) {
		struct xen_platform_op op;

		op.cmd = XENPF_microcode_update;
		set_xen_guest_handle(op.u.microcode.data, kbuf);
		op.u.microcode.length = len;
		err = HYPERVISOR_platform_op(&op);
	} else
		err = -EFAULT;

	vfree(kbuf);

	return err;
}

static int microcode_open(struct inode *unused1, struct file *unused2)
{
	cycle_kernel_lock();
	return capable(CAP_SYS_RAWIO) ? 0 : -EPERM;
}

static ssize_t microcode_write(struct file *file, const char __user *buf,
			       size_t len, loff_t *ppos)
{
	ssize_t ret = -EINVAL;

	if ((len >> PAGE_SHIFT) > totalram_pages) {
		pr_err("microcode: too much data (max %ld pages)\n", totalram_pages);
		return ret;
 	}

	mutex_lock(&microcode_mutex);

	if (do_microcode_update(buf, len) == 0)
		ret = (ssize_t)len;

	mutex_unlock(&microcode_mutex);

	return ret;
}

static const struct file_operations microcode_fops = {
	.owner			= THIS_MODULE,
	.write			= microcode_write,
	.open			= microcode_open,
};

static struct miscdevice microcode_dev = {
	.minor			= MICROCODE_MINOR,
	.name			= "microcode",
	.nodename		= "cpu/microcode",
	.fops			= &microcode_fops,
};

static int __init microcode_dev_init(void)
{
	int error;

	error = misc_register(&microcode_dev);
	if (error) {
		pr_err("microcode: can't misc_register on minor=%d\n", MICROCODE_MINOR);
		return error;
	}

	return 0;
}

static void microcode_dev_exit(void)
{
	misc_deregister(&microcode_dev);
}

MODULE_ALIAS_MISCDEV(MICROCODE_MINOR);
#else
#define microcode_dev_init()	0
#define microcode_dev_exit()	do { } while (0)
#endif

/* fake device for request_firmware */
static struct platform_device	*microcode_pdev;

static int request_microcode(const char *name)
{
	const struct firmware *firmware;
	int error;
	struct xen_platform_op op;

	error = request_firmware(&firmware, name, &microcode_pdev->dev);
	if (error) {
		pr_debug("microcode: data file %s load failed\n", name);
		return error;
	}

	op.cmd = XENPF_microcode_update;
	set_xen_guest_handle(op.u.microcode.data, firmware->data);
	op.u.microcode.length = firmware->size;
	error = HYPERVISOR_platform_op(&op);

	release_firmware(firmware);

	if (error)
		pr_debug("ucode load failed\n");

	return error;
}

static int __init microcode_init(void)
{
	const struct cpuinfo_x86 *c = &boot_cpu_data;
	char buf[32];
	const char *fw_name = buf;
	int error;

	if (c->x86_vendor == X86_VENDOR_INTEL)
		sprintf(buf, "intel-ucode/%02x-%02x-%02x",
			c->x86, c->x86_model, c->x86_mask);
	else if (c->x86_vendor == X86_VENDOR_AMD)
		fw_name = "amd-ucode/microcode_amd.bin";
	else {
		pr_err("microcode: no support for this CPU vendor\n");
		return -ENODEV;
	}

	microcode_pdev = platform_device_register_simple("microcode", -1,
							 NULL, 0);
	if (IS_ERR(microcode_pdev)) {
		return PTR_ERR(microcode_pdev);
	}

	error = microcode_dev_init();
	if (error)
		return error;

	request_microcode(fw_name);

	pr_info("Microcode Update Driver: v" MICROCODE_VERSION
	       " <tigran@aivazian.fsnet.co.uk>,"
	       " Peter Oruba\n");

	return 0;
}
module_init(microcode_init);

static void __exit microcode_exit(void)
{
	microcode_dev_exit();
	platform_device_unregister(microcode_pdev);

	pr_info("Microcode Update Driver: v" MICROCODE_VERSION " removed.\n");
}
module_exit(microcode_exit);
