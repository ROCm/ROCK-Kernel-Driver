#include <linux/config.h>

#ifdef CONFIG_USB_DEBUG
#define DEBUG
#endif

#include <linux/usb.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <asm/byteorder.h>


#define USB_MAXALTSETTING		128	/* Hard limit */
#define USB_MAXENDPOINTS		30	/* Hard limit */

#define USB_MAXCONFIG			8	/* Arbitrary limit */

static int usb_parse_endpoint(struct device *ddev, int cfgno, int inum,
    int asnum, struct usb_host_endpoint *endpoint,
    unsigned char *buffer, int size)
{
	unsigned char *buffer0 = buffer;
	struct usb_descriptor_header *header;
	unsigned char *begin;
	int numskipped;

	header = (struct usb_descriptor_header *)buffer;
	if (header->bDescriptorType != USB_DT_ENDPOINT) {
		dev_err(ddev, "config %d interface %d altsetting %d has an "
		    "unexpected descriptor of type 0x%X, "
		    "expecting endpoint type 0x%X\n",
		    cfgno, inum, asnum,
		    header->bDescriptorType, USB_DT_ENDPOINT);
		return -EINVAL;
	}

	if (header->bLength >= USB_DT_ENDPOINT_AUDIO_SIZE)
		memcpy(&endpoint->desc, buffer, USB_DT_ENDPOINT_AUDIO_SIZE);
	else if (header->bLength >= USB_DT_ENDPOINT_SIZE)
		memcpy(&endpoint->desc, buffer, USB_DT_ENDPOINT_SIZE);
	else {
		dev_err(ddev, "config %d interface %d altsetting %d has an "
		    "invalid endpoint descriptor of length %d\n",
		    cfgno, inum, asnum, header->bLength);
		return -EINVAL;
	}

	if ((endpoint->desc.bEndpointAddress & ~USB_ENDPOINT_DIR_MASK) >= 16) {
		dev_err(ddev, "config %d interface %d altsetting %d has an "
		    "invalid endpoint with address 0x%X\n",
		    cfgno, inum, asnum, endpoint->desc.bEndpointAddress);
		return -EINVAL;
	}

	le16_to_cpus(&endpoint->desc.wMaxPacketSize);

	buffer += header->bLength;
	size -= header->bLength;

	/* Skip over any Class Specific or Vendor Specific descriptors */
	begin = buffer;
	numskipped = 0;
	while (size >= sizeof(struct usb_descriptor_header)) {
		header = (struct usb_descriptor_header *)buffer;

		/* If we find another "proper" descriptor then we're done  */
		if ((header->bDescriptorType == USB_DT_ENDPOINT) ||
		    (header->bDescriptorType == USB_DT_INTERFACE))
			break;

		dev_dbg(ddev, "skipping descriptor 0x%X\n",
		    header->bDescriptorType);
		numskipped++;

		buffer += header->bLength;
		size -= header->bLength;
	}
	if (numskipped) {
		dev_dbg(ddev, "skipped %d class/vendor specific endpoint "
		    "descriptors\n", numskipped);
		endpoint->extra = begin;
		endpoint->extralen = buffer - begin;
	}

	return buffer - buffer0;
}

static void usb_free_intf(struct usb_interface *intf)
{
	int j;

	if (intf->altsetting) {
		for (j = 0; j < intf->num_altsetting; j++) {
			struct usb_host_interface *as = &intf->altsetting[j];

			kfree(as->endpoint);
		}
		kfree(intf->altsetting);
	}
	kfree(intf);
}

static int usb_parse_interface(struct device *ddev, int cfgno,
    struct usb_host_config *config, unsigned char *buffer, int size)
{
	unsigned char *buffer0 = buffer;
	struct usb_interface_descriptor	*d;
	int inum, asnum;
	struct usb_interface *interface;
	struct usb_host_interface *ifp;
	int len, numskipped;
	struct usb_descriptor_header *header;
	unsigned char *begin;
	int i, retval;

	d = (struct usb_interface_descriptor *) buffer;
	if (d->bDescriptorType != USB_DT_INTERFACE) {
		dev_err(ddev, "config %d has an unexpected descriptor of type "
		    "0x%X, expecting interface type 0x%X\n",
		    cfgno, d->bDescriptorType, USB_DT_INTERFACE);
		return -EINVAL;
	}

	inum = d->bInterfaceNumber;
	if (inum >= config->desc.bNumInterfaces) {

		/* Skip to the next interface descriptor */
		buffer += d->bLength;
		size -= d->bLength;
		while (size >= sizeof(struct usb_descriptor_header)) {
			header = (struct usb_descriptor_header *) buffer;

			if (header->bDescriptorType == USB_DT_INTERFACE)
				break;
			buffer += header->bLength;
			size -= header->bLength;
		}
		return buffer - buffer0;
	}

	interface = config->interface[inum];
	asnum = d->bAlternateSetting;
	if (asnum >= interface->num_altsetting) {
		dev_err(ddev, "config %d interface %d has an invalid "
		    "alternate setting number: %d but max is %d\n",
		    cfgno, inum, asnum, interface->num_altsetting - 1);
		return -EINVAL;
	}

	ifp = &interface->altsetting[asnum];
	if (ifp->desc.bLength) {
		dev_err(ddev, "Duplicate descriptor for config %d "
		    "interface %d altsetting %d\n", cfgno, inum, asnum);
		return -EINVAL;
	}
	memcpy(&ifp->desc, buffer, USB_DT_INTERFACE_SIZE);

	buffer += d->bLength;
	size -= d->bLength;

	/* Skip over any Class Specific or Vendor Specific descriptors */
	begin = buffer;
	numskipped = 0;
	while (size >= sizeof(struct usb_descriptor_header)) {
		header = (struct usb_descriptor_header *)buffer;

		/* If we find another "proper" descriptor then we're done  */
		if ((header->bDescriptorType == USB_DT_INTERFACE) ||
		    (header->bDescriptorType == USB_DT_ENDPOINT))
			break;

		dev_dbg(ddev, "skipping descriptor 0x%X\n",
		    header->bDescriptorType);
		numskipped++;

		buffer += header->bLength;
		size -= header->bLength;
	}
	if (numskipped) {
		dev_dbg(ddev, "skipped %d class/vendor specific "
		    "interface descriptors\n", numskipped);
		ifp->extra = begin;
		ifp->extralen = buffer - begin;
	}

	if (ifp->desc.bNumEndpoints > USB_MAXENDPOINTS) {
		dev_err(ddev, "too many endpoints for config %d interface %d "
		    "altsetting %d: %d, maximum allowed: %d\n",
		    cfgno, inum, asnum, ifp->desc.bNumEndpoints,
		    USB_MAXENDPOINTS);
		return -EINVAL;
	}

	len = ifp->desc.bNumEndpoints * sizeof(struct usb_host_endpoint);
	ifp->endpoint = kmalloc(len, GFP_KERNEL);
	if (!ifp->endpoint)
		return -ENOMEM;
	memset(ifp->endpoint, 0, len);

	for (i = 0; i < ifp->desc.bNumEndpoints; i++) {
		if (size < USB_DT_ENDPOINT_SIZE) {
			dev_err(ddev, "too few endpoint descriptors for "
			    "config %d interface %d altsetting %d\n",
			    cfgno, inum, asnum);
			return -EINVAL;
		}

		retval = usb_parse_endpoint(ddev, cfgno, inum, asnum,
		    ifp->endpoint + i, buffer, size);
		if (retval < 0)
			return retval;

		buffer += retval;
		size -= retval;
	}

	return buffer - buffer0;
}

int usb_parse_configuration(struct device *ddev, int cfgidx,
    struct usb_host_config *config, unsigned char *buffer, int size)
{
	int cfgno;
	int nintf, nintf_orig;
	int i, j;
	struct usb_interface *interface;
	unsigned char *buffer2;
	int size2;
	struct usb_descriptor_header *header;
	int numskipped, len;
	unsigned char *begin;
	int retval;

	memcpy(&config->desc, buffer, USB_DT_CONFIG_SIZE);
	if (config->desc.bDescriptorType != USB_DT_CONFIG ||
	    config->desc.bLength < USB_DT_CONFIG_SIZE) {
		dev_err(ddev, "invalid descriptor for config index %d: "
		    "type = 0x%X, length = %d\n", cfgidx,
		    config->desc.bDescriptorType, config->desc.bLength);
		return -EINVAL;
	}
	config->desc.wTotalLength = size;
	cfgno = config->desc.bConfigurationValue;

	nintf = nintf_orig = config->desc.bNumInterfaces;
	if (nintf > USB_MAXINTERFACES) {
		dev_warn(ddev, "config %d has too many interfaces: %d, "
		    "using maximum allowed: %d\n",
		    cfgno, nintf, USB_MAXINTERFACES);
		config->desc.bNumInterfaces = nintf = USB_MAXINTERFACES;
	}

	for (i = 0; i < nintf; ++i) {
		interface = config->interface[i] =
		    kmalloc(sizeof(struct usb_interface), GFP_KERNEL);
		dev_dbg(ddev, "kmalloc IF %p, numif %i\n", interface, i);
		if (!interface)
			return -ENOMEM;
		memset(interface, 0, sizeof(struct usb_interface));
	}

	/* Go through the descriptors, checking their length and counting the
	 * number of altsettings for each interface */
	buffer2 = buffer;
	size2 = size;
	j = 0;
	while (size2 >= sizeof(struct usb_descriptor_header)) {
		header = (struct usb_descriptor_header *) buffer2;
		if ((header->bLength > size2) || (header->bLength < 2)) {
			dev_err(ddev, "config %d has an invalid descriptor "
			    "of length %d\n", cfgno, header->bLength);
			return -EINVAL;
		}

		if (header->bDescriptorType == USB_DT_INTERFACE) {
			struct usb_interface_descriptor *d;

			if (header->bLength < USB_DT_INTERFACE_SIZE) {
				dev_err(ddev, "config %d has an invalid "
				    "interface descriptor of length %d\n",
				    cfgno, header->bLength);
				return -EINVAL;
			}
			d = (struct usb_interface_descriptor *) header;
			i = d->bInterfaceNumber;
			if (i >= nintf_orig) {
				dev_err(ddev, "config %d has an invalid "
				    "interface number: %d but max is %d\n",
				    cfgno, i, nintf_orig - 1);
				return -EINVAL;
			}
			if (i < nintf)
				++config->interface[i]->num_altsetting;

		} else if ((header->bDescriptorType == USB_DT_DEVICE ||
		    header->bDescriptorType == USB_DT_CONFIG) && j) {
			dev_err(ddev, "config %d contains an unexpected "
			    "descriptor of type 0x%X\n",
			    cfgno, header->bDescriptorType);
			return -EINVAL;
		}

		j = 1;
		buffer2 += header->bLength;
		size2 -= header->bLength;
	}

	/* Allocate the altsetting arrays */
	for (i = 0; i < config->desc.bNumInterfaces; ++i) {
		interface = config->interface[i];
		if (interface->num_altsetting > USB_MAXALTSETTING) {
			dev_err(ddev, "too many alternate settings for "
			    "config %d interface %d: %d, "
			    "maximum allowed: %d\n",
			    cfgno, i, interface->num_altsetting,
			    USB_MAXALTSETTING);
			return -EINVAL;
		}
		if (interface->num_altsetting == 0) {
			dev_err(ddev, "config %d has no interface number "
			    "%d\n", cfgno, i);
			return -EINVAL;
		}

		len = sizeof(*interface->altsetting) *
		    interface->num_altsetting;
		interface->altsetting = kmalloc(len, GFP_KERNEL);
		if (!interface->altsetting)
			return -ENOMEM;
		memset(interface->altsetting, 0, len);
	}

	buffer += config->desc.bLength;
	size -= config->desc.bLength;

	/* Skip over any Class Specific or Vendor Specific descriptors */
	begin = buffer;
	numskipped = 0;
	while (size >= sizeof(struct usb_descriptor_header)) {
		header = (struct usb_descriptor_header *)buffer;

		/* If we find another "proper" descriptor then we're done  */
		if ((header->bDescriptorType == USB_DT_ENDPOINT) ||
		    (header->bDescriptorType == USB_DT_INTERFACE))
			break;

		dev_dbg(ddev, "skipping descriptor 0x%X\n",
		    header->bDescriptorType);
		numskipped++;

		buffer += header->bLength;
		size -= header->bLength;
	}
	if (numskipped) {
		dev_dbg(ddev, "skipped %d class/vendor specific configuration "
		    "descriptors\n", numskipped);
		config->extra = begin;
		config->extralen = buffer - begin;
	}

	/* Parse all the interface/altsetting descriptors */
	while (size >= sizeof(struct usb_descriptor_header)) {
		retval = usb_parse_interface(ddev, cfgno, config,
		    buffer, size);
		if (retval < 0)
			return retval;

		buffer += retval;
		size -= retval;
	}

	/* Check for missing altsettings */
	for (i = 0; i < nintf; ++i) {
		interface = config->interface[i];
		for (j = 0; j < interface->num_altsetting; ++j) {
			if (!interface->altsetting[j].desc.bLength) {
				dev_err(ddev, "config %d interface %d has no "
				    "altsetting %d\n", cfgno, i, j);
				return -EINVAL;
			}
		}
	}

	return size;
}

// hub-only!! ... and only exported for reset/reinit path.
// otherwise used internally on disconnect/destroy path
void usb_destroy_configuration(struct usb_device *dev)
{
	int c, i;

	if (!dev->config)
		return;

	if (dev->rawdescriptors) {
		for (i = 0; i < dev->descriptor.bNumConfigurations; i++)
			kfree(dev->rawdescriptors[i]);

		kfree(dev->rawdescriptors);
	}

	for (c = 0; c < dev->descriptor.bNumConfigurations; c++) {
		struct usb_host_config *cf = &dev->config[c];

		for (i = 0; i < cf->desc.bNumInterfaces; i++) {
			struct usb_interface *ifp = cf->interface[i];

			if (ifp)
				usb_free_intf(ifp);
		}
	}
	kfree(dev->config);
}


// hub-only!! ... and only in reset path, or usb_new_device()
// (used by real hubs and virtual root hubs)
int usb_get_configuration(struct usb_device *dev)
{
	struct device *ddev = &dev->dev;
	int ncfg = dev->descriptor.bNumConfigurations;
	int result = -ENOMEM;
	unsigned int cfgno, length;
	unsigned char *buffer;
	unsigned char *bigbuffer;
 	struct usb_config_descriptor *desc;

	if (ncfg > USB_MAXCONFIG) {
		dev_warn(ddev, "too many configurations: %d, "
		    "using maximum allowed: %d\n", ncfg, USB_MAXCONFIG);
		dev->descriptor.bNumConfigurations = ncfg = USB_MAXCONFIG;
	}

	if (ncfg < 1) {
		dev_err(ddev, "no configurations\n");
		return -EINVAL;
	}

	length = ncfg * sizeof(struct usb_host_config);
	dev->config = kmalloc(length, GFP_KERNEL);
	if (!dev->config)
		goto err2;
	memset(dev->config, 0, length);

	length = ncfg * sizeof(char *);
	dev->rawdescriptors = kmalloc(length, GFP_KERNEL);
	if (!dev->rawdescriptors)
		goto err2;
	memset(dev->rawdescriptors, 0, length);

	buffer = kmalloc(8, GFP_KERNEL);
	if (!buffer)
		goto err2;
	desc = (struct usb_config_descriptor *)buffer;

	for (cfgno = 0; cfgno < ncfg; cfgno++) {
		/* We grab the first 8 bytes so we know how long the whole */
		/* configuration is */
		result = usb_get_descriptor(dev, USB_DT_CONFIG, cfgno,
		    buffer, 8);
		if (result < 0) {
			dev_err(ddev, "unable to read config index %d "
			    "descriptor\n", cfgno);
			goto err;
		} else if (result < 8) {
			dev_err(ddev, "config index %d descriptor too short "
			    "(expected %i, got %i)\n", cfgno, 8, result);
			result = -EINVAL;
			goto err;
		}
		length = max((int) le16_to_cpu(desc->wTotalLength),
		    USB_DT_CONFIG_SIZE);

		/* Now that we know the length, get the whole thing */
		bigbuffer = kmalloc(length, GFP_KERNEL);
		if (!bigbuffer) {
			result = -ENOMEM;
			goto err;
		}
		result = usb_get_descriptor(dev, USB_DT_CONFIG, cfgno,
		    bigbuffer, length);
		if (result < 0) {
			dev_err(ddev, "unable to read config index %d "
			    "descriptor\n", cfgno);
			kfree(bigbuffer);
			goto err;
		}
		if (result < length) {
			dev_err(ddev, "config index %d descriptor too short "
			    "(expected %i, got %i)\n", cfgno, length, result);
			result = -EINVAL;
			kfree(bigbuffer);
			goto err;
		}

		dev->rawdescriptors[cfgno] = bigbuffer;

		result = usb_parse_configuration(&dev->dev, cfgno,
		    &dev->config[cfgno], bigbuffer, length);
		if (result > 0)
			dev_dbg(ddev, "config index %d descriptor has %d "
			    "excess byte(s)\n", cfgno, result);
		else if (result < 0) {
			++cfgno;
			goto err;
		}
	}
	result = 0;

err:
	kfree(buffer);
	dev->descriptor.bNumConfigurations = cfgno;
err2:
	if (result == -ENOMEM)
		dev_err(ddev, "out of memory\n");
	return result;
}
