#ifndef AMDKCL_HWMON_H
#define AMDKCL_HWMON_H

#include <linux/hwmon.h>

/**
 * interface added in mainline kernel 3.13
 * but only affect RHEL6 without backport
 */
static inline struct device *
kcl_hwmon_device_register_with_groups(struct device *dev, const char *name,
				      void *drvdata,
				      const struct attribute_group **groups)
{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 32)
	return hwmon_device_register(dev);
#else
	return hwmon_device_register_with_groups(dev, name, drvdata, groups);
#endif
}

#endif /* AMDKCL_HWMON_H */
