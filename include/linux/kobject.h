/*
 * kobject.h - generic kernel object infrastructure.
 *
 */

#ifndef _KOBJECT_H_
#define _KOBJECT_H_

#include <linux/types.h>
#include <linux/list.h>
#include <linux/sysfs.h>
#include <asm/atomic.h>

struct kobject {
	char			name[16];
	atomic_t		refcount;
	struct list_head	entry;
	struct kobject		* parent;
	struct sysfs_dir	dir;
};

extern void kobject_init(struct kobject *);

extern int kobject_register(struct kobject *);
extern void kobject_unregister(struct kobject *);

extern struct kobject * kobject_get(struct kobject *);
extern void kobject_put(struct kobject *);

#endif /* _KOBJECT_H_ */
