/*
 * Xen SCSI backend driver
 *
 * Copyright (c) 2008, FUJITSU Limited
 *
 * Based on the blkback driver code.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdarg.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>

#include "common.h"

struct backend_info
{
	struct xenbus_device *dev;
	struct vscsibk_info *info;
};


static int __vscsiif_name(struct backend_info *be, char *buf)
{
	struct xenbus_device *dev = be->dev;
	unsigned int domid, id;

	sscanf(dev->nodename, "backend/vscsi/%u/%u", &domid, &id);
	snprintf(buf, TASK_COMM_LEN, "vscsi.%u.%u", be->info->domid, id);

	return 0;
}

static int scsiback_map(struct backend_info *be)
{
	struct xenbus_device *dev = be->dev;
	unsigned long ring_ref;
	unsigned int evtchn;
	int err;
	char name[TASK_COMM_LEN];

	err = xenbus_gather(XBT_NIL, dev->otherend,
			"ring-ref", "%lu", &ring_ref,
			"event-channel", "%u", &evtchn, NULL);
	if (err) {
		xenbus_dev_fatal(dev, err, "reading %s ring", dev->otherend);
		return err;
	}

	err = scsiback_init_sring(be->info, ring_ref, evtchn);
	if (err)
		return err;

	err = __vscsiif_name(be, name);
	if (err) {
		xenbus_dev_error(dev, err, "get scsiback dev name");
		return err;
	}

	be->info->kthread = kthread_run(scsiback_schedule, be->info, name);
	if (IS_ERR(be->info->kthread)) {
		err = PTR_ERR(be->info->kthread);
		be->info->kthread = NULL;
		xenbus_dev_error(be->dev, err, "start vscsiif");
		return err;
	}

	return 0;
}


struct scsi_device *scsiback_get_scsi_device(struct ids_tuple *phy)
{
	struct Scsi_Host *shost;
	struct scsi_device *sdev = NULL;

	shost = scsi_host_lookup(phy->hst);
	if (IS_ERR(shost)) {
		printk(KERN_ERR "scsiback: host%d doesn't exist.\n",
			phy->hst);
		return NULL;
	}
	sdev   = scsi_device_lookup(shost, phy->chn, phy->tgt, phy->lun);
	if (!sdev) {
		printk(KERN_ERR "scsiback: %d:%d:%d:%d doesn't exist.\n",
			phy->hst, phy->chn, phy->tgt, phy->lun);
		scsi_host_put(shost);
		return NULL;
	}

	scsi_host_put(shost);
	return (sdev);
}

#define VSCSIBACK_OP_ADD_OR_DEL_LUN	1
#define VSCSIBACK_OP_UPDATEDEV_STATE	2


static void scsiback_do_lun_hotplug(struct backend_info *be, int op)
{
	int i, err = 0;
	struct ids_tuple phy, vir;
	int device_state;
	char str[64], state_str[64];
	char **dir;
	unsigned int dir_n = 0;
	struct xenbus_device *dev = be->dev;
	struct scsi_device *sdev;

	dir = xenbus_directory(XBT_NIL, dev->nodename, "vscsi-devs", &dir_n);
	if (IS_ERR(dir))
		return;

	for (i = 0; i < dir_n; i++) {
		
		/* read status */
		snprintf(state_str, sizeof(state_str), "vscsi-devs/%s/state", dir[i]);
		err = xenbus_scanf(XBT_NIL, dev->nodename, state_str, "%u",
			&device_state);
		if (XENBUS_EXIST_ERR(err))
			continue;

		/* physical SCSI device */
		snprintf(str, sizeof(str), "vscsi-devs/%s/p-dev", dir[i]);
		err = xenbus_scanf(XBT_NIL, dev->nodename, str,
			"%u:%u:%u:%u", &phy.hst, &phy.chn, &phy.tgt, &phy.lun);
		if (XENBUS_EXIST_ERR(err)) {
			xenbus_printf(XBT_NIL, dev->nodename, state_str,
					"%d", XenbusStateClosed);
			continue;
		}

		/* virtual SCSI device */
		snprintf(str, sizeof(str), "vscsi-devs/%s/v-dev", dir[i]);
		err = xenbus_scanf(XBT_NIL, dev->nodename, str,
			"%u:%u:%u:%u", &vir.hst, &vir.chn, &vir.tgt, &vir.lun);
		if (XENBUS_EXIST_ERR(err)) {
			xenbus_printf(XBT_NIL, dev->nodename, state_str,
					"%d", XenbusStateClosed);
			continue;
		}

		switch (op) {
		case VSCSIBACK_OP_ADD_OR_DEL_LUN:
			if (device_state == XenbusStateInitialising) {
				sdev = scsiback_get_scsi_device(&phy);
				if (!sdev)
					xenbus_printf(XBT_NIL, dev->nodename, state_str, 
							    "%d", XenbusStateClosed);
				else {
					err = scsiback_add_translation_entry(be->info, sdev, &vir);
					if (!err) {
						if (xenbus_printf(XBT_NIL, dev->nodename, state_str, 
								    "%d", XenbusStateInitialised)) {
							printk(KERN_ERR "scsiback: xenbus_printf error %s\n", state_str);
							scsiback_del_translation_entry(be->info, &vir);
						}
					} else {
						scsi_device_put(sdev);
						xenbus_printf(XBT_NIL, dev->nodename, state_str, 
								    "%d", XenbusStateClosed);
					}
				}
			}

			if (device_state == XenbusStateClosing) {
				if (!scsiback_del_translation_entry(be->info, &vir)) {
					if (xenbus_printf(XBT_NIL, dev->nodename, state_str, 
							    "%d", XenbusStateClosed))
						printk(KERN_ERR "scsiback: xenbus_printf error %s\n", state_str);
				}
			}
			break;

		case VSCSIBACK_OP_UPDATEDEV_STATE:
			if (device_state == XenbusStateInitialised) {
				/* modify vscsi-devs/dev-x/state */
				if (xenbus_printf(XBT_NIL, dev->nodename, state_str, 
						    "%d", XenbusStateConnected)) {
					printk(KERN_ERR "scsiback: xenbus_printf error %s\n", state_str);
					scsiback_del_translation_entry(be->info, &vir);
					xenbus_printf(XBT_NIL, dev->nodename, state_str, 
							    "%d", XenbusStateClosed);
				}
			}
			break;
		/*When it is necessary, processing is added here.*/
		default:
			break;
		}
	}

	kfree(dir);
	return ;
}


static void scsiback_frontend_changed(struct xenbus_device *dev,
					enum xenbus_state frontend_state)
{
	struct backend_info *be = dev->dev.driver_data;
	int err;

	switch (frontend_state) {
	case XenbusStateInitialising:
		break;
	case XenbusStateInitialised:
		err = scsiback_map(be);
		if (err)
			break;

		scsiback_do_lun_hotplug(be, VSCSIBACK_OP_ADD_OR_DEL_LUN);
		xenbus_switch_state(dev, XenbusStateConnected);

		break;
	case XenbusStateConnected:

		scsiback_do_lun_hotplug(be, VSCSIBACK_OP_UPDATEDEV_STATE);

		if (dev->state == XenbusStateConnected)
			break;

		xenbus_switch_state(dev, XenbusStateConnected);

		break;

	case XenbusStateClosing:
		scsiback_disconnect(be->info);
		xenbus_switch_state(dev, XenbusStateClosing);
		break;

	case XenbusStateClosed:
		xenbus_switch_state(dev, XenbusStateClosed);
		if (xenbus_dev_is_online(dev))
			break;
		/* fall through if not online */
	case XenbusStateUnknown:
		device_unregister(&dev->dev);
		break;

	case XenbusStateReconfiguring:
		scsiback_do_lun_hotplug(be, VSCSIBACK_OP_ADD_OR_DEL_LUN);

		xenbus_switch_state(dev, XenbusStateReconfigured);

		break;

	default:
		xenbus_dev_fatal(dev, -EINVAL, "saw state %d at frontend",
					frontend_state);
		break;
	}
}


static int scsiback_remove(struct xenbus_device *dev)
{
	struct backend_info *be = dev->dev.driver_data;

	if (be->info) {
		scsiback_disconnect(be->info);
		scsiback_release_translation_entry(be->info);
		scsiback_free(be->info);
		be->info = NULL;
	}

	kfree(be);
	dev->dev.driver_data = NULL;

	return 0;
}


static int scsiback_probe(struct xenbus_device *dev,
			   const struct xenbus_device_id *id)
{
	int err;
	unsigned val = 0;

	struct backend_info *be = kzalloc(sizeof(struct backend_info),
					  GFP_KERNEL);

	DPRINTK("%p %d\n", dev, dev->otherend_id);

	if (!be) {
		xenbus_dev_fatal(dev, -ENOMEM,
				 "allocating backend structure");
		return -ENOMEM;
	}
	be->dev = dev;
	dev->dev.driver_data = be;

	be->info = vscsibk_info_alloc(dev->otherend_id);
	if (IS_ERR(be->info)) {
		err = PTR_ERR(be->info);
		be->info = NULL;
		xenbus_dev_fatal(dev, err, "creating scsihost interface");
		goto fail;
	}

	be->info->dev = dev;
	be->info->irq = 0;
	be->info->feature = 0;	/*default not HOSTMODE.*/

	scsiback_init_translation_table(be->info);

	err = xenbus_scanf(XBT_NIL, dev->nodename,
				"feature-host", "%d", &val);
	if (XENBUS_EXIST_ERR(err))
		val = 0;

	if (val)
		be->info->feature = VSCSI_TYPE_HOST;

	err = xenbus_switch_state(dev, XenbusStateInitWait);
	if (err)
		goto fail;

	return 0;


fail:
	printk(KERN_WARNING "scsiback: %s failed\n",__FUNCTION__);
	scsiback_remove(dev);

	return err;
}


static struct xenbus_device_id scsiback_ids[] = {
	{ "vscsi" },
	{ "" }
};

static struct xenbus_driver scsiback = {
	.name			= "vscsi",
	.ids			= scsiback_ids,
	.probe			= scsiback_probe,
	.remove			= scsiback_remove,
	.otherend_changed	= scsiback_frontend_changed
};

int scsiback_xenbus_init(void)
{
	return xenbus_register_backend(&scsiback);
}

void scsiback_xenbus_unregister(void)
{
	xenbus_unregister_driver(&scsiback);
}
