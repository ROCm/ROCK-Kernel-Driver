#include <linux/module.h>
#include <linux/sched.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <asm/uaccess.h>

#include "blktap.h"

DEFINE_MUTEX(blktap_lock);

struct blktap **blktaps;
int blktap_max_minor;
static struct blktap_page_pool *default_pool;

static struct blktap *
blktap_control_get_minor(void)
{
	int minor;
	struct blktap *tap;

	tap = kzalloc(sizeof(*tap), GFP_KERNEL);
	if (unlikely(!tap))
		return NULL;

	mutex_lock(&blktap_lock);

	for (minor = 0; minor < blktap_max_minor; minor++)
		if (!blktaps[minor])
			break;

	if (minor == MAX_BLKTAP_DEVICE)
		goto fail;

	if (minor == blktap_max_minor) {
		void *p;
		int n;

		n = min(2 * blktap_max_minor, MAX_BLKTAP_DEVICE);
		p = krealloc(blktaps, n * sizeof(blktaps[0]), GFP_KERNEL);
		if (!p)
			goto fail;

		blktaps          = p;
		minor            = blktap_max_minor;
		blktap_max_minor = n;

		memset(&blktaps[minor], 0, (n - minor) * sizeof(blktaps[0]));
	}

	tap->minor = minor;
	blktaps[minor] = tap;

	__module_get(THIS_MODULE);
out:
	mutex_unlock(&blktap_lock);
	return tap;

fail:
	mutex_unlock(&blktap_lock);
	kfree(tap);
	tap = NULL;
	goto out;
}

static void
blktap_control_put_minor(struct blktap* tap)
{
	blktaps[tap->minor] = NULL;
	kfree(tap);

	module_put(THIS_MODULE);
}

static struct blktap*
blktap_control_create_tap(void)
{
	struct blktap *tap;
	int err;

	tap = blktap_control_get_minor();
	if (!tap)
		return NULL;

	kobject_get(&default_pool->kobj);
	tap->pool = default_pool;

	err = blktap_ring_create(tap);
	if (err)
		goto fail_tap;

	err = blktap_sysfs_create(tap);
	if (err)
		goto fail_ring;

	return tap;

fail_ring:
	blktap_ring_destroy(tap);
fail_tap:
	blktap_control_put_minor(tap);

	return NULL;
}

int
blktap_control_destroy_tap(struct blktap *tap)
{
	int err;

	err = blktap_ring_destroy(tap);
	if (err)
		return err;

	kobject_put(&tap->pool->kobj);

	blktap_sysfs_destroy(tap);

	blktap_control_put_minor(tap);

	return 0;
}

static long
blktap_control_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct blktap *tap;

	switch (cmd) {
	case BLKTAP2_IOCTL_ALLOC_TAP: {
		struct blktap_handle h;
		void __user *ptr = (void __user*)arg;

		tap = blktap_control_create_tap();
		if (!tap)
			return -ENOMEM;

		h.ring   = blktap_ring_major;
		h.device = blktap_device_major;
		h.minor  = tap->minor;

		if (copy_to_user(ptr, &h, sizeof(h))) {
			blktap_control_destroy_tap(tap);
			return -EFAULT;
		}

		return 0;
	}

	case BLKTAP2_IOCTL_FREE_TAP: {
		int minor = arg;

		if (minor > MAX_BLKTAP_DEVICE)
			return -EINVAL;

		tap = blktaps[minor];
		if (!tap)
			return -ENODEV;

		return blktap_control_destroy_tap(tap);
	}
	}

	return -ENOIOCTLCMD;
}

static const struct file_operations blktap_control_file_operations = {
	.owner    = THIS_MODULE,
	.unlocked_ioctl = blktap_control_ioctl,
};

static struct miscdevice blktap_control = {
	.minor    = MISC_DYNAMIC_MINOR,
	.name     = "blktap-control",
	.nodename = BLKTAP2_DEV_DIR "control",
	.fops     = &blktap_control_file_operations,
};

static struct device *control_device;

static ssize_t
blktap_control_show_default_pool(struct device *device,
				 struct device_attribute *attr,
				 char *buf)
{
	return sprintf(buf, "%s", kobject_name(&default_pool->kobj));
}

static ssize_t
blktap_control_store_default_pool(struct device *device,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct blktap_page_pool *pool, *tmp = default_pool;

	pool = blktap_page_pool_get(buf);
	if (IS_ERR(pool))
		return PTR_ERR(pool);

	default_pool = pool;
	kobject_put(&tmp->kobj);

	return size;
}

static DEVICE_ATTR(default_pool, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH,
		   blktap_control_show_default_pool,
		   blktap_control_store_default_pool);

size_t
blktap_control_debug(struct blktap *tap, char *buf, size_t size)
{
	char *s = buf, *end = buf + size;

	s += snprintf(s, end - s,
		      "tap %u:%u name:'%s' flags:%#08lx\n",
		      MAJOR(tap->ring.devno), MINOR(tap->ring.devno),
		      tap->name, tap->dev_inuse);

	return s - buf;
}

static int __init
blktap_control_init(void)
{
	int err;

	err = misc_register(&blktap_control);
	if (err)
		return err;

	control_device = blktap_control.this_device;

	blktap_max_minor = min(64, MAX_BLKTAP_DEVICE);
	blktaps = kzalloc(blktap_max_minor * sizeof(blktaps[0]), GFP_KERNEL);
	if (!blktaps) {
		BTERR("failed to allocate blktap minor map");
		return -ENOMEM;
	}

	err = blktap_page_pool_init(&control_device->kobj);
	if (err)
		return err;

	default_pool = blktap_page_pool_get("default");
	if (!default_pool)
		return -ENOMEM;

	err = device_create_file(control_device, &dev_attr_default_pool);
	if (err)
		return err;

	return 0;
}

static void
blktap_control_exit(void)
{
	if (default_pool) {
		kobject_put(&default_pool->kobj);
		default_pool = NULL;
	}

	blktap_page_pool_exit();

	if (blktaps) {
		kfree(blktaps);
		blktaps = NULL;
	}

	if (control_device) {
		misc_deregister(&blktap_control);
		control_device = NULL;
	}
}

static void
blktap_exit(void)
{
	blktap_control_exit();
	blktap_ring_exit();
	blktap_sysfs_exit();
	blktap_device_exit();
}

static int __init
blktap_init(void)
{
	int err;

	err = blktap_device_init();
	if (err)
		goto fail;

	err = blktap_ring_init();
	if (err)
		goto fail;

	err = blktap_sysfs_init();
	if (err)
		goto fail;

	err = blktap_control_init();
	if (err)
		goto fail;

	return 0;

fail:
	blktap_exit();
	return err;
}

module_init(blktap_init);
module_exit(blktap_exit);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("devname:" BLKTAP2_DEV_DIR "control");
