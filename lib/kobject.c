/*
 * kobject.c - library routines for handling generic kernel objects
 */

#define DEBUG 1

#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/stat.h>

/**
 *	kobject_init - initialize object.
 *	@kobj:	object in question.
 */

void kobject_init(struct kobject * kobj)
{
	atomic_set(&kobj->refcount,1);
	INIT_LIST_HEAD(&kobj->entry);
}

/**
 *	kobject_register - register an object.
 *	@kobj:	object in question.
 *
 *	For now, fill in the replicated fields in the object's
 *	directory entry, and create a dir in sysfs. 
 *	This stuff should go away in the future, as we move
 *	more implicit things to sysfs.
 */

int kobject_register(struct kobject * kobj)
{
	pr_debug("kobject %s: registering\n",kobj->name);
	if (kobj->parent)
		kobject_get(kobj->parent);
	return 0;
}

/**
 *	kobject_unregister - unlink an object.
 *	@kobj:	object going away.
 *
 *	The device has been told to be removed, but may
 *	not necessarily be disappearing from the kernel.
 *	So, we remove the directory and decrement the refcount
 *	that we set with kobject_register().
 *
 *	Eventually (maybe now), the refcount will hit 0, and 
 *	put_device() will clean the device up.
 */

void kobject_unregister(struct kobject * kobj)
{
	pr_debug("kobject %s: unregistering\n",kobj->name);
	kobject_put(kobj);
}

/**
 *	kobject_get - increment refcount for object.
 *	@kobj:	object.
 */

struct kobject * kobject_get(struct kobject * kobj)
{
	struct kobject * ret = kobj;
	if (atomic_read(&kobj->refcount) > 0)
		atomic_inc(&kobj->refcount);
	else
		ret = NULL;
	return ret;
}

/**
 *	kobject_put - decrement refcount for object.
 *	@kobj:	object.
 *
 *	Decrement the refcount, and check if 0. If it is, then 
 *	we're gonna need to clean it up, and decrement the refcount
 *	of its parent.
 */

void kobject_put(struct kobject * kobj)
{
	struct kobject * parent = kobj->parent;

	if (!atomic_dec_and_test(&kobj->refcount))
		return;
	pr_debug("kobject %s: cleaning up\n",kobj->name);
	if (parent)
		kobject_put(parent);
}

EXPORT_SYMBOL(kobject_init);
EXPORT_SYMBOL(kobject_register);
EXPORT_SYMBOL(kobject_unregister);
EXPORT_SYMBOL(kobject_get);
EXPORT_SYMBOL(kobject_put);
