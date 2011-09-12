#ifndef __XEN_DRIVER_UTIL_H__
#define __XEN_DRIVER_UTIL_H__

#include <linux/compiler.h>
#include <linux/device.h>

extern struct class *get_xen_class(void);
extern struct device *xen_class_device_create(struct device_type *,
					      struct device *parent,
					      dev_t devt, void *drvdata,
					      const char *fmt, ...)
		      __printf(5, 6);

#endif /* __XEN_DRIVER_UTIL_H__ */
