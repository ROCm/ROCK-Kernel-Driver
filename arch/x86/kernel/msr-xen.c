#ifndef CONFIG_XEN_PRIVILEGED_GUEST
#include "msr.c"
#else
/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2010 Novell, Inc.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * x86 MSR access device
 *
 * This device is accessed by lseek() to the appropriate register number
 * and then read/write in chunks of 8 bytes.  A larger size means multiple
 * reads or writes of the same register.
 *
 * This driver uses /dev/xen/cpu/%d/msr where %d correlates to the minor
 * number, and on an SMP box will direct the access to pCPU %d.
 */

static int msr_init(void);
static void msr_exit(void);

#define msr_init(args...) _msr_init(args)
#define msr_exit(args...) _msr_exit(args)
#include "msr.c"
#undef msr_exit
#undef msr_init

#include <linux/slab.h>
#include <xen/pcpu.h>

static struct class *pmsr_class;
static unsigned int minor_bias = 10;
static unsigned int nr_xen_cpu_ids;
static unsigned long *xen_cpu_online_map;

#define PMSR_DEV(cpu) MKDEV(MSR_MAJOR, (cpu) + minor_bias)

static unsigned int pmsr_minor(struct inode *inode)
{
	return iminor(inode) - minor_bias;
}

static ssize_t pmsr_read(struct file *file, char __user *buf,
			 size_t count, loff_t *ppos)
{
	u32 __user *tmp = (u32 __user *) buf;
	u32 data[2];
	u32 reg = *ppos;
	unsigned int cpu = pmsr_minor(file->f_path.dentry->d_inode);
	int err = 0;
	ssize_t bytes = 0;

	if (count % 8)
		return -EINVAL;	/* Invalid chunk size */

	for (; count; count -= 8) {
		err = rdmsr_safe_on_pcpu(cpu, reg, &data[0], &data[1]);
		if (err)
			break;
		if (copy_to_user(tmp, &data, 8)) {
			err = -EFAULT;
			break;
		}
		tmp += 2;
		bytes += 8;
	}

	return bytes ? bytes : err;
}

static ssize_t pmsr_write(struct file *file, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	const u32 __user *tmp = (const u32 __user *)buf;
	u32 data[2];
	u32 reg = *ppos;
	unsigned int cpu = pmsr_minor(file->f_path.dentry->d_inode);
	int err = 0;
	ssize_t bytes = 0;

	if (count % 8)
		return -EINVAL;	/* Invalid chunk size */

	for (; count; count -= 8) {
		if (copy_from_user(&data, tmp, 8)) {
			err = -EFAULT;
			break;
		}
		err = wrmsr_safe_on_pcpu(cpu, reg, data[0], data[1]);
		if (err)
			break;
		tmp += 2;
		bytes += 8;
	}

	return bytes ? bytes : err;
}

static long pmsr_ioctl(struct file *file, unsigned int ioc, unsigned long arg)
{
	u32 __user *uregs = (u32 __user *)arg;
	u32 regs[8];
	unsigned int cpu = pmsr_minor(file->f_path.dentry->d_inode);
	int err;

	switch (ioc) {
	case X86_IOC_RDMSR_REGS:
		if (!(file->f_mode & FMODE_READ)) {
			err = -EBADF;
			break;
		}
		if (copy_from_user(&regs, uregs, sizeof regs)) {
			err = -EFAULT;
			break;
		}
		err = rdmsr_safe_regs_on_pcpu(cpu, regs);
		if (err)
			break;
		if (copy_to_user(uregs, &regs, sizeof regs))
			err = -EFAULT;
		break;

	case X86_IOC_WRMSR_REGS:
		if (!(file->f_mode & FMODE_WRITE)) {
			err = -EBADF;
			break;
		}
		if (copy_from_user(&regs, uregs, sizeof regs)) {
			err = -EFAULT;
			break;
		}
		err = wrmsr_safe_regs_on_pcpu(cpu, regs);
		if (err)
			break;
		if (copy_to_user(uregs, &regs, sizeof regs))
			err = -EFAULT;
		break;

	default:
		err = -ENOTTY;
		break;
	}

	return err;
}

static int pmsr_open(struct inode *inode, struct file *file)
{
	unsigned int cpu;

	cpu = pmsr_minor(file->f_path.dentry->d_inode);
	if (cpu >= nr_xen_cpu_ids || !test_bit(cpu, xen_cpu_online_map))
		return -ENXIO;	/* No such CPU */

	return 0;
}

/*
 * File operations we support
 */
static const struct file_operations pmsr_fops = {
	.owner = THIS_MODULE,
	.llseek = msr_seek,
	.read = pmsr_read,
	.write = pmsr_write,
	.open = pmsr_open,
	.unlocked_ioctl = pmsr_ioctl,
	.compat_ioctl = pmsr_ioctl,
};

static int pmsr_device_create(unsigned int cpu)
{
	struct device *dev;

	if (cpu >= nr_xen_cpu_ids) {
		static bool warned;
		unsigned long *map;

		if ((minor_bias + cpu) >> MINORBITS) {
			if (!warned) {
				warned = true;
				pr_warn("Physical MSRs of CPUs beyond %u"
					" will not be accessible\n",
					MINORMASK - minor_bias);
			}
			return -EDOM;
		}

		map = kzalloc(BITS_TO_LONGS(cpu + 1) * sizeof(*map),
			      GFP_KERNEL);
		if (!map) {
			if (!warned) {
				warned = true;
				pr_warn("Physical MSRs of CPUs beyond %u"
					" may not be accessible\n",
					nr_xen_cpu_ids - 1);
			}
			return -ENOMEM;
		}

		memcpy(map, xen_cpu_online_map,
		       BITS_TO_LONGS(nr_xen_cpu_ids)
		       * sizeof(*xen_cpu_online_map));
		nr_xen_cpu_ids = min_t(unsigned int,
				     BITS_TO_LONGS(cpu + 1) * BITS_PER_LONG,
				     MINORMASK + 1 - minor_bias);
		kfree(xchg(&xen_cpu_online_map, map));
	}
	set_bit(cpu, xen_cpu_online_map);
	dev = device_create(pmsr_class, NULL, PMSR_DEV(cpu), NULL,
			    "pmsr%d", cpu);
	return IS_ERR(dev) ? PTR_ERR(dev) : 0;
}

static void pmsr_device_destroy(unsigned int cpu)
{
	clear_bit(cpu, xen_cpu_online_map);
	device_destroy(pmsr_class, PMSR_DEV(cpu));
}

static int pmsr_cpu_callback(struct notifier_block *nfb,
			     unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	switch (action) {
	case CPU_ONLINE:
		pmsr_device_create(cpu);
		break;
	case CPU_DEAD:
		pmsr_device_destroy(cpu);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block pmsr_cpu_notifier = {
	.notifier_call = pmsr_cpu_callback,
};

static char *pmsr_devnode(struct device *dev, mode_t *mode)
{
	return kasprintf(GFP_KERNEL, "xen/cpu/%u/msr",
			 MINOR(dev->devt) - minor_bias);
}

static int __init msr_init(void)
{
	int err;
	xen_platform_op_t op;

	err = _msr_init();
	if (err || !is_initial_xendomain())
		return err;

	op.cmd = XENPF_get_cpuinfo;
	op.u.pcpu_info.xen_cpuid = 0;
	do {
		err = HYPERVISOR_platform_op(&op);
	} while (err == -EBUSY);
	if (err)
		goto out;
	nr_xen_cpu_ids = BITS_TO_LONGS(op.u.pcpu_info.max_present + 1)
			 * BITS_PER_LONG;

	while (minor_bias < NR_CPUS)
		minor_bias *= 10;
	if ((minor_bias + nr_xen_cpu_ids - 1) >> MINORBITS)
		minor_bias = NR_CPUS;
	if ((minor_bias + nr_xen_cpu_ids - 1) >> MINORBITS)
		nr_xen_cpu_ids = MINORMASK + 1 - NR_CPUS;

	xen_cpu_online_map = kzalloc(BITS_TO_LONGS(nr_xen_cpu_ids)
				     * sizeof(*xen_cpu_online_map),
				     GFP_KERNEL);
	if (!xen_cpu_online_map) {
		err = -ENOMEM;
		goto out;
	}

	if (__register_chrdev(MSR_MAJOR, minor_bias,
			      MINORMASK + 1 - minor_bias,
			      "pcpu/msr", &pmsr_fops)) {
		pr_err("msr: unable to get minors for pmsr\n");
		goto out;
	}
	pmsr_class = class_create(THIS_MODULE, "pmsr");
	if (IS_ERR(pmsr_class)) {
		err = PTR_ERR(pmsr_class);
		goto out_chrdev;
	}
	pmsr_class->devnode = pmsr_devnode;
	err = register_pcpu_notifier(&pmsr_cpu_notifier);

	if (!err && !nr_xen_cpu_ids)
		err = -ENODEV;
	if (!err)
		return 0;

	class_destroy(pmsr_class);

out_chrdev:
	__unregister_chrdev(MSR_MAJOR, minor_bias,
			    MINORMASK + 1 - minor_bias, "pcpu/msr");
out:
	if (err)
		pr_warn("msr: can't initialize physical MSR access (%d)\n",
			err);
	nr_xen_cpu_ids = 0;
	kfree(xen_cpu_online_map);
	return 0;
}

static void __exit msr_exit(void)
{
	if (nr_xen_cpu_ids) {
		unsigned int cpu = 0;

		unregister_pcpu_notifier(&pmsr_cpu_notifier);
		for_each_set_bit(cpu, xen_cpu_online_map, nr_xen_cpu_ids)
			pmsr_device_destroy(cpu);
		class_destroy(pmsr_class);
		__unregister_chrdev(MSR_MAJOR, minor_bias,
				    MINORMASK + 1 - minor_bias, "pcpu/msr");
		kfree(xen_cpu_online_map);
	}
	_msr_exit();
}
#endif /* CONFIG_XEN_PRIVILEGED_GUEST */
