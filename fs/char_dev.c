/*
 *  linux/fs/char_dev.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <linux/major.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/smp_lock.h>
#include <linux/devfs_fs_kernel.h>

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

#define MAX_PROBE_HASH 255	/* random */

static rwlock_t chrdevs_lock = RW_LOCK_UNLOCKED;

static struct char_device_struct {
	struct char_device_struct *next;
	unsigned int major;
	unsigned int baseminor;
	int minorct;
	const char *name;
	struct file_operations *fops;
} *chrdevs[MAX_PROBE_HASH];

/* index in the above */
static inline int major_to_index(int major)
{
	return major % MAX_PROBE_HASH;
}

/* get char device names in somewhat random order */
int get_chrdev_list(char *page)
{
	struct char_device_struct *cd;
	int i, len;

	len = sprintf(page, "Character devices:\n");

	read_lock(&chrdevs_lock);
	for (i = 0; i < ARRAY_SIZE(chrdevs) ; i++) {
		for (cd = chrdevs[i]; cd; cd = cd->next)
			len += sprintf(page+len, "%3d %s\n",
				       cd->major, cd->name);
	}
	read_unlock(&chrdevs_lock);

	return len;
}

/*
 * Return the function table of a device, if present.
 * Increment the reference count of module in question.
 */
static struct file_operations *
lookup_chrfops(unsigned int major, unsigned int minor)
{
	struct char_device_struct *cd;
	struct file_operations *ret = NULL;
	int i;

	i = major_to_index(major);

	read_lock(&chrdevs_lock);
	for (cd = chrdevs[i]; cd; cd = cd->next) {
		if (major == cd->major &&
		    minor - cd->baseminor < cd->minorct) {
			ret = fops_get(cd->fops);
			break;
		}
	}
	read_unlock(&chrdevs_lock);

	return ret;
}

/*
 * Return the function table of a device, if present.
 * Load the driver if needed.
 * Increment the reference count of module in question.
 */
static struct file_operations *
get_chrfops(unsigned int major, unsigned int minor)
{
	struct file_operations *ret = NULL;

	if (!major)
		return NULL;

	ret = lookup_chrfops(major, minor);

#ifdef CONFIG_KMOD
	if (!ret) {
		char name[32];
		sprintf(name, "char-major-%d", major);
		request_module(name);

		read_lock(&chrdevs_lock);
		ret = lookup_chrfops(major, minor);
		read_unlock(&chrdevs_lock);
	}
#endif
	return ret;
}

/*
 * Register a single major with a specified minor range
 */
int register_chrdev_region(unsigned int major, unsigned int baseminor,
			   int minorct, const char *name,
			   struct file_operations *fops)
{
	struct char_device_struct *cd, **cp;
	int ret = 0;
	int i;

	/* temporary */
	if (major == 0) {
		read_lock(&chrdevs_lock);
		for (i = ARRAY_SIZE(chrdevs)-1; i > 0; i--)
			if (chrdevs[i] == NULL)
				break;
		read_unlock(&chrdevs_lock);

		if (i == 0)
			return -EBUSY;
		ret = major = i;
	}

	cd = kmalloc(sizeof(struct char_device_struct), GFP_KERNEL);
	if (cd == NULL)
		return -ENOMEM;

	cd->major = major;
	cd->baseminor = baseminor;
	cd->minorct = minorct;
	cd->name = name;
	cd->fops = fops;

	i = major_to_index(major);

	write_lock(&chrdevs_lock);
	for (cp = &chrdevs[i]; *cp; cp = &(*cp)->next)
		if ((*cp)->major > major ||
		    ((*cp)->major == major && (*cp)->baseminor >= baseminor))
			break;
	if (*cp && (*cp)->major == major &&
	    (*cp)->baseminor < baseminor + minorct) {
		ret = -EBUSY;
	} else {
		cd->next = *cp;
		*cp = cd;
	}
	write_unlock(&chrdevs_lock);

	return ret;
}

int register_chrdev(unsigned int major, const char *name,
		    struct file_operations *fops)
{
	return register_chrdev_region(major, 0, 256, name, fops);
}

/* todo: make void - error printk here */
int unregister_chrdev_region(unsigned int major, unsigned int baseminor,
			     int minorct, const char *name)
{
	struct char_device_struct *cd, **cp;
	int ret = 0;
	int i;

	i = major_to_index(major);

	write_lock(&chrdevs_lock);
	for (cp = &chrdevs[i]; *cp; cp = &(*cp)->next)
		if ((*cp)->major == major &&
		    (*cp)->baseminor == baseminor &&
		    (*cp)->minorct == minorct)
			break;
	if (!*cp || strcmp((*cp)->name, name))
		ret = -EINVAL;
	else {
		cd = *cp;
		*cp = cd->next;
		kfree(cd);
	}
	write_unlock(&chrdevs_lock);

	return ret;
}

int unregister_chrdev(unsigned int major, const char *name)
{
	return unregister_chrdev_region(major, 0, 256, name);
}

/*
 * Called every time a character special file is opened
 */
int chrdev_open(struct inode * inode, struct file * filp)
{
	int ret = -ENODEV;

	filp->f_op = get_chrfops(major(inode->i_rdev), minor(inode->i_rdev));
	if (filp->f_op) {
		ret = 0;
		if (filp->f_op->open != NULL) {
			lock_kernel();
			ret = filp->f_op->open(inode,filp);
			unlock_kernel();
		}
	}
	return ret;
}

/*
 * Dummy default file-operations: the only thing this does
 * is contain the open that then fills in the correct operations
 * depending on the special file...
 */
struct file_operations def_chr_fops = {
	.open = chrdev_open,
};

const char *cdevname(kdev_t dev)
{
	static char buffer[40];
	const char *name = "unknown-char";
	unsigned int major = major(dev);
	unsigned int minor = minor(dev);
	int i = major_to_index(major);
	struct char_device_struct *cd;

	read_lock(&chrdevs_lock);
	for (cd = chrdevs[i]; cd; cd = cd->next)
		if (cd->major == major)
			break;
	if (cd)
		name = cd->name;
	sprintf(buffer, "%s(%d,%d)", name, major, minor);
	read_unlock(&chrdevs_lock);

	return buffer;
}
