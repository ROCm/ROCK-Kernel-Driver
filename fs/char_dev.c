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

#include <linux/kobject.h>
#include <linux/kobj_map.h>
#include <linux/cdev.h>

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

static struct kobj_map *cdev_map;

#define MAX_PROBE_HASH 255	/* random */

static rwlock_t chrdevs_lock = RW_LOCK_UNLOCKED;

static struct char_device_struct {
	struct char_device_struct *next;
	unsigned int major;
	unsigned int baseminor;
	int minorct;
	const char *name;
	struct file_operations *fops;
	struct cdev *cdev;		/* will die */
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
 * Load the driver if needed.
 * Increment the reference count of module in question.
 */
static struct file_operations *get_chrfops(dev_t dev)
{
	struct file_operations *ret = NULL;
	int index;
	struct kobject *kobj = kobj_lookup(cdev_map, dev, &index);

	if (kobj) {
		struct cdev *p = container_of(kobj, struct cdev, kobj);
		struct module *owner = p->owner;
		ret = fops_get(p->ops);
		cdev_put(p);
		module_put(owner);
	}
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
			   int minorct, const char *name)
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

int register_chrdev_region(dev_t from, unsigned count, char *name)
{
	struct char_device_struct *cd;
	dev_t to = from + count;
	dev_t n, next;

	for (n = from; n < to; n = next) {
		next = MKDEV(MAJOR(n)+1, 0);
		if (next > to)
			next = to;
		cd = __register_chrdev_region(MAJOR(n), MINOR(n),
			       next - n, name);
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

int alloc_chrdev_region(dev_t *dev, unsigned count, char *name)
{
	struct char_device_struct *cd;
	cd = __register_chrdev_region(0, 0, count, name);
	if (IS_ERR(cd))
		return PTR_ERR(cd);
	*dev = MKDEV(cd->major, cd->baseminor);
	return 0;
}

int register_chrdev(unsigned int major, const char *name,
		    struct file_operations *fops)
{
	struct char_device_struct *cd;
	struct cdev *cdev;
	char *s;
	int err = -ENOMEM;

	cd = __register_chrdev_region(major, 0, 256, name);
	if (IS_ERR(cd))
		return PTR_ERR(cd);
	
	cdev = cdev_alloc();
	if (!cdev)
		goto out2;

	cdev->owner = fops->owner;
	cdev->ops = fops;
	strcpy(cdev->kobj.name, name);
	for (s = strchr(cdev->kobj.name, '/'); s; s = strchr(s, '/'))
		*s = '!';
		
	err = cdev_add(cdev, MKDEV(cd->major, 0), 256);
	if (err)
		goto out;

	cd->cdev = cdev;

	return major ? 0 : cd->major;
out:
	cdev_put(cdev);
out2:
	__unregister_chrdev_region(cd->major, 0, 256);
	return err;
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
	struct char_device_struct *cd;
	cdev_unmap(MKDEV(major, 0), 256);
	cd = __unregister_chrdev_region(major, 0, 256);
	if (cd && cd->cdev)
		cdev_del(cd->cdev);
	kfree(cd);
	return 0;
}

/*
 * Called every time a character special file is opened
 */
int chrdev_open(struct inode * inode, struct file * filp)
{
	int ret = -ENODEV;

	filp->f_op = get_chrfops(kdev_t_to_nr(inode->i_rdev));
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

static struct kobject *exact_match(dev_t dev, int *part, void *data)
{
	struct cdev *p = data;
	return &p->kobj;
}

static int exact_lock(dev_t dev, void *data)
{
	struct cdev *p = data;
	return cdev_get(p) ? 0 : -1;
}

int cdev_add(struct cdev *p, dev_t dev, unsigned count)
{
	int err = kobject_add(&p->kobj);
	if (err)
		return err;
	return kobj_map(cdev_map, dev, count, NULL, exact_match, exact_lock, p);
}

void cdev_unmap(dev_t dev, unsigned count)
{
	kobj_unmap(cdev_map, dev, count);
}

void cdev_del(struct cdev *p)
{
	kobject_del(&p->kobj);
	cdev_put(p);
}

struct kobject *cdev_get(struct cdev *p)
{
	struct module *owner = p->owner;
	struct kobject *kobj;

	if (owner && !try_module_get(owner))
		return NULL;
	kobj = kobject_get(&p->kobj);
	if (!kobj)
		module_put(owner);
	return kobj;
}

static decl_subsys(cdev, NULL, NULL);

static void cdev_dynamic_release(struct kobject *kobj)
{
	struct cdev *p = container_of(kobj, struct cdev, kobj);
	kfree(p);
}

static struct kobj_type ktype_cdev_dynamic = {
	.release	= cdev_dynamic_release,
};

static struct kset kset_dynamic = {
	.subsys = &cdev_subsys,
	.kobj = {.name = "major",},
	.ktype = &ktype_cdev_dynamic,
};

struct cdev *cdev_alloc(void)
{
	struct cdev *p = kmalloc(sizeof(struct cdev), GFP_KERNEL);
	if (p) {
		memset(p, 0, sizeof(struct cdev));
		p->kobj.kset = &kset_dynamic;
		kobject_init(&p->kobj);
	}
	return p;
}

void cdev_init(struct cdev *cdev, struct file_operations *fops)
{
	kobj_set_kset_s(cdev, cdev_subsys);
	kobject_init(&cdev->kobj);
	cdev->ops = fops;
}

static struct kobject *base_probe(dev_t dev, int *part, void *data)
{
	char name[30];
	sprintf(name, "char-major-%d", MAJOR(dev));
	request_module(name);
	return NULL;
}

static int __init chrdev_init(void)
{
	subsystem_register(&cdev_subsys);
	kset_register(&kset_dynamic);
	cdev_map = kobj_map_init(base_probe, &cdev_subsys);
	return 0;
}
subsys_initcall(chrdev_init);
