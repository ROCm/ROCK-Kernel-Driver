#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/module.h>
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

#define	GENERIC		/* let probe() bind using module params */

/* Some devices that can be used for testing will have "real" drivers.
 * Entries for those need to be enabled here by hand, after disabling
 * that "real" driver.
 */
//#define	IBOT2		/* grab iBOT2 webcams */
//#define	KEYSPAN_19Qi	/* grab un-renumerated serial adapter */

/*-------------------------------------------------------------------------*/

struct usbtest_info {
	const char		*name;
	u8			ep_in;		/* bulk/intr source */
	u8			ep_out;		/* bulk/intr sink */
	int			alt;
};

/* this is accessed only through usbfs ioctl calls.
 * one ioctl to issue a test ... no locking needed!!!
 * tests create other threads if they need them.
 * urbs and buffers are allocated dynamically,
 * and data generated deterministically.
 *
 * there's a minor complication on rmmod, since
 * usbfs.disconnect() waits till our ioctl completes.
 * unplug works fine since we'll see real i/o errors.
 */
struct usbtest_dev {
	struct usb_interface	*intf;
	struct usbtest_info	*info;
	char			id [32];
	int			in_pipe;
	int			out_pipe;

#define TBUF_SIZE	256
	u8			*buf;
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
	if (usb_pipein (pipe))
		urb->transfer_flags |= URB_SHORT_NOT_OK;
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

/* unqueued control message testing
 *
 * there's a nice set of device functional requirements in chapter 9 of the
 * usb 2.0 spec, which we can apply to ANY device, even ones that don't use
 * special test firmware.
 *
 * we know the device is configured (or suspended) by the time it's visible
 * through usbfs.  we can't change that, so we won't test enumeration (which
 * worked 'well enough' to get here, this time), power management (ditto),
 * or remote wakeup (which needs human interaction).
 */

static int get_altsetting (struct usbtest_dev *dev)
{
	struct usb_interface	*iface = dev->intf;
	struct usb_device	*udev = interface_to_usbdev (iface);
	int			retval;

	retval = usb_control_msg (udev, usb_rcvctrlpipe (udev, 0),
			USB_REQ_GET_INTERFACE, USB_DIR_IN|USB_RECIP_INTERFACE,
			0, iface->altsetting [0].bInterfaceNumber,
			dev->buf, 1, HZ * USB_CTRL_GET_TIMEOUT);
	switch (retval) {
	case 1:
		return dev->buf [0];
	case 0:
		retval = -ERANGE;
		// FALLTHROUGH
	default:
		return retval;
	}
}

/* this is usb_set_interface(), with no 'only one altsetting' case */
static int set_altsetting (struct usbtest_dev *dev, int alternate)
{
	struct usb_interface		*iface = dev->intf;
	struct usb_device		*udev;
	struct usb_interface_descriptor	*iface_as;
	int				i, ret;

	if (alternate < 0 || alternate >= iface->num_altsetting)
		return -EINVAL;

	udev = interface_to_usbdev (iface);
	if ((ret = usb_control_msg (udev, usb_sndctrlpipe (udev, 0),
			USB_REQ_SET_INTERFACE, USB_RECIP_INTERFACE,
			iface->altsetting [alternate].bAlternateSetting,
			iface->altsetting [alternate].bInterfaceNumber,
			NULL, 0, HZ * USB_CTRL_SET_TIMEOUT)) < 0)
		return ret;

	// FIXME usbcore should be more like this:
	// - remove that special casing in usbcore.
	// - fix usbcore signature to take interface

	/* prevent requests using previous endpoint settings */
	iface_as = iface->altsetting + iface->act_altsetting;
	for (i = 0; i < iface_as->bNumEndpoints; i++) {
		u8	ep = iface_as->endpoint [i].bEndpointAddress;
		int	out = !(ep & USB_DIR_IN);

		ep &= USB_ENDPOINT_NUMBER_MASK;
		(out ? udev->epmaxpacketout : udev->epmaxpacketin ) [ep] = 0;
		// FIXME want hcd hook here, "forget this endpoint"
	}
	iface->act_altsetting = alternate;

	/* reset toggles and maxpacket for all endpoints affected */
	iface_as = iface->altsetting + iface->act_altsetting;
	for (i = 0; i < iface_as->bNumEndpoints; i++) {
		u8	ep = iface_as->endpoint [i].bEndpointAddress;
		int	out = !(ep & USB_DIR_IN);

		ep &= USB_ENDPOINT_NUMBER_MASK;
		usb_settoggle (udev, ep, out, 0);
		(out ? udev->epmaxpacketout : udev->epmaxpacketin ) [ep]
			= iface_as->endpoint [ep].wMaxPacketSize;
	}

	return 0;
}

static int is_good_config (char *buf, int len)
{
	struct usb_config_descriptor	*config;
	
	if (len < sizeof *config)
		return 0;
	config = (struct usb_config_descriptor *) buf;

	switch (config->bDescriptorType) {
	case USB_DT_CONFIG:
	case USB_DT_OTHER_SPEED_CONFIG:
		if (config->bLength != 9)
			return 0;
#if 0
		/* this bit 'must be 1' but often isn't */
		if (!(config->bmAttributes & 0x80)) {
			dbg ("high bit of config attributes not set");
			return 0;
		}
#endif
		if (config->bmAttributes & 0x1f)	/* reserved == 0 */
			return 0;
		break;
	default:
		return 0;
	}

	le32_to_cpus (&config->wTotalLength);
	if (config->wTotalLength == len)		/* read it all */
		return 1;
	return config->wTotalLength >= TBUF_SIZE;	/* max partial read */
}

/* sanity test for standard requests working with usb_control_mesg() and some
 * of the utility functions which use it.
 *
 * this doesn't test how endpoint halts behave or data toggles get set, since
 * we won't do I/O to bulk/interrupt endpoints here (which is how to change
 * halt or toggle).  toggle testing is impractical without support from hcds.
 *
 * this avoids failing devices linux would normally work with, by not testing
 * config/altsetting operations for devices that only support their defaults.
 * such devices rarely support those needless operations.
 *
 * NOTE that since this is a sanity test, it's not examining boundary cases
 * to see if usbcore, hcd, and device all behave right.  such testing would
 * involve varied read sizes and other operation sequences.
 */
static int ch9_postconfig (struct usbtest_dev *dev)
{
	struct usb_interface	*iface = dev->intf;
	struct usb_device	*udev = interface_to_usbdev (iface);
	int			i, retval;

	/* [9.2.3] if there's more than one altsetting, we need to be able to
	 * set and get each one.  mostly trusts the descriptors from usbcore.
	 */
	for (i = 0; i < iface->num_altsetting; i++) {

		/* 9.2.3 constrains the range here, and Linux ensures
		 * they're ordered meaningfully in this array
		 */
		if (iface->altsetting [i].bAlternateSetting != i) {
			dbg ("%s, illegal alt [%d].bAltSetting = %d",
					dev->id, i, 
					iface->altsetting [i]
						.bAlternateSetting);
			return -EDOM;
		}

		/* [real world] get/set unimplemented if there's only one */
		if (iface->num_altsetting == 1)
			continue;

		/* [9.4.10] set_interface */
		retval = set_altsetting (dev, i);
		if (retval) {
			dbg ("%s can't set_interface = %d, %d",
					dev->id, i, retval);
			return retval;
		}

		/* [9.4.4] get_interface always works */
		retval = get_altsetting (dev);
		if (retval != i) {
			dbg ("%s get alt should be %d, was %d",
					dev->id, i, retval);
			return (retval < 0) ? retval : -EDOM;
		}

	}

	/* [real world] get_config unimplemented if there's only one */
	if (udev->descriptor.bNumConfigurations != 1) {
		int	expected = udev->actconfig->bConfigurationValue;

		/* [9.4.2] get_configuration always works */
		retval = usb_control_msg (udev, usb_rcvctrlpipe (udev, 0),
				USB_REQ_GET_CONFIGURATION, USB_RECIP_DEVICE,
				0, 0, dev->buf, 1, HZ * USB_CTRL_GET_TIMEOUT);
		if (retval != 1 || dev->buf [0] != expected) {
			dbg ("%s get config --> %d (%d)", dev->id, retval,
				expected);
			return (retval < 0) ? retval : -EDOM;
		}
	}

	/* there's always [9.4.3] a device descriptor [9.6.1] */
	retval = usb_get_descriptor (udev, USB_DT_DEVICE, 0,
			dev->buf, sizeof udev->descriptor);
	if (retval != sizeof udev->descriptor) {
		dbg ("%s dev descriptor --> %d", dev->id, retval);
		return (retval < 0) ? retval : -EDOM;
	}

	/* there's always [9.4.3] at least one config descriptor [9.6.3] */
	for (i = 0; i < udev->descriptor.bNumConfigurations; i++) {
		retval = usb_get_descriptor (udev, USB_DT_CONFIG, i,
				dev->buf, TBUF_SIZE);
		if (!is_good_config (dev->buf, retval)) {
			dbg ("%s config [%d] descriptor --> %d",
					dev->id, i, retval);
			return (retval < 0) ? retval : -EDOM;
		}

		// FIXME cross-checking udev->config[i] to make sure usbcore
		// parsed it right (etc) would be good testing paranoia
	}

	/* and sometimes [9.2.6.6] speed dependent descriptors */
	if (udev->descriptor.bcdUSB == 0x0200) {	/* pre-swapped */
		struct usb_qualifier_descriptor		*d = 0;

		/* device qualifier [9.6.2] */
		retval = usb_get_descriptor (udev,
				USB_DT_DEVICE_QUALIFIER, 0, dev->buf,
				sizeof (struct usb_qualifier_descriptor));
		if (retval == -EPIPE) {
			if (udev->speed == USB_SPEED_HIGH) {
				dbg ("%s hs dev qualifier --> %d",
						dev->id, retval);
				return (retval < 0) ? retval : -EDOM;
			}
			/* usb2.0 but not high-speed capable; fine */
		} else if (retval != sizeof (struct usb_qualifier_descriptor)) {
			dbg ("%s dev qualifier --> %d", dev->id, retval);
			return (retval < 0) ? retval : -EDOM;
		} else
			d = (struct usb_qualifier_descriptor *) dev->buf;

		/* might not have [9.6.2] any other-speed configs [9.6.4] */
		if (d) {
			unsigned max = d->bNumConfigurations;
			for (i = 0; i < max; i++) {
				retval = usb_get_descriptor (udev,
					USB_DT_OTHER_SPEED_CONFIG, i,
					dev->buf, TBUF_SIZE);
				if (!is_good_config (dev->buf, retval)) {
					dbg ("%s other speed config --> %d",
							dev->id, retval);
					return (retval < 0) ? retval : -EDOM;
				}
			}
		}
	}
	// FIXME fetch strings from at least the device descriptor

	/* [9.4.5] get_status always works */
	retval = usb_get_status (udev, USB_RECIP_DEVICE, 0, dev->buf);
	if (retval != 2) {
		dbg ("%s get dev status --> %d", dev->id, retval);
		return (retval < 0) ? retval : -EDOM;
	}

	// FIXME configuration.bmAttributes says if we could try to set/clear
	// the device's remote wakeup feature ... if we can, test that here

	retval = usb_get_status (udev, USB_RECIP_INTERFACE,
			iface->altsetting [0].bInterfaceNumber, dev->buf);
	if (retval != 2) {
		dbg ("%s get interface status --> %d", dev->id, retval);
		return (retval < 0) ? retval : -EDOM;
	}
	// FIXME get status for each endpoint in the interface
	
	return 0;
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
	unsigned		i;

	// FIXME USBDEVFS_CONNECTINFO doesn't say how fast the device is.

	if (code != USBTEST_REQUEST)
		return -EOPNOTSUPP;

	if (param->iterations <= 0 || param->length < 0
			|| param->sglen < 0 || param->vary < 0)
		return -EINVAL;

	/* some devices, like ez-usb default devices, need a non-default
	 * altsetting to have any active endpoints.  some tests change
	 * altsettings; force a default so most tests don't need to check.
	 */
	if (dev->info->alt >= 0) {
	    	int	res;

		if (intf->altsetting->bInterfaceNumber)
			return -ENODEV;
		res = set_altsetting (dev, dev->info->alt);
		if (res) {
			err ("%s: set altsetting to %d failed, %d",
					dev->id, dev->info->alt, res);
			return res;
		}
	}

	/*
	 * Just a bunch of test cases that every HCD is expected to handle.
	 *
	 * Some may need specific firmware, though it'd be good to have
	 * one firmware image to handle all the test cases.
	 *
	 * FIXME add more tests!  cancel requests, verify the data, control
	 * queueing, concurrent read+write threads, and so on.
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
		dbg ("%s TEST 4:  read/%d 0..%d bytes %u times", dev->id,
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

	/* non-queued sanity tests for control (chapter 9 subset) */
	case 9:
		retval = 0;
		dbg ("%s TEST 9:  ch9 (subset) control tests, %d times",
				dev->id, param->iterations);
		for (i = param->iterations; retval == 0 && i--; /* NOP */)
			retval = ch9_postconfig (dev);
		if (retval)
			dbg ("ch9 subset failed, iterations left %d", i);
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

static int force_interrupt = 0;
MODULE_PARM (force_interrupt, "i");
MODULE_PARM_DESC (force_interrupt, "0 = test bulk (default), else interrupt");

#ifdef	GENERIC
static int vendor;
MODULE_PARM (vendor, "h");
MODULE_PARM_DESC (vendor, "vendor code (from usb-if)");

static int product;
MODULE_PARM (product, "h");
MODULE_PARM_DESC (product, "product code (from vendor)");
#endif

static int
usbtest_probe (struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device	*udev;
	struct usbtest_dev	*dev;
	struct usbtest_info	*info;
	char			*rtest, *wtest;

	udev = interface_to_usbdev (intf);

#ifdef	GENERIC
	/* specify devices by module parameters? */
	if (id->match_flags == 0) {
		/* vendor match required, product match optional */
		if (!vendor || udev->descriptor.idVendor != (u16)vendor)
			return -ENODEV;
		if (product && udev->descriptor.idProduct != (u16)product)
			return -ENODEV;
		dbg ("matched module params, vend=0x%04x prod=0x%04x",
				udev->descriptor.idVendor,
				udev->descriptor.idProduct);
	}
#endif

	dev = kmalloc (sizeof *dev, SLAB_KERNEL);
	if (!dev)
		return -ENOMEM;
	memset (dev, 0, sizeof *dev);
	info = (struct usbtest_info *) id->driver_info;
	dev->info = info;

	/* use the same kind of id the hid driver shows */
	snprintf (dev->id, sizeof dev->id, "%s-%s:%d",
			udev->bus->bus_name, udev->devpath,
			intf->altsetting [0].bInterfaceNumber);
	dev->intf = intf;

	/* cacheline-aligned scratch for i/o */
	if ((dev->buf = kmalloc (TBUF_SIZE, SLAB_KERNEL)) == 0) {
		kfree (dev);
		return -ENOMEM;
	}

	/* NOTE this doesn't yet test the handful of difference that are
	 * visible with high speed interrupts:  bigger maxpacket (1K) and
	 * "high bandwidth" modes (up to 3 packets/uframe).
	 */
	rtest = wtest = "";
	if (force_interrupt || udev->speed == USB_SPEED_LOW) {
		if (info->ep_in) {
			dev->in_pipe = usb_rcvintpipe (udev, info->ep_in);
			rtest = " intr-in";
		}
		if (info->ep_out) {
			dev->out_pipe = usb_sndintpipe (udev, info->ep_out);
			wtest = " intr-out";
		}

#if 1
		// FIXME disabling this until we finally get rid of
		// interrupt "automagic" resubmission
		dbg ("%s:  no interrupt transfers for now", dev->id);
		kfree (dev);
		return -ENODEV;
#endif
	} else {
		if (info->ep_in) {
			dev->in_pipe = usb_rcvbulkpipe (udev, info->ep_in);
			rtest = " bulk-in";
		}
		if (info->ep_out) {
			dev->out_pipe = usb_sndbulkpipe (udev, info->ep_out);
			wtest = " bulk-out";
		}
	}

	dev_set_drvdata (&intf->dev, dev);
	info ("%s at %s ... %s speed {control%s%s} tests",
			info->name, dev->id,
			({ char *tmp;
			switch (udev->speed) {
			case USB_SPEED_LOW: tmp = "low"; break;
			case USB_SPEED_FULL: tmp = "full"; break;
			case USB_SPEED_HIGH: tmp = "high"; break;
			default: tmp = "unknown"; break;
			}; tmp; }), rtest, wtest);
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
 * Any device can test control transfers (default with GENERIC binding).
 *
 * Several entries work with the default EP0 implementation that's built
 * into EZ-USB chips.  There's a default vendor ID which can be overridden
 * by (very) small config EEPROMS, but otherwise all these devices act
 * identically until firmware is loaded:  only EP0 works.  It turns out
 * to be easy to make other endpoints work, without modifying that EP0
 * behavior.  For now, we expect that kind of firmware.
 */

/* an21xx or fx versions of ez-usb */
static struct usbtest_info ez1_info = {
	.name		= "EZ-USB device",
	.ep_in		= 2,
	.ep_out		= 2,
	.alt		= 1,
};

/* fx2 version of ez-usb */
static struct usbtest_info ez2_info = {
	.name		= "FX2 device",
	.ep_in		= 6,
	.ep_out		= 2,
	.alt		= 1,
};

#ifdef IBOT2
/* this is a nice source of high speed bulk data;
 * uses an FX2, with firmware provided in the device
 */
static struct usbtest_info ibot2_info = {
	.name		= "iBOT2 webcam",
	.ep_in		= 2,
	.alt		= -1,
};
#endif

#ifdef GENERIC
/* we can use any device to test control traffic */
static struct usbtest_info generic_info = {
	.name		= "Generic USB device",
	.alt		= -1,
};
#endif

// FIXME remove this 
static struct usbtest_info hact_info = {
	.name		= "FX2/hact",
	//.ep_in		= 6,
	.ep_out		= 2,
	.alt		= -1,
};


static struct usb_device_id id_table [] = {

	{ USB_DEVICE (0x0547, 0x1002),
		.driver_info = (unsigned long) &hact_info,
		},

	/*-------------------------------------------------------------*/

	/* EZ-USB devices which download firmware to replace (or in our
	 * case augment) the default device implementation.
	 */

	/* generic EZ-USB FX controller */
	{ USB_DEVICE (0x0547, 0x2235),
		.driver_info = (unsigned long) &ez1_info,
		},

	/* CY3671 development board with EZ-USB FX */
	{ USB_DEVICE (0x0547, 0x0080),
		.driver_info = (unsigned long) &ez1_info,
		},

	/* generic EZ-USB FX2 controller (or development board) */
	{ USB_DEVICE (0x04b4, 0x8613),
		.driver_info = (unsigned long) &ez2_info,
		},

#ifdef KEYSPAN_19Qi
	/* Keyspan 19qi uses an21xx (original EZ-USB) */
	// this does not coexist with the real Keyspan 19qi driver!
	{ USB_DEVICE (0x06cd, 0x010b),
		.driver_info = (unsigned long) &ez1_info,
		},
#endif

	/*-------------------------------------------------------------*/

#ifdef IBOT2
	/* iBOT2 makes a nice source of high speed bulk-in data */
	// this does not coexist with a real iBOT2 driver!
	{ USB_DEVICE (0x0b62, 0x0059),
		.driver_info = (unsigned long) &ibot2_info,
		},
#endif

	/*-------------------------------------------------------------*/

#ifdef GENERIC
	/* module params can specify devices to use for control tests */
	{ .driver_info = (unsigned long) &generic_info, },
#endif

	/*-------------------------------------------------------------*/

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
#ifdef GENERIC
	if (vendor)
		dbg ("params: vend=0x%04x prod=0x%04x", vendor, product);
#endif
	return usb_register (&usbtest_driver);
}
module_init (usbtest_init);

static void __exit usbtest_exit (void)
{
	usb_deregister (&usbtest_driver);
}
module_exit (usbtest_exit);

MODULE_DESCRIPTION ("USB Core/HCD Testing Driver");
MODULE_LICENSE ("GPL");

