#ifndef AMDKCL_DEBUGFS_H
#define AMDKCL_DEBUGFS_H

#include <linux/fs.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
#define DEFINE_DEBUGFS_ATTRIBUTE(__fops, __get, __set, __fmt)		\
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
	.write	 = debugfs_attr_write,					\
	.llseek  = generic_file_llseek,					\
}
static struct dentry *debugfs_create_file_unsafe(const char *name, umode_t mode,
					struct dentry *parent, void *data,
					const struct file_operations *fops);
static ssize_t debugfs_attr_read(struct file *file, char __user *buf,
					size_t len, loff_t *ppos);
static ssize_t debugfs_attr_write(struct file *file, const char __user *buf,
					size_t len, loff_t *ppos);

static inline ssize_t debugfs_attr_read(struct file *file, char __user *buf,
					size_t len, loff_t *ppos)
{
	return -ENODEV;
}

static inline ssize_t debugfs_attr_write(struct file *file,
					const char __user *buf,
					size_t len, loff_t *ppos)
{
	return -ENODEV;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
static inline struct dentry *debugfs_create_file_unsafe(const char *name,
					umode_t mode, struct dentry *parent,
					void *data,
					const struct file_operations *fops)
{
	return ERR_PTR(-ENODEV);
}
#endif

#endif
