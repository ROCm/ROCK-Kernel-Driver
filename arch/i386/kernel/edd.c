/*
 * linux/arch/i386/kernel/edd.c
 *  Copyright (C) 2002 Dell Computer Corporation
 *  by Matt Domsch <Matt_Domsch@dell.com>
 *
 * BIOS Enhanced Disk Drive Services (EDD)
 * conformant to T13 Committee www.t13.org
 *   projects 1572D, 1484D, 1386D, 1226DT
 *
 * This code takes information provided by BIOS EDD calls
 * fn41 - Check Extensions Present and
 * fn48 - Get Device Parametes with EDD extensions
 * made in setup.S, copied to safe structures in setup.c,
 * and presents it in driverfs.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2.0 as published by
 * the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/*
 * Known issues:
 * - refcounting of struct device objects could be improved.
 *
 * TODO:
 * - Add IDE and USB disk device support
 * - Get symlink creator helper functions exported from
 *   drivers/base instead of duplicating them here.
 * - move edd.[ch] to better locations if/when one is decided
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/err.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/limits.h>
#include <linux/driverfs_fs.h>
#include <linux/pci.h>
#include <asm/edd.h>
#include <linux/device.h>
#include <linux/blkdev.h>
/* FIXME - this really belongs in include/scsi/scsi.h */
#include <../drivers/scsi/scsi.h>
#include <../drivers/scsi/hosts.h>

MODULE_AUTHOR("Matt Domsch <Matt_Domsch@Dell.com>");
MODULE_DESCRIPTION("driverfs interface to BIOS EDD information");
MODULE_LICENSE("GPL");

#define EDD_VERSION "0.07 2002-Oct-24"
#define EDD_DEVICE_NAME_SIZE 16
#define REPORT_URL "http://domsch.com/linux/edd30/results.html"

#define left (count - (p - buf) - 1)

/*
 * bios_dir may go away completely,
 * and it definitely won't be at the root
 * of driverfs forever.
 */
static struct driver_dir_entry bios_dir = {
	.name = "bios",
	.mode = (S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO),
};

struct edd_device {
	char name[EDD_DEVICE_NAME_SIZE];
	struct edd_info *info;
	struct driver_dir_entry dir;
};

struct edd_attribute {
	struct attribute attr;
	ssize_t(*show) (struct edd_device * edev, char *buf, size_t count,
			loff_t off);
	int (*test) (struct edd_device * edev);
};

/* forward declarations */
static int edd_dev_is_type(struct edd_device *edev, const char *type);
static struct pci_dev *edd_get_pci_dev(struct edd_device *edev);
static struct scsi_device *edd_find_matching_scsi_device(struct edd_device *edev);

static struct edd_device *edd_devices[EDDMAXNR];

#define EDD_DEVICE_ATTR(_name,_mode,_show,_test) \
struct edd_attribute edd_attr_##_name = { 	\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show	= _show,				\
	.test	= _test,				\
};

static inline struct edd_info *
edd_dev_get_info(struct edd_device *edev)
{
	return edev->info;
}
static inline void
edd_dev_set_info(struct edd_device *edev, struct edd_info *info)
{
	edev->info = info;
}

#define to_edd_attr(_attr) container_of(_attr,struct edd_attribute,attr)
#define to_edd_device(_dir) container_of(_dir,struct edd_device,dir)

static ssize_t
edd_attr_show(struct driver_dir_entry *dir, struct attribute *attr,
	      char *buf, size_t count, loff_t off)
{
	struct edd_device *dev = to_edd_device(dir);
	struct edd_attribute *edd_attr = to_edd_attr(attr);
	ssize_t ret = 0;

	if (edd_attr->show)
		ret = edd_attr->show(dev, buf, count, off);
	return ret;
}

static struct driverfs_ops edd_attr_ops = {
	.show = edd_attr_show,
};

static int
edd_dump_raw_data(char *b, int count, void *data, int length)
{
	char *orig_b = b;
	char hexbuf[80], ascbuf[20], *h, *a, c;
	unsigned char *p = data;
	unsigned long column = 0;
	int length_printed = 0, d;
	const char maxcolumn = 16;
	while (length_printed < length && count > 0) {
		h = hexbuf;
		a = ascbuf;
		for (column = 0;
		     column < maxcolumn && length_printed < length; column++) {
			h += sprintf(h, "%02x ", (unsigned char) *p);
			if (!isprint(*p))
				c = '.';
			else
				c = *p;
			a += sprintf(a, "%c", c);
			p++;
			length_printed++;
		}
		/* pad out the line */
		for (; column < maxcolumn; column++) {
			h += sprintf(h, "   ");
			a += sprintf(a, " ");
		}
		d = snprintf(b, count, "%s\t%s\n", hexbuf, ascbuf);
		b += d;
		count -= d;
	}
	return (b - orig_b);
}

static ssize_t
edd_show_host_bus(struct edd_device *edev, char *buf, size_t count, loff_t off)
{
	struct edd_info *info = edd_dev_get_info(edev);
	char *p = buf;
	int i;

	if (!edev || !info || !buf || off) {
		return 0;
	}

	for (i = 0; i < 4; i++) {
		if (isprint(info->params.host_bus_type[i])) {
			p += snprintf(p, left, "%c", info->params.host_bus_type[i]);
		} else {
			p += snprintf(p, left, " ");
		}
	}

	if (!strncmp(info->params.host_bus_type, "ISA", 3)) {
		p += snprintf(p, left, "\tbase_address: %x\n",
			     info->params.interface_path.isa.base_address);
	} else if (!strncmp(info->params.host_bus_type, "PCIX", 4) ||
		   !strncmp(info->params.host_bus_type, "PCI", 3)) {
		p += snprintf(p, left,
			     "\t%02x:%02x.%d  channel: %u\n",
			     info->params.interface_path.pci.bus,
			     info->params.interface_path.pci.slot,
			     info->params.interface_path.pci.function,
			     info->params.interface_path.pci.channel);
	} else if (!strncmp(info->params.host_bus_type, "IBND", 4) ||
		   !strncmp(info->params.host_bus_type, "XPRS", 4) ||
		   !strncmp(info->params.host_bus_type, "HTPT", 4)) {
		p += snprintf(p, left,
			     "\tTBD: %llx\n",
			     info->params.interface_path.ibnd.reserved);

	} else {
		p += snprintf(p, left, "\tunknown: %llx\n",
			     info->params.interface_path.unknown.reserved);
	}
	return (p - buf);
}

static ssize_t
edd_show_interface(struct edd_device *edev, char *buf, size_t count, loff_t off)
{
	struct edd_info *info = edd_dev_get_info(edev);
	char *p = buf;
	int i;

	if (!edev || !info || !buf || off) {
		return 0;
	}

	for (i = 0; i < 8; i++) {
		if (isprint(info->params.interface_type[i])) {
			p += snprintf(p, left, "%c", info->params.interface_type[i]);
		} else {
			p += snprintf(p, left, " ");
		}
	}
	if (!strncmp(info->params.interface_type, "ATAPI", 5)) {
		p += snprintf(p, left, "\tdevice: %u  lun: %u\n",
			     info->params.device_path.atapi.device,
			     info->params.device_path.atapi.lun);
	} else if (!strncmp(info->params.interface_type, "ATA", 3)) {
		p += snprintf(p, left, "\tdevice: %u\n",
			     info->params.device_path.ata.device);
	} else if (!strncmp(info->params.interface_type, "SCSI", 4)) {
		p += snprintf(p, left, "\tid: %u  lun: %llu\n",
			     info->params.device_path.scsi.id,
			     info->params.device_path.scsi.lun);
	} else if (!strncmp(info->params.interface_type, "USB", 3)) {
		p += snprintf(p, left, "\tserial_number: %llx\n",
			     info->params.device_path.usb.serial_number);
	} else if (!strncmp(info->params.interface_type, "1394", 4)) {
		p += snprintf(p, left, "\teui: %llx\n",
			     info->params.device_path.i1394.eui);
	} else if (!strncmp(info->params.interface_type, "FIBRE", 5)) {
		p += snprintf(p, left, "\twwid: %llx lun: %llx\n",
			     info->params.device_path.fibre.wwid,
			     info->params.device_path.fibre.lun);
	} else if (!strncmp(info->params.interface_type, "I2O", 3)) {
		p += snprintf(p, left, "\tidentity_tag: %llx\n",
			     info->params.device_path.i2o.identity_tag);
	} else if (!strncmp(info->params.interface_type, "RAID", 4)) {
		p += snprintf(p, left, "\tidentity_tag: %x\n",
			     info->params.device_path.raid.array_number);
	} else if (!strncmp(info->params.interface_type, "SATA", 4)) {
		p += snprintf(p, left, "\tdevice: %u\n",
			     info->params.device_path.sata.device);
	} else {
		p += snprintf(p, left, "\tunknown: %llx %llx\n",
			     info->params.device_path.unknown.reserved1,
			     info->params.device_path.unknown.reserved2);
	}

	return (p - buf);
}

/**
 * edd_show_raw_data() - unparses EDD information, returned to user-space
 *
 * Returns: number of bytes written, or 0 on failure
 */
static ssize_t
edd_show_raw_data(struct edd_device *edev, char *buf, size_t count, loff_t off)
{
	struct edd_info *info = edd_dev_get_info(edev);
	int i, rc, warn_padding = 0, email = 0, nonzero_path = 0,
		len = sizeof (*edd) - 4, found_pci=0;
	uint8_t checksum = 0, c = 0;
	char *p = buf;
	struct pci_dev *pci_dev=NULL;
	struct scsi_device *sd;
	if (!edev || !info || !buf || off) {
		return 0;
	}

	if (!(info->params.key == 0xBEDD || info->params.key == 0xDDBE))
		len = info->params.length;

	p += snprintf(p, left, "int13 fn48 returned data:\n\n");
	p += edd_dump_raw_data(p, left, ((char *) edd) + 4, len);

	/* Spec violation.  Adaptec AIC7899 returns 0xDDBE
	   here, when it should be 0xBEDD.
	 */
	p += snprintf(p, left, "\n");
	if (info->params.key == 0xDDBE) {
		p += snprintf(p, left,
			     "Warning: Spec violation.  Key should be 0xBEDD, is 0xDDBE\n");
		email++;
	}

	if (!(info->params.key == 0xBEDD || info->params.key == 0xDDBE)) {
		goto out;
	}

	for (i = 30; i <= 73; i++) {
		c = *(((uint8_t *) edd) + i + 4);
		if (c)
			nonzero_path++;
		checksum += c;
	}

	if (checksum) {
		p += snprintf(p, left,
			     "Warning: Spec violation.  Device Path checksum invalid.\n");
		email++;
	}

	if (!nonzero_path) {
		p += snprintf(p, left, "Error: Spec violation.  Empty device path.\n");
		email++;
		goto out;
	}

	for (i = 0; i < 4; i++) {
		if (!isprint(info->params.host_bus_type[i])) {
			warn_padding++;
		}
	}
	for (i = 0; i < 8; i++) {
		if (!isprint(info->params.interface_type[i])) {
			warn_padding++;
		}
	}

	if (warn_padding) {
		p += snprintf(p, left,
			     "Warning: Spec violation.  Padding should be 0x20.\n");
		email++;
	}

	rc = edd_dev_is_type(edev, "PCI");
	if (!rc) {
		pci_dev = pci_find_slot(info->params.interface_path.pci.bus,
					PCI_DEVFN(info->params.interface_path.
						  pci.slot,
						  info->params.interface_path.
						  pci.function));
		if (!pci_dev) {
			p += snprintf(p, left, "Error: BIOS says this is a PCI device, but the OS doesn't know\n");
			p += snprintf(p, left, "  about a PCI device at %02x:%02x.%d\n",
				     info->params.interface_path.pci.bus,
				     info->params.interface_path.pci.slot,
				     info->params.interface_path.pci.function);
			email++;
		}
		else {
			found_pci++;
		}
	}

	if (found_pci && !edd_dev_is_type(edev, "SCSI")) {
		sd = edd_find_matching_scsi_device(edev);
		if (!sd) {
			p += snprintf(p, left, "Error: BIOS says this is a SCSI device, but\n");
			p += snprintf(p, left, "  the OS doesn't know about this SCSI device.\n");
			p += snprintf(p, left, "  Do you have it's driver module loaded?\n");
			email++;
		}
	}

out:
	if (email) {
		p += snprintf(p, left, "\nPlease check %s\n", REPORT_URL);
		p += snprintf(p, left, "to see if this has been reported.  If not,\n");
		p += snprintf(p, left, "please send the information requested there.\n");
	}

	return (p - buf);
}

static ssize_t
edd_show_version(struct edd_device *edev, char *buf, size_t count, loff_t off)
{
	struct edd_info *info = edd_dev_get_info(edev);
	char *p = buf;
	if (!edev || !info || !buf || off) {
		return 0;
	}

	p += snprintf(p, left, "0x%02x\n", info->version);
	return (p - buf);
}

static ssize_t
edd_show_extensions(struct edd_device *edev, char *buf, size_t count,
		    loff_t off)
{
	struct edd_info *info = edd_dev_get_info(edev);
	char *p = buf;
	if (!edev || !info || !buf || off) {
		return 0;
	}

	if (info->interface_support & EDD_EXT_FIXED_DISK_ACCESS) {
		p += snprintf(p, left, "Fixed disk access\n");
	}
	if (info->interface_support & EDD_EXT_DEVICE_LOCKING_AND_EJECTING) {
		p += snprintf(p, left, "Device locking and ejecting\n");
	}
	if (info->interface_support & EDD_EXT_ENHANCED_DISK_DRIVE_SUPPORT) {
		p += snprintf(p, left, "Enhanced Disk Drive support\n");
	}
	if (info->interface_support & EDD_EXT_64BIT_EXTENSIONS) {
		p += snprintf(p, left, "64-bit extensions\n");
	}
	return (p - buf);
}

static ssize_t
edd_show_info_flags(struct edd_device *edev, char *buf, size_t count,
		    loff_t off)
{
	struct edd_info *info = edd_dev_get_info(edev);
	char *p = buf;
	if (!edev || !info || !buf || off) {
		return 0;
	}

	if (info->params.info_flags & EDD_INFO_DMA_BOUNDRY_ERROR_TRANSPARENT)
		p += snprintf(p, left, "DMA boundry error transparent\n");
	if (info->params.info_flags & EDD_INFO_GEOMETRY_VALID)
		p += snprintf(p, left, "geometry valid\n");
	if (info->params.info_flags & EDD_INFO_REMOVABLE)
		p += snprintf(p, left, "removable\n");
	if (info->params.info_flags & EDD_INFO_WRITE_VERIFY)
		p += snprintf(p, left, "write verify\n");
	if (info->params.info_flags & EDD_INFO_MEDIA_CHANGE_NOTIFICATION)
		p += snprintf(p, left, "media change notification\n");
	if (info->params.info_flags & EDD_INFO_LOCKABLE)
		p += snprintf(p, left, "lockable\n");
	if (info->params.info_flags & EDD_INFO_NO_MEDIA_PRESENT)
		p += snprintf(p, left, "no media present\n");
	if (info->params.info_flags & EDD_INFO_USE_INT13_FN50)
		p += snprintf(p, left, "use int13 fn50\n");
	return (p - buf);
}

static ssize_t
edd_show_default_cylinders(struct edd_device *edev, char *buf, size_t count,
			   loff_t off)
{
	struct edd_info *info = edd_dev_get_info(edev);
	char *p = buf;
	if (!edev || !info || !buf || off) {
		return 0;
	}

	p += snprintf(p, left, "0x%x\n", info->params.num_default_cylinders);
	return (p - buf);
}

static ssize_t
edd_show_default_heads(struct edd_device *edev, char *buf, size_t count,
		       loff_t off)
{
	struct edd_info *info = edd_dev_get_info(edev);
	char *p = buf;
	if (!edev || !info || !buf || off) {
		return 0;
	}

	p += snprintf(p, left, "0x%x\n", info->params.num_default_heads);
	return (p - buf);
}

static ssize_t
edd_show_default_sectors_per_track(struct edd_device *edev, char *buf,
				   size_t count, loff_t off)
{
	struct edd_info *info = edd_dev_get_info(edev);
	char *p = buf;
	if (!edev || !info || !buf || off) {
		return 0;
	}

	p += snprintf(p, left, "0x%x\n", info->params.sectors_per_track);
	return (p - buf);
}

static ssize_t
edd_show_sectors(struct edd_device *edev, char *buf, size_t count, loff_t off)
{
	struct edd_info *info = edd_dev_get_info(edev);
	char *p = buf;
	if (!edev || !info || !buf || off) {
		return 0;
	}

	p += snprintf(p, left, "0x%llx\n", info->params.number_of_sectors);
	return (p - buf);
}


/*
 * Some device instances may not have all the above attributes,
 * or the attribute values may be meaningless (i.e. if
 * the device is < EDD 3.0, it won't have host_bus and interface
 * information), so don't bother making files for them.  Likewise
 * if the default_{cylinders,heads,sectors_per_track} values
 * are zero, the BIOS doesn't provide sane values, don't bother
 * creating files for them either.
 */

static int
edd_has_default_cylinders(struct edd_device *edev)
{
	struct edd_info *info = edd_dev_get_info(edev);
	if (!edev || !info)
		return 1;
	return !info->params.num_default_cylinders;
}

static int
edd_has_default_heads(struct edd_device *edev)
{
	struct edd_info *info = edd_dev_get_info(edev);
	if (!edev || !info)
		return 1;
	return !info->params.num_default_heads;
}

static int
edd_has_default_sectors_per_track(struct edd_device *edev)
{
	struct edd_info *info = edd_dev_get_info(edev);
	if (!edev || !info)
		return 1;
	return !info->params.sectors_per_track;
}

static int
edd_has_edd30(struct edd_device *edev)
{
	struct edd_info *info = edd_dev_get_info(edev);
	int i, nonzero_path = 0;
	char c;

	if (!edev || !info)
		return 1;

	if (!(info->params.key == 0xBEDD || info->params.key == 0xDDBE)) {
		return 1;
	}

	for (i = 30; i <= 73; i++) {
		c = *(((uint8_t *) edd) + i + 4);
		if (c) {
			nonzero_path++;
			break;
		}
	}
	if (!nonzero_path) {
		return 1;
	}

	return 0;
}

static EDD_DEVICE_ATTR(raw_data, 0444, edd_show_raw_data, NULL);
static EDD_DEVICE_ATTR(version, 0444, edd_show_version, NULL);
static EDD_DEVICE_ATTR(extensions, 0444, edd_show_extensions, NULL);
static EDD_DEVICE_ATTR(info_flags, 0444, edd_show_info_flags, NULL);
static EDD_DEVICE_ATTR(sectors, 0444, edd_show_sectors, NULL);
static EDD_DEVICE_ATTR(default_cylinders, 0444, edd_show_default_cylinders,
		       edd_has_default_cylinders);
static EDD_DEVICE_ATTR(default_heads, 0444, edd_show_default_heads,
		       edd_has_default_heads);
static EDD_DEVICE_ATTR(default_sectors_per_track, 0444,
		       edd_show_default_sectors_per_track,
		       edd_has_default_sectors_per_track);
static EDD_DEVICE_ATTR(interface, 0444, edd_show_interface, edd_has_edd30);
static EDD_DEVICE_ATTR(host_bus, 0444, edd_show_host_bus, edd_has_edd30);


static struct edd_attribute * def_attrs[] = {
	&edd_attr_raw_data,
	&edd_attr_version,
	&edd_attr_extensions,
	&edd_attr_info_flags,
	&edd_attr_sectors,
	&edd_attr_default_cylinders,
	&edd_attr_default_heads,
	&edd_attr_default_sectors_per_track,
	&edd_attr_interface,
	&edd_attr_host_bus,
	NULL,
};

/* edd_get_devpath_length(), edd_fill_devpath(), and edd_device_link()
   were taken from linux/drivers/base/fs/device.c.  When these
   or similar are exported to generic code, remove these.
*/

static int
edd_get_devpath_length(struct device *dev)
{
	int length = 1;
	struct device *parent = dev;

	/* walk up the ancestors until we hit the root.
	 * Add 1 to strlen for leading '/' of each level.
	 */
	do {
		length += strlen(parent->bus_id) + 1;
		parent = parent->parent;
	} while (parent);
	return length;
}

static void
edd_fill_devpath(struct device *dev, char *path, int length)
{
	struct device *parent;
	--length;
	for (parent = dev; parent; parent = parent->parent) {
		int cur = strlen(parent->bus_id);

		/* back up enough to print this bus id with '/' */
		length -= cur;
		strncpy(path + length, parent->bus_id, cur);
		*(path + --length) = '/';
	}
}

static int
edd_device_symlink(struct edd_device *edev, struct device *dev, char *name)
{
	char *path;
	int length;
	int error = 0;

	if (!dev->bus || !name)
		return 0;

	length = edd_get_devpath_length(dev);

	/* now add the path from the edd_device directory
	 * It should be '../..' (one to get to the 'bios' directory,
	 * and one to get to the root of the fs.)
	 */
	length += strlen("../../root");

	if (length > PATH_MAX)
		return -ENAMETOOLONG;

	if (!(path = kmalloc(length, GFP_KERNEL)))
		return -ENOMEM;
	memset(path, 0, length);

	/* our relative position */
	strcpy(path, "../../root");

	edd_fill_devpath(dev, path, length);
	error = driverfs_create_symlink(&edev->dir, name, path);
	kfree(path);
	return error;
}

/**
 * edd_dev_is_type() - is this EDD device a 'type' device?
 * @edev
 * @type - a host bus or interface identifier string per the EDD spec
 *
 * Returns 0 if it is a 'type' device, nonzero otherwise.
 */
static int
edd_dev_is_type(struct edd_device *edev, const char *type)
{
	int rc;
	struct edd_info *info = edd_dev_get_info(edev);
	if (!edev || !info)
		return 1;

	rc = strncmp(info->params.host_bus_type, type, strlen(type));
	if (!rc)
		return 0;

	return strncmp(info->params.interface_type, type, strlen(type));
}

/**
 * edd_get_pci_dev() - finds pci_dev that matches edev
 * @edev - edd_device
 *
 * Returns pci_dev if found, or NULL
 */
static struct pci_dev *
edd_get_pci_dev(struct edd_device *edev)
{
	struct edd_info *info = edd_dev_get_info(edev);
	int rc;

	rc = edd_dev_is_type(edev, "PCI");
	if (rc)
		return NULL;

	return pci_find_slot(info->params.interface_path.pci.bus,
			     PCI_DEVFN(info->params.interface_path.pci.slot,
				       info->params.interface_path.pci.
				       function));
}

static int
edd_create_symlink_to_pcidev(struct edd_device *edev)
{

	struct pci_dev *pci_dev = edd_get_pci_dev(edev);
	if (!pci_dev)
		return 1;
	return edd_device_symlink(edev, &pci_dev->dev, "pci_dev");
}

/**
 * edd_match_scsidev()
 * @edev - EDD device is a known SCSI device
 * @sd - scsi_device with host who's parent is a PCI controller
 * 
 * returns 0 on success, 1 on failure
 */
static int
edd_match_scsidev(struct edd_device *edev, struct scsi_device *sd)
{
	struct edd_info *info = edd_dev_get_info(edev);

	if (!edev || !sd || !info)
		return 1;

	if ((sd->channel == info->params.interface_path.pci.channel) &&
	    (sd->id == info->params.device_path.scsi.id) &&
	    (sd->lun == info->params.device_path.scsi.lun)) {
		return 0;
	}

	return 1;
}

/**
 * edd_find_matching_device()
 * @edev - edd_device to match
 *
 * Returns struct scsi_device * on success,
 * or NULL on failure.
 * This assumes that all children of the PCI controller
 * are scsi_hosts, and that all children of scsi_hosts
 * are scsi_devices.
 * The reference counting probably isn't the best it could be.
 */

#define	to_scsi_host(d)	\
	container_of(d, struct Scsi_Host, host_driverfs_dev)
#define children_to_dev(n) container_of(n,struct device,node)
static struct scsi_device *
edd_find_matching_scsi_device(struct edd_device *edev)
{
	struct list_head *shost_node, *sdev_node;
	int rc = 1;
	struct scsi_device *sd = NULL;
	struct device *shost_dev, *sdev_dev;
	struct pci_dev *pci_dev;
	struct Scsi_Host *sh;

	rc = edd_dev_is_type(edev, "SCSI");
	if (rc)
		return NULL;

	pci_dev = edd_get_pci_dev(edev);
	if (!pci_dev)
		return NULL;

	get_device(&pci_dev->dev);

	list_for_each(shost_node, &pci_dev->dev.children) {
		shost_dev = children_to_dev(shost_node);
		get_device(shost_dev);
		sh = to_scsi_host(shost_dev);

		list_for_each(sdev_node, &shost_dev->children) {
			sdev_dev = children_to_dev(sdev_node);
			get_device(sdev_dev);
			sd = to_scsi_device(sdev_dev);
			rc = edd_match_scsidev(edev, sd);
			put_device(sdev_dev);
			if (!rc)
				break;
		}
		put_device(shost_dev);
		if (!rc)
			break;
	}

	put_device(&pci_dev->dev);
	if (!rc)
		return sd;
	return NULL;
}

static int
edd_create_symlink_to_scsidev(struct edd_device *edev)
{

	struct scsi_device *sdev;
	struct pci_dev *pci_dev;
	struct edd_info *info = edd_dev_get_info(edev);
	int rc;

	rc = edd_dev_is_type(edev, "PCI");
	if (rc)
		return rc;

	pci_dev = pci_find_slot(info->params.interface_path.pci.bus,
				PCI_DEVFN(info->params.interface_path.pci.slot,
					  info->params.interface_path.pci.
					  function));
	if (!pci_dev)
		return 1;

	sdev = edd_find_matching_scsi_device(edev);
	if (!sdev)
		return 1;

	get_device(&sdev->sdev_driverfs_dev);
	rc = edd_device_symlink(edev, &sdev->sdev_driverfs_dev, "disc");
	put_device(&sdev->sdev_driverfs_dev);

	return rc;
}

static inline int
edd_create_file(struct edd_device *edev, struct edd_attribute *attr)
{
	return driverfs_create_file(&attr->attr, &edev->dir);
}

static inline void
edd_device_unregister(struct edd_device *edev)
{
	driverfs_remove_dir(&edev->dir);
}

static int
edd_populate_dir(struct edd_device *edev)
{
	struct edd_attribute *attr;
	int i;
	int error = 0;

	for (i = 0; (attr=def_attrs[i]); i++) {
		if (!attr->test || (attr->test && !attr->test(edev))) {
			if ((error = edd_create_file(edev, attr))) {
				break;
			}
		}
	}

	if (error)
		return error;

	edd_create_symlink_to_pcidev(edev);
	edd_create_symlink_to_scsidev(edev);

	return 0;
}

static int
edd_make_dir(struct edd_device *edev)
{
	int error;

	edev->dir.name = edev->name;
	edev->dir.mode = (S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO);
	edev->dir.ops = &edd_attr_ops;

	error = driverfs_create_dir(&edev->dir, &bios_dir);
	if (!error)
		error = edd_populate_dir(edev);
	return error;
}

static int
edd_device_register(struct edd_device *edev, int i)
{
	int error;

	if (!edev)
		return 1;
	memset(edev, 0, sizeof (*edev));
	edd_dev_set_info(edev, &edd[i]);
	snprintf(edev->name, EDD_DEVICE_NAME_SIZE, "int13_dev%02x",
		 edd[i].device);
	error = edd_make_dir(edev);
	return error;
}

/**
 * edd_init() - creates driverfs tree of EDD data
 *
 * This assumes that eddnr and edd were
 * assigned in setup.c already.
 */
static int __init
edd_init(void)
{
	unsigned int i;
	int rc=0;
	struct edd_device *edev;

	printk(KERN_INFO "BIOS EDD facility v%s, %d devices found\n",
	       EDD_VERSION, eddnr);

	if (!eddnr) {
		printk(KERN_INFO "EDD information not available.\n");
		return 1;
	}

	rc = driverfs_create_dir(&bios_dir, NULL);
	if (rc)
		return rc;

	for (i = 0; i < eddnr && i < EDDMAXNR && !rc; i++) {
		edev = kmalloc(sizeof (*edev), GFP_KERNEL);
		if (!edev)
			return -ENOMEM;

		rc = edd_device_register(edev, i);
		if (rc) {
			kfree(edev);
			break;
		}
		edd_devices[i] = edev;
	}

	if (rc) {
		driverfs_remove_dir(&bios_dir);
		return rc;
	}

	return 0;
}

static void __exit
edd_exit(void)
{
	int i;
	struct edd_device *edev;

	for (i = 0; i < eddnr && i < EDDMAXNR; i++) {
		if ((edev = edd_devices[i])) {
			edd_device_unregister(edev);
			kfree(edev);
		}
	}

	driverfs_remove_dir(&bios_dir);
}

late_initcall(edd_init);
module_exit(edd_exit);
