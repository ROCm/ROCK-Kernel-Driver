#include <linux/usb.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/byteorder.h>


#define USB_MAXALTSETTING		128	/* Hard limit */
#define USB_MAXENDPOINTS		30	/* Hard limit */

/* these maximums are arbitrary */
#define USB_MAXCONFIG			8
#define USB_MAXINTERFACES		32

static int usb_parse_endpoint(struct usb_host_endpoint *endpoint, unsigned char *buffer, int size)
{
	unsigned char *buffer0 = buffer;
	struct usb_descriptor_header *header;
	unsigned char *begin;
	int len, numskipped;

	header = (struct usb_descriptor_header *)buffer;
	if (header->bDescriptorType != USB_DT_ENDPOINT) {
		warn("unexpected descriptor 0x%X, expecting endpoint, 0x%X",
			header->bDescriptorType, USB_DT_ENDPOINT);
		return -EINVAL;
	}

	if (header->bLength == USB_DT_ENDPOINT_AUDIO_SIZE)
		memcpy(&endpoint->desc, buffer, USB_DT_ENDPOINT_AUDIO_SIZE);
	else
		memcpy(&endpoint->desc, buffer, USB_DT_ENDPOINT_SIZE);

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

		dbg("skipping descriptor 0x%X", header->bDescriptorType);
		numskipped++;

		buffer += header->bLength;
		size -= header->bLength;
	}
	if (numskipped) {
		dbg("skipped %d class/vendor specific endpoint descriptors", numskipped);

		/* Copy any unknown descriptors into a storage area for drivers */
		/*  to later parse */
		len = buffer - begin;

		endpoint->extra = kmalloc(len, GFP_KERNEL);
		if (!endpoint->extra) {
			err("couldn't allocate memory for endpoint extra descriptors");
			return -ENOMEM;
		}

		memcpy(endpoint->extra, begin, len);
		endpoint->extralen = len;
	}

	return buffer - buffer0;
}

static void usb_release_intf(struct device *dev)
{
	struct usb_interface *intf;
	int j;
	int k;

	intf = to_usb_interface(dev);

	if (intf->altsetting) {
		for (j = 0; j < intf->num_altsetting; j++) {
			struct usb_host_interface *as = &intf->altsetting[j];
			if (as->extra)
				kfree(as->extra);

			if (as->endpoint) {
				for (k = 0; k < as->desc.bNumEndpoints; k++)
					if (as->endpoint[k].extra)
						kfree(as->endpoint[k].extra);
				kfree(as->endpoint);
			}
		}
		kfree(intf->altsetting);
	}
	kfree(intf);
}

static int usb_parse_interface(struct usb_host_config *config, unsigned char *buffer, int size)
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
		warn("unexpected descriptor 0x%X, expecting interface, 0x%X",
			d->bDescriptorType, USB_DT_INTERFACE);
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
		warn("invalid alternate setting %d for interface %d",
		    asnum, inum);
		return -EINVAL;
	}

	ifp = &interface->altsetting[asnum];
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

		dbg("skipping descriptor 0x%X", header->bDescriptorType);
		numskipped++;

		buffer += header->bLength;
		size -= header->bLength;
	}
	if (numskipped) {
		dbg("skipped %d class/vendor specific interface descriptors", numskipped);

		/* Copy any unknown descriptors into a storage area for */
		/*  drivers to later parse */
		len = buffer - begin;
		ifp->extra = kmalloc(len, GFP_KERNEL);

		if (!ifp->extra) {
			err("couldn't allocate memory for interface extra descriptors");
			return -ENOMEM;
		}
		memcpy(ifp->extra, begin, len);
		ifp->extralen = len;
	}

	if (ifp->desc.bNumEndpoints > USB_MAXENDPOINTS) {
		warn("too many endpoints");
		return -EINVAL;
	}

	len = ifp->desc.bNumEndpoints * sizeof(struct usb_host_endpoint);
	ifp->endpoint = kmalloc(len, GFP_KERNEL);
	if (!ifp->endpoint) {
		err("out of memory");
		return -ENOMEM;
	}
	memset(ifp->endpoint, 0, len);

	for (i = 0; i < ifp->desc.bNumEndpoints; i++) {
		if (size < USB_DT_ENDPOINT_SIZE) {
			err("ran out of descriptors parsing");
			return -EINVAL;
		}

		retval = usb_parse_endpoint(ifp->endpoint + i, buffer, size);
		if (retval < 0)
			return retval;

		buffer += retval;
		size -= retval;
	}

	return buffer - buffer0;
}

int usb_parse_configuration(struct usb_host_config *config, char *buffer)
{
	int nintf, nintf_orig;
	int i, j, size;
	struct usb_interface *interface;
	char *buffer2;
	int size2;
	struct usb_descriptor_header *header;
	int numskipped, len;
	char *begin;
	int retval;

	memcpy(&config->desc, buffer, USB_DT_CONFIG_SIZE);
	le16_to_cpus(&config->desc.wTotalLength);
	size = config->desc.wTotalLength;

	nintf = nintf_orig = config->desc.bNumInterfaces;
	if (nintf > USB_MAXINTERFACES) {
		warn("too many interfaces (%d max %d)",
		    nintf, USB_MAXINTERFACES);
		config->desc.bNumInterfaces = nintf = USB_MAXINTERFACES;
	}

	for (i = 0; i < nintf; ++i) {
		interface = config->interface[i] =
		    kmalloc(sizeof(struct usb_interface), GFP_KERNEL);
		dbg("kmalloc IF %p, numif %i", interface, i);
		if (!interface) {
			err("out of memory");
			return -ENOMEM;
		}
		memset(interface, 0, sizeof(struct usb_interface));
		interface->dev.release = usb_release_intf;
		device_initialize(&interface->dev);

		/* put happens in usb_destroy_configuration */
		get_device(&interface->dev);
	}

	/* Go through the descriptors, checking their length and counting the
	 * number of altsettings for each interface */
	buffer2 = buffer;
	size2 = size;
	j = 0;
	while (size2 >= sizeof(struct usb_descriptor_header)) {
		header = (struct usb_descriptor_header *) buffer2;
		if ((header->bLength > size2) || (header->bLength < 2)) {
			err("invalid descriptor of length %d", header->bLength);
			return -EINVAL;
		}

		if (header->bDescriptorType == USB_DT_INTERFACE) {
			struct usb_interface_descriptor *d;

			if (header->bLength < USB_DT_INTERFACE_SIZE) {
				warn("invalid interface descriptor");
				return -EINVAL;
			}
			d = (struct usb_interface_descriptor *) header;
			i = d->bInterfaceNumber;
			if (i >= nintf_orig) {
				warn("invalid interface number (%d/%d)",
				    i, nintf_orig);
				return -EINVAL;
			}
			if (i < nintf)
				++config->interface[i]->num_altsetting;

		} else if ((header->bDescriptorType == USB_DT_DEVICE ||
		    header->bDescriptorType == USB_DT_CONFIG) && j) {
			warn("unexpected descriptor type 0x%X", header->bDescriptorType);
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
			warn("too many alternate settings for interface %d (%d max %d)\n",
			    i, interface->num_altsetting, USB_MAXALTSETTING);
			return -EINVAL;
		}
		if (interface->num_altsetting == 0) {
			warn("no alternate settings for interface %d", i);
			return -EINVAL;
		}

		len = sizeof(*interface->altsetting) * interface->num_altsetting;
		interface->altsetting = kmalloc(len, GFP_KERNEL);
		if (!interface->altsetting) {
			err("couldn't kmalloc interface->altsetting");
			return -ENOMEM;
		}
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

		dbg("skipping descriptor 0x%X", header->bDescriptorType);
		numskipped++;

		buffer += header->bLength;
		size -= header->bLength;
	}
	if (numskipped) {
		dbg("skipped %d class/vendor specific configuration descriptors", numskipped);

		/* Copy any unknown descriptors into a storage area for */
		/*  drivers to later parse */
		len = buffer - begin;
		config->extra = kmalloc(len, GFP_KERNEL);
		if (!config->extra) {
			err("couldn't allocate memory for config extra descriptors");
			return -ENOMEM;
		}

		memcpy(config->extra, begin, len);
		config->extralen = len;
	}

	/* Parse all the interface/altsetting descriptors */
	while (size >= sizeof(struct usb_descriptor_header)) {
		retval = usb_parse_interface(config, buffer, size);
		if (retval < 0)
			return retval;

		buffer += retval;
		size -= retval;
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
				put_device(&ifp->dev);
		}
		kfree(cf->extra);
	}
	kfree(dev->config);
}


// hub-only!! ... and only in reset path, or usb_new_device()
// (used by real hubs and virtual root hubs)
int usb_get_configuration(struct usb_device *dev)
{
	int ncfg = dev->descriptor.bNumConfigurations;
	int result;
	unsigned int cfgno, length;
	unsigned char *buffer;
	unsigned char *bigbuffer;
 	struct usb_config_descriptor *desc;

	if (ncfg > USB_MAXCONFIG) {
		warn("too many configurations (%d max %d)",
		    ncfg, USB_MAXCONFIG);
		dev->descriptor.bNumConfigurations = ncfg = USB_MAXCONFIG;
	}

	if (ncfg < 1) {
		warn("no configurations");
		return -EINVAL;
	}

	length = ncfg * sizeof(struct usb_host_config);
	dev->config = kmalloc(length, GFP_KERNEL);
	if (!dev->config) {
		err("out of memory");
		return -ENOMEM;
	}
	memset(dev->config, 0, length);

	length = ncfg * sizeof(char *);
	dev->rawdescriptors = kmalloc(length, GFP_KERNEL);
	if (!dev->rawdescriptors) {
		err("out of memory");
		return -ENOMEM;
	}
	memset(dev->rawdescriptors, 0, length);

	buffer = kmalloc(8, GFP_KERNEL);
	if (!buffer) {
		err("unable to allocate memory for configuration descriptors");
		return -ENOMEM;
	}
	desc = (struct usb_config_descriptor *)buffer;

	for (cfgno = 0; cfgno < ncfg; cfgno++) {
		/* We grab the first 8 bytes so we know how long the whole */
		/*  configuration is */
		result = usb_get_descriptor(dev, USB_DT_CONFIG, cfgno, buffer, 8);
		if (result < 8) {
			if (result < 0)
				err("unable to get descriptor");
			else {
				err("config descriptor too short (expected %i, got %i)", 8, result);
				result = -EINVAL;
			}
			goto err;
		}

  	  	/* Get the full buffer */
		length = le16_to_cpu(desc->wTotalLength);

		bigbuffer = kmalloc(length, GFP_KERNEL);
		if (!bigbuffer) {
			err("unable to allocate memory for configuration descriptors");
			result = -ENOMEM;
			goto err;
		}

		/* Now that we know the length, get the whole thing */
		result = usb_get_descriptor(dev, USB_DT_CONFIG, cfgno, bigbuffer, length);
		if (result < 0) {
			err("couldn't get all of config descriptors");
			kfree(bigbuffer);
			goto err;
		}

		if (result < length) {
			err("config descriptor too short (expected %i, got %i)", length, result);
			result = -EINVAL;
			kfree(bigbuffer);
			goto err;
		}

		dev->rawdescriptors[cfgno] = bigbuffer;

		result = usb_parse_configuration(&dev->config[cfgno], bigbuffer);
		if (result > 0)
			dbg("descriptor data left");
		else if (result < 0) {
			++cfgno;
			goto err;
		}
	}

	kfree(buffer);
	return 0;
err:
	kfree(buffer);
	dev->descriptor.bNumConfigurations = cfgno;
	return result;
}

