// SPDX-License-Identifier: GPL-2.0
/*
 *  inode.c - part of debugfs, a tiny little debug file system
 *
 *  Copyright (C) 2004,2019 Greg Kroah-Hartman <greg@kroah.com>
 *  Copyright (C) 2004 IBM Inc.
 *  Copyright (C) 2019 Linux Foundation <gregkh@linuxfoundation.org>
 *
 *  debugfs is for people to use instead of /proc or /sys.
 *  See ./Documentation/core-api/kernel-api.rst for more details.
 */

#include <linux/uaccess.h>
#include <linux/fs.h>
#include <kcl/kcl_debugfs.h>

#ifdef KCL_FAKE_DEBUGFS_ATTRIBUTE_SIGNED
/* Copied from fs/libfs.c */
struct simple_attr {
	int (*get)(void *, u64 *);
	int (*set)(void *, u64);
	char get_buf[24];	/* enough to store a u64 and "\n\0" */
	char set_buf[24];
	void *data;
	const char *fmt;	/* format for read operation */
	struct mutex mutex;	/* protects access to these buffers */
};

static ssize_t simple_attr_write_xsigned(struct file *file, const char __user *buf,
			  size_t len, loff_t *ppos, bool is_signed)
{
	struct simple_attr *attr;
	unsigned long long val;
	size_t size;
	ssize_t ret;

	attr = file->private_data;
	if (!attr->set)
		return -EACCES;

	ret = mutex_lock_interruptible(&attr->mutex);
	if (ret)
		return ret;

	ret = -EFAULT;
	size = min(sizeof(attr->set_buf) - 1, len);
	if (copy_from_user(attr->set_buf, buf, size))
		goto out;

	attr->set_buf[size] = '\0';
	if (is_signed)
		ret = kstrtoll(attr->set_buf, 0, &val);
	else
		ret = kstrtoull(attr->set_buf, 0, &val);
	if (ret)
		goto out;
	ret = attr->set(attr->data, val);
	if (ret == 0)
		ret = len; /* on success, claim we got the whole input */
out:
	mutex_unlock(&attr->mutex);
	return ret;
}

ssize_t simple_attr_write_signed(struct file *file, const char __user *buf,
			  size_t len, loff_t *ppos)
{
	return simple_attr_write_xsigned(file, buf, len, ppos, true);
}
EXPORT_SYMBOL_GPL(simple_attr_write_signed);

/* Copied from fs/debugfs/file.c */
#define F_DENTRY(filp) ((filp)->f_path.dentry)
static ssize_t debugfs_attr_write_xsigned(struct file *file, const char __user *buf,
			 size_t len, loff_t *ppos, bool is_signed)
{
	struct dentry *dentry = F_DENTRY(file);
	ssize_t ret;

	ret = debugfs_file_get(dentry);
	if (unlikely(ret))
		return ret;
	if (is_signed)
		ret = simple_attr_write_signed(file, buf, len, ppos);
	else
		ret = simple_attr_write(file, buf, len, ppos);
	debugfs_file_put(dentry);
	return ret;
}

ssize_t debugfs_attr_write_signed(struct file *file, const char __user *buf,
			 size_t len, loff_t *ppos)
{
	return debugfs_attr_write_xsigned(file, buf, len, ppos, true);
}
EXPORT_SYMBOL_GPL(debugfs_attr_write_signed);
#endif
