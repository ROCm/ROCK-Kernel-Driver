#include <linux/module.h>
#include <linux/miscdevice.h>

#include "blktap.h"

static DEFINE_SPINLOCK(blktap_control_lock);
struct blktap *blktaps[MAX_BLKTAP_DEVICE];

static int ring_major;
static int device_major;
static int blktap_control_registered;

static void
blktap_control_initialize_tap(struct blktap *tap)
{
	int minor = tap->minor;

	memset(tap, 0, sizeof(*tap));
	set_bit(BLKTAP_CONTROL, &tap->dev_inuse);
	init_rwsem(&tap->tap_sem);
	sg_init_table(tap->sg, BLKIF_MAX_SEGMENTS_PER_REQUEST);
	init_waitqueue_head(&tap->wq);
	atomic_set(&tap->refcnt, 0);

	tap->minor = minor;
}

static struct blktap *
blktap_control_create_tap(void)
{
	int minor;
	struct blktap *tap;

	tap = kmalloc(sizeof(*tap), GFP_KERNEL);
	if (unlikely(!tap))
		return NULL;

	blktap_control_initialize_tap(tap);

	spin_lock_irq(&blktap_control_lock);
	for (minor = 0; minor < MAX_BLKTAP_DEVICE; minor++)
		if (!blktaps[minor])
			break;

	if (minor == MAX_BLKTAP_DEVICE) {
		kfree(tap);
		tap = NULL;
		goto out;
	}

	tap->minor = minor;
	blktaps[minor] = tap;

out:
	spin_unlock_irq(&blktap_control_lock);
	return tap;
}

static struct blktap *
blktap_control_allocate_tap(void)
{
	int err, minor;
	struct blktap *tap;

	/*
	 * This is called only from the ioctl, which
	 * means we should always have interrupts enabled.
	 */
	BUG_ON(irqs_disabled());

	spin_lock_irq(&blktap_control_lock);

	for (minor = 0; minor < MAX_BLKTAP_DEVICE; minor++) {
		tap = blktaps[minor];
		if (!tap)
			goto found;

		if (!tap->dev_inuse) {
			blktap_control_initialize_tap(tap);
			goto found;
		}
	}

	tap = NULL;

found:
	spin_unlock_irq(&blktap_control_lock);

	if (!tap) {
		tap = blktap_control_create_tap();
		if (!tap)
			return NULL;
	}

	err = blktap_ring_create(tap);
	if (err) {
		BTERR("ring creation failed: %d\n", err);
		clear_bit(BLKTAP_CONTROL, &tap->dev_inuse);
		return NULL;
	}

	BTINFO("allocated tap %p\n", tap);
	return tap;
}

static int
blktap_control_ioctl(struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg)
{
	unsigned long dev;
	struct blktap *tap;

	switch (cmd) {
	case BLKTAP2_IOCTL_ALLOC_TAP: {
		struct blktap_handle h;

		tap = blktap_control_allocate_tap();
		if (!tap) {
			BTERR("error allocating device\n");
			return -ENOMEM;
		}

		h.ring   = ring_major;
		h.device = device_major;
		h.minor  = tap->minor;

		if (copy_to_user((struct blktap_handle __user *)arg,
				 &h, sizeof(h))) {
			blktap_control_destroy_device(tap);
			return -EFAULT;
		}

		return 0;
	}

	case BLKTAP2_IOCTL_FREE_TAP:
		dev = arg;

		if (dev > MAX_BLKTAP_DEVICE || !blktaps[dev])
			return -EINVAL;

		blktap_control_destroy_device(blktaps[dev]);
		return 0;
	}

	return -ENOIOCTLCMD;
}

static struct file_operations blktap_control_file_operations = {
	.owner    = THIS_MODULE,
	.ioctl    = blktap_control_ioctl,
};

static struct miscdevice blktap_misc = {
	.minor    = MISC_DYNAMIC_MINOR,
	.name     = "blktap-control",
	.fops     = &blktap_control_file_operations,
};

int
blktap_control_destroy_device(struct blktap *tap)
{
	int err;
	unsigned long inuse;

	if (!tap)
		return 0;

	set_bit(BLKTAP_SHUTDOWN_REQUESTED, &tap->dev_inuse);

	for (;;) {
		inuse = tap->dev_inuse;
		err   = blktap_device_destroy(tap);
		if (err)
			goto wait;

		inuse = tap->dev_inuse;
		err   = blktap_ring_destroy(tap);
		if (err)
			goto wait;

		inuse = tap->dev_inuse;
		err   = blktap_sysfs_destroy(tap);
		if (err)
			goto wait;

		break;

	wait:
		BTDBG("inuse: 0x%lx, dev_inuse: 0x%lx\n",
		      inuse, tap->dev_inuse);
		if (wait_event_interruptible(tap->wq, tap->dev_inuse != inuse))
			break;
	}

	clear_bit(BLKTAP_SHUTDOWN_REQUESTED, &tap->dev_inuse);

	if (tap->dev_inuse == (1UL << BLKTAP_CONTROL)) {
		err = 0;
		clear_bit(BLKTAP_CONTROL, &tap->dev_inuse);
	}

	return err;
}

static int
blktap_control_init(void)
{
	int err;

	err = misc_register(&blktap_misc);
	if (err) {
		BTERR("misc_register failed for control device");
		return err;
	}

	blktap_control_registered = 1;
	return 0;
}

static void
blktap_control_free(void)
{
	int i;

	for (i = 0; i < MAX_BLKTAP_DEVICE; i++)
		blktap_control_destroy_device(blktaps[i]);

	if (blktap_control_registered)
		if (misc_deregister(&blktap_misc) < 0)
			BTERR("misc_deregister failed for control device");
}

static void
blktap_exit(void)
{
	blktap_control_free();
	blktap_ring_free();
	blktap_sysfs_free();
	blktap_device_free();
	blktap_request_pool_free();
}

static int __init
blktap_init(void)
{
	int err;

	err = blktap_request_pool_init();
	if (err)
		return err;

	err = blktap_device_init(&device_major);
	if (err)
		goto fail;

	err = blktap_ring_init(&ring_major);
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
