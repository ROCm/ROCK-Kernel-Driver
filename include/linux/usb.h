#ifndef __LINUX_USB_H
#define __LINUX_USB_H

/* USB constants */

/*
 * Device and/or Interface Class codes
 */
#define USB_CLASS_PER_INTERFACE		0	/* for DeviceClass */
#define USB_CLASS_AUDIO			1
#define USB_CLASS_COMM			2
#define USB_CLASS_HID			3
#define USB_CLASS_PHYSICAL		5
#define USB_CLASS_STILL_IMAGE		6
#define USB_CLASS_PRINTER		7
#define USB_CLASS_MASS_STORAGE		8
#define USB_CLASS_HUB			9
#define USB_CLASS_CDC_DATA		0x0a
#define USB_CLASS_CSCID			0x0b	/* chip+ smart card */
#define USB_CLASS_CONTENT_SEC		0x0d	/* content security */
#define USB_CLASS_APP_SPEC		0xfe
#define USB_CLASS_VENDOR_SPEC		0xff

/*
 * USB types
 */
#define USB_TYPE_MASK			(0x03 << 5)
#define USB_TYPE_STANDARD		(0x00 << 5)
#define USB_TYPE_CLASS			(0x01 << 5)
#define USB_TYPE_VENDOR			(0x02 << 5)
#define USB_TYPE_RESERVED		(0x03 << 5)

/*
 * USB recipients
 */
#define USB_RECIP_MASK			0x1f
#define USB_RECIP_DEVICE		0x00
#define USB_RECIP_INTERFACE		0x01
#define USB_RECIP_ENDPOINT		0x02
#define USB_RECIP_OTHER			0x03

/*
 * USB directions
 */
#define USB_DIR_OUT			0		/* to device */
#define USB_DIR_IN			0x80		/* to host */

/*
 * Endpoints
 */
#define USB_ENDPOINT_NUMBER_MASK	0x0f	/* in bEndpointAddress */
#define USB_ENDPOINT_DIR_MASK		0x80

#define USB_ENDPOINT_XFERTYPE_MASK	0x03	/* in bmAttributes */
#define USB_ENDPOINT_XFER_CONTROL	0
#define USB_ENDPOINT_XFER_ISOC		1
#define USB_ENDPOINT_XFER_BULK		2
#define USB_ENDPOINT_XFER_INT		3

/*
 * USB Packet IDs (PIDs)
 */
#define USB_PID_UNDEF_0			0xf0
#define USB_PID_OUT			0xe1
#define USB_PID_ACK			0xd2
#define USB_PID_DATA0			0xc3
#define USB_PID_PING			0xb4	/* USB 2.0 */
#define USB_PID_SOF			0xa5
#define USB_PID_NYET			0x96	/* USB 2.0 */
#define USB_PID_DATA2			0x87	/* USB 2.0 */
#define USB_PID_SPLIT			0x78	/* USB 2.0 */
#define USB_PID_IN			0x69
#define USB_PID_NAK			0x5a
#define USB_PID_DATA1			0x4b
#define USB_PID_PREAMBLE		0x3c	/* Token mode */
#define USB_PID_ERR			0x3c	/* USB 2.0: handshake mode */
#define USB_PID_SETUP			0x2d
#define USB_PID_STALL			0x1e
#define USB_PID_MDATA			0x0f	/* USB 2.0 */

/*
 * Standard requests
 */
#define USB_REQ_GET_STATUS		0x00
#define USB_REQ_CLEAR_FEATURE		0x01
#define USB_REQ_SET_FEATURE		0x03
#define USB_REQ_SET_ADDRESS		0x05
#define USB_REQ_GET_DESCRIPTOR		0x06
#define USB_REQ_SET_DESCRIPTOR		0x07
#define USB_REQ_GET_CONFIGURATION	0x08
#define USB_REQ_SET_CONFIGURATION	0x09
#define USB_REQ_GET_INTERFACE		0x0A
#define USB_REQ_SET_INTERFACE		0x0B
#define USB_REQ_SYNCH_FRAME		0x0C

#define USB_MAJOR			180


#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/errno.h>        /* for -ENODEV */
#include <linux/delay.h>	/* for mdelay() */
#include <linux/interrupt.h>	/* for in_interrupt() */
#include <linux/list.h>		/* for struct list_head */
#include <linux/device.h>	/* for struct device */
#include <linux/fs.h>		/* for struct file_operations */


static __inline__ void wait_ms(unsigned int ms)
{
	if(!in_interrupt()) {
		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout(1 + ms * HZ / 1000);
	}
	else
		mdelay(ms);
}

/**
 * struct usb_ctrlrequest - structure used to make USB device control requests easier to create and decode
 * @bRequestType: matches the USB bmRequestType field
 * @bRequest: matches the USB bRequest field
 * @wValue: matches the USB wValue field
 * @wIndex: matches the USB wIndex field
 * @wLength: matches the USB wLength field
 *
 * This structure is used to send control requests to a USB device.  It matches
 * the different fields of the USB 2.0 Spec section 9.3, table 9-2.  See the
 * USB spec for a fuller description of the different fields, and what they are
 * used for.
 */
struct usb_ctrlrequest {
	__u8 bRequestType;
	__u8 bRequest;
	__u16 wValue;
	__u16 wIndex;
	__u16 wLength;
} __attribute__ ((packed));

/*
 * USB device number allocation bitmap. There's one bitmap
 * per USB tree.
 */
struct usb_devmap {
	unsigned long devicemap[128 / (8*sizeof(unsigned long))];
};

struct usb_device;

/*-------------------------------------------------------------------------*/

/*
 * Standard USB Descriptor support.
 * Devices may also have class-specific or vendor-specific descriptors.
 */

/*
 * Descriptor types ... USB 2.0 spec table 9.5
 */
#define USB_DT_DEVICE			0x01
#define USB_DT_CONFIG			0x02
#define USB_DT_STRING			0x03
#define USB_DT_INTERFACE		0x04
#define USB_DT_ENDPOINT			0x05
#define USB_DT_DEVICE_QUALIFIER		0x06
#define USB_DT_OTHER_SPEED_CONFIG	0x07
#define USB_DT_INTERFACE_POWER		0x08

// FIXME should be internal to hub driver
#define USB_DT_HUB			(USB_TYPE_CLASS | 0x09)
#define USB_DT_HUB_NONVAR_SIZE		7

/*
 * Descriptor sizes per descriptor type
 */
#define USB_DT_DEVICE_SIZE		18
#define USB_DT_CONFIG_SIZE		9
#define USB_DT_INTERFACE_SIZE		9
#define USB_DT_ENDPOINT_SIZE		7
#define USB_DT_ENDPOINT_AUDIO_SIZE	9	/* Audio extension */

/* most of these maximums are arbitrary */
#define USB_MAXCONFIG			8
#define USB_ALTSETTINGALLOC		4
#define USB_MAXALTSETTING		128	/* Hard limit */
#define USB_MAXINTERFACES		32
#define USB_MAXENDPOINTS		32	/* Hard limit */

/* All standard descriptors have these 2 fields in common */
struct usb_descriptor_header {
	__u8  bLength;
	__u8  bDescriptorType;
} __attribute__ ((packed));

/* USB_DT_DEVICE: Device descriptor */
struct usb_device_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u16 bcdUSB;
	__u8  bDeviceClass;
	__u8  bDeviceSubClass;
	__u8  bDeviceProtocol;
	__u8  bMaxPacketSize0;
	__u16 idVendor;
	__u16 idProduct;
	__u16 bcdDevice;
	__u8  iManufacturer;
	__u8  iProduct;
	__u8  iSerialNumber;
	__u8  bNumConfigurations;
} __attribute__ ((packed));

/* USB_DT_ENDPOINT: Endpoint descriptor */
struct usb_endpoint_descriptor {
	__u8  bLength		__attribute__ ((packed));
	__u8  bDescriptorType	__attribute__ ((packed));
	__u8  bEndpointAddress	__attribute__ ((packed));
	__u8  bmAttributes	__attribute__ ((packed));
	__u16 wMaxPacketSize	__attribute__ ((packed));
	__u8  bInterval		__attribute__ ((packed));
	__u8  bRefresh		__attribute__ ((packed));
	__u8  bSynchAddress	__attribute__ ((packed));

	/* the rest is internal to the Linux implementation */
	unsigned char *extra;   /* Extra descriptors */
	int extralen;
};

/* USB_DT_INTERFACE: Interface descriptor */
struct usb_interface_descriptor {
	__u8  bLength		__attribute__ ((packed));
	__u8  bDescriptorType	__attribute__ ((packed));
	__u8  bInterfaceNumber	__attribute__ ((packed));
	__u8  bAlternateSetting	__attribute__ ((packed));
	__u8  bNumEndpoints	__attribute__ ((packed));
	__u8  bInterfaceClass	__attribute__ ((packed));
	__u8  bInterfaceSubClass __attribute__ ((packed));
	__u8  bInterfaceProtocol __attribute__ ((packed));
	__u8  iInterface	__attribute__ ((packed));

	/* the rest is internal to the Linux implementation */
	struct usb_endpoint_descriptor *endpoint;

	unsigned char *extra;   /* Extra descriptors */
	int extralen;
};

struct usb_interface {
	struct usb_interface_descriptor *altsetting;

	int act_altsetting;		/* active alternate setting */
	int num_altsetting;		/* number of alternate settings */
	int max_altsetting;		/* total memory allocated */
 
	struct usb_driver *driver;	/* driver */
	struct device dev;		/* interface specific device info */
	void *private_data;
};

/* USB_DT_CONFIG: Configuration descriptor information.
 *
 * USB_DT_OTHER_SPEED_CONFIG is the same descriptor, except that the
 * descriptor type is different.  Highspeed-capable devices can look
 * different depending on what speed they're currently running.  Only
 * devices with a USB_DT_DEVICE_QUALIFIER have an OTHER_SPEED_CONFIG.
 */
struct usb_config_descriptor {
	__u8  bLength		__attribute__ ((packed));
	__u8  bDescriptorType	__attribute__ ((packed));
	__u16 wTotalLength	__attribute__ ((packed));
	__u8  bNumInterfaces	__attribute__ ((packed));
	__u8  bConfigurationValue __attribute__ ((packed));
	__u8  iConfiguration	__attribute__ ((packed));
	__u8  bmAttributes	__attribute__ ((packed));
	__u8  MaxPower		__attribute__ ((packed));

	/* the rest is internal to the Linux implementation */
	struct usb_interface *interface;

	unsigned char *extra;   /* Extra descriptors */
	int extralen;
};

/* USB_DT_STRING: String descriptor */
struct usb_string_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u16 wData[1];		/* UTF-16LE encoded */
} __attribute__ ((packed));

/* USB_DT_DEVICE_QUALIFIER: Device Qualifier descriptor */
struct usb_qualifier_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u16 bcdUSB;
	__u8  bDeviceClass;
	__u8  bDeviceSubClass;
	__u8  bDeviceProtocol;
	__u8  bMaxPacketSize0;
	__u8  bNumConfigurations;
	__u8  bRESERVED;
} __attribute__ ((packed));

/* helpers for driver access to descriptors */
extern int usb_ifnum_to_ifpos(struct usb_device *dev, unsigned ifnum);
extern struct usb_interface *
	usb_ifnum_to_if(struct usb_device *dev, unsigned ifnum);
extern struct usb_endpoint_descriptor *
	usb_epnum_to_ep_desc(struct usb_device *dev, unsigned epnum);

int __usb_get_extra_descriptor(char *buffer, unsigned size,
	unsigned char type, void **ptr);
#define usb_get_extra_descriptor(ifpoint,type,ptr)\
	__usb_get_extra_descriptor((ifpoint)->extra,(ifpoint)->extralen,\
		type,(void**)ptr)

/* -------------------------------------------------------------------------- */

/* Host Controller Driver (HCD) support */

struct usb_operations;

#define DEVNUM_ROUND_ROBIN	/***** OPTION *****/

/*
 * Allocated per bus we have
 */
struct usb_bus {
	int busnum;			/* Bus number (in order of reg) */
	char *bus_name;			/* stable id (PCI slot_name etc) */

#ifdef DEVNUM_ROUND_ROBIN
	int devnum_next;		/* Next open device number in round-robin allocation */
#endif /* DEVNUM_ROUND_ROBIN */

	struct usb_devmap devmap;	/* device address allocation map */
	struct usb_operations *op;	/* Operations (specific to the HC) */
	struct usb_device *root_hub;	/* Root hub */
	struct list_head bus_list;	/* list of busses */
	void *hcpriv;                   /* Host Controller private data */

	int bandwidth_allocated;	/* on this bus: how much of the time
					 * reserved for periodic (intr/iso)
					 * requests is used, on average?
					 * Units: microseconds/frame.
					 * Limits: Full/low speed reserve 90%,
					 * while high speed reserves 80%.
					 */
	int bandwidth_int_reqs;		/* number of Interrupt requests */
	int bandwidth_isoc_reqs;	/* number of Isoc. requests */

	struct dentry *dentry;		/* usbfs dentry entry for the bus */

	atomic_t refcnt;
};

// FIXME:  root_hub_string vanishes when "usb_hcd" conversion is done,
// along with pre-hcd versions of the OHCI and UHCI drivers.
extern int usb_root_hub_string(int id, int serial,
		char *type, __u8 *data, int len);

/* -------------------------------------------------------------------------- */

/* This is arbitrary.
 * From USB 2.0 spec Table 11-13, offset 7, a hub can
 * have up to 255 ports. The most yet reported is 10.
 */
#define USB_MAXCHILDREN		(16)

struct usb_tt;

struct usb_device {
	int		devnum;		/* Address on USB bus */
	char		devpath [16];	/* Use in messages: /port/port/... */

	enum {
		USB_SPEED_UNKNOWN = 0,			/* enumerating */
		USB_SPEED_LOW, USB_SPEED_FULL,		/* usb 1.1 */
		USB_SPEED_HIGH				/* usb 2.0 */
	} speed;

	struct usb_tt	*tt; 		/* low/full speed dev, highspeed hub */
	int		ttport;		/* device port on that tt hub */

	atomic_t refcnt;		/* Reference count */
	struct semaphore serialize;

	unsigned int toggle[2];		/* one bit for each endpoint ([0] = IN, [1] = OUT) */
	unsigned int halted[2];		/* endpoint halts; one bit per endpoint # & direction; */
					/* [0] = IN, [1] = OUT */
	int epmaxpacketin[16];		/* INput endpoint specific maximums */
	int epmaxpacketout[16];		/* OUTput endpoint specific maximums */

	struct usb_device *parent;	/* our hub, unless we're the root */
	struct usb_bus *bus;		/* Bus we're part of */

	struct device dev;		/* Generic device interface */

	struct usb_device_descriptor descriptor;/* Descriptor */
	struct usb_config_descriptor *config;	/* All of the configs */
	struct usb_config_descriptor *actconfig;/* the active configuration */

	char **rawdescriptors;		/* Raw descriptors for each config */

	int have_langid;		/* whether string_langid is valid yet */
	int string_langid;		/* language ID for strings */

	void *hcpriv;			/* Host Controller private data */
	
	struct list_head filelist;
	struct dentry *dentry;		/* usbfs dentry entry for the device */

	/*
	 * Child devices - these can be either new devices
	 * (if this is a hub device), or different instances
	 * of this same device.
	 *
	 * Each instance needs its own set of data structures.
	 */

	int maxchild;			/* Number of ports if hub */
	struct usb_device *children[USB_MAXCHILDREN];
};

extern struct usb_device *usb_alloc_dev(struct usb_device *parent, struct usb_bus *);
extern struct usb_device *usb_get_dev(struct usb_device *dev);
extern void usb_free_dev(struct usb_device *);
#define usb_put_dev usb_free_dev

/* for when layers above USB add new non-USB drivers */
extern void usb_scan_devices(void);

/* mostly for devices emulating SCSI over USB */
extern int usb_reset_device(struct usb_device *dev);

/* for drivers using iso endpoints */
extern int usb_get_current_frame_number (struct usb_device *usb_dev);

/* used these for multi-interface device registration */
extern int usb_find_interface_driver_for_ifnum(struct usb_device *dev, unsigned int ifnum);
extern void usb_driver_claim_interface(struct usb_driver *driver,
			struct usb_interface *iface, void* priv);
extern int usb_interface_claimed(struct usb_interface *iface);
extern void usb_driver_release_interface(struct usb_driver *driver,
			struct usb_interface *iface);
const struct usb_device_id *usb_match_id(struct usb_device *dev,
					 struct usb_interface *interface,
					 const struct usb_device_id *id);

/**
 * usb_make_path - returns stable device path in the usb tree
 * @dev: the device whose path is being constructed
 * @buf: where to put the string
 * @size: how big is "buf"?
 *
 * Returns length of the string (> 0) or negative if size was too small.
 *
 * This identifier is intended to be "stable", reflecting physical paths in
 * hardware such as physical bus addresses for host controllers or ports on
 * USB hubs.  That makes it stay the same until systems are physically
 * reconfigured, by re-cabling a tree of USB devices or by moving USB host
 * controllers.  Adding and removing devices, including virtual root hubs
 * in host controller driver modules, does not change these path identifers;
 * neither does rebooting or re-enumerating.  These are more useful identifiers
 * than changeable ("unstable") ones like bus numbers or device addresses.
 *
 * With a partial exception for devices connected to USB 2.0 root hubs, these
 * identifiers are also predictable.  So long as the device tree isn't changed,
 * plugging any USB device into a given hub port always gives it the same path.
 * Because of the use of "companion" controllers, devices connected to ports on
 * USB 2.0 root hubs (EHCI host controllers) will get one path ID if they are
 * high speed, and a different one if they are full or low speed.
 */
static inline int usb_make_path (struct usb_device *dev, char *buf, size_t size)
{
	int actual;
	actual = snprintf (buf, size, "usb-%s-%s", dev->bus->bus_name, dev->devpath);
	return (actual >= size) ? -1 : actual;
}

/*-------------------------------------------------------------------------*/

/*
 * Device table entry for "new style" table-driven USB drivers.
 * User mode code can read these tables to choose which modules to load.
 * Declare the table as a MODULE_DEVICE_TABLE.
 *
 * The third probe() parameter will point to a matching entry from this
 * table.  (Null value reserved.)  Use the driver_data field for each
 * match to hold information tied to that match:  device quirks, etc.
 *
 * Terminate the driver's table with an all-zeroes entry.
 * Use the flag values to control which fields are compared.
 */

/**
 * struct usb_device_id - identifies USB devices for probing and hotplugging
 * @match_flags: Bit mask controlling of the other fields are used to match
 *	against new devices.  Any field except for driver_info may be used,
 *	although some only make sense in conjunction with other fields.
 *	This is usually set by a USB_DEVICE_*() macro, which sets all
 *	other fields in this structure except for driver_info.
 * @idVendor: USB vendor ID for a device; numbers are assigned
 *	by the USB forum to its members.
 * @idProduct: Vendor-assigned product ID.
 * @bcdDevice_lo: Low end of range of vendor-assigned product version numbers.
 *	This is also used to identify individual product versions, for
 *	a range consisting of a single device.
 * @bcdDevice_hi: High end of version number range.  The range of product
 *	versions is inclusive.
 * @bDeviceClass: Class of device; numbers are assigned
 *	by the USB forum.  Products may choose to implement classes,
 *	or be vendor-specific.  Device classes specify behavior of all
 *	the interfaces on a devices.
 * @bDeviceSubClass: Subclass of device; associated with bDeviceClass.
 * @bDeviceProtocol: Protocol of device; associated with bDeviceClass.
 * @bInterfaceClass: Class of interface; numbers are assigned
 *	by the USB forum.  Products may choose to implement classes,
 *	or be vendor-specific.  Interface classes specify behavior only
 *	of a given interface; other interfaces may support other classes.
 * @bInterfaceSubClass: Subclass of interface; associated with bInterfaceClass.
 * @bInterfaceProtocol: Protocol of interface; associated with bInterfaceClass.
 * @driver_info: Holds information used by the driver.  Usually it holds
 *	a pointer to a descriptor understood by the driver, or perhaps
 *	device flags.
 *
 * In most cases, drivers will create a table of device IDs by using
 * USB_DEVICE(), or similar macros designed for that purpose.
 * They will then export it to userspace using MODULE_DEVICE_TABLE(),
 * and provide it to the USB core through their usb_driver structure.
 *
 * See the usb_match_id() function for information about how matches are
 * performed.  Briefly, you will normally use one of several macros to help
 * construct these entries.  Each entry you provide will either identify
 * one or more specific products, or will identify a class of products
 * which have agreed to behave the same.  You should put the more specific
 * matches towards the beginning of your table, so that driver_info can
 * record quirks of specific products.
 */
struct usb_device_id {
	/* which fields to match against? */
	__u16		match_flags;

	/* Used for product specific matches; range is inclusive */
	__u16		idVendor;
	__u16		idProduct;
	__u16		bcdDevice_lo;
	__u16		bcdDevice_hi;

	/* Used for device class matches */
	__u8		bDeviceClass;
	__u8		bDeviceSubClass;
	__u8		bDeviceProtocol;

	/* Used for interface class matches */
	__u8		bInterfaceClass;
	__u8		bInterfaceSubClass;
	__u8		bInterfaceProtocol;

	/* not matched against */
	unsigned long	driver_info;
};

/* Some useful macros to use to create struct usb_device_id */
#define USB_DEVICE_ID_MATCH_VENDOR		0x0001
#define USB_DEVICE_ID_MATCH_PRODUCT		0x0002
#define USB_DEVICE_ID_MATCH_DEV_LO		0x0004
#define USB_DEVICE_ID_MATCH_DEV_HI		0x0008
#define USB_DEVICE_ID_MATCH_DEV_CLASS		0x0010
#define USB_DEVICE_ID_MATCH_DEV_SUBCLASS	0x0020
#define USB_DEVICE_ID_MATCH_DEV_PROTOCOL	0x0040
#define USB_DEVICE_ID_MATCH_INT_CLASS		0x0080
#define USB_DEVICE_ID_MATCH_INT_SUBCLASS	0x0100
#define USB_DEVICE_ID_MATCH_INT_PROTOCOL	0x0200

#define USB_DEVICE_ID_MATCH_DEVICE		(USB_DEVICE_ID_MATCH_VENDOR | USB_DEVICE_ID_MATCH_PRODUCT)
#define USB_DEVICE_ID_MATCH_DEV_RANGE		(USB_DEVICE_ID_MATCH_DEV_LO | USB_DEVICE_ID_MATCH_DEV_HI)
#define USB_DEVICE_ID_MATCH_DEVICE_AND_VERSION	(USB_DEVICE_ID_MATCH_DEVICE | USB_DEVICE_ID_MATCH_DEV_RANGE)
#define USB_DEVICE_ID_MATCH_DEV_INFO \
	(USB_DEVICE_ID_MATCH_DEV_CLASS | USB_DEVICE_ID_MATCH_DEV_SUBCLASS | USB_DEVICE_ID_MATCH_DEV_PROTOCOL)
#define USB_DEVICE_ID_MATCH_INT_INFO \
	(USB_DEVICE_ID_MATCH_INT_CLASS | USB_DEVICE_ID_MATCH_INT_SUBCLASS | USB_DEVICE_ID_MATCH_INT_PROTOCOL)

/**
 * USB_DEVICE - macro used to describe a specific usb device
 * @vend: the 16 bit USB Vendor ID
 * @prod: the 16 bit USB Product ID
 *
 * This macro is used to create a struct usb_device_id that matches a
 * specific device.
 */
#define USB_DEVICE(vend,prod) \
	match_flags: USB_DEVICE_ID_MATCH_DEVICE, idVendor: (vend), idProduct: (prod)
/**
 * USB_DEVICE_VER - macro used to describe a specific usb device with a version range
 * @vend: the 16 bit USB Vendor ID
 * @prod: the 16 bit USB Product ID
 * @lo: the bcdDevice_lo value
 * @hi: the bcdDevice_hi value
 *
 * This macro is used to create a struct usb_device_id that matches a
 * specific device, with a version range.
 */
#define USB_DEVICE_VER(vend,prod,lo,hi) \
	match_flags: USB_DEVICE_ID_MATCH_DEVICE_AND_VERSION, idVendor: (vend), idProduct: (prod), bcdDevice_lo: (lo), bcdDevice_hi: (hi)

/**
 * USB_DEVICE_INFO - macro used to describe a class of usb devices
 * @cl: bDeviceClass value
 * @sc: bDeviceSubClass value
 * @pr: bDeviceProtocol value
 *
 * This macro is used to create a struct usb_device_id that matches a
 * specific class of devices.
 */
#define USB_DEVICE_INFO(cl,sc,pr) \
	match_flags: USB_DEVICE_ID_MATCH_DEV_INFO, bDeviceClass: (cl), bDeviceSubClass: (sc), bDeviceProtocol: (pr)

/**
 * USB_INTERFACE_INFO - macro used to describe a class of usb interfaces 
 * @cl: bInterfaceClass value
 * @sc: bInterfaceSubClass value
 * @pr: bInterfaceProtocol value
 *
 * This macro is used to create a struct usb_device_id that matches a
 * specific class of interfaces.
 */
#define USB_INTERFACE_INFO(cl,sc,pr) \
	match_flags: USB_DEVICE_ID_MATCH_INT_INFO, bInterfaceClass: (cl), bInterfaceSubClass: (sc), bInterfaceProtocol: (pr)

/* -------------------------------------------------------------------------- */

/**
 * struct usb_driver - identifies USB driver to usbcore
 * @owner: Pointer to the module owner of this driver; initialize
 *	it using THIS_MODULE.
 * @name: The driver name should be unique among USB drivers,
 *	and should normally be the same as the module name.
 * @probe: Called to see if the driver is willing to manage a particular
 *	interface on a device.  The probe routine returns a handle that 
 *	will later be provided to disconnect(), or a null pointer to
 *	indicate that the driver will not handle the interface.
 *	The handle is normally a pointer to driver-specific data.
 *	If the probe() routine needs to access the interface
 *	structure itself, use usb_ifnum_to_if() to make sure it's using
 *	the right one.
 * @disconnect: Called when the interface is no longer accessible, usually
 *	because its device has been (or is being) disconnected.  The
 *	handle passed is what was returned by probe(), or was provided
 *	to usb_driver_claim_interface().
 * @ioctl: Used for drivers that want to talk to userspace through
 *	the "usbfs" filesystem.  This lets devices provide ways to
 *	expose information to user space regardless of where they
 *	do (or don't) show up otherwise in the filesystem.
 * @id_table: USB drivers use ID table to support hotplugging.
 *	Export this with MODULE_DEVICE_TABLE(usb,...), or use NULL to
 *	say that probe() should be called for any unclaimed interfce.
 *
 * USB drivers should provide a name, probe() and disconnect() methods,
 * and an id_table.  Other driver fields are optional.
 *
 * The id_table is used in hotplugging.  It holds a set of descriptors,
 * and specialized data may be associated with each entry.  That table
 * is used by both user and kernel mode hotplugging support.
 *
 * The probe() and disconnect() methods are called in a context where
 * they can sleep, but they should avoid abusing the privilege.  Most
 * work to connect to a device should be done when the device is opened,
 * and undone at the last close.  The disconnect code needs to address
 * concurrency issues with respect to open() and close() methods, as
 * well as forcing all pending I/O requests to complete (by unlinking
 * them as necessary, and blocking until the unlinks complete).
 */
struct usb_driver {
	struct module *owner;
	const char *name;

	void *(*probe)(
	    struct usb_device *dev,		/* the device */
	    unsigned intf,			/* what interface */
	    const struct usb_device_id *id	/* from id_table */
	    );
	void (*disconnect)(
	    struct usb_device *dev,		/* the device */
	    void *handle			/* as returned by probe() */
	    );

	struct list_head driver_list;
	struct semaphore serialize;

	/* ioctl -- userspace apps can talk to drivers through usbfs */
	int (*ioctl)(struct usb_device *dev, unsigned int code, void *buf);

	/* support for "new-style" USB hotplugging */
	const struct usb_device_id *id_table;

	/* suspend before the bus suspends;
	 * disconnect or resume when the bus resumes */
	/* void (*suspend)(struct usb_device *dev); */
	/* void (*resume)(struct usb_device *dev); */
};

extern struct bus_type usb_bus_type;

/*
 * use these in module_init()/module_exit()
 * and don't forget MODULE_DEVICE_TABLE(usb, ...)
 */
extern int usb_register(struct usb_driver *);
extern void usb_deregister(struct usb_driver *);

extern int usb_register_dev(struct file_operations *fops, int minor, int num_minors, int *start_minor);
extern void usb_deregister_dev(int num_minors, int start_minor);

/* -------------------------------------------------------------------------- */

/*
 * URB support, for asynchronous request completions
 */

/*
 * urb->transfer_flags:
 *
 * FIXME should _all_ be URB_* flags
 */
#define URB_SHORT_NOT_OK	0x0001	/* report short reads as errors */
#define USB_ISO_ASAP		0x0002	/* iso-only, urb->start_frame ignored */
#define USB_ASYNC_UNLINK	0x0008	/* usb_unlink_urb() returns asap */
#define USB_NO_FSBR		0x0020	/* UHCI-specific */
#define USB_ZERO_PACKET		0x0040	/* Finish bulk OUTs with short packet */
#define URB_NO_INTERRUPT	0x0080	/* HINT: no non-error interrupt needed */
#define USB_TIMEOUT_KILLED	0x1000	/* only set by HCD! */

struct usb_iso_packet_descriptor {
	unsigned int offset;
	unsigned int length;		/* expected length */
	unsigned int actual_length;
	unsigned int status;
};

struct urb;

typedef void (*usb_complete_t)(struct urb *);

/**
 * struct urb - USB Request Block
 * @urb_list: For use by current owner of the URB.
 * @pipe: Holds endpoint number, direction, type, and max packet size.
 *	Create these values with the eight macros available;
 *	usb_{snd,rcv}TYPEpipe(dev,endpoint), where the type is "ctrl"
 *	(control), "bulk", "int" (interrupt), or "iso" (isochronous).
 *	For example usb_sndbulkpipe() or usb_rcvintpipe().  Endpoint
 *	numbers range from zero to fifteen.  Note that "in" endpoint two
 *	is a different endpoint (and pipe) from "out" endpoint two.
 *	The current configuration controls the existence, type, and
 *	maximum packet size of any given endpoint.
 * @dev: Identifies the USB device to perform the request.
 * @status: This is read in non-iso completion functions to get the
 *	status of the particular request.  ISO requests only use it
 *	to tell whether the URB was unlinked; detailed status for
 *	each frame is in the fields of the iso_frame-desc.
 * @transfer_flags: A variety of flags may be used to affect how URB
 *	submission, unlinking, or operation are handled.  Different
 *	kinds of URB can use different flags.
 * @transfer_buffer: For non-iso transfers, this identifies the buffer
 *	to (or from) which the I/O request will be performed.  This
 *	buffer must be suitable for DMA; allocate it with kmalloc()
 *	or equivalent.  For transfers to "in" endpoints, contents of
 *	this buffer will be modified.
 * @transfer_buffer_length: How big is transfer_buffer.  The transfer may
 *	be broken up into chunks according to the current maximum packet
 *	size for the endpoint, which is a function of the configuration
 *	and is encoded in the pipe.
 * @actual_length: This is read in non-iso completion functions, and
 *	it tells how many bytes (out of transfer_buffer_length) were
 *	transferred.  It will normally be the same as requested, unless
 *	either an error was reported or a short read was performed.
 *	The URB_SHORT_NOT_OK transfer flag may be used to make such
 *	short reads be reported as errors. 
 * @setup_packet: Only used for control transfers, this points to eight bytes
 *	of setup data.  Control transfers always start by sending this data
 *	to the device.  Then transfer_buffer is read or written, if needed.
 * @start_frame: Returns the initial frame for interrupt or isochronous
 *	transfers.
 * @number_of_packets: Lists the number of ISO transfer buffers.
 * @interval: Specifies the polling interval for interrupt or isochronous
 *	transfers.  The units are frames (milliseconds) for for full and low
 *	speed devices, and microframes (1/8 millisecond) for highspeed ones.
 * @error_count: Returns the number of ISO transfers that reported errors.
 * @context: For use in completion functions.  This normally points to
 *	request-specific driver context.
 * @complete: Completion handler. This URB is passed as the parameter to the
 *	completion function.  Except for interrupt or isochronous transfers
 *	that aren't being unlinked, the completion function may then do what
 *	it likes with the URB, including resubmitting or freeing it.
 * @iso_frame_desc: Used to provide arrays of ISO transfer buffers and to 
 *	collect the transfer status for each buffer.
 *
 * This structure identifies USB transfer requests.  URBs must be allocated by
 * calling usb_alloc_urb() and freed with a call to usb_free_urb().
 * Initialization may be done using various usb_fill_*_urb() functions.  URBs
 * are submitted using usb_submit_urb(), and pending requests may be canceled
 * using usb_unlink_urb().
 *
 * Initialization:
 *
 * All URBs submitted must initialize dev, pipe,
 * transfer_flags (may be zero), complete, timeout (may be zero).
 * The USB_ASYNC_UNLINK transfer flag affects later invocations of
 * the usb_unlink_urb() routine.
 *
 * All non-isochronous URBs must also initialize 
 * transfer_buffer and transfer_buffer_length.  They may provide the
 * URB_SHORT_NOT_OK transfer flag, indicating that short reads are
 * to be treated as errors.
 *
 * Bulk URBs may
 * use the USB_ZERO_PACKET transfer flag, indicating that bulk OUT transfers
 * should always terminate with a short packet, even if it means adding an
 * extra zero length packet.
 *
 * Control URBs must provide a setup_packet.
 *
 * Interrupt UBS must provide an interval, saying how often (in milliseconds
 * or, for highspeed devices, 125 microsecond units)
 * to poll for transfers.  After the URB has been submitted, the interval
 * and start_frame fields reflect how the transfer was actually scheduled.
 * The polling interval may be more frequent than requested.
 * For example, some controllers have a maximum interval of 32 microseconds,
 * while others support intervals of up to 1024 microseconds.
 * Isochronous URBs also have transfer intervals.  (Note that for isochronous
 * endpoints, as well as high speed interrupt endpoints, the encoding of
 * the transfer interval in the endpoint descriptor is logarithmic.)
 *
 * Isochronous URBs normally use the USB_ISO_ASAP transfer flag, telling
 * the host controller to schedule the transfer as soon as bandwidth
 * utilization allows, and then set start_frame to reflect the actual frame
 * selected during submission.  Otherwise drivers must specify the start_frame
 * and handle the case where the transfer can't begin then.  However, drivers
 * won't know how bandwidth is currently allocated, and while they can
 * find the current frame using usb_get_current_frame_number () they can't
 * know the range for that frame number.  (Ranges for frame counter values
 * are HC-specific, and can go from 256 to 65536 frames from "now".)
 *
 * Isochronous URBs have a different data transfer model, in part because
 * the quality of service is only "best effort".  Callers provide specially
 * allocated URBs, with number_of_packets worth of iso_frame_desc structures
 * at the end.  Each such packet is an individual ISO transfer.  Isochronous
 * URBs are normally queued, submitted by drivers to arrange that
 * transfers are at least double buffered, and then explicitly resubmitted
 * in completion handlers, so
 * that data (such as audio or video) streams at as constant a rate as the
 * host controller scheduler can support.
 *
 * Completion Callbacks:
 *
 * The completion callback is made in_interrupt(), and one of the first
 * things that a completion handler should do is check the status field.
 * The status field is provided for all URBs.  It is used to report
 * unlinked URBs, and status for all non-ISO transfers.  It should not
 * be examined outside of the completion handler.
 *
 * The context field is normally used to link URBs back to the relevant
 * driver or request state.
 *
 * When completion callback is invoked for non-isochronous URBs, the
 * actual_length field tells how many bytes were transferred.
 *
 * For interrupt URBs, the URB provided to the callback
 * function is still "owned" by the USB core subsystem unless the status
 * indicates that the URB has been unlinked.  Completion handlers should
 * not modify such URBs until they have been unlinked.
 *
 * ISO transfer status is reported in the status and actual_length fields
 * of the iso_frame_desc array, and the number of errors is reported in
 * error_count.  Completion callbacks for ISO transfers will normally
 * (re)submit URBs to ensure a constant transfer rate.
 */
struct urb
{
	spinlock_t lock;		/* lock for the URB */
	atomic_t count;			/* reference count of the URB */
	void *hcpriv;			/* private data for host controller */
	struct list_head urb_list;	/* list pointer to all active urbs */
	struct usb_device *dev; 	/* (in) pointer to associated device */
	unsigned int pipe;		/* (in) pipe information */
	int status;			/* (return) non-ISO status */
	unsigned int transfer_flags;	/* (in) URB_SHORT_NOT_OK | ...*/
	void *transfer_buffer;		/* (in) associated data buffer */
	int transfer_buffer_length;	/* (in) data buffer length */
	int actual_length;		/* (return) actual transfer length */
	int bandwidth;			/* bandwidth for INT/ISO request */
	unsigned char *setup_packet;	/* (in) setup packet (control only) */
	int start_frame;		/* (modify) start frame (INT/ISO) */
	int number_of_packets;		/* (in) number of ISO packets */
	int interval;			/* (in) transfer interval (INT/ISO) */
	int error_count;		/* (return) number of ISO errors */
	int timeout;			/* (in) timeout, in jiffies */
	void *context;			/* (in) context for completion */
	usb_complete_t complete;	/* (in) completion routine */
	struct usb_iso_packet_descriptor iso_frame_desc[0];	/* (in) ISO ONLY */
};

/**
 * usb_fill_control_urb - initializes a control urb
 * @urb: pointer to the urb to initialize.
 * @dev: pointer to the struct usb_device for this urb.
 * @pipe: the endpoint pipe
 * @setup_packet: pointer to the setup_packet buffer
 * @transfer_buffer: pointer to the transfer buffer
 * @buffer_length: length of the transfer buffer
 * @complete: pointer to the usb_complete_t function
 * @context: what to set the urb context to.
 *
 * Initializes a control urb with the proper information needed to submit
 * it to a device.
 */
static inline void usb_fill_control_urb (struct urb *urb,
					 struct usb_device *dev,
					 unsigned int pipe,
					 unsigned char *setup_packet,
					 void *transfer_buffer,
					 int buffer_length,
					 usb_complete_t complete,
					 void *context)
{
	spin_lock_init(&urb->lock);
	urb->dev = dev;
	urb->pipe = pipe;
	urb->setup_packet = setup_packet;
	urb->transfer_buffer = transfer_buffer;
	urb->transfer_buffer_length = buffer_length;
	urb->complete = complete;
	urb->context = context;
}

/**
 * usb_fill_bulk_urb - macro to help initialize a bulk urb
 * @urb: pointer to the urb to initialize.
 * @dev: pointer to the struct usb_device for this urb.
 * @pipe: the endpoint pipe
 * @transfer_buffer: pointer to the transfer buffer
 * @buffer_length: length of the transfer buffer
 * @complete: pointer to the usb_complete_t function
 * @context: what to set the urb context to.
 *
 * Initializes a bulk urb with the proper information needed to submit it
 * to a device.
 */
static inline void usb_fill_bulk_urb (struct urb *urb,
				      struct usb_device *dev,
				      unsigned int pipe,
				      void *transfer_buffer,
				      int buffer_length,
				      usb_complete_t complete,
				      void *context)
{
	spin_lock_init(&urb->lock);
	urb->dev = dev;
	urb->pipe = pipe;
	urb->transfer_buffer = transfer_buffer;
	urb->transfer_buffer_length = buffer_length;
	urb->complete = complete;
	urb->context = context;
}

/**
 * usb_fill_int_urb - macro to help initialize a interrupt urb
 * @urb: pointer to the urb to initialize.
 * @dev: pointer to the struct usb_device for this urb.
 * @pipe: the endpoint pipe
 * @transfer_buffer: pointer to the transfer buffer
 * @buffer_length: length of the transfer buffer
 * @complete: pointer to the usb_complete_t function
 * @context: what to set the urb context to.
 * @interval: what to set the urb interval to, encoded like
 *	the endpoint descriptor's bInterval value.
 *
 * Initializes a interrupt urb with the proper information needed to submit
 * it to a device.
 * Note that high speed interrupt endpoints use a logarithmic encoding of
 * the endpoint interval, and express polling intervals in microframes
 * (eight per millisecond) rather than in frames (one per millisecond).
 */
static inline void usb_fill_int_urb (struct urb *urb,
				     struct usb_device *dev,
				     unsigned int pipe,
				     void *transfer_buffer,
				     int buffer_length,
				     usb_complete_t complete,
				     void *context,
				     int interval)
{
	spin_lock_init(&urb->lock);
	urb->dev = dev;
	urb->pipe = pipe;
	urb->transfer_buffer = transfer_buffer;
	urb->transfer_buffer_length = buffer_length;
	urb->complete = complete;
	urb->context = context;
	if (dev->speed == USB_SPEED_HIGH)
		urb->interval = 1 << (interval - 1);
	else
		urb->interval = interval;
	urb->start_frame = -1;
}

/*
 * old style macros to enable 2.4 and 2.2 drivers to build
 * properly.  Please do not use these for new USB drivers.
 */
#define FILL_CONTROL_URB(URB,DEV,PIPE,SETUP_PACKET,TRANSFER_BUFFER,BUFFER_LENGTH,COMPLETE,CONTEXT) \
    usb_fill_control_urb(URB,DEV,PIPE,SETUP_PACKET,TRANSFER_BUFFER,BUFFER_LENGTH,COMPLETE,CONTEXT)
#define FILL_BULK_URB(URB,DEV,PIPE,TRANSFER_BUFFER,BUFFER_LENGTH,COMPLETE,CONTEXT) \
    usb_fill_bulk_urb(URB,DEV,PIPE,TRANSFER_BUFFER,BUFFER_LENGTH,COMPLETE,CONTEXT)
#define FILL_INT_URB(URB,DEV,PIPE,TRANSFER_BUFFER,BUFFER_LENGTH,COMPLETE,CONTEXT,INTERVAL) \
    usb_fill_int_urb(URB,DEV,PIPE,TRANSFER_BUFFER,BUFFER_LENGTH,COMPLETE,CONTEXT,INTERVAL)

extern struct urb *usb_alloc_urb(int iso_packets, int mem_flags);
extern void usb_free_urb(struct urb *urb);
#define usb_put_urb usb_free_urb
extern struct urb *usb_get_urb(struct urb *urb);
extern int usb_submit_urb(struct urb *urb, int mem_flags);
extern int usb_unlink_urb(struct urb *urb);

/*-------------------------------------------------------------------*
 *                         SYNCHRONOUS CALL SUPPORT                  *
 *-------------------------------------------------------------------*/

extern int usb_control_msg(struct usb_device *dev, unsigned int pipe,
	__u8 request, __u8 requesttype, __u16 value, __u16 index,
	void *data, __u16 size, int timeout);
extern int usb_bulk_msg(struct usb_device *usb_dev, unsigned int pipe,
	void *data, int len, int *actual_length,
	int timeout);

/* wrappers around usb_control_msg() for the most common standard requests */
extern int usb_clear_halt(struct usb_device *dev, int pipe);
extern int usb_get_descriptor(struct usb_device *dev, unsigned char desctype,
	unsigned char descindex, void *buf, int size);
extern int usb_get_device_descriptor(struct usb_device *dev);
extern int usb_get_status(struct usb_device *dev,
	int type, int target, void *data);
extern int usb_get_string(struct usb_device *dev,
	unsigned short langid, unsigned char index, void *buf, int size);
extern int usb_string(struct usb_device *dev, int index,
	char *buf, size_t size);
extern int usb_set_configuration(struct usb_device *dev, int configuration);
extern int usb_set_interface(struct usb_device *dev, int ifnum, int alternate);

/*
 * timeouts, in seconds, used for sending/receiving control messages
 * they typically complete within a few frames (msec) after they're issued
 */
#ifdef CONFIG_USB_LONG_TIMEOUT
#define USB_CTRL_GET_TIMEOUT	4
#else
#define USB_CTRL_GET_TIMEOUT	3
#endif

#define USB_CTRL_SET_TIMEOUT	3

/* -------------------------------------------------------------------------- */

/*
 * Calling this entity a "pipe" is glorifying it. A USB pipe
 * is something embarrassingly simple: it basically consists
 * of the following information:
 *  - device number (7 bits)
 *  - endpoint number (4 bits)
 *  - current Data0/1 state (1 bit) [Historical; now gone]
 *  - direction (1 bit)
 *  - speed (1 bit) [Historical and specific to USB 1.1; now gone.]
 *  - max packet size (2 bits: 8, 16, 32 or 64) [Historical; now gone.]
 *  - pipe type (2 bits: control, interrupt, bulk, isochronous)
 *
 * That's 18 bits. Really. Nothing more. And the USB people have
 * documented these eighteen bits as some kind of glorious
 * virtual data structure.
 *
 * Let's not fall in that trap. We'll just encode it as a simple
 * unsigned int. The encoding is:
 *
 *  - max size:		bits 0-1	[Historical; now gone.]
 *  - direction:	bit 7		(0 = Host-to-Device [Out],
 *					 1 = Device-to-Host [In])
 *  - device:		bits 8-14
 *  - endpoint:		bits 15-18
 *  - Data0/1:		bit 19		[Historical; now gone. ]
 *  - lowspeed:		bit 26		[Historical; now gone. ]
 *  - pipe type:	bits 30-31	(00 = isochronous, 01 = interrupt,
 *					 10 = control, 11 = bulk)
 *
 * Why? Because it's arbitrary, and whatever encoding we select is really
 * up to us. This one happens to share a lot of bit positions with the UHCI
 * specification, so that much of the uhci driver can just mask the bits
 * appropriately.
 */

/* NOTE:  these are not the standard USB_ENDPOINT_XFER_* values!! */
#define PIPE_ISOCHRONOUS		0
#define PIPE_INTERRUPT			1
#define PIPE_CONTROL			2
#define PIPE_BULK			3

#define usb_maxpacket(dev, pipe, out)	(out \
				? (dev)->epmaxpacketout[usb_pipeendpoint(pipe)] \
				: (dev)->epmaxpacketin [usb_pipeendpoint(pipe)] )
#define usb_packetid(pipe)	(((pipe) & USB_DIR_IN) ? USB_PID_IN : USB_PID_OUT)

#define usb_pipeout(pipe)	((((pipe) >> 7) & 1) ^ 1)
#define usb_pipein(pipe)	(((pipe) >> 7) & 1)
#define usb_pipedevice(pipe)	(((pipe) >> 8) & 0x7f)
#define usb_pipeendpoint(pipe)	(((pipe) >> 15) & 0xf)
#define usb_pipetype(pipe)	(((pipe) >> 30) & 3)
#define usb_pipeisoc(pipe)	(usb_pipetype((pipe)) == PIPE_ISOCHRONOUS)
#define usb_pipeint(pipe)	(usb_pipetype((pipe)) == PIPE_INTERRUPT)
#define usb_pipecontrol(pipe)	(usb_pipetype((pipe)) == PIPE_CONTROL)
#define usb_pipebulk(pipe)	(usb_pipetype((pipe)) == PIPE_BULK)

#define PIPE_DEVEP_MASK		0x0007ff00

/* The D0/D1 toggle bits */
#define usb_gettoggle(dev, ep, out) (((dev)->toggle[out] >> (ep)) & 1)
#define	usb_dotoggle(dev, ep, out)  ((dev)->toggle[out] ^= (1 << (ep)))
#define usb_settoggle(dev, ep, out, bit) ((dev)->toggle[out] = ((dev)->toggle[out] & ~(1 << (ep))) | ((bit) << (ep)))

/* Endpoint halt control/status */
#define usb_endpoint_out(ep_dir)	((((ep_dir) >> 7) & 1) ^ 1)
#define usb_endpoint_halt(dev, ep, out) ((dev)->halted[out] |= (1 << (ep)))
#define usb_endpoint_running(dev, ep, out) ((dev)->halted[out] &= ~(1 << (ep)))
#define usb_endpoint_halted(dev, ep, out) ((dev)->halted[out] & (1 << (ep)))

static inline unsigned int __create_pipe(struct usb_device *dev, unsigned int endpoint)
{
	return (dev->devnum << 8) | (endpoint << 15);
}

/* Create various pipes... */
#define usb_sndctrlpipe(dev,endpoint)	((PIPE_CONTROL << 30) | __create_pipe(dev,endpoint))
#define usb_rcvctrlpipe(dev,endpoint)	((PIPE_CONTROL << 30) | __create_pipe(dev,endpoint) | USB_DIR_IN)
#define usb_sndisocpipe(dev,endpoint)	((PIPE_ISOCHRONOUS << 30) | __create_pipe(dev,endpoint))
#define usb_rcvisocpipe(dev,endpoint)	((PIPE_ISOCHRONOUS << 30) | __create_pipe(dev,endpoint) | USB_DIR_IN)
#define usb_sndbulkpipe(dev,endpoint)	((PIPE_BULK << 30) | __create_pipe(dev,endpoint))
#define usb_rcvbulkpipe(dev,endpoint)	((PIPE_BULK << 30) | __create_pipe(dev,endpoint) | USB_DIR_IN)
#define usb_sndintpipe(dev,endpoint)	((PIPE_INTERRUPT << 30) | __create_pipe(dev,endpoint))
#define usb_rcvintpipe(dev,endpoint)	((PIPE_INTERRUPT << 30) | __create_pipe(dev,endpoint) | USB_DIR_IN)
#define usb_snddefctrl(dev)		((PIPE_CONTROL << 30))
#define usb_rcvdefctrl(dev)		((PIPE_CONTROL << 30) | USB_DIR_IN)

/* -------------------------------------------------------------------------- */

/*
 * Debugging and troubleshooting/diagnostic helpers.
 */
void usb_show_device_descriptor(struct usb_device_descriptor *);
void usb_show_config_descriptor(struct usb_config_descriptor *);
void usb_show_interface_descriptor(struct usb_interface_descriptor *);
void usb_show_endpoint_descriptor(struct usb_endpoint_descriptor *);
void usb_show_device(struct usb_device *);
void usb_show_string(struct usb_device *dev, char *id, int index);

#ifdef DEBUG
#define dbg(format, arg...) printk(KERN_DEBUG __FILE__ ": " format "\n" , ## arg)
#else
#define dbg(format, arg...) do {} while (0)
#endif

#define err(format, arg...) printk(KERN_ERR __FILE__ ": " format "\n" , ## arg)
#define info(format, arg...) printk(KERN_INFO __FILE__ ": " format "\n" , ## arg)
#define warn(format, arg...) printk(KERN_WARNING __FILE__ ": " format "\n" , ## arg)


/* -------------------------------------------------------------------------- */

/*
 * driver list
 * exported only for usbfs (not visible outside usbcore)
 */

extern struct list_head usb_driver_list;

/*
 * USB device fs stuff
 */

#ifdef CONFIG_USB_DEVICEFS

/*
 * these are expected to be called from the USB core/hub thread
 * with the kernel lock held
 */
extern void usbfs_add_bus(struct usb_bus *bus);
extern void usbfs_remove_bus(struct usb_bus *bus);
extern void usbfs_add_device(struct usb_device *dev);
extern void usbfs_remove_device(struct usb_device *dev);
extern void usbfs_update_special (void);

extern int usbfs_init(void);
extern void usbfs_cleanup(void);

#else /* CONFIG_USB_DEVICEFS */

static inline void usbfs_add_bus(struct usb_bus *bus) {}
static inline void usbfs_remove_bus(struct usb_bus *bus) {}
static inline void usbfs_add_device(struct usb_device *dev) {}
static inline void usbfs_remove_device(struct usb_device *dev) {}
static inline void usbfs_update_special (void) {}

static inline int usbfs_init(void) { return 0; }
static inline void usbfs_cleanup(void) { }

#endif /* CONFIG_USB_DEVICEFS */

#endif  /* __KERNEL__ */

#endif
