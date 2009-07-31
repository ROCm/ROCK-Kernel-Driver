#include <linux/types.h>
#include <linux/device.h>
#include <linux/module.h>

#include "blktap.h"

int blktap_debug_level = 1;

static struct class *class;
static DECLARE_WAIT_QUEUE_HEAD(sysfs_wq);

static inline void
blktap_sysfs_get(struct blktap *tap)
{
	atomic_inc(&tap->ring.sysfs_refcnt);
}

static inline void
blktap_sysfs_put(struct blktap *tap)
{
	if (atomic_dec_and_test(&tap->ring.sysfs_refcnt))
		wake_up(&sysfs_wq);
}

static inline void
blktap_sysfs_enter(struct blktap *tap)
{
	blktap_sysfs_get(tap);               /* pin sysfs device */
	mutex_lock(&tap->ring.sysfs_mutex);  /* serialize sysfs operations */
}

static inline void
blktap_sysfs_exit(struct blktap *tap)
{
	mutex_unlock(&tap->ring.sysfs_mutex);
	blktap_sysfs_put(tap);
}

static ssize_t blktap_sysfs_pause_device(struct device *,
					 struct device_attribute *,
					 const char *, size_t);
DEVICE_ATTR(pause, S_IWUSR, NULL, blktap_sysfs_pause_device);
static ssize_t blktap_sysfs_resume_device(struct device *,
					  struct device_attribute *,
					  const char *, size_t);
DEVICE_ATTR(resume, S_IWUSR, NULL, blktap_sysfs_resume_device);

static ssize_t
blktap_sysfs_set_name(struct device *dev, struct device_attribute *attr,
		      const char *buf, size_t size)
{
	int err;
	struct blktap *tap = dev_get_drvdata(dev);

	blktap_sysfs_enter(tap);

	if (!tap->ring.dev ||
	    test_bit(BLKTAP_SHUTDOWN_REQUESTED, &tap->dev_inuse)) {
		err = -ENODEV;
		goto out;
	}

	if (!test_bit(BLKTAP_PAUSED, &tap->dev_inuse)) {
		err = -EPERM;
		goto out;
	}

	if (size > BLKTAP2_MAX_MESSAGE_LEN) {
		err = -ENAMETOOLONG;
		goto out;
	}

	if (strnlen(buf, BLKTAP2_MAX_MESSAGE_LEN) >= BLKTAP2_MAX_MESSAGE_LEN) {
		err = -EINVAL;
		goto out;
	}

	snprintf(tap->params.name, sizeof(tap->params.name) - 1, "%s", buf);
	err = size;

out:
	blktap_sysfs_exit(tap);	
	return err;
}

static ssize_t
blktap_sysfs_get_name(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	ssize_t size;
	struct blktap *tap = dev_get_drvdata(dev);

	blktap_sysfs_enter(tap);

	if (!tap->ring.dev)
		size = -ENODEV;
	else if (tap->params.name[0])
		size = sprintf(buf, "%s\n", tap->params.name);
	else
		size = sprintf(buf, "%d\n", tap->minor);

	blktap_sysfs_exit(tap);

	return size;
}
DEVICE_ATTR(name, S_IRUSR | S_IWUSR,
		  blktap_sysfs_get_name, blktap_sysfs_set_name);

static ssize_t
blktap_sysfs_remove_device(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t size)
{
	int err;
	struct blktap *tap = dev_get_drvdata(dev);

	if (!tap->ring.dev)
		return size;

	if (test_and_set_bit(BLKTAP_SHUTDOWN_REQUESTED, &tap->dev_inuse))
		return -EBUSY;

	err = blktap_control_destroy_device(tap);

	return (err ? : size);
}
DEVICE_ATTR(remove, S_IWUSR, NULL, blktap_sysfs_remove_device);

static ssize_t
blktap_sysfs_pause_device(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t size)
{
	int err;
	struct blktap *tap = dev_get_drvdata(dev);

	blktap_sysfs_enter(tap);

	BTDBG("pausing %u:%u: dev_inuse: %lu\n",
	      MAJOR(tap->ring.devno), MINOR(tap->ring.devno), tap->dev_inuse);

	if (!tap->ring.dev ||
	    test_bit(BLKTAP_SHUTDOWN_REQUESTED, &tap->dev_inuse)) {
		err = -ENODEV;
		goto out;
	}

	if (test_bit(BLKTAP_PAUSE_REQUESTED, &tap->dev_inuse)) {
		err = -EBUSY;
		goto out;
	}

	if (test_bit(BLKTAP_PAUSED, &tap->dev_inuse)) {
		err = 0;
		goto out;
	}

	err = blktap_device_pause(tap);
	if (!err) {
		device_remove_file(dev, &dev_attr_pause);
		err = device_create_file(dev, &dev_attr_resume);
	}

out:
	blktap_sysfs_exit(tap);

	return (err ? err : size);
}

static ssize_t
blktap_sysfs_resume_device(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t size)
{
	int err;
	struct blktap *tap = dev_get_drvdata(dev);

	blktap_sysfs_enter(tap);

	if (!tap->ring.dev ||
	    test_bit(BLKTAP_SHUTDOWN_REQUESTED, &tap->dev_inuse)) {
		err = -ENODEV;
		goto out;
	}

	if (!test_bit(BLKTAP_PAUSED, &tap->dev_inuse)) {
		err = -EINVAL;
		goto out;
	}

	err = blktap_device_resume(tap);
	if (!err) {
		device_remove_file(dev, &dev_attr_resume);
		err = device_create_file(dev, &dev_attr_pause);
	}

out:
	blktap_sysfs_exit(tap);

	BTDBG("returning %zd\n", (err ? err : size));
	return (err ? err : size);
}

#ifdef ENABLE_PASSTHROUGH
static ssize_t
blktap_sysfs_enable_passthrough(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int err;
	unsigned major, minor;
	struct blktap *tap = dev_get_drvdata(dev);

	BTINFO("passthrough request enabled\n");

	blktap_sysfs_enter(tap);

	if (!tap->ring.dev ||
	    test_bit(BLKTAP_SHUTDOWN_REQUESTED, &tap->dev_inuse)) {
		err = -ENODEV;
		goto out;
	}

	if (!test_bit(BLKTAP_PAUSED, &tap->dev_inuse)) {
		err = -EINVAL;
		goto out;
	}

	if (test_bit(BLKTAP_PASSTHROUGH, &tap->dev_inuse)) {
		err = -EINVAL;
		goto out;
	}

	err = sscanf(buf, "%x:%x", &major, &minor);
	if (err != 2) {
		err = -EINVAL;
		goto out;
	}

	err = blktap_device_enable_passthrough(tap, major, minor);

out:
	blktap_sysfs_exit(tap);
	BTDBG("returning %d\n", (err ? err : size));
	return (err ? err : size);
}
#endif

static ssize_t
blktap_sysfs_debug_device(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	char *tmp;
	int i, ret;
	struct blktap *tap = dev_get_drvdata(dev);

	tmp = buf;
	blktap_sysfs_get(tap);

	if (!tap->ring.dev) {
		ret = sprintf(tmp, "no device\n");
		goto out;
	}

	tmp += sprintf(tmp, "%s (%u:%u), refcnt: %d, dev_inuse: 0x%08lx\n",
		       tap->params.name, MAJOR(tap->ring.devno),
		       MINOR(tap->ring.devno), atomic_read(&tap->refcnt),
		       tap->dev_inuse);
	tmp += sprintf(tmp, "capacity: 0x%llx, sector size: 0x%lx, "
		       "device users: %d\n", tap->params.capacity,
		       tap->params.sector_size, tap->device.users);

	down_read(&tap->tap_sem);

	tmp += sprintf(tmp, "pending requests: %d\n", tap->pending_cnt);
	for (i = 0; i < MAX_PENDING_REQS; i++) {
		struct blktap_request *req = tap->pending_requests[i];
		if (!req)
			continue;

		tmp += sprintf(tmp, "req %d: id: %llu, usr_idx: %d, "
			       "status: 0x%02x, pendcnt: %d, "
			       "nr_pages: %u, op: %d, time: %lu:%lu\n",
			       i, (unsigned long long)req->id, req->usr_idx,
			       req->status, atomic_read(&req->pendcnt),
			       req->nr_pages, req->operation, req->time.tv_sec,
			       req->time.tv_usec);
	}

	up_read(&tap->tap_sem);
	ret = (tmp - buf) + 1;

out:
	blktap_sysfs_put(tap);
	BTDBG("%s\n", buf);

	return ret;
}
DEVICE_ATTR(debug, S_IRUSR, blktap_sysfs_debug_device, NULL);

int
blktap_sysfs_create(struct blktap *tap)
{
	struct blktap_ring *ring;
	struct device *dev;

	if (!class)
		return -ENODEV;

	ring = &tap->ring;

	dev = device_create(class, NULL, ring->devno, tap,
			    "blktap%d", tap->minor);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	ring->dev = dev;

	mutex_init(&ring->sysfs_mutex);
	atomic_set(&ring->sysfs_refcnt, 0);
	set_bit(BLKTAP_SYSFS, &tap->dev_inuse);

	if (device_create_file(dev, &dev_attr_name) ||
	    device_create_file(dev, &dev_attr_remove) ||
	    device_create_file(dev, &dev_attr_pause) ||
	    device_create_file(dev, &dev_attr_debug))
		printk(KERN_WARNING
		       "One or more attibute files not created for blktap%d\n",
		       tap->minor);

	return 0;
}

int
blktap_sysfs_destroy(struct blktap *tap)
{
	struct blktap_ring *ring;
	struct device *dev;

	ring = &tap->ring;
	dev  = ring->dev;
	if (!class || !dev)
		return 0;

	ring->dev = NULL;
	if (wait_event_interruptible(sysfs_wq,
				     !atomic_read(&tap->ring.sysfs_refcnt)))
		return -EAGAIN;

	/* XXX: is it safe to remove the class from a sysfs attribute? */
	device_remove_file(dev, &dev_attr_name);
	device_remove_file(dev, &dev_attr_remove);
	device_remove_file(dev, &dev_attr_pause);
	device_remove_file(dev, &dev_attr_resume);
	device_remove_file(dev, &dev_attr_debug);
	device_destroy(class, ring->devno);

	clear_bit(BLKTAP_SYSFS, &tap->dev_inuse);

	return 0;
}

static ssize_t
blktap_sysfs_show_verbosity(struct class *class, char *buf)
{
	return sprintf(buf, "%d\n", blktap_debug_level);
}

static ssize_t
blktap_sysfs_set_verbosity(struct class *class, const char *buf, size_t size)
{
	int level;

	if (sscanf(buf, "%d", &level) == 1) {
		blktap_debug_level = level;
		return size;
	}

	return -EINVAL;
}
CLASS_ATTR(verbosity, S_IRUSR | S_IWUSR,
	   blktap_sysfs_show_verbosity, blktap_sysfs_set_verbosity);

static ssize_t
blktap_sysfs_show_devices(struct class *class, char *buf)
{
	int i, ret;
	struct blktap *tap;

	ret = 0;
	for (i = 0; i < MAX_BLKTAP_DEVICE; i++) {
		tap = blktaps[i];
		if (!tap)
			continue;

		if (!test_bit(BLKTAP_DEVICE, &tap->dev_inuse))
			continue;

		ret += sprintf(buf + ret, "%d ", tap->minor);
		ret += snprintf(buf + ret, sizeof(tap->params.name) - 1,
				tap->params.name);
		ret += sprintf(buf + ret, "\n");
	}

	return ret;
}
CLASS_ATTR(devices, S_IRUSR, blktap_sysfs_show_devices, NULL);

void
blktap_sysfs_free(void)
{
	if (!class)
		return;

	class_remove_file(class, &class_attr_verbosity);
	class_remove_file(class, &class_attr_devices);

	class_destroy(class);
}

int
blktap_sysfs_init(void)
{
	struct class *cls;

	if (class)
		return -EEXIST;

	cls = class_create(THIS_MODULE, "blktap2");
	if (IS_ERR(cls))
		return PTR_ERR(cls);

	if (class_create_file(cls, &class_attr_verbosity) ||
	    class_create_file(cls, &class_attr_devices))
		printk(KERN_WARNING "blktap2: One or more "
		       "class attribute files could not be created.\n");

	class = cls;
	return 0;
}
