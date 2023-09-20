#ifndef __AMDKCL_CLASS_H__
#define __AMDKCL_CLASS_H__

#ifdef HAVE_LINUX_DEVICE_CLASS_H
#include <linux/device/class.h>
#endif
#include <linux/device.h>
static inline struct class* kcl_class_create(struct module *owner, const char* name)
{
#ifdef HAVE_ONE_ARGUMENT_OF_CLASS_CREATE
	return class_create(name);
#else
	return class_create(owner, name);
#endif
}
#endif

