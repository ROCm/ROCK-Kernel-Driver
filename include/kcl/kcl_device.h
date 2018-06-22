#ifndef AMDKCL_DEVICE_H
#define AMDKCL_DEVICE_H

#if !defined(HAVE_KOBJ_TO_DEV)
static inline struct device *kobj_to_dev(struct kobject *kobj)
{
	return container_of(kobj, struct device, kobj);
}
#endif

#endif /* AMDKCL_DEVICE_H */
