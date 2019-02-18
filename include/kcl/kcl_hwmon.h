#ifndef AMDKCL_HWMON_H
#define AMDKCL_HWMON_H

#include <linux/hwmon.h>

/**
 * interface added in mainline kernel 3.13
 * but only affect RHEL6 without backport
 */
#if !defined(HAVE_HWMON_DEVICE_REGISTER_WITH_GROUPS)
static inline struct device *
hwmon_device_register_with_groups(struct device *dev, const char *name,
				  void *drvdata,
				  const struct attribute_group **groups)
{
	return hwmon_device_register(dev);
}
#endif
#endif /* AMDKCL_HWMON_H */
