/*
 * kobject_uevent.h - list of kobject user events that can be generated
 *
 * Copyright (C) 2004 IBM Corp.
 * Copyright (C) 2004 Greg Kroah-Hartman <greg@kroah.com>
 *
 * This file is released under the GPLv2.
 *
 */

#ifndef _KOBJECT_EVENT_H_
#define _KOBJECT_EVENT_H_

/*
 * If you add an action here, you must also add the proper string to the
 * lib/kobject_uevent.c file.
 */

enum kobject_action {
	KOBJ_ADD	= 0x00,	/* add event, for hotplug */
	KOBJ_REMOVE	= 0x01,	/* remove event, for hotplug */
	KOBJ_CHANGE	= 0x02,	/* a sysfs attribute file has changed */
	KOBJ_MOUNT	= 0x03,	/* mount event for block devices */
	KOBJ_MAX_ACTION,	/* must be last action listed */
};


#ifdef CONFIG_KOBJECT_UEVENT
int kobject_uevent(struct kobject *kobj,
		   enum kobject_action action,
		   struct attribute *attr);
int kobject_uevent_atomic(struct kobject *kobj,
			  enum kobject_action action,
			  struct attribute *attr);
#else
static inline int kobject_uevent(struct kobject *kobj,
				 enum kobject_action action,
				 struct attribute *attr)
{
	return 0;
}
static inline int kobject_uevent_atomic(struct kobject *kobj,
				        enum kobject_action action,
					struct attribute *attr)
{
	return 0;
}
#endif

#endif
