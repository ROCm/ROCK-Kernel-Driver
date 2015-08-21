/*
 * CPU Microcode Update Driver for Linux on Xen
 *
 * Copyright (C) 2000-2006 Tigran Aivazian <tigran@aivazian.fsnet.co.uk>
 *	      2006	Shaohua Li <shaohua.li@intel.com>
 *	      2013-2015	Borislav Petkov <bp@alien8.de>
 *
 * This driver allows to upgrade microcode on x86 processors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/capability.h>
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
#include <asm/cpu_device_id.h>

#include <xen/pcpu.h>

MODULE_DESCRIPTION("Microcode Update Driver");
MODULE_AUTHOR("Tigran Aivazian <tigran@aivazian.fsnet.co.uk>");
MODULE_LICENSE("GPL");

static int verbose;
module_param(verbose, int, 0644);

#define MICROCODE_VERSION	"2.00-xen"

bool dis_ucode_ldr;
module_param(dis_ucode_ldr, bool, 0);

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

static int microcode_open(struct inode *inode, struct file *file)
{
	return capable(CAP_SYS_RAWIO) ? nonseekable_open(inode, file) : -EPERM;
}

static ssize_t microcode_write(struct file *file, const char __user *buf,
			       size_t len, loff_t *ppos)
{
	ssize_t ret = -EINVAL;

	if ((len >> PAGE_SHIFT) > totalram_pages) {
		pr_err("too much data (max %ld pages)\n", totalram_pages);
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
	.llseek		= no_llseek,
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

	if (!is_initial_xendomain())
		return -ENODEV;

	error = misc_register(&microcode_dev);
	if (error) {
		pr_err("can't misc_register on minor=%d\n", MICROCODE_MINOR);
		return error;
	}

	return 0;
}

static void __exit microcode_dev_exit(void)
{
	misc_deregister(&microcode_dev);
}

MODULE_ALIAS_MISCDEV(MICROCODE_MINOR);
MODULE_ALIAS("devname:cpu/microcode");
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

static const char amd_uc_name[] = "amd-ucode/microcode_amd.bin";
static const char amd_uc_fmt[] = "amd-ucode/microcode_amd_fam%x.bin";
static const char intel_uc_fmt[] = "intel-ucode/%02x-%02x-%02x";

static int ucode_cpu_callback(struct notifier_block *nfb,
			      unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct xen_platform_op op;
	char buf[36];
	const char *uc_name = buf;

	switch (action) {
	case CPU_ONLINE:
		op.cmd = XENPF_get_cpu_version;
		op.u.pcpu_version.xen_cpuid = cpu;
		if (HYPERVISOR_platform_op(&op))
			break;
		if (op.u.pcpu_version.family == boot_cpu_data.x86
		    && op.u.pcpu_version.model == boot_cpu_data.x86_model
		    && op.u.pcpu_version.stepping == boot_cpu_data.x86_mask)
			break;
		if (strncmp(op.u.pcpu_version.vendor_id,
			    "GenuineIntel", 12) == 0)
			snprintf(buf, sizeof(buf), intel_uc_fmt,
				 op.u.pcpu_version.family,
				 op.u.pcpu_version.model,
				 op.u.pcpu_version.stepping);
		else if (strncmp(op.u.pcpu_version.vendor_id,
				 "AuthenicAMD", 12) == 0) {
			if (op.u.pcpu_version.family >= 0x15)
				snprintf(buf, sizeof(buf), amd_uc_fmt,
					 op.u.pcpu_version.family);
			else
				uc_name = amd_uc_name;
		} else
			break;
		request_microcode(uc_name);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block ucode_cpu_notifier = {
	.notifier_call = ucode_cpu_callback
};

#ifdef MODULE
/* Autoload on Intel and AMD systems */
static const struct x86_cpu_id __initconst microcode_id[] = {
#ifdef CONFIG_MICROCODE_INTEL
	{ X86_VENDOR_INTEL, X86_FAMILY_ANY, X86_MODEL_ANY, },
#endif
#ifdef CONFIG_MICROCODE_AMD
	{ X86_VENDOR_AMD, X86_FAMILY_ANY, X86_MODEL_ANY, },
#endif
	{}
};
MODULE_DEVICE_TABLE(x86cpu, microcode_id);
#endif

static int __init microcode_init(void)
{
	const struct cpuinfo_x86 *c = &boot_cpu_data;
	char buf[36];
	const char *fw_name = buf;
	int error;

	if (dis_ucode_ldr)
		return -EINVAL;

	if (c->x86_vendor == X86_VENDOR_INTEL)
		snprintf(buf, sizeof(buf), intel_uc_fmt,
			 c->x86, c->x86_model, c->x86_mask);
	else if (c->x86_vendor == X86_VENDOR_AMD) {
		if (c->x86 >= 0x15)
			snprintf(buf, sizeof(buf), amd_uc_fmt, c->x86);
		else
			fw_name = amd_uc_name;
	} else {
		pr_err("no support for this CPU vendor\n");
		return -ENODEV;
	}

	microcode_pdev = platform_device_register_simple("microcode", -1,
							 NULL, 0);
	if (IS_ERR(microcode_pdev))
		return PTR_ERR(microcode_pdev);

	request_microcode(fw_name);

	error = microcode_dev_init();
	if (error) {
		platform_device_unregister(microcode_pdev);
		return error;
	}

	pr_info("Microcode Update Driver: v" MICROCODE_VERSION
		" <tigran@aivazian.fsnet.co.uk>, Peter Oruba\n");

	error = register_pcpu_notifier(&ucode_cpu_notifier);
	if (error)
		pr_warn("pCPU notifier registration failed (%d)\n", error);

	return 0;
}
module_init(microcode_init);

static void __exit microcode_exit(void)
{
	unregister_pcpu_notifier(&ucode_cpu_notifier);
	microcode_dev_exit();
	platform_device_unregister(microcode_pdev);

	pr_info("Microcode Update Driver: v" MICROCODE_VERSION " removed.\n");
}
module_exit(microcode_exit);
