/* 
 * dvbdev.c
 *
 * Copyright (C) 2000 Ralph  Metzler <ralph@convergence.de>
 *                  & Marcus Metzler <marcus@convergence.de>
 *                    for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/slab.h>

#include "dvbdev.h"
#include "dvb_functions.h"

static int dvbdev_debug = 0;
#define dprintk if (dvbdev_debug) printk

static LIST_HEAD(dvb_adapter_list);
static DECLARE_MUTEX(dvbdev_register_lock);


static char *dnames[] = { 
        "video", "audio", "sec", "frontend", "demux", "dvr", "ca",
	"net", "osd"
};


#define DVB_MAX_IDS              4
#define nums2minor(num,type,id)  ((num << 6) | (id << 4) | type)

static struct dvb_device* dvbdev_find_device (int minor)
{
	struct list_head *entry;

	list_for_each (entry, &dvb_adapter_list) {
		struct list_head *entry0;
		struct dvb_adapter *adap;
		adap = list_entry (entry, struct dvb_adapter, list_head);
		list_for_each (entry0, &adap->device_list) {
			struct dvb_device *dev;
			dev = list_entry (entry0, struct dvb_device, list_head);
			if (nums2minor(adap->num, dev->type, dev->id) == minor)
				return dev;
		}
	}

	return NULL;
}


static int dvb_device_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev;
	
	dvbdev = dvbdev_find_device (iminor(inode));

	if (dvbdev && dvbdev->fops) {
		int err = 0;
		struct file_operations *old_fops;

		file->private_data = dvbdev;
		old_fops = file->f_op;
                file->f_op = fops_get(dvbdev->fops);
                if(file->f_op->open)
                        err = file->f_op->open(inode,file);
                if (err) {
                        fops_put(file->f_op);
                        file->f_op = fops_get(old_fops);
                }
                fops_put(old_fops);
                return err;
	}
	return -ENODEV;
}


static struct file_operations dvb_device_fops =
{
	.owner =	THIS_MODULE,
	.open =		dvb_device_open,
};


int dvb_generic_open(struct inode *inode, struct file *file)
{
        struct dvb_device *dvbdev = file->private_data;

        if (!dvbdev)
                return -ENODEV;

	if (!dvbdev->users)
                return -EBUSY;

	if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
                if (!dvbdev->readers)
		        return -EBUSY;
		dvbdev->readers--;
	} else {
                if (!dvbdev->writers)
		        return -EBUSY;
		dvbdev->writers--;
	}

	dvbdev->users--;
	return 0;
}


int dvb_generic_release(struct inode *inode, struct file *file)
{
        struct dvb_device *dvbdev = file->private_data;

	if (!dvbdev)
                return -ENODEV;

	if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
		dvbdev->readers++;
	} else {
		dvbdev->writers++;
	}

	dvbdev->users++;
	return 0;
}


int dvb_generic_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg)
{
        struct dvb_device *dvbdev = file->private_data;
	
        if (!dvbdev)
	        return -ENODEV;

	if (!dvbdev->kernel_ioctl)
		return -EINVAL;

	return dvb_usercopy (inode, file, cmd, arg, dvbdev->kernel_ioctl);
}


static int dvbdev_get_free_id (struct dvb_adapter *adap, int type)
{
	u32 id = 0;

	while (id < DVB_MAX_IDS) {
		struct list_head *entry;
		list_for_each (entry, &adap->device_list) {
			struct dvb_device *dev;
			dev = list_entry (entry, struct dvb_device, list_head);
			if (dev->type == type && dev->id == id)
				goto skip;
		}
		return id;
skip:
		id++;
	}
	return -ENFILE;
}


int dvb_register_device(struct dvb_adapter *adap, struct dvb_device **pdvbdev, 
			const struct dvb_device *template, void *priv, int type)
{
	struct dvb_device *dvbdev;
	int id;

	if (down_interruptible (&dvbdev_register_lock))
		return -ERESTARTSYS;

	if ((id = dvbdev_get_free_id (adap, type)) < 0) {
		up (&dvbdev_register_lock);
		*pdvbdev = NULL;
		printk ("%s: could get find free device id...\n", __FUNCTION__);
		return -ENFILE;
	}

	*pdvbdev = dvbdev = kmalloc(sizeof(struct dvb_device), GFP_KERNEL);

	if (!dvbdev) {
		up(&dvbdev_register_lock);
		return -ENOMEM;
	}

	up (&dvbdev_register_lock);
	
	memcpy(dvbdev, template, sizeof(struct dvb_device));
	dvbdev->type = type;
	dvbdev->id = id;
	dvbdev->adapter = adap;
	dvbdev->priv = priv;

	dvbdev->fops->owner = adap->module;

	list_add_tail (&dvbdev->list_head, &adap->device_list);

	devfs_mk_cdev(MKDEV(DVB_MAJOR, nums2minor(adap->num, type, id)),
			S_IFCHR | S_IRUSR | S_IWUSR,
			"dvb/adapter%d/%s%d", adap->num, dnames[type], id);

	dprintk("DVB: register adapter%d/%s%d @ minor: %i (0x%02x)\n",
		adap->num, dnames[type], id, nums2minor(adap->num, type, id),
		nums2minor(adap->num, type, id));

	return 0;
}


void dvb_unregister_device(struct dvb_device *dvbdev)
{
	if (!dvbdev)
		return;

		devfs_remove("dvb/adapter%d/%s%d", dvbdev->adapter->num,
				dnames[dvbdev->type], dvbdev->id);

		list_del(&dvbdev->list_head);
		kfree(dvbdev);
	}


static int dvbdev_get_free_adapter_num (void)
{
	int num = 0;

	while (1) {
		struct list_head *entry;
		list_for_each (entry, &dvb_adapter_list) {
			struct dvb_adapter *adap;
			adap = list_entry (entry, struct dvb_adapter, list_head);
			if (adap->num == num)
				goto skip;
		}
		return num;
skip:
		num++;
	}

	return -ENFILE;
}


int dvb_register_adapter(struct dvb_adapter **padap, const char *name, struct module *module)
{
	struct dvb_adapter *adap;
	int num;

	if (down_interruptible (&dvbdev_register_lock))
		return -ERESTARTSYS;

	if ((num = dvbdev_get_free_adapter_num ()) < 0) {
		up (&dvbdev_register_lock);
		return -ENFILE;
	}

	if (!(*padap = adap = kmalloc(sizeof(struct dvb_adapter), GFP_KERNEL))) {
		up(&dvbdev_register_lock);
		return -ENOMEM;
	}

	memset (adap, 0, sizeof(struct dvb_adapter));
	INIT_LIST_HEAD (&adap->device_list);

	printk ("DVB: registering new adapter (%s).\n", name);
	
	devfs_mk_dir("dvb/adapter%d", num);

	adap->num = num;
	adap->name = name;
	adap->module = module;

	list_add_tail (&adap->list_head, &dvb_adapter_list);

	up (&dvbdev_register_lock);

	return num;
}


int dvb_unregister_adapter(struct dvb_adapter *adap)
{
	if (down_interruptible (&dvbdev_register_lock))
		return -ERESTARTSYS;
        devfs_remove("dvb/adapter%d", adap->num);
	list_del (&adap->list_head);
	up (&dvbdev_register_lock);
	kfree (adap);
	return 0;
}


static int __init init_dvbdev(void)
{
	int retval;
	devfs_mk_dir("dvb");

	retval = register_chrdev(DVB_MAJOR,"DVB", &dvb_device_fops);
	if (retval)
		printk("video_dev: unable to get major %d\n", DVB_MAJOR);

	return retval;
}


static void __exit exit_dvbdev(void)
{
	unregister_chrdev(DVB_MAJOR, "DVB");
        devfs_remove("dvb");
}

module_init(init_dvbdev);
module_exit(exit_dvbdev);

MODULE_DESCRIPTION("DVB Core Driver");
MODULE_AUTHOR("Marcus Metzler, Ralph Metzler, Holger Waechtler");
MODULE_LICENSE("GPL");

MODULE_PARM(dvbdev_debug,"i");
MODULE_PARM_DESC(dvbdev_debug, "enable verbose debug messages");

