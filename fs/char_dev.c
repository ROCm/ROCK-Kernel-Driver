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
#include <linux/tty.h>

/* serial module kmod load support */
struct tty_driver *get_tty_driver(kdev_t device);
#define is_a_tty_dev(ma)	(ma == TTY_MAJOR || ma == TTYAUX_MAJOR)
#define need_serial(ma,mi) (get_tty_driver(mk_kdev(ma,mi)) == NULL)
#endif

struct device_struct {
	const char * name;
	struct file_operations * fops;
};

static rwlock_t chrdevs_lock = RW_LOCK_UNLOCKED;
static struct device_struct chrdevs[MAX_CHRDEV];

int get_chrdev_list(char *page)
{
	int i;
	int len;

	len = sprintf(page, "Character devices:\n");
	read_lock(&chrdevs_lock);
	for (i = 0; i < MAX_CHRDEV ; i++) {
		if (chrdevs[i].fops) {
			len += sprintf(page+len, "%3d %s\n",
				       i, chrdevs[i].name);
		}
	}
	read_unlock(&chrdevs_lock);
	return len;
}

/*
 *	Return the function table of a device.
 *	Load the driver if needed.
 *	Increment the reference count of module in question.
 */
static struct file_operations *
get_chrfops(unsigned int major, unsigned int minor)
{
	struct file_operations *ret;

	if (!major || major >= MAX_CHRDEV)
		return NULL;

	read_lock(&chrdevs_lock);
	ret = fops_get(chrdevs[major].fops);
	read_unlock(&chrdevs_lock);
#ifdef CONFIG_KMOD
	if (ret && is_a_tty_dev(major)) {
		lock_kernel();
		if (need_serial(major,minor)) {
			/* Force request_module anyway, but what for? */
			/* The reason is that we may have a driver for
			   /dev/tty1 already, but need one for /dev/ttyS1. */
			fops_put(ret);
			ret = NULL;
		}
		unlock_kernel();
	}
	if (!ret) {
		char name[20];
		sprintf(name, "char-major-%d", major);
		request_module(name);

		read_lock(&chrdevs_lock);
		ret = fops_get(chrdevs[major].fops);
		read_unlock(&chrdevs_lock);
	}
#endif
	return ret;
}

int register_chrdev(unsigned int major, const char *name,
		    struct file_operations *fops)
{
	if (major == 0) {
		write_lock(&chrdevs_lock);
		for (major = MAX_CHRDEV-1; major > 0; major--) {
			if (chrdevs[major].fops == NULL) {
				chrdevs[major].name = name;
				chrdevs[major].fops = fops;
				write_unlock(&chrdevs_lock);
				return major;
			}
		}
		write_unlock(&chrdevs_lock);
		return -EBUSY;
	}
	if (major >= MAX_CHRDEV)
		return -EINVAL;
	write_lock(&chrdevs_lock);
	if (chrdevs[major].fops && chrdevs[major].fops != fops) {
		write_unlock(&chrdevs_lock);
		return -EBUSY;
	}
	chrdevs[major].name = name;
	chrdevs[major].fops = fops;
	write_unlock(&chrdevs_lock);
	return 0;
}

int unregister_chrdev(unsigned int major, const char * name)
{
	if (major >= MAX_CHRDEV)
		return -EINVAL;
	write_lock(&chrdevs_lock);
	if (!chrdevs[major].fops || strcmp(chrdevs[major].name, name)) {
		write_unlock(&chrdevs_lock);
		return -EINVAL;
	}
	chrdevs[major].name = NULL;
	chrdevs[major].fops = NULL;
	write_unlock(&chrdevs_lock);
	return 0;
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

const char * cdevname(kdev_t dev)
{
	static char buffer[32];
	const char * name = chrdevs[major(dev)].name;

	if (!name)
		name = "unknown-char";
	sprintf(buffer, "%s(%d,%d)", name, major(dev), minor(dev));
	return buffer;
}
