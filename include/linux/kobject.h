/*
 * kobject.h - generic kernel object infrastructure.
 *
 */

#if defined(__KERNEL__) && !defined(_KOBJECT_H_)
#define _KOBJECT_H_

#include <linux/types.h>
#include <linux/list.h>
#include <linux/sysfs.h>
#include <linux/rwsem.h>
#include <asm/atomic.h>

#define KOBJ_NAME_LEN	16

struct kobject {
	char			name[KOBJ_NAME_LEN];
	atomic_t		refcount;
	struct list_head	entry;
	struct kobject		* parent;
	struct subsystem	* subsys;
	struct kobj_type	* ktype;
	struct dentry		* dentry;
};

extern void kobject_init(struct kobject *);
extern void kobject_cleanup(struct kobject *);

extern int kobject_add(struct kobject *);
extern void kobject_del(struct kobject *);

extern int kobject_register(struct kobject *);
extern void kobject_unregister(struct kobject *);

extern struct kobject * kobject_get(struct kobject *);
extern void kobject_put(struct kobject *);


struct kobj_type {
	void (*release)(struct kobject *);
	struct sysfs_ops	* sysfs_ops;
	struct attribute	** default_attrs;
};

struct subsystem {
	struct kobject		kobj;
	struct list_head	list;
	struct rw_semaphore	rwsem;
	struct subsystem	* parent;
};

extern void subsystem_init(struct subsystem *);
extern int subsystem_register(struct subsystem *);
extern void subsystem_unregister(struct subsystem *);

static inline struct subsystem * subsys_get(struct subsystem * s)
{
	return s ? container_of(kobject_get(&s->kobj),struct subsystem,kobj) : NULL;
}

static inline void subsys_put(struct subsystem * s)
{
	kobject_put(&s->kobj);
}

struct subsys_attribute {
	struct attribute attr;
	ssize_t (*show)(struct subsystem *, char *, size_t, loff_t);
	ssize_t (*store)(struct subsystem *, const char *, size_t, loff_t); 
};

extern int subsys_create_file(struct subsystem * , struct subsys_attribute *);
extern void subsys_remove_file(struct subsystem * , struct subsys_attribute *);

#endif /* _KOBJECT_H_ */
