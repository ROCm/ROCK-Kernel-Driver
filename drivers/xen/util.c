#include <linux/err.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <xen/driver_util.h>

static struct class *_get_xen_class(void)
{
	static struct class *xen_class;
	static DEFINE_MUTEX(xc_mutex);

	mutex_lock(&xc_mutex);
	if (IS_ERR_OR_NULL(xen_class))
		xen_class = class_create(THIS_MODULE, "xen");
	mutex_unlock(&xc_mutex);
	if (IS_ERR(xen_class))
		pr_err("failed to create xen sysfs class\n");

	return xen_class;
}

struct class *get_xen_class(void)
{
	struct class *class = _get_xen_class();

	return !IS_ERR(class) ? class : NULL;
}
EXPORT_SYMBOL_GPL(get_xen_class);

static void xcdev_release(struct device *dev)
{
	kfree(dev);
}

struct device *xen_class_device_create(struct device_type *type,
				       struct device *parent,
				       dev_t devt, void *drvdata,
				       const char *fmt, ...)
{
	struct device *dev;
	int err;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev) {
		va_list vargs;

		va_start(vargs, fmt);
		err = kobject_set_name_vargs(&dev->kobj, fmt, vargs);
		va_end(vargs);
	} else
		err = -ENOMEM;

	if (!err) {
		dev->devt = devt;
		dev->class = _get_xen_class();
		if (IS_ERR(dev->class))
			err = PTR_ERR(dev->class);
	}

	if (!err) {
		dev->type = type;
		dev->parent = parent;
		dev_set_drvdata(dev, drvdata);
		dev->release = xcdev_release;
		err = device_register(dev);
		if (!err)
			return dev;
		put_device(dev);
	} else
		kfree(dev);

	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(xen_class_device_create);
