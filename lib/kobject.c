/*
 * kobject.c - library routines for handling generic kernel objects
 */

#undef DEBUG

#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/stat.h>

/**
 *	kobject_populate_dir - populate directory with attributes.
 *	@kobj:	object we're working on.
 *
 *	Most subsystems have a set of default attributes that 
 *	are associated with an object that registers with them.
 *	This is a helper called during object registration that 
 *	loops through the default attributes of the subsystem 
 *	and creates attributes files for them in sysfs.
 *
 */

static int kobject_populate_dir(struct kobject * kobj)
{
	struct subsystem * s = kobj->subsys;
	struct attribute * attr;
	int error = 0;
	int i;
	
	if (s && s->default_attrs) {
		for (i = 0; (attr = s->default_attrs[i]); i++) {
			if ((error = sysfs_create_file(kobj,attr)))
				break;
		}
	}
	return error;
}

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
	int error = 0;
	struct subsystem * s = subsys_get(kobj->subsys);
	struct kobject * parent = kobject_get(kobj->parent);

	pr_debug("kobject %s: registering\n",kobj->name);
	if (parent)
		pr_debug("  parent is %s\n",parent->name);
	if (s) {
		down_write(&s->rwsem);
		if (parent) 
			list_add_tail(&kobj->entry,&parent->entry);
		else {
			list_add_tail(&kobj->entry,&s->list);
			kobj->parent = &s->kobj;
		}
		up_write(&s->rwsem);
	}
	if (strlen(kobj->name)) {
		error = sysfs_create_dir(kobj);
		if (!error) {
			error = kobject_populate_dir(kobj);
			if (error)
				sysfs_remove_dir(kobj);
		}
	}
	return error;
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
	sysfs_remove_dir(kobj);
	if (kobj->subsys) {
		down_write(&kobj->subsys->rwsem);
		list_del_init(&kobj->entry);
		up_write(&kobj->subsys->rwsem);
	}
	kobject_put(kobj);
}

/**
 *	kobject_get - increment refcount for object.
 *	@kobj:	object.
 */

struct kobject * kobject_get(struct kobject * kobj)
{
	struct kobject * ret = kobj;
	if (kobj && atomic_read(&kobj->refcount) > 0)
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
 *
 *	@kobj->parent could point to its subsystem, which we also 
 *	want to decrement the reference count for. We always dec 
 *	the refcount for the parent, but only do so for the subsystem
 *	if it points to a different place than the parent.
 */

void kobject_put(struct kobject * kobj)
{
	struct kobject * parent = kobj->parent;
	struct subsystem * s = kobj->subsys;

	if (!atomic_dec_and_test(&kobj->refcount))
		return;

	pr_debug("kobject %s: cleaning up\n",kobj->name);
	if (s) {
		if (s->release)
			s->release(kobj);
		if (&s->kobj != parent)
			subsys_put(s);
	} 

	if (parent) 
		kobject_put(parent);
}


void subsystem_init(struct subsystem * s)
{
	kobject_init(&s->kobj);
	init_rwsem(&s->rwsem);
	INIT_LIST_HEAD(&s->list);
}

/**
 *	subsystem_register - register a subsystem.
 *	@s:	the subsystem we're registering.
 */

int subsystem_register(struct subsystem * s)
{
	subsystem_init(s);
	if (s->parent)
		s->kobj.parent = &s->parent->kobj;
	pr_debug("subsystem %s: registering\n",s->kobj.name);
	if (s->parent)
		pr_debug("  parent is %s\n",s->parent->kobj.name);
	return kobject_register(&s->kobj);
}

void subsystem_unregister(struct subsystem * s)
{
	pr_debug("subsystem %s: unregistering\n",s->kobj.name);
	kobject_unregister(&s->kobj);
}


EXPORT_SYMBOL(kobject_init);
EXPORT_SYMBOL(kobject_register);
EXPORT_SYMBOL(kobject_unregister);
EXPORT_SYMBOL(kobject_get);
EXPORT_SYMBOL(kobject_put);

EXPORT_SYMBOL(subsystem_init);
EXPORT_SYMBOL(subsystem_register);
EXPORT_SYMBOL(subsystem_unregister);
