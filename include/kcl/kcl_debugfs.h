// SPDX-License-Identifier: GPL-2.0
/*
 *  debugfs.h - a tiny little debug file system
 *
 *  Copyright (C) 2004 Greg Kroah-Hartman <greg@kroah.com>
 *  Copyright (C) 2004 IBM Inc.
 *
 *  debugfs is for people to use instead of /proc or /sys.
 *  See Documentation/filesystems/ for more details.
 */

#ifndef KCL_DEBUGFS_H_
#define KCL_DEBUGFS_H_

#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>

#include <linux/types.h>
#include <linux/compiler.h>

#if defined(DEFINE_DEBUGFS_ATTRIBUTE) && !defined(DEFINE_DEBUGFS_ATTRIBUTE_SIGNED)
#define KCL_FAKE_DEBUGFS_ATTRIBUTE_SIGNED
#define DEFINE_DEBUGFS_ATTRIBUTE_XSIGNED(__fops, __get, __set, __fmt, __is_signed)	\
static int __fops ## _open(struct inode *inode, struct file *file)	\
{									\
	__simple_attr_check_format(__fmt, 0ull);			\
	return simple_attr_open(inode, file, __get, __set, __fmt);	\
}									\
static const struct file_operations __fops = {				\
	.owner	 = THIS_MODULE,						\
	.open	 = __fops ## _open,					\
	.release = simple_attr_release,					\
	.read	 = debugfs_attr_read,					\
	.write	 = (__is_signed) ? debugfs_attr_write_signed : debugfs_attr_write,	\
	.llseek  = no_llseek,						\
}

#define DEFINE_DEBUGFS_ATTRIBUTE_SIGNED(__fops, __get, __set, __fmt)	\
	DEFINE_DEBUGFS_ATTRIBUTE_XSIGNED(__fops, __get, __set, __fmt, true)

#if defined(CONFIG_DEBUG_FS)
ssize_t debugfs_attr_write_signed(struct file *file, const char __user *buf,
			size_t len, loff_t *ppos);
#else
static inline ssize_t debugfs_attr_write_signed(struct file *file,
					const char __user *buf,
					size_t len, loff_t *ppos)
{
	return -ENODEV;
}
#endif /* CONFIG_DEBUG_FS */

#endif /* DEFINE_DEBUGFS_ATTRIBUTE_SIGNED */

#endif
