#ifndef _LINUX_CDEV_H
#define _LINUX_CDEV_H
#ifdef __KERNEL__

struct cdev {
	struct kobject kobj;
	struct module *owner;
	struct file_operations *ops;
	struct list_head list;
};

void cdev_init(struct cdev *, struct file_operations *);

struct cdev *cdev_alloc(void);

void cdev_put(struct cdev *p);

struct kobject *cdev_get(struct cdev *);

int cdev_add(struct cdev *, dev_t, unsigned);

void cdev_del(struct cdev *);

void cdev_unmap(dev_t, unsigned);

void cd_forget(struct inode *);

#endif
#endif
