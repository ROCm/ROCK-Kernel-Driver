#ifndef _LINUX_CDEV_H
#define _LINUX_CDEV_H
#ifdef __KERNEL__

struct cdev {
	struct kobject kobj;
	struct module *owner;
	struct file_operations *ops;
};

void cdev_init(struct cdev *, struct file_operations *);

struct cdev *cdev_alloc(void);

static inline void cdev_put(struct cdev *p)
{
	if (p)
		kobject_put(&p->kobj);
}

struct kobject *cdev_get(struct cdev *);

int cdev_add(struct cdev *, dev_t, unsigned);

void cdev_del(struct cdev *);

void cdev_unmap(dev_t, unsigned);

#endif
#endif
