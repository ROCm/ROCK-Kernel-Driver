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
#include "scsi_priv.h"

#define FC_PRINTK(x, l, f, a...)	printk(l "scsi(%d:%d:%d:%d): " f, (x)->host->host_no, (x)->channel, (x)->id, (x)->lun , ##a)

static void transport_class_release(struct class_device *class_dev);
static void host_class_release(struct class_device *class_dev);

#define FC_STARGET_NUM_ATTRS 	4	/* increase this if you add attributes */
#define FC_STARGET_OTHER_ATTRS 	0	/* increase this if you add "always on"
					 * attributes */
#define FC_HOST_NUM_ATTRS	1

struct fc_internal {
	struct scsi_transport_template t;
	struct fc_function_template *f;
	/* The actual attributes */
	struct class_device_attribute private_starget_attrs[
						FC_STARGET_NUM_ATTRS];
	/* The array of null terminated pointers to attributes
	 * needed by scsi_sysfs.c */
	struct class_device_attribute *starget_attrs[
			FC_STARGET_NUM_ATTRS + FC_STARGET_OTHER_ATTRS + 1];

	struct class_device_attribute private_host_attrs[FC_HOST_NUM_ATTRS];
	struct class_device_attribute *host_attrs[FC_HOST_NUM_ATTRS + 1];
};

#define to_fc_internal(tmpl)	container_of(tmpl, struct fc_internal, t)

struct class fc_transport_class = {
	.name = "fc_transport",
	.release = transport_class_release,
};

struct class fc_host_class = {
	.name = "fc_host",
	.release = host_class_release,
};

static __init int fc_transport_init(void)
{
	int error = class_register(&fc_host_class);
	if (error)
		return error;
	return class_register(&fc_transport_class);
}

static void __exit fc_transport_exit(void)
{
	class_unregister(&fc_transport_class);
	class_unregister(&fc_host_class);
}

static int fc_setup_starget_transport_attrs(struct scsi_target *starget)
{
	/* 
	 * Set default values easily detected by the midlayer as
	 * failure cases.  The scsi lldd is responsible for initializing
	 * all transport attributes to valid values per target.
	 */
	fc_starget_node_name(starget) = -1;
	fc_starget_port_name(starget) = -1;
	fc_starget_port_id(starget) = -1;
	fc_starget_dev_loss_tmo(starget) = -1;
	init_timer(&fc_starget_dev_loss_timer(starget));
	return 0;
}

static int fc_setup_host_transport_attrs(struct Scsi_Host *shost)
{
	/* 
	 * Set default values easily detected by the midlayer as
	 * failure cases.  The scsi lldd is responsible for initializing
	 * all transport attributes to valid values per host.
	 */
	fc_host_link_down_tmo(shost) = -1;
	init_timer(&fc_host_link_down_timer(shost));
	return 0;
}

static void transport_class_release(struct class_device *class_dev)
{
	struct scsi_target *starget = transport_class_to_starget(class_dev);
	put_device(&starget->dev);
}

static void host_class_release(struct class_device *class_dev)
{
	struct Scsi_Host *shost = transport_class_to_shost(class_dev);
	put_device(&shost->shost_gendev);
}


/*
 * Remote Port Attribute Management
 */

#define fc_starget_show_function(field, format_string, cast)		\
static ssize_t								\
show_fc_starget_##field (struct class_device *cdev, char *buf)		\
{									\
	struct scsi_target *starget = transport_class_to_starget(cdev);	\
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);	\
	struct fc_starget_attrs *tp;					\
	struct fc_internal *i = to_fc_internal(shost->transportt);	\
	tp = (struct fc_starget_attrs *)&starget->starget_data;		\
	if (i->f->get_starget_##field)					\
		i->f->get_starget_##field(starget);			\
	return snprintf(buf, 20, format_string, cast tp->field);	\
}

#define fc_starget_store_function(field, format_string)			\
static ssize_t								\
store_fc_starget_##field(struct class_device *cdev, const char *buf,	\
			   size_t count)				\
{									\
	int val;							\
	struct scsi_target *starget = transport_class_to_starget(cdev);	\
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);	\
	struct fc_internal *i = to_fc_internal(shost->transportt);	\
									\
	val = simple_strtoul(buf, NULL, 0);				\
	i->f->set_starget_##field(starget, val);			\
	return count;							\
}

#define fc_starget_rd_attr(field, format_string)			\
	fc_starget_show_function(field, format_string, )		\
static CLASS_DEVICE_ATTR(field, S_IRUGO,				\
			 show_fc_starget_##field, NULL)

#define fc_starget_rd_attr_cast(field, format_string, cast)		\
	fc_starget_show_function(field, format_string, (cast))		\
static CLASS_DEVICE_ATTR(field, S_IRUGO,				\
			  show_fc_starget_##field, NULL)

#define fc_starget_rw_attr(field, format_string)			\
	fc_starget_show_function(field, format_string, )		\
	fc_starget_store_function(field, format_string)			\
static CLASS_DEVICE_ATTR(field, S_IRUGO | S_IWUSR,			\
			show_fc_starget_##field,			\
			store_fc_starget_##field)

#define SETUP_STARGET_ATTRIBUTE_RD(field)				\
	i->private_starget_attrs[count] = class_device_attr_##field;	\
	i->private_starget_attrs[count].attr.mode = S_IRUGO;		\
	i->private_starget_attrs[count].store = NULL;			\
	i->starget_attrs[count] = &i->private_starget_attrs[count];	\
	if (i->f->show_starget_##field)					\
		count++

#define SETUP_STARGET_ATTRIBUTE_RW(field)				\
	i->private_starget_attrs[count] = class_device_attr_##field;	\
	if (!i->f->set_starget_##field) {				\
		i->private_starget_attrs[count].attr.mode = S_IRUGO;	\
		i->private_starget_attrs[count].store = NULL;		\
	}								\
	i->starget_attrs[count] = &i->private_starget_attrs[count];	\
	if (i->f->show_starget_##field)					\
		count++

/* The FC Tranport Remote Port (Target) Attributes: */
fc_starget_rd_attr_cast(node_name, "0x%llx\n", unsigned long long);
fc_starget_rd_attr_cast(port_name, "0x%llx\n", unsigned long long);
fc_starget_rd_attr(port_id, "0x%06x\n");
fc_starget_rw_attr(dev_loss_tmo, "%d\n");


/*
 * Host Attribute Management
 */

#define fc_host_show_function(field, format_string, cast)		\
static ssize_t								\
show_fc_host_##field (struct class_device *cdev, char *buf)		\
{									\
	struct Scsi_Host *shost = transport_class_to_shost(cdev);	\
	struct fc_host_attrs *tp;					\
	struct fc_internal *i = to_fc_internal(shost->transportt);	\
	tp = (struct fc_host_attrs *)shost->shost_data;		\
	if (i->f->get_host_##field)					\
		i->f->get_host_##field(shost);				\
	return snprintf(buf, 20, format_string, cast tp->field);	\
}

#define fc_host_store_function(field, format_string)			\
static ssize_t								\
store_fc_host_##field(struct class_device *cdev, const char *buf,	\
			   size_t count)				\
{									\
	int val;							\
	struct Scsi_Host *shost = transport_class_to_shost(cdev);	\
	struct fc_internal *i = to_fc_internal(shost->transportt);	\
									\
	val = simple_strtoul(buf, NULL, 0);				\
	i->f->set_host_##field(shost, val);				\
	return count;							\
}

#define fc_host_rd_attr(field, format_string)				\
	fc_host_show_function(field, format_string, )			\
static CLASS_DEVICE_ATTR(host_##field, S_IRUGO,				\
			 show_fc_host_##field, NULL)

#define fc_host_rd_attr_cast(field, format_string, cast)		\
	fc_host_show_function(field, format_string, (cast))		\
static CLASS_DEVICE_ATTR(host_##field, S_IRUGO,				\
			  show_fc_host_##field, NULL)

#define fc_host_rw_attr(field, format_string)				\
	fc_host_show_function(field, format_string, )			\
	fc_host_store_function(field, format_string)			\
static CLASS_DEVICE_ATTR(host_##field, S_IRUGO | S_IWUSR,		\
			show_fc_host_##field,				\
			store_fc_host_##field)

#define SETUP_HOST_ATTRIBUTE_RD(field)					\
	i->private_host_attrs[count] = class_device_attr_host_##field;	\
	i->private_host_attrs[count].attr.mode = S_IRUGO;		\
	i->private_host_attrs[count].store = NULL;			\
	i->host_attrs[count] = &i->private_host_attrs[count];		\
	if (i->f->show_host_##field)					\
		count++

#define SETUP_HOST_ATTRIBUTE_RW(field)					\
	i->private_host_attrs[count] = class_device_attr_host_##field;	\
	if (!i->f->set_host_##field) {					\
		i->private_host_attrs[count].attr.mode = S_IRUGO;	\
		i->private_host_attrs[count].store = NULL;		\
	}								\
	i->host_attrs[count] = &i->private_host_attrs[count];		\
	if (i->f->show_host_##field)					\
		count++

/* The FC Tranport Host Attributes: */
fc_host_rw_attr(link_down_tmo, "%d\n");



struct scsi_transport_template *
fc_attach_transport(struct fc_function_template *ft)
{
	struct fc_internal *i = kmalloc(sizeof(struct fc_internal),
					GFP_KERNEL);
	int count = 0;

	if (unlikely(!i))
		return NULL;

	memset(i, 0, sizeof(struct fc_internal));

	i->t.target_attrs = &i->starget_attrs[0];
	i->t.target_class = &fc_transport_class;
	i->t.target_setup = &fc_setup_starget_transport_attrs;
	i->t.target_size = sizeof(struct fc_starget_attrs);

	i->t.host_attrs = &i->host_attrs[0];
	i->t.host_class = &fc_host_class;
	i->t.host_setup = &fc_setup_host_transport_attrs;
	i->t.host_size = sizeof(struct fc_host_attrs);
	i->f = ft;

	
	/*
	 * setup remote port (target) attributes
	 */
	SETUP_STARGET_ATTRIBUTE_RD(port_id);
	SETUP_STARGET_ATTRIBUTE_RD(port_name);
	SETUP_STARGET_ATTRIBUTE_RD(node_name);
	SETUP_STARGET_ATTRIBUTE_RW(dev_loss_tmo);

	BUG_ON(count > FC_STARGET_NUM_ATTRS);

	/* Setup the always-on attributes here */

	i->starget_attrs[count] = NULL;


	/* setup host attributes */
	count=0;
	SETUP_HOST_ATTRIBUTE_RW(link_down_tmo);

	BUG_ON(count > FC_HOST_NUM_ATTRS);

	/* Setup the always-on attributes here */

	i->host_attrs[count] = NULL;


	return &i->t;
}
EXPORT_SYMBOL(fc_attach_transport);

void fc_release_transport(struct scsi_transport_template *t)
{
	struct fc_internal *i = to_fc_internal(t);

	kfree(i);
}
EXPORT_SYMBOL(fc_release_transport);



/**
 * fc_device_block - called by target functions to block a scsi device
 * @dev:	scsi device
 * @data:	unused
 **/
static int fc_device_block(struct device *dev, void *data)
{
	scsi_internal_device_block(to_scsi_device(dev));
	return 0;
}

/**
 * fc_device_unblock - called by target functions to unblock a scsi device
 * @dev:	scsi device
 * @data:	unused
 **/
static int fc_device_unblock(struct device *dev, void *data)
{
	scsi_internal_device_unblock(to_scsi_device(dev));
	return 0;
}

/**
 * fc_timeout_blocked_tgt - Timeout handler for blocked scsi targets
 *			 that fail to recover in the alloted time.
 * @data:	scsi target that failed to reappear in the alloted time.
 **/
static void fc_timeout_blocked_tgt(unsigned long data)
{
	struct scsi_target *starget = (struct scsi_target *)data;

	dev_printk(KERN_ERR, &starget->dev, 
		"blocked target time out: target resuming\n");

	/* 
	 * set the device going again ... if the scsi lld didn't
	 * unblock this device, then IO errors will probably
	 * result if the host still isn't ready.
	 */
	device_for_each_child(&starget->dev, NULL, fc_device_unblock);
}

/**
 * fc_target_block - block a target by temporarily putting all its scsi devices
 *		into the SDEV_BLOCK state.
 * @starget:	scsi target managed by this fc scsi lldd.
 *
 * scsi lldd's with a FC transport call this routine to temporarily stop all
 * scsi commands to all devices managed by this scsi target.  Called 
 * from interrupt or normal process context.
 *
 * Returns zero if successful or error if not
 *
 * Notes:       
 *	The timeout and timer types are extracted from the fc transport 
 *	attributes from the caller's target pointer.  This routine assumes no
 *	locks are held on entry.
 **/
int
fc_target_block(struct scsi_target *starget)
{
	int timeout = fc_starget_dev_loss_tmo(starget);
	struct timer_list *timer = &fc_starget_dev_loss_timer(starget);

	if (timeout < 0 || timeout > SCSI_DEVICE_BLOCK_MAX_TIMEOUT)
		return -EINVAL;

	device_for_each_child(&starget->dev, NULL, fc_device_block);

	/* The scsi lld blocks this target for the timeout period only. */
	timer->data = (unsigned long)starget;
	timer->expires = jiffies + timeout * HZ;
	timer->function = fc_timeout_blocked_tgt;
	add_timer(timer);

	return 0;
}
EXPORT_SYMBOL(fc_target_block);

/**
 * fc_target_unblock - unblock a target following a fc_target_block request.
 * @starget:	scsi target managed by this fc scsi lldd.	
 *
 * scsi lld's with a FC transport call this routine to restart IO to all 
 * devices associated with the caller's scsi target following a fc_target_block
 * request.  Called from interrupt or normal process context.
 *
 * Notes:       
 *	This routine assumes no locks are held on entry.
 **/
void
fc_target_unblock(struct scsi_target *starget)
{
	/* 
	 * Stop the target timer first. Take no action on the del_timer
	 * failure as the state machine state change will validate the
	 * transaction. 
	 */
	del_timer_sync(&fc_starget_dev_loss_timer(starget));

	device_for_each_child(&starget->dev, NULL, fc_device_unblock);
}
EXPORT_SYMBOL(fc_target_unblock);

/**
 * fc_timeout_blocked_host - Timeout handler for blocked scsi hosts
 *			 that fail to recover in the alloted time.
 * @data:	scsi host that failed to recover its devices in the alloted
 *		time.
 **/
static void fc_timeout_blocked_host(unsigned long data)
{
	struct Scsi_Host *shost = (struct Scsi_Host *)data;
	struct scsi_device *sdev;

	dev_printk(KERN_ERR, &shost->shost_gendev, 
		"blocked host time out: host resuming\n");

	shost_for_each_device(sdev, shost) {
		/* 
		 * set the device going again ... if the scsi lld didn't
		 * unblock this device, then IO errors will probably
		 * result if the host still isn't ready.
		 */
		scsi_internal_device_unblock(sdev);
	}
}

/**
 * fc_host_block - block all scsi devices managed by the calling host temporarily 
 *		by putting each device in the SDEV_BLOCK state.
 * @shost:	scsi host pointer that contains all scsi device siblings.
 *
 * scsi lld's with a FC transport call this routine to temporarily stop all
 * scsi commands to all devices managed by this host.  Called 
 * from interrupt or normal process context.
 *
 * Returns zero if successful or error if not
 *
 * Notes:
 *	The timeout and timer types are extracted from the fc transport 
 *	attributes from the caller's host pointer.  This routine assumes no
 *	locks are held on entry.
 **/
int
fc_host_block(struct Scsi_Host *shost)
{
	struct scsi_device *sdev;
	int timeout = fc_host_link_down_tmo(shost);
	struct timer_list *timer = &fc_host_link_down_timer(shost);

	if (timeout < 0 || timeout > SCSI_DEVICE_BLOCK_MAX_TIMEOUT)
		return -EINVAL;

	shost_for_each_device(sdev, shost) {
		scsi_internal_device_block(sdev);
	}

	/* The scsi lld blocks this host for the timeout period only. */
	timer->data = (unsigned long)shost;
	timer->expires = jiffies + timeout * HZ;
	timer->function = fc_timeout_blocked_host;
	add_timer(timer);

	return 0;
}
EXPORT_SYMBOL(fc_host_block);

/**
 * fc_host_unblock - unblock all devices managed by this host following a 
 *		fc_host_block request.
 * @shost:	scsi host containing all scsi device siblings to unblock.
 *
 * scsi lld's with a FC transport call this routine to restart IO to all scsi
 * devices managed by the specified scsi host following an fc_host_block 
 * request.  Called from interrupt or normal process context.
 *
 * Notes:       
 *	This routine assumes no locks are held on entry.
 **/
void
fc_host_unblock(struct Scsi_Host *shost)
{
	struct scsi_device *sdev;

	/* 
	 * Stop the host timer first. Take no action on the del_timer
	 * failure as the state machine state change will validate the
	 * transaction.
	 */
	del_timer_sync(&fc_host_link_down_timer(shost));
	shost_for_each_device(sdev, shost) {
		scsi_internal_device_unblock(sdev);
	}
}
EXPORT_SYMBOL(fc_host_unblock);

MODULE_AUTHOR("Martin Hicks");
MODULE_DESCRIPTION("FC Transport Attributes");
MODULE_LICENSE("GPL");

module_init(fc_transport_init);
module_exit(fc_transport_exit);
