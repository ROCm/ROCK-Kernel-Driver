/* 
 *  FiberChannel transport specific attributes exported to sysfs.
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
#include <scsi/scsi_transport_fc.h>

#define FC_PRINTK(x, l, f, a...)	printk(l "scsi(%d:%d:%d:%d): " f, (x)->host->host_no, (x)->channel, (x)->id, (x)->lun , ##a)

static void transport_class_release(struct class_device *class_dev);

#define FC_NUM_ATTRS 	3	/* increase this if you add attributes */
#define FC_OTHER_ATTRS 	0	/* increase this if you add "always on"
				 * attributes */

struct fc_internal {
	struct scsi_transport_template t;
	struct fc_function_template *f;
	/* The actual attributes */
	struct class_device_attribute private_attrs[FC_NUM_ATTRS];
	/* The array of null terminated pointers to attributes
	 * needed by scsi_sysfs.c */
	struct class_device_attribute *attrs[FC_NUM_ATTRS + FC_OTHER_ATTRS + 1];
};

#define to_fc_internal(tmpl)	container_of(tmpl, struct fc_internal, t)

struct class fc_transport_class = {
	.name = "fc_transport",
	.release = transport_class_release,
};

static __init int fc_transport_init(void)
{
	return class_register(&fc_transport_class);
}

static void __exit fc_transport_exit(void)
{
	class_unregister(&fc_transport_class);
}

static int fc_setup_transport_attrs(struct scsi_device *sdev)
{
	/* I'm not sure what values are invalid.  We should pick some invalid
	 * values for the defaults */
	fc_node_name(sdev) = -1;
	fc_port_name(sdev) = -1;
	fc_port_id(sdev) = -1;

	return 0;
}

static void transport_class_release(struct class_device *class_dev)
{
	struct scsi_device *sdev = transport_class_to_sdev(class_dev);
	put_device(&sdev->sdev_gendev);
}

#define fc_transport_show_function(field, format_string, cast)		\
									\
static ssize_t								\
show_fc_transport_##field (struct class_device *cdev, char *buf)	\
{									\
	struct scsi_device *sdev = transport_class_to_sdev(cdev);	\
	struct fc_transport_attrs *tp;					\
	struct fc_internal *i = to_fc_internal(sdev->host->transportt);	\
	tp = (struct fc_transport_attrs *)&sdev->transport_data;	\
	if (i->f->get_##field)						\
		i->f->get_##field(sdev);				\
	return snprintf(buf, 20, format_string, cast tp->field);	\
}

#define fc_transport_store_function(field, format_string)		\
static ssize_t								\
store_fc_transport_##field(struct class_device *cdev, const char *buf,	\
			   size_t count)				\
{									\
	int val;							\
	struct scsi_device *sdev = transport_class_to_sdev(cdev);	\
	struct fc_internal *i = to_fc_internal(sdev->host->transportt);	\
									\
	val = simple_strtoul(buf, NULL, 0);				\
	i->f->set_##field(sdev, val);					\
	return count;							\
}

#define fc_transport_rd_attr(field, format_string)			\
	fc_transport_show_function(field, format_string, )		\
static CLASS_DEVICE_ATTR(field, S_IRUGO,				\
			 show_fc_transport_##field, NULL)

#define fc_transport_rd_attr_cast(field, format_string, cast)		\
	fc_transport_show_function(field, format_string, (cast))	\
static CLASS_DEVICE_ATTR( field, S_IRUGO,				\
			  show_fc_transport_##field, NULL)

#define fc_transport_rw_attr(field, format_string)			\
	fc_transport_show_function(field, format_string, )		\
	fc_transport_store_function(field, format_string)		\
static CLASS_DEVICE_ATTR(field, S_IRUGO | S_IWUSR,			\
			show_fc_transport_##field,			\
			store_fc_transport_##field)

/* the FiberChannel Tranport Attributes: */
fc_transport_rd_attr_cast(node_name, "0x%llx\n", unsigned long long);
fc_transport_rd_attr_cast(port_name, "0x%llx\n", unsigned long long);
fc_transport_rd_attr(port_id, "0x%06x\n");

#define SETUP_ATTRIBUTE_RD(field)				\
	i->private_attrs[count] = class_device_attr_##field;	\
	i->private_attrs[count].attr.mode = S_IRUGO;		\
	i->private_attrs[count].store = NULL;			\
	i->attrs[count] = &i->private_attrs[count];		\
	if (i->f->show_##field)					\
		count++

#define SETUP_ATTRIBUTE_RW(field)				\
	i->private_attrs[count] = class_device_attr_##field;	\
	if (!i->f->set_##field) {				\
		i->private_attrs[count].attr.mode = S_IRUGO;	\
		i->private_attrs[count].store = NULL;		\
	}							\
	i->attrs[count] = &i->private_attrs[count];		\
	if (i->f->show_##field)					\
		count++

struct scsi_transport_template *
fc_attach_transport(struct fc_function_template *ft)
{
	struct fc_internal *i = kmalloc(sizeof(struct fc_internal),
					GFP_KERNEL);
	int count = 0;

	if (unlikely(!i))
		return NULL;

	memset(i, 0, sizeof(struct fc_internal));

	i->t.attrs = &i->attrs[0];
	i->t.class = &fc_transport_class;
	i->t.setup = &fc_setup_transport_attrs;
	i->t.size = sizeof(struct fc_transport_attrs);
	i->f = ft;

	SETUP_ATTRIBUTE_RD(port_id);
	SETUP_ATTRIBUTE_RD(port_name);
	SETUP_ATTRIBUTE_RD(node_name);

	BUG_ON(count > FC_NUM_ATTRS);

	/* Setup the always-on attributes here */

	i->attrs[count] = NULL;

	return &i->t;
}
EXPORT_SYMBOL(fc_attach_transport);

void fc_release_transport(struct scsi_transport_template *t)
{
	struct fc_internal *i = to_fc_internal(t);

	kfree(i);
}
EXPORT_SYMBOL(fc_release_transport);


MODULE_AUTHOR("Martin Hicks");
MODULE_DESCRIPTION("FC Transport Attributes");
MODULE_LICENSE("GPL");

module_init(fc_transport_init);
module_exit(fc_transport_exit);
