/*
 * kobject.c - library routines for handling generic kernel objects
 */

#undef DEBUG

#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/stat.h>

static spinlock_t kobj_lock = SPIN_LOCK_UNLOCKED;

/**
 *	populate_dir - populate directory with attributes.
 *	@kobj:	object we're working on.
 *
 *	Most subsystems have a set of default attributes that 
 *	are associated with an object that registers with them.
 *	This is a helper called during object registration that 
 *	loops through the default attributes of the subsystem 
 *	and creates attributes files for them in sysfs.
 *
 */

static int populate_dir(struct kobject * kobj)
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

static int create_dir(struct kobject * kobj)
{
	int error = 0;
	if (strlen(kobj->name)) {
		error = sysfs_create_dir(kobj);
		if (!error) {
			if ((error = populate_dir(kobj)))
				sysfs_remove_dir(kobj);
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
	kobj->subsys = subsys_get(kobj->subsys);
}

/**
 *	kobject_add - add an object to the hierarchy.
 *	@kobj:	object.
 */

int kobject_add(struct kobject * kobj)
{
	int error = 0;
	struct subsystem * s = kobj->subsys;
	struct kobject * parent = kobject_get(kobj->parent);

	if (!(kobj = kobject_get(kobj)))
		return -ENOENT;
	pr_debug("kobject %s: registering. parent: %s, subsys: %s\n",
		 kobj->name, parent ? parent->name : "<NULL>", 
		 kobj->subsys ? kobj->subsys->kobj.name : "<NULL>" );

	if (s) {
		down_write(&s->rwsem);
		if (parent) 
			list_add_tail(&kobj->entry,&parent->entry);
		else {
			list_add_tail(&kobj->entry,&s->list);
			kobj->parent = kobject_get(&s->kobj);
		}
		up_write(&s->rwsem);
	}
	error = create_dir(kobj);
	if (error && kobj->parent)
		kobject_put(kobj->parent);
	return error;
}


/**
 *	kobject_register - initialize and add an object.
 *	@kobj:	object in question.
 */

int kobject_register(struct kobject * kobj)
{
	int error = 0;
	if (kobj) {
		kobject_init(kobj);
		error = kobject_add(kobj);
		if (error)
			kobject_cleanup(kobj);
	} else
		error = -EINVAL;
	return error;
}

/**
 *	kobject_del - unlink kobject from hierarchy.
 * 	@kobj:	object.
 */

void kobject_del(struct kobject * kobj)
{
	sysfs_remove_dir(kobj);
	if (kobj->subsys) {
		down_write(&kobj->subsys->rwsem);
		list_del_init(&kobj->entry);
		up_write(&kobj->subsys->rwsem);
	}
	if (kobj->parent) 
		kobject_put(kobj->parent);
	kobject_put(kobj);
}

/**
 *	kobject_unregister - remove object from hierarchy and decrement refcount.
 *	@kobj:	object going away.
 */

void kobject_unregister(struct kobject * kobj)
{
	pr_debug("kobject %s: unregistering\n",kobj->name);
	kobject_del(kobj);
	kobject_put(kobj);
}

/**
 *	kobject_get - increment refcount for object.
 *	@kobj:	object.
 */

struct kobject * kobject_get(struct kobject * kobj)
{
	struct kobject * ret = kobj;
	spin_lock(&kobj_lock);
	if (kobj && atomic_read(&kobj->refcount) > 0)
		atomic_inc(&kobj->refcount);
	else
		ret = NULL;
	spin_unlock(&kobj_lock);
	return ret;
}

/**
 *	kobject_cleanup - free kobject resources. 
 *	@kobj:	object.
 */

void kobject_cleanup(struct kobject * kobj)
{
	struct subsystem * s = kobj->subsys;

	pr_debug("kobject %s: cleaning up\n",kobj->name);
	if (s) {
		down_write(&s->rwsem);
		list_del_init(&kobj->entry);
		if (s->release)
			s->release(kobj);
		up_write(&s->rwsem);
		subsys_put(s);
	}
}

/**
 *	kobject_put - decrement refcount for object.
 *	@kobj:	object.
 *
 *	Decrement the refcount, and if 0, call kobject_cleanup().
 */

void kobject_put(struct kobject * kobj)
{
	if (!atomic_dec_and_lock(&kobj->refcount, &kobj_lock))
		return;
	spin_unlock(&kobj_lock);
	kobject_cleanup(kobj);
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
	pr_debug("subsystem %s: registering, parent: %s\n",
		 s->kobj.name,s->parent ? s->parent->kobj.name : "<none>");
	return kobject_register(&s->kobj);
}

void subsystem_unregister(struct subsystem * s)
{
	pr_debug("subsystem %s: unregistering\n",s->kobj.name);
	kobject_unregister(&s->kobj);
}


/**
 *	subsystem_create_file - export sysfs attribute file.
 *	@s:	subsystem.
 *	@a:	subsystem attribute descriptor.
 */

int subsys_create_file(struct subsystem * s, struct subsys_attribute * a)
{
	int error = 0;
	if (subsys_get(s)) {
		error = sysfs_create_file(&s->kobj,&a->attr);
		subsys_put(s);
	}
	return error;
}


/**
 *	subsystem_remove_file - remove sysfs attribute file.
 *	@s:	subsystem.
 *	@a:	attribute desciptor.
 */

void subsys_remove_file(struct subsystem * s, struct subsys_attribute * a)
{
	if (subsys_get(s)) {
		sysfs_remove_file(&s->kobj,&a->attr);
		subsys_put(s);
	}
}


EXPORT_SYMBOL(kobject_init);
EXPORT_SYMBOL(kobject_register);
EXPORT_SYMBOL(kobject_unregister);
EXPORT_SYMBOL(kobject_get);
EXPORT_SYMBOL(kobject_put);

EXPORT_SYMBOL(subsystem_init);
EXPORT_SYMBOL(subsystem_register);
EXPORT_SYMBOL(subsystem_unregister);
EXPORT_SYMBOL(subsys_create_file);
EXPORT_SYMBOL(subsys_remove_file);
