/*
 *  linux/fs/block_dev.c
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
#define isa_tty_dev(ma)	(ma == TTY_MAJOR || ma == TTYAUX_MAJOR)
#define need_serial(ma,mi) (get_tty_driver(mk_kdev(ma,mi)) == NULL)
#endif

#define HASH_BITS	6
#define HASH_SIZE	(1UL << HASH_BITS)
#define HASH_MASK	(HASH_SIZE-1)
static struct list_head cdev_hashtable[HASH_SIZE];
static spinlock_t cdev_lock = SPIN_LOCK_UNLOCKED;
static kmem_cache_t * cdev_cachep;

#define alloc_cdev() \
	 ((struct char_device *) kmem_cache_alloc(cdev_cachep, SLAB_KERNEL))
#define destroy_cdev(cdev) kmem_cache_free(cdev_cachep, (cdev))

static void init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct char_device * cdev = (struct char_device *) foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR)
	{
		memset(cdev, 0, sizeof(*cdev));
		sema_init(&cdev->sem, 1);
	}
}

void __init cdev_cache_init(void)
{
	int i;
	struct list_head *head = cdev_hashtable;

	i = HASH_SIZE;
	do {
		INIT_LIST_HEAD(head);
		head++;
		i--;
	} while (i);

	cdev_cachep = kmem_cache_create("cdev_cache",
					 sizeof(struct char_device),
					 0, SLAB_HWCACHE_ALIGN, init_once,
					 NULL);
	if (!cdev_cachep)
		panic("Cannot create cdev_cache SLAB cache");
}

/*
 * Most likely _very_ bad one - but then it's hardly critical for small
 * /dev and can be fixed when somebody will need really large one.
 */
static inline unsigned long hash(dev_t dev)
{
	unsigned long tmp = dev;
	tmp = tmp + (tmp >> HASH_BITS) + (tmp >> HASH_BITS*2);
	return tmp & HASH_MASK;
}

static struct char_device *cdfind(dev_t dev, struct list_head *head)
{
	struct list_head *p;
	struct char_device *cdev;
	list_for_each(p, head) {
		cdev = list_entry(p, struct char_device, hash);
		if (cdev->dev != dev)
			continue;
		atomic_inc(&cdev->count);
		return cdev;
	}
	return NULL;
}

struct char_device *cdget(dev_t dev)
{
	struct list_head * head = cdev_hashtable + hash(dev);
	struct char_device *cdev, *new_cdev;
	spin_lock(&cdev_lock);
	cdev = cdfind(dev, head);
	spin_unlock(&cdev_lock);
	if (cdev)
		return cdev;
	new_cdev = alloc_cdev();
	if (!new_cdev)
		return NULL;
	atomic_set(&new_cdev->count,1);
	new_cdev->dev = dev;
	spin_lock(&cdev_lock);
	cdev = cdfind(dev, head);
	if (!cdev) {
		list_add(&new_cdev->hash, head);
		spin_unlock(&cdev_lock);
		return new_cdev;
	}
	spin_unlock(&cdev_lock);
	destroy_cdev(new_cdev);
	return cdev;
}

void cdput(struct char_device *cdev)
{
	if (atomic_dec_and_lock(&cdev->count, &cdev_lock)) {
		list_del(&cdev->hash);
		spin_unlock(&cdev_lock);
		destroy_cdev(cdev);
	}
}

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
			len += sprintf(page+len, "%3d %s\n", i, chrdevs[i].name);
		}
	}
	read_unlock(&chrdevs_lock);
	return len;
}

/*
	Return the function table of a device.
	Load the driver if needed.
	Increment the reference count of module in question.
*/
static struct file_operations * get_chrfops(unsigned int major, unsigned int minor)
{
	struct file_operations *ret = NULL;

	if (!major || major >= MAX_CHRDEV)
		return NULL;

	read_lock(&chrdevs_lock);
	ret = fops_get(chrdevs[major].fops);
	read_unlock(&chrdevs_lock);
#ifdef CONFIG_KMOD
	if (ret && isa_tty_dev(major)) {
		lock_kernel();
		if (need_serial(major,minor)) {
			/* Force request_module anyway, but what for? */
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

int register_chrdev(unsigned int major, const char * name, struct file_operations *fops)
{
	if (devfs_only())
		return 0;
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
	if (devfs_only())
		return 0;
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
