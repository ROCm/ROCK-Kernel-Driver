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

//#define DEBUG /* pr_debug */
#include <linux/capability.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/cpu.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>

#include <asm/msr.h>
#include <asm/uaccess.h>
#include <asm/processor.h>

MODULE_DESCRIPTION("Intel CPU (IA-32) Microcode Update Driver");
MODULE_AUTHOR("Tigran Aivazian <tigran@aivazian.fsnet.co.uk>");
MODULE_LICENSE("GPL");

static int verbose;
module_param(verbose, int, 0644);

#define MICROCODE_VERSION 	"1.14a-xen"

#define DEFAULT_UCODE_DATASIZE 	(2000) 	  /* 2000 bytes */
#define MC_HEADER_SIZE		(sizeof (microcode_header_t))  	  /* 48 bytes */
#define DEFAULT_UCODE_TOTALSIZE (DEFAULT_UCODE_DATASIZE + MC_HEADER_SIZE) /* 2048 bytes */

/* no concurrent ->write()s are allowed on /dev/cpu/microcode */
static DEFINE_MUTEX(microcode_mutex);
				
#ifdef CONFIG_MICROCODE_OLD_INTERFACE
static int do_microcode_update (const void __user *ubuf, size_t len)
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

static int microcode_open (struct inode *unused1, struct file *unused2)
{
	cycle_kernel_lock();
	return capable(CAP_SYS_RAWIO) ? 0 : -EPERM;
}

static ssize_t microcode_write (struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
	ssize_t ret;

	if (len < MC_HEADER_SIZE) {
		printk(KERN_ERR "microcode: not enough data\n"); 
		return -EINVAL;
	}

	mutex_lock(&microcode_mutex);

	ret = do_microcode_update(buf, len);
	if (!ret)
		ret = (ssize_t)len;

	mutex_unlock(&microcode_mutex);

	return ret;
}

static const struct file_operations microcode_fops = {
	.owner		= THIS_MODULE,
	.write		= microcode_write,
	.open		= microcode_open,
};

static struct miscdevice microcode_dev = {
	.minor		= MICROCODE_MINOR,
	.name		= "microcode",
	.fops		= &microcode_fops,
};

static int __init microcode_dev_init (void)
{
	int error;

	error = misc_register(&microcode_dev);
	if (error) {
		printk(KERN_ERR
			"microcode: can't misc_register on minor=%d\n",
			MICROCODE_MINOR);
		return error;
	}

	return 0;
}

static void microcode_dev_exit (void)
{
	misc_deregister(&microcode_dev);
}

MODULE_ALIAS_MISCDEV(MICROCODE_MINOR);
#else
#define microcode_dev_init() 0
#define microcode_dev_exit() do { } while(0)
#endif

/* fake device for request_firmware */
static struct platform_device *microcode_pdev;

static int request_microcode(void)
{
	char name[30];
	const struct cpuinfo_x86 *c = &boot_cpu_data;
	const struct firmware *firmware;
	int error;
	struct xen_platform_op op;

	sprintf(name,"intel-ucode/%02x-%02x-%02x",
		c->x86, c->x86_model, c->x86_mask);
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

static int __init microcode_init (void)
{
	int error;

	printk(KERN_INFO
		"IA-32 Microcode Update Driver: v" MICROCODE_VERSION " <tigran@aivazian.fsnet.co.uk>\n");

	error = microcode_dev_init();
	if (error)
		return error;
	microcode_pdev = platform_device_register_simple("microcode", -1,
							 NULL, 0);
	if (IS_ERR(microcode_pdev)) {
		microcode_dev_exit();
		return PTR_ERR(microcode_pdev);
	}

	request_microcode();

	return 0;
}

static void __exit microcode_exit (void)
{
	microcode_dev_exit();
	platform_device_unregister(microcode_pdev);
}

module_init(microcode_init)
module_exit(microcode_exit)
