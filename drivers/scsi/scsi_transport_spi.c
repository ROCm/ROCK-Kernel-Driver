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

#define SPI_NUM_ATTRS 10	/* increase this if you add attributes */

struct spi_internal {
	struct scsi_transport_template t;
	struct spi_function_template *f;
	/* The actual attributes */
	struct class_device_attribute private_attrs[SPI_NUM_ATTRS];
	/* The array of null terminated pointers to attributes 
	 * needed by scsi_sysfs.c */
	struct class_device_attribute *attrs[SPI_NUM_ATTRS + 1];
};

#define to_spi_internal(tmpl)	container_of(tmpl, struct spi_internal, t)

static const char *const ppr_to_ns[] = {
	/* The PPR values 0-6 are reserved, fill them in when
	 * the committee defines them */
	NULL,			/* 0x00 */
	NULL,			/* 0x01 */
	NULL,			/* 0x02 */
	NULL,			/* 0x03 */
	NULL,			/* 0x04 */
	NULL,			/* 0x05 */
	NULL,			/* 0x06 */
	"3.125",		/* 0x07 */
	"6.25",			/* 0x08 */
	"12.5",			/* 0x09 */
	"25",			/* 0x0a */
	"30.3",			/* 0x0b */
	"50",			/* 0x0c */
};
/* The PPR values at which you calculate the period in ns by multiplying
 * by 4 */
#define SPI_STATIC_PPR	0x0c

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
	spi_period(sdev) = -1;	/* illegal value */
	spi_offset(sdev) = 0;	/* async */
	spi_width(sdev) = 0;	/* narrow */
	spi_iu(sdev) = 0;	/* no IU */
	spi_dt(sdev) = 0;	/* ST */
	spi_qas(sdev) = 0;
	spi_wr_flow(sdev) = 0;
	spi_rd_strm(sdev) = 0;
	spi_rti(sdev) = 0;
	spi_pcomp_en(sdev) = 0;

	return 0;
}

static void transport_class_release(struct class_device *class_dev)
{
	struct scsi_device *sdev = transport_class_to_sdev(class_dev);
	put_device(&sdev->sdev_gendev);
}

#define spi_transport_show_function(field, format_string)		\
									\
static ssize_t								\
show_spi_transport_##field(struct class_device *cdev, char *buf)	\
{									\
	struct scsi_device *sdev = transport_class_to_sdev(cdev);	\
	struct spi_transport_attrs *tp;					\
	struct spi_internal *i = to_spi_internal(sdev->host->transportt); \
	tp = (struct spi_transport_attrs *)&sdev->transport_data;	\
	if(i->f->get_##field)						\
		i->f->get_##field(sdev);				\
	return snprintf(buf, 20, format_string, tp->field);		\
}

#define spi_transport_store_function(field, format_string)		\
static ssize_t								\
store_spi_transport_##field(struct class_device *cdev, const char *buf, \
			    size_t count)				\
{									\
	int val;							\
	struct scsi_device *sdev = transport_class_to_sdev(cdev);	\
	struct spi_internal *i = to_spi_internal(sdev->host->transportt); \
									\
	val = simple_strtoul(buf, NULL, 0);				\
	i->f->set_##field(sdev, val);					\
	return count;							\
}

#define spi_transport_rd_attr(field, format_string)			\
	spi_transport_show_function(field, format_string)		\
	spi_transport_store_function(field, format_string)		\
static CLASS_DEVICE_ATTR(field, S_IRUGO | S_IWUSR,			\
			 show_spi_transport_##field,			\
			 store_spi_transport_##field)

/* The Parallel SCSI Tranport Attributes: */
spi_transport_rd_attr(offset, "%d\n");
spi_transport_rd_attr(width, "%d\n");
spi_transport_rd_attr(iu, "%d\n");
spi_transport_rd_attr(dt, "%d\n");
spi_transport_rd_attr(qas, "%d\n");
spi_transport_rd_attr(wr_flow, "%d\n");
spi_transport_rd_attr(rd_strm, "%d\n");
spi_transport_rd_attr(rti, "%d\n");
spi_transport_rd_attr(pcomp_en, "%d\n");

/* Translate the period into ns according to the current spec
 * for SDTR/PPR messages */
static ssize_t show_spi_transport_period(struct class_device *cdev, char *buf)

{
	struct scsi_device *sdev = transport_class_to_sdev(cdev);
	struct spi_transport_attrs *tp;
	const char *str;
	struct spi_internal *i = to_spi_internal(sdev->host->transportt);

	tp = (struct spi_transport_attrs *)&sdev->transport_data;

	if(i->f->get_period)
		i->f->get_period(sdev);

	switch(tp->period) {

	case 0x07 ... SPI_STATIC_PPR:
		str = ppr_to_ns[tp->period];
		if(!str)
			str = "reserved";
		break;


	case (SPI_STATIC_PPR+1) ... 0xff:
		return sprintf(buf, "%d\n", tp->period * 4);

	default:
		str = "unknown";
	}
	return sprintf(buf, "%s\n", str);
}

static ssize_t
store_spi_transport_period(struct class_device *cdev, const char *buf,
			    size_t count)
{
	struct scsi_device *sdev = transport_class_to_sdev(cdev);
	struct spi_internal *i = to_spi_internal(sdev->host->transportt);
	int j, period = -1;

	for(j = 0; j < SPI_STATIC_PPR; j++) {
		int len;

		if(ppr_to_ns[j] == NULL)
			continue;

		len = strlen(ppr_to_ns[j]);

		if(strncmp(ppr_to_ns[j], buf, len) != 0)
			continue;

		if(buf[len] != '\n')
			continue;
		
		period = j;
		break;
	}

	if(period == -1) {
		int val = simple_strtoul(buf, NULL, 0);


		/* Should probably check limits here, but this
		 * gets reasonably close to OK for most things */
		period = val/4;
	}

	if(period > 0xff)
		period = 0xff;

	i->f->set_period(sdev, period);

	return count;
}
	
static CLASS_DEVICE_ATTR(period, S_IRUGO | S_IWUSR, 
			 show_spi_transport_period,
			 store_spi_transport_period);

#define SETUP_ATTRIBUTE(field)						\
	i->private_attrs[count] = class_device_attr_##field;		\
	if(!i->f->set_##field) {					\
		i->private_attrs[count].attr.mode = S_IRUGO;		\
		i->private_attrs[count].store = NULL;			\
	}								\
	i->attrs[count] = &i->private_attrs[count];			\
	if(i->f->show_##field)						\
		count++

struct scsi_transport_template *
spi_attach_transport(struct spi_function_template *ft)
{
	struct spi_internal *i = kmalloc(sizeof(struct spi_internal),
					 GFP_KERNEL);
	int count = 0;
	if(!i)
		return NULL;

	memset(i, 0, sizeof(struct spi_internal));


	i->t.attrs = &i->attrs[0];
	i->t.class = &spi_transport_class;
	i->t.setup = &spi_setup_transport_attrs;
	i->t.size = sizeof(struct spi_transport_attrs) - sizeof(unsigned long);
	i->f = ft;

	SETUP_ATTRIBUTE(period);
	SETUP_ATTRIBUTE(offset);
	SETUP_ATTRIBUTE(width);
	SETUP_ATTRIBUTE(iu);
	SETUP_ATTRIBUTE(dt);
	SETUP_ATTRIBUTE(qas);
	SETUP_ATTRIBUTE(wr_flow);
	SETUP_ATTRIBUTE(rd_strm);
	SETUP_ATTRIBUTE(rti);
	SETUP_ATTRIBUTE(pcomp_en);

	/* if you add an attribute but forget to increase SPI_NUM_ATTRS
	 * this bug will trigger */
	BUG_ON(count > SPI_NUM_ATTRS);

	i->attrs[count] = NULL;

	return &i->t;
}
EXPORT_SYMBOL(spi_attach_transport);

void spi_release_transport(struct scsi_transport_template *t)
{
	struct spi_internal *i = to_spi_internal(t);

	kfree(i);
}
EXPORT_SYMBOL(spi_release_transport);


MODULE_AUTHOR("Martin Hicks");
MODULE_DESCRIPTION("SPI Transport Attributes");
MODULE_LICENSE("GPL");

module_init(spi_transport_init);
module_exit(spi_transport_exit);
