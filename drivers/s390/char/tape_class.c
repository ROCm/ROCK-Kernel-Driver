/*
 * Tape class device support
 *
 * Author: Stefan Bader <shbader@de.ibm.com>
 * Based on simple class device code by Greg K-H
 */
#include "tape_class.h"

#ifndef TAPE390_INTERNAL_CLASS
MODULE_AUTHOR("Stefan Bader <shbader@de.ibm.com>");
MODULE_DESCRIPTION("Tape class");
MODULE_LICENSE("GPL");
#endif

struct class_simple *tape_class;

/*
 * Register a tape device and return a pointer to the cdev structure.
 *
 * device
 *	The pointer to the struct device of the physical (base) device.
 * drivername
 *	The pointer to the drivers name for it's character devices.
 * dev
 *	The intended major/minor number. The major number may be 0 to
 *	get a dynamic major number.
 * fops
 *	The pointer to the drivers file operations for the tape device.
 * devname
 *	The pointer to the name of the character device.
 */
struct cdev *register_tape_dev(
	struct device *		device,
	dev_t			dev,
	struct file_operations *fops,
	char *			devname
) {
	struct cdev *	cdev;
	int		rc;
	char *		s;

	cdev = cdev_alloc();
	if (!cdev)
		return ERR_PTR(-ENOMEM);

	cdev->owner = fops->owner;
	cdev->ops   = fops;
	cdev->dev   = dev;
	strcpy(cdev->kobj.name, devname);
	for (s = strchr(cdev->kobj.name, '/'); s; s = strchr(s, '/'))
		*s = '!';

	rc = cdev_add(cdev, cdev->dev, 1);
	if (rc) {
		kobject_put(&cdev->kobj);
		return ERR_PTR(rc);
	}
	class_simple_device_add(tape_class, cdev->dev, device, "%s", devname);

	return cdev;
}
EXPORT_SYMBOL(register_tape_dev);

void unregister_tape_dev(struct cdev *cdev)
{
	if (cdev != NULL) {
		class_simple_device_remove(cdev->dev);
		cdev_del(cdev);
	}
}
EXPORT_SYMBOL(unregister_tape_dev);


#ifndef TAPE390_INTERNAL_CLASS
static int __init tape_init(void)
#else
int tape_setup_class(void)
#endif
{
	tape_class = class_simple_create(THIS_MODULE, "tape390");
	return 0;
}

#ifndef TAPE390_INTERNAL_CLASS
static void __exit tape_exit(void)
#else
void tape_cleanup_class(void)
#endif
{
	class_simple_destroy(tape_class);
	tape_class = NULL;
}

#ifndef TAPE390_INTERNAL_CLASS
postcore_initcall(tape_init);
module_exit(tape_exit);
#else
EXPORT_SYMBOL(tape_setup_class);
EXPORT_SYMBOL(tape_cleanup_class);
#endif
