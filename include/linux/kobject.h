/*
 * kobject.h - generic kernel object infrastructure.
 *
 */

#ifndef _KOBJECT_H_
#define _KOBJECT_H_

#include <linux/types.h>
#include <linux/list.h>
#include <linux/sysfs.h>
#include <linux/rwsem.h>
#include <asm/atomic.h>

struct kobject {
	char			name[16];
	atomic_t		refcount;
	struct list_head	entry;
	struct kobject		* parent;
	struct subsystem	* subsys;
	struct dentry		* dentry;
};

extern void kobject_init(struct kobject *);

extern int kobject_register(struct kobject *);
extern void kobject_unregister(struct kobject *);

extern struct kobject * kobject_get(struct kobject *);
extern void kobject_put(struct kobject *);


struct subsystem {
	struct kobject		kobj;
	struct list_head	list;
	struct rw_semaphore	rwsem;
	struct subsystem	* parent;
	void (*release)(struct kobject *);
	struct sysfs_ops	* sysfs_ops;
};

extern void subsystem_init(struct subsystem *);
extern int subsystem_register(struct subsystem *);
extern void subsystem_unregister(struct subsystem *);

static inline struct subsystem * subsys_get(struct subsystem * s)
{
	return container_of(kobject_get(&s->kobj),struct subsystem,kobj);
}

static inline void subsys_put(struct subsystem * s)
{
	kobject_put(&s->kobj);
}

#endif /* _KOBJECT_H_ */
