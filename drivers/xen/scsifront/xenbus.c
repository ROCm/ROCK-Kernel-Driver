/*
 * Xen SCSI frontend driver
 *
 * Copyright (c) 2008, FUJITSU Limited
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
 

#include <linux/version.h>
#include "common.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11)
  #define DEFAULT_TASK_COMM_LEN	16
#else
  #define DEFAULT_TASK_COMM_LEN	TASK_COMM_LEN
#endif

extern struct scsi_host_template scsifront_sht;

static void scsifront_free(struct vscsifrnt_info *info)
{
	struct Scsi_Host *host = info->host;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14)
	if (host->shost_state != SHOST_DEL) {
#else
	if (!test_bit(SHOST_DEL, &host->shost_state)) {
#endif
		scsi_remove_host(info->host);
	}

	if (info->ring_ref != GRANT_INVALID_REF) {
		gnttab_end_foreign_access(info->ring_ref,
					(unsigned long)info->ring.sring);
		info->ring_ref = GRANT_INVALID_REF;
		info->ring.sring = NULL;
	}

	if (info->irq)
		unbind_from_irqhandler(info->irq, info);
	info->irq = 0;

	scsi_host_put(info->host);
}


static int scsifront_alloc_ring(struct vscsifrnt_info *info)
{
	struct xenbus_device *dev = info->dev;
	struct vscsiif_sring *sring;
	int err = -ENOMEM;


	info->ring_ref = GRANT_INVALID_REF;

	/***** Frontend to Backend ring start *****/
	sring = (struct vscsiif_sring *) __get_free_page(GFP_KERNEL);
	if (!sring) {
		xenbus_dev_fatal(dev, err, "fail to allocate shared ring (Front to Back)");
		return err;
	}
	SHARED_RING_INIT(sring);
	FRONT_RING_INIT(&info->ring, sring, PAGE_SIZE);

	err = xenbus_grant_ring(dev, virt_to_mfn(sring));
	if (err < 0) {
		free_page((unsigned long) sring);
		info->ring.sring = NULL;
		xenbus_dev_fatal(dev, err, "fail to grant shared ring (Front to Back)");
		goto free_sring;
	}
	info->ring_ref = err;

	err = bind_listening_port_to_irqhandler(
			dev->otherend_id, scsifront_intr,
			IRQF_SAMPLE_RANDOM, "scsifront", info);

	if (err <= 0) {
		xenbus_dev_fatal(dev, err, "bind_listening_port_to_irqhandler");
		goto free_sring;
	}
	info->irq = err;

	return 0;

/* free resource */
free_sring:
	scsifront_free(info);

	return err;
}


static int scsifront_init_ring(struct vscsifrnt_info *info)
{
	struct xenbus_device *dev = info->dev;
	struct xenbus_transaction xbt;
	int err;

	DPRINTK("%s\n",__FUNCTION__);

	err = scsifront_alloc_ring(info);
	if (err)
		return err;
	DPRINTK("%u %u\n", info->ring_ref, info->evtchn);

again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		xenbus_dev_fatal(dev, err, "starting transaction");
	}

	err = xenbus_printf(xbt, dev->nodename, "ring-ref", "%u",
				info->ring_ref);
	if (err) {
		xenbus_dev_fatal(dev, err, "%s", "writing ring-ref");
		goto fail;
	}

	err = xenbus_printf(xbt, dev->nodename, "event-channel", "%u",
				irq_to_evtchn_port(info->irq));

	if (err) {
		xenbus_dev_fatal(dev, err, "%s", "writing event-channel");
		goto fail;
	}

	err = xenbus_transaction_end(xbt, 0);
	if (err) {
		if (err == -EAGAIN)
			goto again;
		xenbus_dev_fatal(dev, err, "completing transaction");
		goto free_sring;
	}

	return 0;

fail:
	xenbus_transaction_end(xbt, 1);
free_sring:
	/* free resource */
	scsifront_free(info);
	
	return err;
}


static int scsifront_probe(struct xenbus_device *dev,
				const struct xenbus_device_id *id)
{
	struct vscsifrnt_info *info;
	struct Scsi_Host *host;
	int i, err = -ENOMEM;
	char name[DEFAULT_TASK_COMM_LEN];

	host = scsi_host_alloc(&scsifront_sht, sizeof(*info));
	if (!host) {
		xenbus_dev_fatal(dev, err, "fail to allocate scsi host");
		return err;
	}
	info = (struct vscsifrnt_info *) host->hostdata;
	info->host = host;


	dev_set_drvdata(&dev->dev, info);
	info->dev  = dev;

	for (i = 0; i < VSCSIIF_MAX_REQS; i++) {
		info->shadow[i].next_free = i + 1;
		init_waitqueue_head(&(info->shadow[i].wq_reset));
		info->shadow[i].wait_reset = 0;
	}
	info->shadow[VSCSIIF_MAX_REQS - 1].next_free = 0x0fff;

	err = scsifront_init_ring(info);
	if (err) {
		scsi_host_put(host);
		return err;
	}

	init_waitqueue_head(&info->wq);
	spin_lock_init(&info->io_lock);
	spin_lock_init(&info->shadow_lock);

	snprintf(name, DEFAULT_TASK_COMM_LEN, "vscsiif.%d", info->host->host_no);

	info->kthread = kthread_run(scsifront_schedule, info, name);
	if (IS_ERR(info->kthread)) {
		err = PTR_ERR(info->kthread);
		info->kthread = NULL;
		printk(KERN_ERR "scsifront: kthread start err %d\n", err);
		goto free_sring;
	}

	host->max_id      = VSCSIIF_MAX_TARGET;
	host->max_channel = 0;
	host->max_lun     = VSCSIIF_MAX_LUN;
	host->max_sectors = (VSCSIIF_SG_TABLESIZE - 1) * PAGE_SIZE / 512;

	err = scsi_add_host(host, &dev->dev);
	if (err) {
		printk(KERN_ERR "scsifront: fail to add scsi host %d\n", err);
		goto free_sring;
	}

	xenbus_switch_state(dev, XenbusStateInitialised);

	return 0;

free_sring:
	/* free resource */
	scsifront_free(info);
	return err;
}

static int scsifront_remove(struct xenbus_device *dev)
{
	struct vscsifrnt_info *info = dev_get_drvdata(&dev->dev);

	DPRINTK("%s: %s removed\n",__FUNCTION__ ,dev->nodename);

	if (info->kthread) {
		kthread_stop(info->kthread);
		info->kthread = NULL;
	}

	scsifront_free(info);
	
	return 0;
}


static int scsifront_disconnect(struct vscsifrnt_info *info)
{
	struct xenbus_device *dev = info->dev;
	struct Scsi_Host *host = info->host;

	DPRINTK("%s: %s disconnect\n",__FUNCTION__ ,dev->nodename);

	/* 
	  When this function is executed,  all devices of 
	  Frontend have been deleted. 
	  Therefore, it need not block I/O before remove_host.
	*/

	scsi_remove_host(host);
	xenbus_frontend_closed(dev);

	return 0;
}

#define VSCSIFRONT_OP_ADD_LUN	1
#define VSCSIFRONT_OP_DEL_LUN	2

static void scsifront_do_lun_hotplug(struct vscsifrnt_info *info, int op)
{
	struct xenbus_device *dev = info->dev;
	int i, err = 0;
	char str[64], state_str[64];
	char **dir;
	unsigned int dir_n = 0;
	unsigned int device_state;
	unsigned int hst, chn, tgt, lun;
	struct scsi_device *sdev;

	dir = xenbus_directory(XBT_NIL, dev->otherend, "vscsi-devs", &dir_n);
	if (IS_ERR(dir))
		return;

	for (i = 0; i < dir_n; i++) {
		/* read status */
		snprintf(str, sizeof(str), "vscsi-devs/%s/state", dir[i]);
		err = xenbus_scanf(XBT_NIL, dev->otherend, str, "%u",
			&device_state);
		if (XENBUS_EXIST_ERR(err))
			continue;
		
		/* virtual SCSI device */
		snprintf(str, sizeof(str), "vscsi-devs/%s/v-dev", dir[i]);
		err = xenbus_scanf(XBT_NIL, dev->otherend, str,
			"%u:%u:%u:%u", &hst, &chn, &tgt, &lun);
		if (XENBUS_EXIST_ERR(err))
			continue;

		/* front device state path */
		snprintf(state_str, sizeof(state_str), "vscsi-devs/%s/state", dir[i]);

		switch (op) {
		case VSCSIFRONT_OP_ADD_LUN:
			if (device_state == XenbusStateInitialised) {
				sdev = scsi_device_lookup(info->host, chn, tgt, lun);
				if (sdev) {
					printk(KERN_ERR "scsifront: Device already in use.\n");
					scsi_device_put(sdev);
					xenbus_printf(XBT_NIL, dev->nodename,
						state_str, "%d", XenbusStateClosed);
				} else {
					scsi_add_device(info->host, chn, tgt, lun);
					xenbus_printf(XBT_NIL, dev->nodename,
						state_str, "%d", XenbusStateConnected);
				}
			}
			break;
		case VSCSIFRONT_OP_DEL_LUN:
			if (device_state == XenbusStateClosing) {
				sdev = scsi_device_lookup(info->host, chn, tgt, lun);
				if (sdev) {
					scsi_remove_device(sdev);
					scsi_device_put(sdev);
					xenbus_printf(XBT_NIL, dev->nodename,
						state_str, "%d", XenbusStateClosed);
				}
			}
			break;
		default:
			break;
		}
	}
	
	kfree(dir);
	return;
}




static void scsifront_backend_changed(struct xenbus_device *dev,
				enum xenbus_state backend_state)
{
	struct vscsifrnt_info *info = dev_get_drvdata(&dev->dev);

	DPRINTK("%p %u %u\n", dev, dev->state, backend_state);

	switch (backend_state) {
	case XenbusStateUnknown:
	case XenbusStateInitialising:
	case XenbusStateInitWait:
	case XenbusStateClosed:
		break;

	case XenbusStateInitialised:
		break;

	case XenbusStateConnected:
		if (xenbus_read_driver_state(dev->nodename) ==
			XenbusStateInitialised) {
			scsifront_do_lun_hotplug(info, VSCSIFRONT_OP_ADD_LUN);
		}
		
		if (dev->state == XenbusStateConnected)
			break;
			
		xenbus_switch_state(dev, XenbusStateConnected);
		break;

	case XenbusStateClosing:
		scsifront_disconnect(info);
		break;

	case XenbusStateReconfiguring:
		scsifront_do_lun_hotplug(info, VSCSIFRONT_OP_DEL_LUN);
		xenbus_switch_state(dev, XenbusStateReconfiguring);
		break;

	case XenbusStateReconfigured:
		scsifront_do_lun_hotplug(info, VSCSIFRONT_OP_ADD_LUN);
		xenbus_switch_state(dev, XenbusStateConnected);
		break;
	}
}


static struct xenbus_device_id scsifront_ids[] = {
	{ "vscsi" },
	{ "" }
};


static struct xenbus_driver scsifront_driver = {
	.name			= "vscsi",
	.ids			= scsifront_ids,
	.probe			= scsifront_probe,
	.remove			= scsifront_remove,
/* 	.resume			= scsifront_resume, */
	.otherend_changed	= scsifront_backend_changed,
};

int scsifront_xenbus_init(void)
{
	return xenbus_register_frontend(&scsifront_driver);
}

void scsifront_xenbus_unregister(void)
{
	xenbus_unregister_driver(&scsifront_driver);
}

