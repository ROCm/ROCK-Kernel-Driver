/* 
 *  Parallel SCSI (SPI) transport specific attributes exported to sysfs.
 *
 *  Copyright (c) 2003 Silicon Graphics, Inc.  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/module.h>
#include <linux/init.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_spi.h>

static void transport_class_release(struct class_device *class_dev);

struct class spi_transport_class = {
	.name = "spi_transport",
	.release = transport_class_release,
};

static __init int spi_transport_init(void)
{
	return class_register(&spi_transport_class);
}

static void __exit spi_transport_exit(void)
{
	class_unregister(&spi_transport_class);
}

static int spi_setup_transport_attrs(struct scsi_device *sdev)
{
	/* FIXME: should callback into the driver to get these values */
	spi_period(sdev) = -1;
	spi_offset(sdev) = -1;

	return 0;
}

static void transport_class_release(struct class_device *class_dev)
{
	struct scsi_device *sdev = transport_class_to_sdev(class_dev);
	put_device(&sdev->sdev_gendev);
}

#define spi_transport_show_function(field, format_string)			\
static ssize_t									\
show_spi_transport_##field (struct class_device *cdev, char *buf)		\
{										\
	struct scsi_device *sdev = transport_class_to_sdev(cdev);		\
	struct spi_transport_attrs *tp;						\
	tp = (struct spi_transport_attrs *)&sdev->transport_data;		\
	return snprintf(buf, 20, format_string, tp->field);			\
}

#define spi_transport_rd_attr(field, format_string)				\
	spi_transport_show_function(field, format_string)			\
static CLASS_DEVICE_ATTR( field, S_IRUGO, show_spi_transport_##field, NULL)

/* The Parallel SCSI Tranport Attributes: */
spi_transport_rd_attr(period, "%d\n");
spi_transport_rd_attr(offset, "%d\n");

struct class_device_attribute *spi_transport_attrs[] = {
	&class_device_attr_period,
	&class_device_attr_offset,
	NULL
};

struct scsi_transport_template spi_transport_template = {
	.attrs = spi_transport_attrs,
	.class = &spi_transport_class,
	.setup = &spi_setup_transport_attrs,
	.cleanup = NULL,
	.size = sizeof(struct spi_transport_attrs) - sizeof(unsigned long),
};
EXPORT_SYMBOL(spi_transport_template);

MODULE_AUTHOR("Martin Hicks");
MODULE_DESCRIPTION("SPI Transport Attributes");
MODULE_LICENSE("GPL");

module_init(spi_transport_init);
module_exit(spi_transport_exit);
