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
		request_module("char-major-%d", major);

		read_lock(&chrdevs_lock);
		ret = lookup_chrfops(major, minor);
		read_unlock(&chrdevs_lock);
	}
#endif
	return ret;
}

/*
 * Register a single major with a specified minor range.
 *
 * If major == 0 this functions will dynamically allocate a major and return
 * its number.
 *
 * If major > 0 this function will attempt to reserve the passed range of
 * minors and will return zero on success.
 *
 * Returns a -ve errno on failure.
 */
static struct char_device_struct *
__register_chrdev_region(unsigned int major, unsigned int baseminor,
			   int minorct, const char *name,
			   struct file_operations *fops)
{
	struct char_device_struct *cd, **cp;
	int ret = 0;
	int i;

	cd = kmalloc(sizeof(struct char_device_struct), GFP_KERNEL);
	if (cd == NULL)
		return ERR_PTR(-ENOMEM);

	write_lock_irq(&chrdevs_lock);

	/* temporary */
	if (major == 0) {
		for (i = ARRAY_SIZE(chrdevs)-1; i > 0; i--) {
			if (chrdevs[i] == NULL)
				break;
		}

		if (i == 0) {
			ret = -EBUSY;
			goto out;
		}
		major = i;
		ret = major;
	}

	cd->major = major;
	cd->baseminor = baseminor;
	cd->minorct = minorct;
	cd->name = name;
	cd->fops = fops;

	i = major_to_index(major);

	for (cp = &chrdevs[i]; *cp; cp = &(*cp)->next)
		if ((*cp)->major > major ||
		    ((*cp)->major == major && (*cp)->baseminor >= baseminor))
			break;
	if (*cp && (*cp)->major == major &&
	    (*cp)->baseminor < baseminor + minorct) {
		ret = -EBUSY;
		goto out;
	}
	cd->next = *cp;
	*cp = cd;
	write_unlock_irq(&chrdevs_lock);
	return cd;
out:
	write_unlock_irq(&chrdevs_lock);
	kfree(cd);
	return ERR_PTR(ret);
}

static struct char_device_struct *
__unregister_chrdev_region(unsigned major, unsigned baseminor, int minorct)
{
	struct char_device_struct *cd = NULL, **cp;
	int i = major_to_index(major);

	write_lock_irq(&chrdevs_lock);
	for (cp = &chrdevs[i]; *cp; cp = &(*cp)->next)
		if ((*cp)->major == major &&
		    (*cp)->baseminor == baseminor &&
		    (*cp)->minorct == minorct)
			break;
	if (*cp) {
		cd = *cp;
		*cp = cd->next;
	}
	write_unlock_irq(&chrdevs_lock);
	return cd;
}

int register_chrdev_region(dev_t from, unsigned count, char *name,
			   struct file_operations *fops)
{
	struct char_device_struct *cd;
	dev_t to = from + count;
	dev_t n, next;

	for (n = from; n < to; n = next) {
		next = MKDEV(MAJOR(n)+1, 0);
		if (next > to)
			next = to;
		cd = __register_chrdev_region(MAJOR(n), MINOR(n),
			       next - n, name, fops);
		if (IS_ERR(cd))
			goto fail;
	}
	return 0;
fail:
	to = n;
	for (n = from; n < to; n = next) {
		next = MKDEV(MAJOR(n)+1, 0);
		kfree(__unregister_chrdev_region(MAJOR(n), MINOR(n), next - n));
	}
	return PTR_ERR(cd);
}

int alloc_chrdev_region(dev_t *dev, unsigned count, char *name,
			   struct file_operations *fops)
{
	struct char_device_struct *cd;
	cd = __register_chrdev_region(0, 0, count, name, fops);
	if (IS_ERR(cd))
		return PTR_ERR(cd);
	*dev = MKDEV(cd->major, cd->baseminor);
	return 0;
}

int register_chrdev(unsigned int major, const char *name,
		    struct file_operations *fops)
{
	struct char_device_struct *cd;

	cd = __register_chrdev_region(major, 0, 256, name, fops);
	if (IS_ERR(cd))
		return PTR_ERR(cd);
	return cd->major;
}

void unregister_chrdev_region(dev_t from, unsigned count)
{
	dev_t to = from + count;
	dev_t n, next;

	for (n = from; n < to; n = next) {
		next = MKDEV(MAJOR(n)+1, 0);
		if (next > to)
			next = to;
		kfree(__unregister_chrdev_region(MAJOR(n), MINOR(n), next - n));
	}
}

int unregister_chrdev(unsigned int major, const char *name)
{
	kfree(__unregister_chrdev_region(major, 0, 256));
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
