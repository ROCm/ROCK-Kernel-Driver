/*
 * sys.c - pseudo-bus for system 'devices' (cpus, PICs, timers, etc)
 *
 * Copyright (c) 2002-3 Patrick Mochel
 *               2002-3 Open Source Development Lab
 *
 * This file is released under the GPLv2
 * 
 * This exports a 'system' bus type. 
 * By default, a 'sys' bus gets added to the root of the system. There will
 * always be core system devices. Devices can use sys_device_register() to
 * add themselves as children of the system bus.
 */

#define DEBUG

#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>


extern struct subsystem devices_subsys;

/* 
 * declare system_subsys 
 */
decl_subsys(system,NULL,NULL);

int sysdev_class_register(struct sysdev_class * cls)
{
	pr_debug("Registering sysdev class '%s'\n",cls->kset.kobj.name);
	INIT_LIST_HEAD(&cls->drivers);
	cls->kset.subsys = &system_subsys;
	kset_set_kset_s(cls,system_subsys);
	return kset_register(&cls->kset);
}

void sysdev_class_unregister(struct sysdev_class * cls)
{
	pr_debug("Unregistering sysdev class '%s'\n",cls->kset.kobj.name);
	kset_unregister(&cls->kset);
}

EXPORT_SYMBOL(sysdev_class_register);
EXPORT_SYMBOL(sysdev_class_unregister);


static LIST_HEAD(global_drivers);

/**
 *	sysdev_driver_register - Register auxillary driver
 * 	@cls:	Device class driver belongs to.
 *	@drv:	Driver.
 *
 *	If @cls is valid, then @drv is inserted into @cls->drivers to be 
 *	called on each operation on devices of that class. The refcount
 *	of @cls is incremented. 
 *	Otherwise, @drv is inserted into global_drivers, and called for 
 *	each device.
 */

int sysdev_driver_register(struct sysdev_class * cls, 
			   struct sysdev_driver * drv)
{
	down_write(&system_subsys.rwsem);
	if (kset_get(&cls->kset))
		list_add_tail(&drv->entry,&cls->drivers);
	else
		list_add_tail(&drv->entry,&global_drivers);
	up_write(&system_subsys.rwsem);
	return 0;
}


/**
 *	sysdev_driver_unregister - Remove an auxillary driver.
 *	@cls:	Class driver belongs to.
 *	@drv:	Driver.
 */
void sysdev_driver_unregister(struct sysdev_class * cls,
			      struct sysdev_driver * drv)
{
	down_write(&system_subsys.rwsem);
	list_del_init(&drv->entry);
	if (cls)
		kset_put(&cls->kset);
	up_write(&system_subsys.rwsem);
}


/**
 *	sys_device_register - add a system device to the tree
 *	@sysdev:	device in question
 *
 */
int sys_device_register(struct sys_device * sysdev)
{
	int error;
	struct sysdev_class * cls = sysdev->cls;

	if (!cls)
		return -EINVAL;

	/* Make sure the kset is set */
	sysdev->kobj.kset = &cls->kset;

	/* set the kobject name */
	snprintf(sysdev->kobj.name,KOBJ_NAME_LEN,"%s%d",
		 cls->kset.kobj.name,sysdev->id);

	pr_debug("Registering sys device '%s'\n",sysdev->kobj.name);

	/* Register the object */
	error = kobject_register(&sysdev->kobj);

	if (!error) {
		struct sysdev_driver * drv;

		down_read(&system_subsys.rwsem);
		/* Generic notification is implicit, because it's that 
		 * code that should have called us. 
		 */

		/* Notify global drivers */
		list_for_each_entry(drv,&global_drivers,entry) {
			if (drv->add)
				drv->add(sysdev);
		}

		/* Notify class auxillary drivers */
		list_for_each_entry(drv,&cls->drivers,entry) {
			if (drv->add)
				drv->add(sysdev);
		}
		up_read(&system_subsys.rwsem);
	}
	return error;
}

void sys_device_unregister(struct sys_device * sysdev)
{
	struct sysdev_driver * drv;

	down_read(&system_subsys.rwsem);
	list_for_each_entry(drv,&global_drivers,entry) {
		if (drv->remove)
			drv->remove(sysdev);
	}

	list_for_each_entry(drv,&sysdev->cls->drivers,entry) {
		if (drv->remove)
			drv->remove(sysdev);
	}
	up_read(&system_subsys.rwsem);

	kobject_unregister(&sysdev->kobj);
}



/**
 *	sys_device_shutdown - Shut down all system devices.
 *
 *	Loop over each class of system devices, and the devices in each
 *	of those classes. For each device, we call the shutdown method for
 *	each driver registered for the device - the globals, the auxillaries,
 *	and the class driver. 
 *
 *	Note: The list is iterated in reverse order, so that we shut down
 *	child devices before we shut down thier parents. The list ordering
 *	is guaranteed by virtue of the fact that child devices are registered
 *	after their parents. 
 */

void sys_device_shutdown(void)
{
	struct sysdev_class * cls;

	pr_debug("Shutting Down System Devices\n");

	down_write(&system_subsys.rwsem);
	list_for_each_entry_reverse(cls,&system_subsys.kset.list,
				    kset.kobj.entry) {
		struct sys_device * sysdev;

		pr_debug("Shutting down type '%s':\n",cls->kset.kobj.name);

		list_for_each_entry(sysdev,&cls->kset.list,kobj.entry) {
			struct sysdev_driver * drv;
			pr_debug(" %s\n",sysdev->kobj.name);

			/* Call global drivers first. */
			list_for_each_entry(drv,&global_drivers,entry) {
				if (drv->shutdown)
					drv->shutdown(sysdev);
			}

			/* Call auxillary drivers next. */
			list_for_each_entry(drv,&cls->drivers,entry) {
				if (drv->shutdown)
					drv->shutdown(sysdev);
			}

			/* Now call the generic one */
			if (cls->shutdown)
				cls->shutdown(sysdev);
		}
	}
	up_write(&system_subsys.rwsem);
}


/**
 *	sys_device_suspend - Suspend all system devices.
 *	@state:		Power state to enter.
 *
 *	We perform an almost identical operation as sys_device_shutdown()
 *	above, though calling ->suspend() instead.
 *
 *	Note: Interrupts are disabled when called, so we can't sleep when
 *	trying to get the subsystem's rwsem. If that happens, print a nasty
 *	warning and return an error.
 */

int sys_device_suspend(u32 state)
{
	struct sysdev_class * cls;

	pr_debug("Suspending System Devices\n");

	if (!down_write_trylock(&system_subsys.rwsem)) {
		printk("%s: Cannot acquire semaphore; Failing\n",__FUNCTION__);
		return -EFAULT;
	}

	list_for_each_entry_reverse(cls,&system_subsys.kset.list,
				    kset.kobj.entry) {
		struct sys_device * sysdev;

		pr_debug("Suspending type '%s':\n",cls->kset.kobj.name);

		list_for_each_entry(sysdev,&cls->kset.list,kobj.entry) {
			struct sysdev_driver * drv;
			pr_debug(" %s\n",sysdev->kobj.name);

			/* Call global drivers first. */
			list_for_each_entry(drv,&global_drivers,entry) {
				if (drv->suspend)
					drv->suspend(sysdev,state);
			}

			/* Call auxillary drivers next. */
			list_for_each_entry(drv,&cls->drivers,entry) {
				if (drv->suspend)
					drv->suspend(sysdev,state);
			}

			/* Now call the generic one */
			if (cls->suspend)
				cls->suspend(sysdev,state);
		}
	}
	up_write(&system_subsys.rwsem);

	return 0;
}


/**
 *	sys_device_resume - Bring system devices back to life.
 *
 *	Similar to sys_device_suspend(), but we iterate the list forwards
 *	to guarantee that parent devices are resumed before their children.
 *
 *	Note: Interrupts are disabled when called.
 */

int sys_device_resume(void)
{
	struct sysdev_class * cls;

	pr_debug("Resuming System Devices\n");

	if(!down_write_trylock(&system_subsys.rwsem))
		return -EFAULT;

	list_for_each_entry(cls,&system_subsys.kset.list,kset.kobj.entry) {
		struct sys_device * sysdev;

		pr_debug("Resuming type '%s':\n",cls->kset.kobj.name);

		list_for_each_entry(sysdev,&cls->kset.list,kobj.entry) {
			struct sysdev_driver * drv;
			pr_debug(" %s\n",sysdev->kobj.name);

			/* Call global drivers first. */
			list_for_each_entry(drv,&global_drivers,entry) {
				if (drv->resume)
					drv->resume(sysdev);
			}

			/* Call auxillary drivers next. */
			list_for_each_entry(drv,&cls->drivers,entry) {
				if (drv->resume)
					drv->resume(sysdev);
			}

			/* Now call the generic one */
			if (cls->resume)
				cls->resume(sysdev);
		}
	}
	up_write(&system_subsys.rwsem);
	return 0;
}

int __init sys_bus_init(void)
{
	system_subsys.kset.kobj.parent = &devices_subsys.kset.kobj;
	return subsystem_register(&system_subsys);
}

EXPORT_SYMBOL(sys_device_register);
EXPORT_SYMBOL(sys_device_unregister);
