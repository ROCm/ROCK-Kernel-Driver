#include <linux/err.h>
#include <linux/module.h>
#include <xen/driver_util.h>

struct class *get_xen_class(void)
{
	static struct class *xen_class;

	if (xen_class)
		return xen_class;

	xen_class = class_create(THIS_MODULE, "xen");
	if (IS_ERR(xen_class)) {
		printk("Failed to create xen sysfs class.\n");
		xen_class = NULL;
	}

	return xen_class;
}
EXPORT_SYMBOL_GPL(get_xen_class);
