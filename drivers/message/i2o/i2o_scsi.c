/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * For the avoidance of doubt the "preferred form" of this code is one which
 * is in an open non patent encumbered format. Where cryptographic key signing
 * forms part of the process of creating an executable the information
 * including keys needed to generate an equivalently functional executable
 * are deemed to be part of the source code.
 *
 *  Complications for I2O scsi
 *
 *	o	Each (bus,lun) is a logical device in I2O. We keep a map
 *		table. We spoof failed selection for unmapped units
 *	o	Request sense buffers can come back for free.
 *	o	Scatter gather is a bit dynamic. We have to investigate at
 *		setup time.
 *	o	Some of our resources are dynamically shared. The i2o core
 *		needs a message reservation protocol to avoid swap v net
 *		deadlocking. We need to back off queue requests.
 *
 *	In general the firmware wants to help. Where its help isn't performance
 *	useful we just ignore the aid. Its not worth the code in truth.
 *
 * Fixes/additions:
 *	Steve Ralston:
 *		Scatter gather now works
 *	Markus Lidel <Markus.Lidel@shadowconnect.com>:
 *		Minor fixes for 2.6.
 *
 * To Do:
 *	64bit cleanups
 *	Fix the resource management problems.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/prefetch.h>
#include <linux/pci.h>
#include <linux/blkdev.h>
#include <linux/i2o.h>

#include <asm/dma.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/atomic.h>

#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>

#define VERSION_STRING        "Version 0.1.2"

static int i2o_scsi_max_id = 16;
static int i2o_scsi_max_lun = 8;

static LIST_HEAD(i2o_scsi_hosts);

struct i2o_scsi_host {
	struct list_head list;	/* node in in i2o_scsi_hosts */
	struct Scsi_Host *scsi_host;	/* pointer to the SCSI host */
	struct i2o_controller *iop;	/* pointer to the I2O controller */
	struct i2o_device *channel[0];	/* channel->i2o_dev mapping table */
};

static struct scsi_host_template i2o_scsi_host_template;

/*
 * This is only needed, because we can only set the hostdata after the device is
 * added to the scsi core. So we need this little workaround.
 */
static DECLARE_MUTEX(i2o_scsi_probe_lock);
static struct i2o_device *i2o_scsi_probe_dev = NULL;

static int i2o_scsi_slave_alloc(struct scsi_device *sdp)
{
	sdp->hostdata = i2o_scsi_probe_dev;
	return 0;
};

#define I2O_SCSI_CAN_QUEUE	4

/* SCSI OSM class handling definition */
static struct i2o_class_id i2o_scsi_class_id[] = {
	{I2O_CLASS_SCSI_PERIPHERAL},
	{I2O_CLASS_END}
};

static struct i2o_scsi_host *i2o_scsi_host_alloc(struct i2o_controller *c)
{
	struct i2o_scsi_host *i2o_shost;
	struct i2o_device *i2o_dev;
	struct Scsi_Host *scsi_host;
	int max_channel = 0;
	u8 type;
	int i;
	size_t size;
	i2o_status_block *sb;

	list_for_each_entry(i2o_dev, &c->devices, list)
	    if (i2o_dev->lct_data.class_id == I2O_CLASS_BUS_ADAPTER_PORT) {
		if (i2o_parm_field_get(i2o_dev, 0x0000, 0, &type, 1) || (type == 1))	/* SCSI bus */
			max_channel++;
	}

	if (!max_channel) {
		printk(KERN_WARNING "scsi-osm: no channels found on %s\n",
		       c->name);
		return ERR_PTR(-EFAULT);
	}

	size = max_channel * sizeof(struct i2o_device *)
	    + sizeof(struct i2o_scsi_host);

	scsi_host = scsi_host_alloc(&i2o_scsi_host_template, size);
	if (!scsi_host) {
		printk(KERN_WARNING "scsi-osm: Could not allocate SCSI host\n");
		return ERR_PTR(-ENOMEM);
	}

	scsi_host->max_channel = max_channel - 1;
	scsi_host->max_id = i2o_scsi_max_id;
	scsi_host->max_lun = i2o_scsi_max_lun;
	scsi_host->this_id = c->unit;

	sb = c->status_block.virt;

	scsi_host->sg_tablesize = (sb->inbound_frame_size -
				   sizeof(struct i2o_message) / 4 - 6) / 2;

	i2o_shost = (struct i2o_scsi_host *)scsi_host->hostdata;
	i2o_shost->scsi_host = scsi_host;
	i2o_shost->iop = c;

	i = 0;
	list_for_each_entry(i2o_dev, &c->devices, list)
	    if (i2o_dev->lct_data.class_id == I2O_CLASS_BUS_ADAPTER_PORT) {
		if (i2o_parm_field_get(i2o_dev, 0x0000, 0, &type, 1) || (type == 1))	/* only SCSI bus */
			i2o_shost->channel[i++] = i2o_dev;

		if (i >= max_channel)
			break;
	}

	return i2o_shost;
};

/**
 *	i2o_scsi_get_host - Get an I2O SCSI host
 *	@c: I2O controller to for which to get the SCSI host
 *
 *	If the I2O controller already exists as SCSI host, the SCSI host
 *	is returned, otherwise the I2O controller is added to the SCSI
 *	core.
 *
 *	Returns pointer to the I2O SCSI host on success or negative error code
 *	on failure.
 */
static struct i2o_scsi_host *i2o_scsi_get_host(struct i2o_controller *c)
{
	struct i2o_scsi_host *i2o_shost;
	int rc;

	/* skip if already registered as I2O SCSI host */
	list_for_each_entry(i2o_shost, &i2o_scsi_hosts, list)
	    if (i2o_shost->iop == c)
		return i2o_shost;

	i2o_shost = i2o_scsi_host_alloc(c);
	if (IS_ERR(i2o_shost)) {
		printk(KERN_ERR "scsi-osm: Could not initialize SCSI host\n");
		return i2o_shost;
	}

	rc = scsi_add_host(i2o_shost->scsi_host, &c->device);
	if (rc) {
		printk(KERN_ERR "scsi-osm: Could not add SCSI host\n");
		scsi_host_put(i2o_shost->scsi_host);
		return ERR_PTR(rc);
	}

	list_add(&i2o_shost->list, &i2o_scsi_hosts);
	pr_debug("new I2O SCSI host added\n");

	return i2o_shost;

};

/**
 *	i2o_scsi_remove - Remove I2O device from SCSI core
 *	@dev: device which should be removed
 *
 *	Removes the I2O device from the SCSI core again.
 *
 *	Returns 0 on success.
 */
static int i2o_scsi_remove(struct device *dev)
{
	struct i2o_device *i2o_dev = to_i2o_device(dev);
	struct i2o_controller *c = i2o_dev->iop;
	struct i2o_scsi_host *i2o_shost;
	struct scsi_device *scsi_dev;

	i2o_shost = i2o_scsi_get_host(c);

	shost_for_each_device(scsi_dev, i2o_shost->scsi_host)
	    if (scsi_dev->hostdata == i2o_dev) {
		scsi_remove_device(scsi_dev);
		scsi_device_put(scsi_dev);
		break;
	}

	return 0;
};

/**
 *	i2o_scsi_probe - verify if dev is a I2O SCSI device and install it
 *	@dev: device to verify if it is a I2O SCSI device
 *
 *	Retrieve channel, id and lun for I2O device. If everthing goes well
 *	register the I2O device as SCSI device on the I2O SCSI controller.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int i2o_scsi_probe(struct device *dev)
{
	struct i2o_device *i2o_dev = to_i2o_device(dev);
	struct i2o_controller *c = i2o_dev->iop;
	struct i2o_scsi_host *i2o_shost;
	struct Scsi_Host *scsi_host;
	struct i2o_device *parent;
	struct scsi_device *scsi_dev;
	u32 id;
	u64 lun;
	int channel = -1;
	int i;

	i2o_shost = i2o_scsi_get_host(c);
	if (IS_ERR(i2o_shost))
		return PTR_ERR(i2o_shost);

	scsi_host = i2o_shost->scsi_host;

	if (i2o_parm_field_get(i2o_dev, 0, 3, &id, 4) < 0)
		return -EFAULT;

	if (id >= scsi_host->max_id) {
		printk(KERN_WARNING "scsi-osm: SCSI device id (%d) >= max_id "
		       "of I2O host (%d)", id, scsi_host->max_id);
		return -EFAULT;
	}

	if (i2o_parm_field_get(i2o_dev, 0, 4, &lun, 8) < 0)
		return -EFAULT;
	if (lun >= scsi_host->max_lun) {
		printk(KERN_WARNING "scsi-osm: SCSI device id (%d) >= max_lun "
		       "of I2O host (%d)", (unsigned int)lun,
		       scsi_host->max_lun);
		return -EFAULT;
	}

	parent = i2o_iop_find_device(c, i2o_dev->lct_data.parent_tid);
	if (!parent) {
		printk(KERN_WARNING "scsi-osm: can not find parent of device "
		       "%03x\n", i2o_dev->lct_data.tid);
		return -EFAULT;
	}

	for (i = 0; i <= i2o_shost->scsi_host->max_channel; i++)
		if (i2o_shost->channel[i] == parent)
			channel = i;

	if (channel == -1) {
		printk(KERN_WARNING "scsi-osm: can not find channel of device "
		       "%03x\n", i2o_dev->lct_data.tid);
		return -EFAULT;
	}

	down_interruptible(&i2o_scsi_probe_lock);
	i2o_scsi_probe_dev = i2o_dev;
	scsi_dev = scsi_add_device(i2o_shost->scsi_host, channel, id, lun);
	i2o_scsi_probe_dev = NULL;
	up(&i2o_scsi_probe_lock);

	if (!scsi_dev) {
		printk(KERN_WARNING "scsi-osm: can not add SCSI device "
		       "%03x\n", i2o_dev->lct_data.tid);
		return -EFAULT;
	}

	pr_debug("Added new SCSI device %03x (cannel: %d, id: %d, lun: %d)\n",
		 i2o_dev->lct_data.tid, channel, id, (unsigned int)lun);

	return 0;
};

static const char *i2o_scsi_info(struct Scsi_Host *SChost)
{
	struct i2o_scsi_host *hostdata;
	hostdata = (struct i2o_scsi_host *)SChost->hostdata;
	return hostdata->iop->name;
}

#if 0
/**
 *	i2o_retry_run		-	retry on timeout
 *	@f: unused
 *
 *	Retry congested frames. This actually needs pushing down into
 *	i2o core. We should only bother the OSM with this when we can't
 *	queue and retry the frame. Or perhaps we should call the OSM
 *	and its default handler should be this in the core, and this
 *	call a 2nd "I give up" handler in the OSM ?
 */

static void i2o_retry_run(unsigned long f)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&retry_lock, flags);
	for (i = 0; i < retry_ct; i++)
		i2o_post_message(retry_ctrl[i], virt_to_bus(retry[i]));
	retry_ct = 0;
	spin_unlock_irqrestore(&retry_lock, flags);
}

/**
 *	flush_pending		-	empty the retry queue
 *
 *	Turn each of the pending commands into a NOP and post it back
 *	to the controller to clear it.
 */

static void flush_pending(void)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&retry_lock, flags);
	for (i = 0; i < retry_ct; i++) {
		retry[i][0] &= ~0xFFFFFF;
		retry[i][0] |= I2O_CMD_UTIL_NOP << 24;
		i2o_post_message(retry_ctrl[i], virt_to_bus(retry[i]));
	}
	retry_ct = 0;
	spin_unlock_irqrestore(&retry_lock, flags);
}
#endif

/**
 *	i2o_scsi_reply - SCSI OSM message reply handler
 *	@c: controller issuing the reply
 *	@m: message id for flushing
 *	@msg: the message from the controller
 *
 *	Process reply messages (interrupts in normal scsi controller think).
 *	We can get a variety of messages to process. The normal path is
 *	scsi command completions. We must also deal with IOP failures,
 *	the reply to a bus reset and the reply to a LUN query.
 *
 *	Returns 0 on success and if the reply should not be flushed or > 0
 *	on success and if the reply should be flushed. Returns negative error
 *	code on failure and if the reply should be flushed.
 */
static int i2o_scsi_reply(struct i2o_controller *c, u32 m,
			  struct i2o_message *msg)
{
	struct scsi_cmnd *cmd;
	struct device *dev;
	u8 as, ds, st;

	cmd = i2o_cntxt_list_get(c, readl(&msg->u.s.tcntxt));

	if (msg->u.head[0] & (1 << 13)) {
		struct i2o_message *pmsg;	/* preserved message */
		u32 pm;

		pm = readl(&msg->body[3]);

		pmsg = c->in_queue.virt + pm;

		printk("IOP fail.\n");
		printk("From %d To %d Cmd %d.\n",
		       (msg->u.head[1] >> 12) & 0xFFF,
		       msg->u.head[1] & 0xFFF, msg->u.head[1] >> 24);
		printk("Failure Code %d.\n", msg->body[0] >> 24);
		if (msg->body[0] & (1 << 16))
			printk("Format error.\n");
		if (msg->body[0] & (1 << 17))
			printk("Path error.\n");
		if (msg->body[0] & (1 << 18))
			printk("Path State.\n");
		if (msg->body[0] & (1 << 18))
			printk("Congestion.\n");

		printk("Failing message is %p.\n", pmsg);

		cmd = i2o_cntxt_list_get(c, readl(&pmsg->u.s.tcntxt));
		if (!cmd)
			return 1;

		printk("Aborted %ld\n", cmd->serial_number);
		cmd->result = DID_ERROR << 16;
		cmd->scsi_done(cmd);

		/* Now flush the message by making it a NOP */
		i2o_msg_nop(c, pm);

		return 1;
	}

	/*
	 *      Low byte is device status, next is adapter status,
	 *      (then one byte reserved), then request status.
	 */
	ds = (u8) readl(&msg->body[0]);
	as = (u8) (readl(&msg->body[0]) >> 8);
	st = (u8) (readl(&msg->body[0]) >> 24);

	/*
	 *      Is this a control request coming back - eg an abort ?
	 */

	if (!cmd) {
		if (st)
			printk(KERN_WARNING "SCSI abort: %08X",
			       readl(&msg->body[0]));
		printk(KERN_INFO "SCSI abort completed.\n");
		return -EFAULT;
	}

	pr_debug("Completed %ld\n", cmd->serial_number);

	if (st) {
		u32 count, error;
		/* An error has occurred */

		switch (st) {
		case 0x06:
			count = readl(&msg->body[1]);
			if (count < cmd->underflow) {
				int i;
				printk(KERN_ERR "SCSI: underflow 0x%08X 0x%08X"
				       "\n", count, cmd->underflow);
				printk("Cmd: ");
				for (i = 0; i < 15; i++)
					printk("%02X ", cmd->cmnd[i]);
				printk(".\n");
				cmd->result = (DID_ERROR << 16);
			}
			break;

		default:
			error = readl(&msg->body[0]);

			printk(KERN_ERR "scsi-osm: SCSI error %08x\n", error);

			if ((error & 0xff) == 0x02 /*CHECK_CONDITION */ ) {
				int i;
				u32 len = sizeof(cmd->sense_buffer);
				len = (len > 40) ? 40 : len;
				// Copy over the sense data
				memcpy(cmd->sense_buffer, (void *)&msg->body[3],
				       len);
				for (i = 0; i <= len; i++)
					printk(KERN_INFO "%02x\n",
					       cmd->sense_buffer[i]);
				if (cmd->sense_buffer[0] == 0x70
				    && cmd->sense_buffer[2] == DATA_PROTECT) {
					/* This is to handle an array failed */
					cmd->result = (DID_TIME_OUT << 16);
					printk(KERN_WARNING "%s: SCSI Data "
					       "Protect-Device (%d,%d,%d) "
					       "hba_status=0x%x, dev_status="
					       "0x%x, cmd=0x%x\n", c->name,
					       (u32) cmd->device->channel,
					       (u32) cmd->device->id,
					       (u32) cmd->device->lun,
					       (error >> 8) & 0xff,
					       error & 0xff, cmd->cmnd[0]);
				} else
					cmd->result = (DID_ERROR << 16);

				break;
			}

			switch (as) {
			case 0x0E:
				/* SCSI Reset */
				cmd->result = DID_RESET << 16;
				break;

			case 0x0F:
				cmd->result = DID_PARITY << 16;
				break;

			default:
				cmd->result = DID_ERROR << 16;
				break;
			}

			break;
		}

		cmd->scsi_done(cmd);
		return 1;
	}

	cmd->result = DID_OK << 16 | ds;

	cmd->scsi_done(cmd);

	dev = &c->pdev->dev;
	if (cmd->use_sg)
		dma_unmap_sg(dev, (struct scatterlist *)cmd->buffer,
			     cmd->use_sg, cmd->sc_data_direction);
	else if (cmd->request_bufflen)
		dma_unmap_single(dev, (dma_addr_t) ((long)cmd->SCp.ptr),
				 cmd->request_bufflen, cmd->sc_data_direction);

	return 1;
}

/* SCSI OSM driver struct */
static struct i2o_driver i2o_scsi_driver = {
	.name = "scsi-osm",
	.reply = i2o_scsi_reply,
	.classes = i2o_scsi_class_id,
	.driver = {
		   .probe = i2o_scsi_probe,
		   .remove = i2o_scsi_remove,
		   },
};

/**
 *	i2o_scsi_queuecommand - queue a SCSI command
 *	@SCpnt: scsi command pointer
 *	@done: callback for completion
 *
 *	Issue a scsi command asynchronously. Return 0 on success or 1 if
 *	we hit an error (normally message queue congestion). The only
 *	minor complication here is that I2O deals with the device addressing
 *	so we have to map the bus/dev/lun back to an I2O handle as well
 *	as faking absent devices ourself.
 *
 *	Locks: takes the controller lock on error path only
 */

static int i2o_scsi_queuecommand(struct scsi_cmnd *SCpnt,
				 void (*done) (struct scsi_cmnd *))
{
	struct i2o_controller *c;
	struct Scsi_Host *host;
	struct i2o_device *i2o_dev;
	struct device *dev;
	int tid;
	struct i2o_message *msg;
	u32 m;
	u32 scsi_flags, sg_flags;
	u32 *mptr, *lenptr;
	u32 len, reqlen;
	int i;

	/*
	 *      Do the incoming paperwork
	 */

	i2o_dev = SCpnt->device->hostdata;
	host = SCpnt->device->host;
	c = i2o_dev->iop;
	dev = &c->pdev->dev;

	SCpnt->scsi_done = done;

	if (unlikely(!i2o_dev)) {
		printk(KERN_WARNING "scsi-osm: no I2O device in request\n");
		SCpnt->result = DID_NO_CONNECT << 16;
		done(SCpnt);
		return 0;
	}

	tid = i2o_dev->lct_data.tid;

	pr_debug("qcmd: Tid = %03x\n", tid);
	pr_debug("Real scsi messages.\n");

	/*
	 *      Obtain an I2O message. If there are none free then
	 *      throw it back to the scsi layer
	 */

	m = i2o_msg_get_wait(c, &msg, I2O_TIMEOUT_MESSAGE_GET);
	if (m == I2O_QUEUE_EMPTY)
		return SCSI_MLQUEUE_HOST_BUSY;

	/*
	 *      Put together a scsi execscb message
	 */

	len = SCpnt->request_bufflen;

	switch (SCpnt->sc_data_direction) {
	case PCI_DMA_NONE:
		scsi_flags = 0x00000000;	// DATA NO XFER
		sg_flags = 0x00000000;
		break;

	case PCI_DMA_TODEVICE:
		scsi_flags = 0x80000000;	// DATA OUT (iop-->dev)
		sg_flags = 0x14000000;
		break;

	case PCI_DMA_FROMDEVICE:
		scsi_flags = 0x40000000;	// DATA IN  (iop<--dev)
		sg_flags = 0x10000000;
		break;

	default:
		/* Unknown - kill the command */
		SCpnt->result = DID_NO_CONNECT << 16;
		done(SCpnt);
		return 0;
	}

	writel(I2O_CMD_SCSI_EXEC << 24 | HOST_TID << 12 | tid, &msg->u.head[1]);
	writel(i2o_scsi_driver.context, &msg->u.s.icntxt);

	/* We want the SCSI control block back */
	writel(i2o_cntxt_list_add(c, SCpnt), &msg->u.s.tcntxt);

	/* LSI_920_PCI_QUIRK
	 *
	 *      Intermittant observations of msg frame word data corruption
	 *      observed on msg[4] after:
	 *        WRITE, READ-MODIFY-WRITE
	 *      operations.  19990606 -sralston
	 *
	 *      (Hence we build this word via tag. Its good practice anyway
	 *       we don't want fetches over PCI needlessly)
	 */

	/* Attach tags to the devices */
	/*
	   if(SCpnt->device->tagged_supported) {
	   if(SCpnt->tag == HEAD_OF_QUEUE_TAG)
	   scsi_flags |= 0x01000000;
	   else if(SCpnt->tag == ORDERED_QUEUE_TAG)
	   scsi_flags |= 0x01800000;
	   }
	 */

	/* Direction, disconnect ok, tag, CDBLen */
	writel(scsi_flags | 0x20200000 | SCpnt->cmd_len, &msg->body[0]);

	mptr = &msg->body[1];

	/* Write SCSI command into the message - always 16 byte block */
	memcpy_toio(mptr, SCpnt->cmnd, 16);
	mptr += 4;
	lenptr = mptr++;	/* Remember me - fill in when we know */

	reqlen = 12;		// SINGLE SGE

	/* Now fill in the SGList and command */
	if (SCpnt->use_sg) {
		struct scatterlist *sg;
		int sg_count;

		sg = SCpnt->request_buffer;
		len = 0;

		sg_count = dma_map_sg(dev, sg, SCpnt->use_sg,
				      SCpnt->sc_data_direction);

		if (unlikely(sg_count <= 0))
			return -ENOMEM;

		for (i = SCpnt->use_sg; i > 0; i--) {
			if (i == 1)
				sg_flags |= 0xC0000000;
			writel(sg_flags | sg_dma_len(sg), mptr++);
			writel(sg_dma_address(sg), mptr++);
			len += sg_dma_len(sg);
			sg++;
		}

		reqlen = mptr - &msg->u.head[0];
		writel(len, lenptr);
	} else {
		len = SCpnt->request_bufflen;

		writel(len, lenptr);

		if (len > 0) {
			dma_addr_t dma_addr;

			dma_addr = dma_map_single(dev, SCpnt->request_buffer,
						  SCpnt->request_bufflen,
						  SCpnt->sc_data_direction);
			if (!dma_addr)
				return -ENOMEM;

			SCpnt->SCp.ptr = (void *)(unsigned long)dma_addr;
			sg_flags |= 0xC0000000;
			writel(sg_flags | SCpnt->request_bufflen, mptr++);
			writel(dma_addr, mptr++);
		} else
			reqlen = 9;
	}

	/* Stick the headers on */
	writel(reqlen << 16 | SGL_OFFSET_10, &msg->u.head[0]);

	/* Queue the message */
	i2o_msg_post(c, m);

	pr_debug("Issued %ld\n", SCpnt->serial_number);

	return 0;
};

#if 0
FIXME
/**
 *	i2o_scsi_abort	-	abort a running command
 *	@SCpnt: command to abort
 *
 *	Ask the I2O controller to abort a command. This is an asynchrnous
 *	process and our callback handler will see the command complete
 *	with an aborted message if it succeeds.
 *
 *	Locks: no locks are held or needed
 */
int i2o_scsi_abort(struct scsi_cmnd *SCpnt)
{
	struct i2o_controller *c;
	struct Scsi_Host *host;
	struct i2o_scsi_host *hostdata;
	u32 msg[5];
	int tid;
	int status = FAILED;

	printk(KERN_WARNING "i2o_scsi: Aborting command block.\n");

	host = SCpnt->device->host;
	hostdata = (struct i2o_scsi_host *)host->hostdata;
	tid = hostdata->task[SCpnt->device->id][SCpnt->device->lun];
	if (tid == -1) {
		printk(KERN_ERR "i2o_scsi: Impossible command to abort!\n");
		return status;
	}
	c = hostdata->controller;

	spin_unlock_irq(host->host_lock);

	msg[0] = FIVE_WORD_MSG_SIZE;
	msg[1] = I2O_CMD_SCSI_ABORT << 24 | HOST_TID << 12 | tid;
	msg[2] = scsi_context;
	msg[3] = 0;
	msg[4] = i2o_context_list_remove(SCpnt, c);
	if (i2o_post_wait(c, msg, sizeof(msg), 240))
		status = SUCCESS;

	spin_lock_irq(host->host_lock);
	return status;
}

#endif

/**
 *	i2o_scsi_bios_param	-	Invent disk geometry
 *	@sdev: scsi device
 *	@dev: block layer device
 *	@capacity: size in sectors
 *	@ip: geometry array
 *
 *	This is anyones guess quite frankly. We use the same rules everyone
 *	else appears to and hope. It seems to work.
 */

static int i2o_scsi_bios_param(struct scsi_device *sdev,
			       struct block_device *dev, sector_t capacity,
			       int *ip)
{
	int size;

	size = capacity;
	ip[0] = 64;		/* heads                        */
	ip[1] = 32;		/* sectors                      */
	if ((ip[2] = size >> 11) > 1024) {	/* cylinders, test for big disk */
		ip[0] = 255;	/* heads                        */
		ip[1] = 63;	/* sectors                      */
		ip[2] = size / (255 * 63);	/* cylinders                    */
	}
	return 0;
}

static struct scsi_host_template i2o_scsi_host_template = {
	.proc_name = "SCSI-OSM",
	.name = "I2O SCSI Peripheral OSM",
	.info = i2o_scsi_info,
	.queuecommand = i2o_scsi_queuecommand,
/*
	.eh_abort_handler	= i2o_scsi_abort,
*/
	.bios_param = i2o_scsi_bios_param,
	.can_queue = I2O_SCSI_CAN_QUEUE,
	.sg_tablesize = 8,
	.cmd_per_lun = 6,
	.use_clustering = ENABLE_CLUSTERING,
	.slave_alloc = i2o_scsi_slave_alloc,
};

/*
int
i2o_scsi_queuecommand(struct scsi_cmnd * cmd, void (*done) (struct scsi_cmnd *))
{
	printk(KERN_INFO "queuecommand\n");
	return SCSI_MLQUEUE_HOST_BUSY;
};
*/

/**
 *	i2o_scsi_init - SCSI OSM initialization function
 *
 *	Register SCSI OSM into I2O core.
 *
 *	Returns 0 on success or negative error code on failure.
 */
static int __init i2o_scsi_init(void)
{
	int rc;

	printk(KERN_INFO "I2O SCSI Peripheral OSM\n");

	/* Register SCSI OSM into I2O core */
	rc = i2o_driver_register(&i2o_scsi_driver);
	if (rc) {
		printk(KERN_ERR "scsi-osm: Could not register SCSI driver\n");
		return rc;
	}

	return 0;
};

/**
 *	i2o_scsi_exit - SCSI OSM exit function
 *
 *	Unregisters SCSI OSM from I2O core.
 */
static void __exit i2o_scsi_exit(void)
{
	struct i2o_scsi_host *i2o_shost, *tmp;

	/* Remove I2O SCSI hosts */
	list_for_each_entry_safe(i2o_shost, tmp, &i2o_scsi_hosts, list) {
		scsi_remove_host(i2o_shost->scsi_host);
		scsi_host_put(i2o_shost->scsi_host);
	}

	/* Unregister I2O SCSI OSM from I2O core */
	i2o_driver_unregister(&i2o_scsi_driver);
};

MODULE_AUTHOR("Red Hat Software");
MODULE_LICENSE("GPL");

module_init(i2o_scsi_init);
module_exit(i2o_scsi_exit);
