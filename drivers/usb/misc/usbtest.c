#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/module.h>
//#include <linux/time.h>
#include <asm/scatterlist.h>

#if !defined (DEBUG) && defined (CONFIG_USB_DEBUG)
#   define DEBUG
#endif
#include <linux/usb.h>


/*-------------------------------------------------------------------------*/

// FIXME make these public somewhere; usbdevfs.h?
//
struct usbtest_param {
	// inputs
	unsigned		test_num;	/* 0..(TEST_CASES-1) */
	int			iterations;
	int			length;
	int			vary;
	int			sglen;

	// outputs
	struct timeval		duration;
};
#define USBTEST_REQUEST	_IOWR('U', 100, struct usbtest_param)

/*-------------------------------------------------------------------------*/

/* this is accessed only through usbfs ioctl calls.
 * one ioctl to issue a test ... no locking needed!!!
 * tests create other threads if they need them.
 * urbs and buffers are allocated dynamically,
 * and data generated deterministically.
 *
 * there's a minor complication on disconnect(), since
 * usbfs.disconnect() waits till our ioctl completes.
 */
struct usbtest_dev {
	struct usb_interface	*intf;
	struct testdev_info	*info;
	char			id [32];
	int			in_pipe;
	int			out_pipe;
};

static struct usb_device *testdev_to_usbdev (struct usbtest_dev *test)
{
	return interface_to_usbdev (test->intf);
}

/* set up all urbs so they can be used with either bulk or interrupt */
#define	INTERRUPT_RATE		1	/* msec/transfer */

/*-------------------------------------------------------------------------*/

/* Support for testing basic non-queued I/O streams.
 *
 * These just package urbs as requests that can be easily canceled.
 * Each urb's data buffer is dynamically allocated; callers can fill
 * them with non-zero test data (or test for it) when appropriate.
 */

static void simple_callback (struct urb *urb)
{
	complete ((struct completion *) urb->context);
}

static struct urb *simple_alloc_urb (
	struct usb_device	*udev,
	int			pipe,
	long			bytes
)
{
	struct urb		*urb;

	if (bytes < 0)
		return 0;
	urb = usb_alloc_urb (0, SLAB_KERNEL);
	if (!urb)
		return urb;
	usb_fill_bulk_urb (urb, udev, pipe, 0, bytes, simple_callback, 0);
	urb->interval = (udev->speed == USB_SPEED_HIGH)
			? (INTERRUPT_RATE << 3)
			: INTERRUPT_RATE,
	urb->transfer_flags = URB_NO_DMA_MAP;
	urb->transfer_buffer = usb_buffer_alloc (udev, bytes, SLAB_KERNEL,
			&urb->transfer_dma);
	if (!urb->transfer_buffer) {
		usb_free_urb (urb);
		urb = 0;
	} else
		memset (urb->transfer_buffer, 0, bytes);
	return urb;
}

static void simple_free_urb (struct urb *urb)
{
	usb_buffer_free (urb->dev, urb->transfer_buffer_length,
			urb->transfer_buffer, urb->transfer_dma);
	usb_free_urb (urb);
}

static int simple_io (
	struct urb		*urb,
	int			iterations,
	int			vary
)
{
	struct usb_device	*udev = urb->dev;
	int			max = urb->transfer_buffer_length;
	struct completion	completion;
	int			retval = 0;

	urb->context = &completion;
	while (iterations-- > 0 && retval == 0) {
		init_completion (&completion);
		if ((retval = usb_submit_urb (urb, SLAB_KERNEL)) != 0)
			break;

		/* NOTE:  no timeouts; can't be broken out of by interrupt */
		wait_for_completion (&completion);
		retval = urb->status;
		urb->dev = udev;

		if (vary) {
			int	len = urb->transfer_buffer_length;

			len += max;
			len %= max;
			if (len == 0)
				len = (vary < max) ? vary : max;
			urb->transfer_buffer_length = len;
		}

		/* FIXME if endpoint halted, clear halt (and log) */
	}
	urb->transfer_buffer_length = max;

	// FIXME for unlink or fault handling tests, don't report
	// failure if retval is as we expected ...
	if (retval)
		dbg ("simple_io failed, iterations left %d, status %d",
				iterations, retval);
	return retval;
}

/*-------------------------------------------------------------------------*/

/* We use scatterlist primitives to test queued I/O.
 * Yes, this also tests the scatterlist primitives.
 */

static void free_sglist (struct scatterlist *sg, int nents)
{
	unsigned		i;
	
	if (!sg)
		return;
	for (i = 0; i < nents; i++) {
		if (!sg [i].page)
			continue;
		kfree (page_address (sg [i].page) + sg [i].offset);
	}
	kfree (sg);
}

static struct scatterlist *
alloc_sglist (int nents, int max, int vary)
{
	struct scatterlist	*sg;
	unsigned		i;
	unsigned		size = max;

	sg = kmalloc (nents * sizeof *sg, SLAB_KERNEL);
	if (!sg)
		return 0;
	memset (sg, 0, nents * sizeof *sg);

	for (i = 0; i < nents; i++) {
		char		*buf;

		buf = kmalloc (size, SLAB_KERNEL);
		if (!buf) {
			free_sglist (sg, i);
			return 0;
		}
		memset (buf, 0, size);

		/* kmalloc pages are always physically contiguous! */
		sg [i].page = virt_to_page (buf);
		sg [i].offset = ((unsigned) buf) & ~PAGE_MASK;
		sg [i].length = size;

		if (vary) {
			size += vary;
			size %= max;
			if (size == 0)
				size = (vary < max) ? vary : max;
		}
	}

	return sg;
}

static int perform_sglist (
	struct usb_device	*udev,
	unsigned		iterations,
	int			pipe,
	struct usb_sg_request	*req,
	struct scatterlist	*sg,
	int			nents
)
{
	int			retval = 0;

	while (retval == 0 && iterations-- > 0) {
		retval = usb_sg_init (req, udev, pipe,
				(udev->speed == USB_SPEED_HIGH)
					? (INTERRUPT_RATE << 3)
					: INTERRUPT_RATE,
				sg, nents, 0, SLAB_KERNEL);
		
		if (retval)
			break;
		usb_sg_wait (req);
		retval = req->status;

		/* FIXME if endpoint halted, clear halt (and log) */
	}

	// FIXME for unlink or fault handling tests, don't report
	// failure if retval is as we expected ...

	if (retval)
		dbg ("perform_sglist failed, iterations left %d, status %d",
				iterations, retval);
	return retval;
}


/*-------------------------------------------------------------------------*/

/* We only have this one interface to user space, through usbfs.
 * User mode code can scan usbfs to find N different devices (maybe on
 * different busses) to use when testing, and allocate one thread per
 * test.  So discovery is simplified, and we have no device naming issues.
 *
 * Don't use these only as stress/load tests.  Use them along with with
 * other USB bus activity:  plugging, unplugging, mousing, mp3 playback,
 * video capture, and so on.  Run different tests at different times, in
 * different sequences.  Nothing here should interact with other devices,
 * except indirectly by consuming USB bandwidth and CPU resources for test
 * threads and request completion.
 */

static int usbtest_ioctl (struct usb_interface *intf, unsigned int code, void *buf)
{
	struct usbtest_dev	*dev = dev_get_drvdata (&intf->dev);
	struct usb_device	*udev = testdev_to_usbdev (dev);
	struct usbtest_param	*param = buf;
	int			retval = -EOPNOTSUPP;
	struct urb		*urb;
	struct scatterlist	*sg;
	struct usb_sg_request	req;
	struct timeval		start;

	// FIXME USBDEVFS_CONNECTINFO doesn't say how fast the device is.

	if (code != USBTEST_REQUEST)
		return -EOPNOTSUPP;

	if (param->iterations <= 0 || param->length < 0
			|| param->sglen < 0 || param->vary < 0)
		return -EINVAL;

	/*
	 * Just a bunch of test cases that every HCD is expected to handle.
	 *
	 * Some may need specific firmware, though it'd be good to have
	 * one firmware image to handle all the test cases.
	 *
	 * FIXME add more tests!  cancel requests, verify the data, control
	 * requests, and so on.
	 */
	do_gettimeofday (&start);
	switch (param->test_num) {

	case 0:
		dbg ("%s TEST 0:  NOP", dev->id);
		retval = 0;
		break;

	/* Simple non-queued bulk I/O tests */
	case 1:
		if (dev->out_pipe == 0)
			break;
		dbg ("%s TEST 1:  write %d bytes %u times", dev->id,
				param->length, param->iterations);
		urb = simple_alloc_urb (udev, dev->out_pipe, param->length);
		if (!urb) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk sink (maybe accepts short writes)
		retval = simple_io (urb, param->iterations, 0);
		simple_free_urb (urb);
		break;
	case 2:
		if (dev->in_pipe == 0)
			break;
		dbg ("%s TEST 2:  read %d bytes %u times", dev->id,
				param->length, param->iterations);
		urb = simple_alloc_urb (udev, dev->in_pipe, param->length);
		if (!urb) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk source (maybe generates short writes)
		retval = simple_io (urb, param->iterations, 0);
		simple_free_urb (urb);
		break;
	case 3:
		if (dev->out_pipe == 0 || param->vary == 0)
			break;
		dbg ("%s TEST 3:  write/%d 0..%d bytes %u times", dev->id,
				param->vary, param->length, param->iterations);
		urb = simple_alloc_urb (udev, dev->out_pipe, param->length);
		if (!urb) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk sink (maybe accepts short writes)
		retval = simple_io (urb, param->iterations, param->vary);
		simple_free_urb (urb);
		break;
	case 4:
		if (dev->in_pipe == 0 || param->vary == 0)
			break;
		dbg ("%s TEST 3:  read/%d 0..%d bytes %u times", dev->id,
				param->vary, param->length, param->iterations);
		urb = simple_alloc_urb (udev, dev->out_pipe, param->length);
		if (!urb) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk source (maybe generates short writes)
		retval = simple_io (urb, param->iterations, param->vary);
		simple_free_urb (urb);
		break;

	/* Queued bulk I/O tests */
	case 5:
		if (dev->out_pipe == 0 || param->sglen == 0)
			break;
		dbg ("%s TEST 5:  write %d sglists, %d entries of %d bytes",
				dev->id, param->iterations,
				param->sglen, param->length);
		sg = alloc_sglist (param->sglen, param->length, 0);
		if (!sg) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk sink (maybe accepts short writes)
		retval = perform_sglist (udev, param->iterations, dev->out_pipe,
				&req, sg, param->sglen);
		free_sglist (sg, param->sglen);
		break;

	case 6:
		if (dev->in_pipe == 0 || param->sglen == 0)
			break;
		dbg ("%s TEST 6:  read %d sglists, %d entries of %d bytes",
				dev->id, param->iterations,
				param->sglen, param->length);
		sg = alloc_sglist (param->sglen, param->length, 0);
		if (!sg) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk source (maybe generates short writes)
		retval = perform_sglist (udev, param->iterations, dev->in_pipe,
				&req, sg, param->sglen);
		free_sglist (sg, param->sglen);
		break;
	case 7:
		if (dev->out_pipe == 0 || param->sglen == 0 || param->vary == 0)
			break;
		dbg ("%s TEST 7:  write/%d %d sglists, %d entries 0..%d bytes",
				dev->id, param->vary, param->iterations,
				param->sglen, param->length);
		sg = alloc_sglist (param->sglen, param->length, param->vary);
		if (!sg) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk sink (maybe accepts short writes)
		retval = perform_sglist (udev, param->iterations, dev->out_pipe,
				&req, sg, param->sglen);
		free_sglist (sg, param->sglen);
		break;
	case 8:
		if (dev->in_pipe == 0 || param->sglen == 0 || param->vary == 0)
			break;
		dbg ("%s TEST 8:  read/%d %d sglists, %d entries 0..%d bytes",
				dev->id, param->vary, param->iterations,
				param->sglen, param->length);
		sg = alloc_sglist (param->sglen, param->length, param->vary);
		if (!sg) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk source (maybe generates short writes)
		retval = perform_sglist (udev, param->iterations, dev->in_pipe,
				&req, sg, param->sglen);
		free_sglist (sg, param->sglen);
		break;

	/* test cases for the unlink/cancel codepaths need a thread to
	 * usb_unlink_urb() or usg_sg_cancel(), and a way to check if
	 * the urb/sg_request was properly canceled.
	 *
	 * for the unlink-queued cases, the usb_sg_*() code uses/tests
	 * the "streamed" cleanup mode, not the "packet" one
	 */

	}
	do_gettimeofday (&param->duration);
	param->duration.tv_sec -= start.tv_sec;
	param->duration.tv_usec -= start.tv_usec;
	if (param->duration.tv_usec < 0) {
		param->duration.tv_usec += 1000 * 1000;
		param->duration.tv_sec -= 1;
	}
	return retval;
}

/*-------------------------------------------------------------------------*/

/* most programmable USB devices can be given firmware that will support the
 * test cases above.  one basic question is which endpoints to use for
 * testing; endpoint numbers are not always firmware-selectable.
 *
 * for now, the driver_info in the device_id table entry just encodes the
 * endpoint info for a pair of bulk-capable endpoints, which we can use
 * for some interrupt transfer tests too.  later this could get fancier.
 */
#define EP_PAIR(in,out) (((in)<<4)|(out))

static int force_interrupt = 0;
MODULE_PARM (force_interrupt, "i");
MODULE_PARM_DESC (force_interrupt, "0 = test bulk (default), else interrupt");

static int
usbtest_probe (struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device	*udev;
	struct usbtest_dev	*dev;
	unsigned long		driver_info = id->driver_info;

	udev = interface_to_usbdev (intf);

	dev = kmalloc (sizeof *dev, SLAB_KERNEL);
	if (!dev)
		return -ENOMEM;
	memset (dev, 0, sizeof *dev);
	snprintf (dev->id, sizeof dev->id, "%s-%s",
			udev->bus->bus_name, udev->devpath);
	dev->intf = intf;

	/* NOTE this doesn't yet test the handful of difference that are
	 * visible with high speed devices:  bigger maxpacket (1K) and
	 * "high bandwidth" modes (up to 3 packets/uframe).
	 */
	if (force_interrupt || udev->speed == USB_SPEED_LOW) {
		if (driver_info & 0xf0)
			dev->in_pipe = usb_rcvintpipe (udev,
				(driver_info >> 4) & 0x0f);
		if (driver_info & 0x0f)
			dev->out_pipe = usb_sndintpipe (udev,
				driver_info & 0x0f);

#if 1
		// FIXME disabling this until we finally get rid of
		// interrupt "automagic" resubmission
		dbg ("%s:  no interrupt transfers for now", dev->id);
		kfree (dev);
		return -ENODEV;
#endif
	} else {
		if (driver_info & 0xf0)
			dev->in_pipe = usb_rcvbulkpipe (udev,
				(driver_info >> 4) & 0x0f);
		if (driver_info & 0x0f)
			dev->out_pipe = usb_sndbulkpipe (udev,
				driver_info & 0x0f);
	}

	dev_set_drvdata (&intf->dev, dev);
	info ("bound to %s ...%s%s", dev->id,
			dev->out_pipe ? " writes" : "",
			dev->in_pipe ? " reads" : "");
	return 0;
}

static void usbtest_disconnect (struct usb_interface *intf)
{
	struct usbtest_dev	*dev = dev_get_drvdata (&intf->dev);

	dev_set_drvdata (&intf->dev, 0);
	info ("unbound %s", dev->id);
	kfree (intf->private_data);
}

/* Basic testing only needs a device that can source or sink bulk traffic.
 */
static struct usb_device_id id_table [] = {

	/* EZ-USB FX2 "bulksrc" or "bulkloop" firmware from Cypress
	 * reads disabled on this one, my version has some problem there
	 */
	{ USB_DEVICE (0x0547, 0x1002),
		.driver_info = EP_PAIR (0, 2),
		},
#if 1
	// this does not coexist with a real iBOT2 driver!
	// it makes a nice source of high speed bulk-in data
	{ USB_DEVICE (0x0b62, 0x0059),
		.driver_info = EP_PAIR (2, 0),
		},
#endif

	/* can that old "usbstress-0.3" firmware be used with this? */

	{ }
};
MODULE_DEVICE_TABLE (usb, id_table);

static struct usb_driver usbtest_driver = {
	.owner =	THIS_MODULE,
	.name =		"usbtest",
	.id_table =	id_table,
	.probe =	usbtest_probe,
	.ioctl =	usbtest_ioctl,
	.disconnect =	usbtest_disconnect,
};

/*-------------------------------------------------------------------------*/

static int __init usbtest_init (void)
{
	return usb_register (&usbtest_driver);
}
module_init (usbtest_init);

static void __exit usbtest_exit (void)
{
	usb_deregister (&usbtest_driver);
}
module_exit (usbtest_exit);

MODULE_DESCRIPTION ("USB HCD Testing Driver");
MODULE_LICENSE ("GPL");

