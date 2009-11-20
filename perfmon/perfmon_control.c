/*
 * perfmon_control.c: perfmon2 ioctl interface
 *
 * This file implements an ioctl interface alternative replacing the
 * following syscalls:
 *
 *	sys_pfm_create_context
 *	sys_pfm_write_pmcs
 *	sys_pfm_write_pmds
 *	sys_pfm_read_pmds
 *	sys_pfm_load_context
 *	sys_pfm_start
 *	sys_pfm_stop
 *	sys_pfm_restart
 *	sys_pfm_create_evtsets
 *	sys_pfm_getinfo_evtsets
 *	sys_pfm_delete_evtsets
 *	sys_pfm_unload_context
 *
 * For SLES11
 *
 * Tony Jones <tonyj@suse.de>
 *
 * Copyright (c) 2008 Novell Inc
 * Contributed by Tony Jones <tonyj@suse.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/perfmon_kern.h>
#include "perfmon_priv.h"
#include <linux/device.h>
#include <linux/compat.h>

/* elements arranged to ensure current padding for 32/64bit */
struct pfm_control_init {
	__u64 req;		/* struct pfarg_ctx* */
	__u64 fmt_name;		/* char* */
	__u64 fmt_arg;		/* void* */
	__u64 fmt_size;		/* size_t */
};

struct pfm_control_arglist {
	__s32 fd;		/* int */
	__s32 count;		/* int */
	__u64 req;		/* void* */
};

struct pfm_control_argptr {
	__u64 req;		/* void* */
	__s32 fd;		/* int */
	__s32 _pad;
};

struct pfm_control_fd {
	__s32 fd;		/* int */
	__s32 _pad;
};

union pfm_control {
	struct pfm_control_init init;
	struct pfm_control_arglist arglist;
	struct pfm_control_argptr argptr;
	struct pfm_control_fd fd;
};

#define _PTR(p) (compat ? compat_ptr(p) : (void*)p)

static long pfm_control_create_context(union pfm_control *cdata, int compat)
{
	struct pfm_control_init *d = &cdata->init;

	return sys_pfm_create_context((struct pfarg_ctx *)_PTR(d->req),
				      (char *)_PTR(d->fmt_name),
				      (void *)_PTR(d->fmt_arg),
				      (size_t)d->fmt_size);
}

static long pfm_control_write_pmcs(union pfm_control *cdata, int compat)
{
	struct pfm_control_arglist *d = &cdata->arglist;

	return sys_pfm_write_pmcs(d->fd,
				  (struct pfarg_pmc __user *)_PTR(d->req),
				  d->count);
}

static long pfm_control_write_pmds(union pfm_control *cdata, int compat)
{
	struct pfm_control_arglist *d = &cdata->arglist;

	return sys_pfm_write_pmds(d->fd,
				  (struct pfarg_pmd __user *)_PTR(d->req),
				  d->count);
}

static long pfm_control_read_pmds(union pfm_control *cdata, int compat)
{
	struct pfm_control_arglist *d = &cdata->arglist;

	return sys_pfm_read_pmds(d->fd,
				 (struct pfarg_pmd __user *)_PTR(d->req),
				 d->count);
}

static long pfm_control_load_context(union pfm_control *cdata, int compat)
{
	struct pfm_control_argptr *d = &cdata->argptr;

	return sys_pfm_load_context(d->fd,
				    (struct pfarg_load __user *)_PTR(d->req));
}

static long pfm_control_start(union pfm_control *cdata, int compat)
{
	struct pfm_control_argptr *d = &cdata->argptr;

	return sys_pfm_start(d->fd, (struct pfarg_start __user *)_PTR(d->req));
}

static long pfm_control_stop(union pfm_control *cdata, int compat)
{
	struct pfm_control_fd *d = &cdata->fd;

	return sys_pfm_stop(d->fd);
}

static long pfm_control_restart(union pfm_control *cdata, int compat)
{
	struct pfm_control_fd *d = &cdata->fd;

	return sys_pfm_restart(d->fd);
}

static long pfm_control_create_evtsets(union pfm_control *cdata, int compat)
{
	struct pfm_control_arglist *d = &cdata->arglist;

	return sys_pfm_create_evtsets(d->fd,
				(struct pfarg_setdesc __user *)_PTR(d->req),
				d->count);
}

static long pfm_control_getinfo_evtsets(union pfm_control *cdata, int compat)
{
	struct pfm_control_arglist *d = &cdata->arglist;

	return sys_pfm_getinfo_evtsets(d->fd,
				(struct pfarg_setinfo __user *)_PTR(d->req),
				d->count);
}

static long pfm_control_delete_evtsets(union pfm_control *cdata, int compat)
{
	struct pfm_control_arglist *d = &cdata->arglist;

	return sys_pfm_delete_evtsets(d->fd,
				(struct pfarg_setinfo __user *)_PTR(d->req),
				d->count);
}

static long pfm_control_unload_context(union pfm_control *cdata, int compat)
{
	struct pfm_control_fd *d = &cdata->fd;

	return sys_pfm_unload_context(d->fd);
}

#define PFM_CONTROL_COUNT ARRAY_SIZE(pfm_control_tab)
#define PFM_CMD(func, elem) {func, sizeof(struct elem)}

struct pfm_control_elem {
	long (*func)(union pfm_control *, int compat);
	size_t size;
};

static struct pfm_control_elem pfm_control_tab[] = {
	PFM_CMD(pfm_control_create_context,	pfm_control_init),
	PFM_CMD(pfm_control_write_pmcs,		pfm_control_arglist),
	PFM_CMD(pfm_control_write_pmds,	 	pfm_control_arglist),
	PFM_CMD(pfm_control_read_pmds,	 	pfm_control_arglist),
	PFM_CMD(pfm_control_load_context, 	pfm_control_argptr),
	PFM_CMD(pfm_control_start,		pfm_control_argptr),
	PFM_CMD(pfm_control_stop,		pfm_control_fd),
	PFM_CMD(pfm_control_restart,		pfm_control_fd),
	PFM_CMD(pfm_control_create_evtsets,	pfm_control_arglist),
	PFM_CMD(pfm_control_getinfo_evtsets,	pfm_control_arglist),
	PFM_CMD(pfm_control_delete_evtsets,	pfm_control_arglist),
	PFM_CMD(pfm_control_unload_context,	pfm_control_fd),
};

static int __pfm_control_ioctl(struct inode *inode, struct file *file,
			       unsigned int cmd, unsigned long arg,
			       int compat)
{
	union pfm_control cdata;
	int rc, op;

	if (perfmon_disabled)
		return -ENOSYS;

	op = _IOC_NR(cmd);

	if (unlikely(op < 0 || op >= PFM_CONTROL_COUNT ||
	    pfm_control_tab[op].func == NULL)) {
		PFM_ERR("Invalid control request %d", op);
		return -EINVAL;
	}

	if (_IOC_SIZE(cmd) != pfm_control_tab[op].size) {
		PFM_ERR("Invalid control request %d, size %d, expected %ld\n",
			op, _IOC_SIZE(cmd), pfm_control_tab[op].size);
		return -EINVAL;
	}

	if (_IOC_TYPE(cmd) != 0 || _IOC_DIR(cmd) != _IOC_WRITE)
                return -EINVAL;

	if (copy_from_user(&cdata, (void*)arg, pfm_control_tab[op].size) != 0)
		return -EFAULT;

	rc = pfm_control_tab[op].func(&cdata, compat);
	return rc;
}

static int pfm_control_ioctl(struct inode *inode, struct file *file,
			     unsigned int cmd, unsigned long arg)
{
	return __pfm_control_ioctl(inode, file, cmd, arg, 0);
}

static long compat_pfm_control_ioctl(struct file *file,
				    unsigned int cmd, unsigned long arg)
{
	return __pfm_control_ioctl(file->f_dentry->d_inode, file, cmd, arg, 1);
}

static const struct file_operations pfm_control_operations = {
	.owner = THIS_MODULE,
	.ioctl = pfm_control_ioctl,
	.compat_ioctl = compat_pfm_control_ioctl,
};

#ifdef USE_MISC_REGISTER
static struct miscdevice pfm_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "perfmonctl",
	.fops  = &pfm_control_operations
};
#endif

int __init pfm_init_control(void)
{
	int ret=0;
#ifndef USE_MISC_REGISTER
	static struct class *pfm_class;
	struct device *dev;
	int major;
#endif

#ifdef USE_MISC_REGISTER
	ret = misc_register(&pfm_misc_device);
	if (ret) {
		PFM_ERR("Failed to create perfmon control file. Error %d\n", ret);
	}
#else
	major = register_chrdev(0, "perfmon", &pfm_control_operations);
	if (major < 0) {
		PFM_ERR("Failed to register_chardev %d\n", major);
		return major;
	}
	pfm_class = class_create(THIS_MODULE, "perfmon");
	if (IS_ERR(pfm_class)) {
		PFM_ERR("Failed to class_create %ld\n", PTR_ERR(pfm_class));
		return -ENOENT;
	}
	dev = device_create(pfm_class, NULL, MKDEV(major,0), NULL, "perfmonctl");
	if (IS_ERR(dev)) {
		PFM_ERR("Failed to device_create %ld\n", PTR_ERR(dev));
		return -ENOENT;
	}
#endif
	return ret;
}
