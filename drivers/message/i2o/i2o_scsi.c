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
 *	Fixes:
 *		Steve Ralston	:	Scatter gather now works
 *
 *	To Do
 *		64bit cleanups
 *		Fix the resource management problems.
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
#include <asm/dma.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <linux/blkdev.h>
#include <linux/i2o.h>
#include "../../scsi/scsi.h"
#include "../../scsi/hosts.h"

#if BITS_PER_LONG == 64
#error FIXME: driver does not support 64-bit platforms
#endif


#define VERSION_STRING        "Version 0.1.2"

#define dprintk(x)

#define I2O_SCSI_CAN_QUEUE	4
#define MAXHOSTS		32

struct i2o_scsi_host
{
	struct i2o_controller *controller;
	s16 task[16][8];		/* Allow 16 devices for now */
	unsigned long tagclock[16][8];	/* Tag clock for queueing */
	s16 bus_task;		/* The adapter TID */
};

static int scsi_context;
static int lun_done;
static int i2o_scsi_hosts;

static u32 *retry[32];
static struct i2o_controller *retry_ctrl[32];
static struct timer_list retry_timer;
static spinlock_t retry_lock = SPIN_LOCK_UNLOCKED;
static int retry_ct = 0;

static atomic_t queue_depth;

/*
 *	SG Chain buffer support...
 */

#define SG_MAX_FRAGS		64

/*
 *	FIXME: we should allocate one of these per bus we find as we
 *	locate them not in a lump at boot.
 */
 
typedef struct _chain_buf
{
	u32 sg_flags_cnt[SG_MAX_FRAGS];
	u32 sg_buf[SG_MAX_FRAGS];
} chain_buf;

#define SG_CHAIN_BUF_SZ sizeof(chain_buf)

#define SG_MAX_BUFS		(i2o_num_controllers * I2O_SCSI_CAN_QUEUE)
#define SG_CHAIN_POOL_SZ	(SG_MAX_BUFS * SG_CHAIN_BUF_SZ)

static int max_sg_len = 0;
static chain_buf *sg_chain_pool = NULL;
static int sg_chain_tag = 0;
static int sg_max_frags = SG_MAX_FRAGS;

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
	for(i=0;i<retry_ct;i++)
		i2o_post_message(retry_ctrl[i], virt_to_bus(retry[i]));
	retry_ct=0;
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
	for(i=0;i<retry_ct;i++)
	{
		retry[i][0]&=~0xFFFFFF;
		retry[i][0]|=I2O_CMD_UTIL_NOP<<24;
		i2o_post_message(retry_ctrl[i],virt_to_bus(retry[i]));
	}
	retry_ct=0;
	spin_unlock_irqrestore(&retry_lock, flags);
}

/**
 *	i2o_scsi_reply		-	scsi message reply processor
 *	@h: our i2o handler
 *	@c: controller issuing the reply
 *	@msg: the message from the controller (mapped)
 *
 *	Process reply messages (interrupts in normal scsi controller think).
 *	We can get a variety of messages to process. The normal path is
 *	scsi command completions. We must also deal with IOP failures,
 *	the reply to a bus reset and the reply to a LUN query.
 *
 *	Locks: the queue lock is taken to call the completion handler
 */

static void i2o_scsi_reply(struct i2o_handler *h, struct i2o_controller *c, struct i2o_message *msg)
{
	Scsi_Cmnd *current_command;
	spinlock_t *lock;
	u32 *m = (u32 *)msg;
	u8 as,ds,st;
	unsigned long flags;

	if(m[0] & (1<<13))
	{
		printk("IOP fail.\n");
		printk("From %d To %d Cmd %d.\n",
			(m[1]>>12)&0xFFF,
			m[1]&0xFFF,
			m[1]>>24);
		printk("Failure Code %d.\n", m[4]>>24);
		if(m[4]&(1<<16))
			printk("Format error.\n");
		if(m[4]&(1<<17))
			printk("Path error.\n");
		if(m[4]&(1<<18))
			printk("Path State.\n");
		if(m[4]&(1<<18))
			printk("Congestion.\n");
		
		m=(u32 *)bus_to_virt(m[7]);
		printk("Failing message is %p.\n", m);
		
		/* This isnt a fast path .. */
		spin_lock_irqsave(&retry_lock, flags);
		
		if((m[4]&(1<<18)) && retry_ct < 32)
		{
			retry_ctrl[retry_ct]=c;
			retry[retry_ct]=m;
			if(!retry_ct++)
			{
				retry_timer.expires=jiffies+1;
				add_timer(&retry_timer);
			}
			spin_unlock_irqrestore(&retry_lock, flags);
		}
		else
		{
			spin_unlock_irqrestore(&retry_lock, flags);
			/* Create a scsi error for this */
			current_command = (Scsi_Cmnd *)m[3];
			lock = current_command->device->host->host_lock;
			printk("Aborted %ld\n", current_command->serial_number);

			spin_lock_irqsave(lock, flags);
			current_command->result = DID_ERROR << 16;
			current_command->scsi_done(current_command);
			spin_unlock_irqrestore(lock, flags);
			
			/* Now flush the message by making it a NOP */
			m[0]&=0x00FFFFFF;
			m[0]|=(I2O_CMD_UTIL_NOP)<<24;
			i2o_post_message(c,virt_to_bus(m));
		}
		return;
	}
	
	prefetchw(&queue_depth);
		
	
	/*
	 *	Low byte is device status, next is adapter status,
	 *	(then one byte reserved), then request status.
	 */
	ds=(u8)le32_to_cpu(m[4]);
	as=(u8)le32_to_cpu(m[4]>>8);
	st=(u8)le32_to_cpu(m[4]>>24);
	
	dprintk(("i2o got a scsi reply %08X: ", m[0]));
	dprintk(("m[2]=%08X: ", m[2]));
	dprintk(("m[4]=%08X\n", m[4]));
 
	if(m[2]&0x80000000)
	{
		if(m[2]&0x40000000)
		{
			dprintk(("Event.\n"));
			lun_done=1;
			return;
		}
		printk(KERN_INFO "i2o_scsi: bus reset completed.\n");
		return;
	}
	/*
 	 *	FIXME: 64bit breakage
	 */

	current_command = (Scsi_Cmnd *)m[3];
	
	/*
	 *	Is this a control request coming back - eg an abort ?
	 */
	 
	if(current_command==NULL)
	{
		if(st)
			dprintk(("SCSI abort: %08X", m[4]));
		dprintk(("SCSI abort completed.\n"));
		return;
	}
	
	dprintk(("Completed %ld\n", current_command->serial_number));
	
	atomic_dec(&queue_depth);
	
	if(st == 0x06)
	{
		if(le32_to_cpu(m[5]) < current_command->underflow)
		{
			int i;
			printk(KERN_ERR "SCSI: underflow 0x%08X 0x%08X\n",
				le32_to_cpu(m[5]), current_command->underflow);
			printk("Cmd: ");
			for(i=0;i<15;i++)
				printk("%02X ", current_command->cmnd[i]);
			printk(".\n");
		}
		else st=0;
	}
	
	if(st)
	{
		/* An error has occurred */

		dprintk((KERN_DEBUG "SCSI error %08X", m[4]));
			
		if (as == 0x0E) 
			/* SCSI Reset */
			current_command->result = DID_RESET << 16;
		else if (as == 0x0F)
			current_command->result = DID_PARITY << 16;
		else
			current_command->result = DID_ERROR << 16;
	}
	else
		/*
		 *	It worked maybe ?
		 */		
		current_command->result = DID_OK << 16 | ds;

	if (current_command->use_sg)
		pci_unmap_sg(c->pdev, (struct scatterlist *)current_command->buffer, current_command->use_sg, scsi_to_pci_dma_dir(current_command->sc_data_direction));
	else if (current_command->request_bufflen)
		pci_unmap_single(c->pdev, (dma_addr_t)((long)current_command->SCp.ptr), current_command->request_bufflen, scsi_to_pci_dma_dir(current_command->sc_data_direction));

	lock = current_command->device->host->host_lock;
	spin_lock_irqsave(lock, flags);
	current_command->scsi_done(current_command);
	spin_unlock_irqrestore(lock, flags);
	return;
}

struct i2o_handler i2o_scsi_handler = {
	.reply	= i2o_scsi_reply,
	.name	= "I2O SCSI OSM",
	.class	= I2O_CLASS_SCSI_PERIPHERAL,
};

/**
 *	i2o_find_lun		-	report the lun of an i2o device
 *	@c: i2o controller owning the device
 *	@d: i2o disk device
 *	@target: filled in with target id
 *	@lun: filled in with target lun
 *
 *	Query an I2O device to find out its SCSI lun and target numbering. We
 *	don't currently handle some of the fancy SCSI-3 stuff although our
 *	querying is sufficient to do so.
 */
 
static int i2o_find_lun(struct i2o_controller *c, struct i2o_device *d, int *target, int *lun)
{
	u8 reply[8];
	
	if(i2o_query_scalar(c, d->lct_data.tid, 0, 3, reply, 4)<0)
		return -1;
		
	*target=reply[0];
	
	if(i2o_query_scalar(c, d->lct_data.tid, 0, 4, reply, 8)<0)
		return -1;

	*lun=reply[1];

	dprintk(("SCSI (%d,%d)\n", *target, *lun));
	return 0;
}

/**
 *	i2o_scsi_init		-	initialize an i2o device for scsi
 *	@c: i2o controller owning the device
 *	@d: scsi controller
 *	@shpnt: scsi device we wish it to become
 *
 *	Enumerate the scsi peripheral/fibre channel peripheral class
 *	devices that are children of the controller. From that we build
 *	a translation map for the command queue code. Since I2O works on
 *	its own tid's we effectively have to think backwards to get what
 *	the midlayer wants
 */
 
static void i2o_scsi_init(struct i2o_controller *c, struct i2o_device *d, struct Scsi_Host *shpnt)
{
	struct i2o_device *unit;
	struct i2o_scsi_host *h =(struct i2o_scsi_host *)shpnt->hostdata;
	int lun;
	int target;
	
	h->controller=c;
	h->bus_task=d->lct_data.tid;
	
	for(target=0;target<16;target++)
		for(lun=0;lun<8;lun++)
			h->task[target][lun] = -1;
			
	for(unit=c->devices;unit!=NULL;unit=unit->next)
	{
		dprintk(("Class %03X, parent %d, want %d.\n",
			unit->lct_data.class_id, unit->lct_data.parent_tid, d->lct_data.tid));
			
		/* Only look at scsi and fc devices */
		if (    (unit->lct_data.class_id != I2O_CLASS_SCSI_PERIPHERAL)
		     && (unit->lct_data.class_id != I2O_CLASS_FIBRE_CHANNEL_PERIPHERAL)
		   )
			continue;

		/* On our bus ? */
		dprintk(("Found a disk (%d).\n", unit->lct_data.tid));
		if ((unit->lct_data.parent_tid == d->lct_data.tid)
		     || (unit->lct_data.parent_tid == d->lct_data.parent_tid)
		   )
		{
			u16 limit;
			dprintk(("Its ours.\n"));
			if(i2o_find_lun(c, unit, &target, &lun)==-1)
			{
				printk(KERN_ERR "i2o_scsi: Unable to get lun for tid %d.\n", unit->lct_data.tid);
				continue;
			}
			dprintk(("Found disk %d %d.\n", target, lun));
			h->task[target][lun]=unit->lct_data.tid;
			h->tagclock[target][lun]=jiffies;

			/* Get the max fragments/request */
			i2o_query_scalar(c, d->lct_data.tid, 0xF103, 3, &limit, 2);
			
			/* sanity */
			if ( limit == 0 )
			{
				printk(KERN_WARNING "i2o_scsi: Ignoring unreasonable SG limit of 0 from IOP!\n");
				limit = 1;
			}
			
			shpnt->sg_tablesize = limit;

			dprintk(("i2o_scsi: set scatter-gather to %d.\n", 
				shpnt->sg_tablesize));
		}
	}		
}

/**
 *	i2o_scsi_detect		-	probe for I2O scsi devices
 *	@tpnt: scsi layer template
 *
 *	I2O is a little odd here. The I2O core already knows what the
 *	devices are. It also knows them by disk and tape as well as
 *	by controller. We register each I2O scsi class object as a
 *	scsi controller and then let the enumeration fake up the rest
 */
 
static int i2o_scsi_detect(Scsi_Host_Template * tpnt)
{
	struct Scsi_Host *shpnt = NULL;
	int i;
	int count;

	printk(KERN_INFO "i2o_scsi.c: %s\n", VERSION_STRING);

	if(i2o_install_handler(&i2o_scsi_handler)<0)
	{
		printk(KERN_ERR "i2o_scsi: Unable to install OSM handler.\n");
		return 0;
	}
	scsi_context = i2o_scsi_handler.context;
	
	if((sg_chain_pool = kmalloc(SG_CHAIN_POOL_SZ, GFP_KERNEL)) == NULL)
	{
		printk(KERN_INFO "i2o_scsi: Unable to alloc %d byte SG chain buffer pool.\n", SG_CHAIN_POOL_SZ);
		printk(KERN_INFO "i2o_scsi: SG chaining DISABLED!\n");
		sg_max_frags = 11;
	}
	else
	{
		printk(KERN_INFO "  chain_pool: %d bytes @ %p\n", SG_CHAIN_POOL_SZ, sg_chain_pool);
		printk(KERN_INFO "  (%d byte buffers X %d can_queue X %d i2o controllers)\n",
				SG_CHAIN_BUF_SZ, I2O_SCSI_CAN_QUEUE, i2o_num_controllers);
		sg_max_frags = SG_MAX_FRAGS;    // 64
	}
	
	init_timer(&retry_timer);
	retry_timer.data = 0UL;
	retry_timer.function = i2o_retry_run;
	
//	printk("SCSI OSM at %d.\n", scsi_context);

	for (count = 0, i = 0; i < MAX_I2O_CONTROLLERS; i++)
	{
		struct i2o_controller *c=i2o_find_controller(i);
		struct i2o_device *d;
		/*
		 *	This controller doesn't exist.
		 */
		
		if(c==NULL)
			continue;
			
		/*
		 *	Fixme - we need some altered device locking. This
		 *	is racing with device addition in theory. Easy to fix.
		 */
		
		for(d=c->devices;d!=NULL;d=d->next)
		{
			/*
			 *	bus_adapter, SCSI (obsolete), or FibreChannel busses only
			 */
			if(    (d->lct_data.class_id!=I2O_CLASS_BUS_ADAPTER_PORT)	// bus_adapter
//			    && (d->lct_data.class_id!=I2O_CLASS_FIBRE_CHANNEL_PORT)	// FC_PORT
			  )
				continue;
		
			shpnt = scsi_register(tpnt, sizeof(struct i2o_scsi_host));
			if(shpnt==NULL)
				continue;
			shpnt->unique_id = (u32)d;
			shpnt->io_port = 0;
			shpnt->n_io_port = 0;
			shpnt->irq = 0;
			shpnt->this_id = /* Good question */15;
			i2o_scsi_init(c, d, shpnt);
			count++;
		}
	}
	i2o_scsi_hosts = count;
	
	if(count==0)
	{
		if(sg_chain_pool!=NULL)
		{
			kfree(sg_chain_pool);
			sg_chain_pool = NULL;
		}
		flush_pending();
		del_timer(&retry_timer);
		i2o_remove_handler(&i2o_scsi_handler);
	}
	
	return count;
}

static int i2o_scsi_release(struct Scsi_Host *host)
{
	if(--i2o_scsi_hosts==0)
	{
		if(sg_chain_pool!=NULL)
		{
			kfree(sg_chain_pool);
			sg_chain_pool = NULL;
		}
		flush_pending();
		del_timer(&retry_timer);
		i2o_remove_handler(&i2o_scsi_handler);
	}
	return 0;
}


static const char *i2o_scsi_info(struct Scsi_Host *SChost)
{
	struct i2o_scsi_host *hostdata;
	hostdata = (struct i2o_scsi_host *)SChost->hostdata;
	return(&hostdata->controller->name[0]);
}

/**
 *	i2o_scsi_queuecommand	-	queue a SCSI command
 *	@SCpnt: scsi command pointer
 *	@done: callback for completion
 *
 *	Issue a scsi comamnd asynchronously. Return 0 on success or 1 if
 *	we hit an error (normally message queue congestion). The only 
 *	minor complication here is that I2O deals with the device addressing
 *	so we have to map the bus/dev/lun back to an I2O handle as well
 *	as faking absent devices ourself. 
 *
 *	Locks: takes the controller lock on error path only
 */
 
static int i2o_scsi_queuecommand(Scsi_Cmnd * SCpnt, void (*done) (Scsi_Cmnd *))
{
	int i;
	int tid;
	struct i2o_controller *c;
	Scsi_Cmnd *current_command;
	struct Scsi_Host *host;
	struct i2o_scsi_host *hostdata;
	u32 *msg, *mptr;
	u32 m;
	u32 *lenptr;
	int direction;
	int scsidir;
	u32 len;
	u32 reqlen;
	u32 tag;
	unsigned long flags;
	
	static int max_qd = 1;
	
	/*
	 *	Do the incoming paperwork
	 */
	 
	host = SCpnt->device->host;
	hostdata = (struct i2o_scsi_host *)host->hostdata;
	 
	c = hostdata->controller;
	prefetch(c);
	prefetchw(&queue_depth);

	SCpnt->scsi_done = done;
	
	if(SCpnt->device->id > 15)
	{
		printk(KERN_ERR "i2o_scsi: Wild target %d.\n", SCpnt->device->id);
		return -1;
	}
	
	tid = hostdata->task[SCpnt->device->id][SCpnt->device->lun];
	
	dprintk(("qcmd: Tid = %d\n", tid));
	
	current_command = SCpnt;		/* set current command                */
	current_command->scsi_done = done;	/* set ptr to done function           */

	/* We don't have such a device. Pretend we did the command 
	   and that selection timed out */
	
	if(tid == -1)
	{
		SCpnt->result = DID_NO_CONNECT << 16;
		spin_lock_irqsave(host->host_lock, flags);
		done(SCpnt);
		spin_unlock_irqrestore(host->host_lock, flags);
		return 0;
	}
	
	dprintk(("Real scsi messages.\n"));

	/*
	 *	Obtain an I2O message. If there are none free then 
	 *	throw it back to the scsi layer
	 */	
	 
	m = le32_to_cpu(I2O_POST_READ32(c));
	if(m==0xFFFFFFFF)
		return 1;

	msg = (u32 *)(c->mem_offset + m);
	
	/*
	 *	Put together a scsi execscb message
	 */
	
	len = SCpnt->request_bufflen;
	direction = 0x00000000;			// SGL IN  (osm<--iop)
	
	if(SCpnt->sc_data_direction == SCSI_DATA_NONE)
		scsidir = 0x00000000;			// DATA NO XFER
	else if(SCpnt->sc_data_direction == SCSI_DATA_WRITE)
	{
		direction=0x04000000;	// SGL OUT  (osm-->iop)
		scsidir  =0x80000000;	// DATA OUT (iop-->dev)
	}
	else if(SCpnt->sc_data_direction == SCSI_DATA_READ)
	{
		scsidir  =0x40000000;	// DATA IN  (iop<--dev)
	}
	else
	{
		/* Unknown - kill the command */
		SCpnt->result = DID_NO_CONNECT << 16;
		
		/* We must lock the request queue while completing */
		spin_lock_irqsave(host->host_lock, flags);
		done(SCpnt);
		spin_unlock_irqrestore(host->host_lock, flags);
		return 0;
	}

	
	i2o_raw_writel(I2O_CMD_SCSI_EXEC<<24|HOST_TID<<12|tid, &msg[1]);
	i2o_raw_writel(scsi_context, &msg[2]);	/* So the I2O layer passes to us */
	/* Sorry 64bit folks. FIXME */
	i2o_raw_writel((u32)SCpnt, &msg[3]);	/* We want the SCSI control block back */

	/* LSI_920_PCI_QUIRK
	 *
	 *	Intermittant observations of msg frame word data corruption
	 *	observed on msg[4] after:
	 *	  WRITE, READ-MODIFY-WRITE
	 *	operations.  19990606 -sralston
	 *
	 *	(Hence we build this word via tag. Its good practice anyway
	 *	 we don't want fetches over PCI needlessly)
	 */

	tag=0;
	
	/*
	 *	Attach tags to the devices
	 */	
	if(SCpnt->device->tagged_supported)
	{
		/*
		 *	Some drives are too stupid to handle fairness issues
		 *	with tagged queueing. We throw in the odd ordered
		 *	tag to stop them starving themselves.
		 */
		if((jiffies - hostdata->tagclock[SCpnt->device->id][SCpnt->device->lun]) > (5*HZ))
		{
			tag=0x01800000;		/* ORDERED! */
			hostdata->tagclock[SCpnt->device->id][SCpnt->device->lun]=jiffies;
		}
		else
		{
			/* Hmmm...  I always see value of 0 here,
			 *  of which {HEAD_OF, ORDERED, SIMPLE} are NOT!  -sralston
			 */
			if(SCpnt->tag == HEAD_OF_QUEUE_TAG)
				tag=0x01000000;
			else if(SCpnt->tag == ORDERED_QUEUE_TAG)
				tag=0x01800000;
		}
	}

	/* Direction, disconnect ok, tag, CDBLen */
	i2o_raw_writel(scsidir|0x20000000|SCpnt->cmd_len|tag, &msg[4]);

	mptr=msg+5;

	/* 
	 *	Write SCSI command into the message - always 16 byte block 
	 */
	 
	memcpy_toio(mptr, SCpnt->cmnd, 16);
	mptr+=4;
	lenptr=mptr++;		/* Remember me - fill in when we know */
	
	reqlen = 12;		// SINGLE SGE
	
	/*
	 *	Now fill in the SGList and command 
	 *
	 *	FIXME: we need to set the sglist limits according to the 
	 *	message size of the I2O controller. We might only have room
	 *	for 6 or so worst case
	 */
	
	if(SCpnt->use_sg)
	{
		struct scatterlist *sg = (struct scatterlist *)SCpnt->request_buffer;
		int sg_count;
		int chain = 0;
		
		len = 0;

		sg_count = pci_map_sg(c->pdev, sg, SCpnt->use_sg,
				      scsi_to_pci_dma_dir(SCpnt->sc_data_direction));

		/* FIXME: handle fail */
		if(!sg_count)
			BUG();
		
		if((sg_max_frags > 11) && (SCpnt->use_sg > 11))
		{
			chain = 1;
			/*
			 *	Need to chain!
			 */
			i2o_raw_writel(direction|0xB0000000|(SCpnt->use_sg*2*4), mptr++);
			i2o_raw_writel(virt_to_bus(sg_chain_pool + sg_chain_tag), mptr);
			mptr = (u32*)(sg_chain_pool + sg_chain_tag);
			if (SCpnt->use_sg > max_sg_len)
			{
				max_sg_len = SCpnt->use_sg;
				printk("i2o_scsi: Chain SG! SCpnt=%p, SG_FragCnt=%d, SG_idx=%d\n",
					SCpnt, SCpnt->use_sg, sg_chain_tag);
			}
			if ( ++sg_chain_tag == SG_MAX_BUFS )
				sg_chain_tag = 0;
			for(i = 0 ; i < SCpnt->use_sg; i++)
			{
				*mptr++=cpu_to_le32(direction|0x10000000|sg_dma_len(sg));
				len+=sg_dma_len(sg);
				*mptr++=cpu_to_le32(sg_dma_address(sg));
				sg++;
			}
			mptr[-2]=cpu_to_le32(direction|0xD0000000|sg_dma_len(sg-1));
		}
		else
		{		
			for(i = 0 ; i < SCpnt->use_sg; i++)
			{
				i2o_raw_writel(direction|0x10000000|sg_dma_len(sg), mptr++);
				len+=sg->length;
				i2o_raw_writel(sg_dma_address(sg), mptr++);
				sg++;
			}

			/* Make this an end of list. Again evade the 920 bug and
			   unwanted PCI read traffic */
		
			i2o_raw_writel(direction|0xD0000000|sg_dma_len(sg-1), &mptr[-2]);
		}
		
		if(!chain)
			reqlen = mptr - msg;
		
		i2o_raw_writel(len, lenptr);
		
		if(len != SCpnt->underflow)
			printk("Cmd len %08X Cmd underflow %08X\n",
				len, SCpnt->underflow);
	}
	else
	{
		dprintk(("non sg for %p, %d\n", SCpnt->request_buffer,
				SCpnt->request_bufflen));
		i2o_raw_writel(len = SCpnt->request_bufflen, lenptr);
		if(len == 0)
		{
			reqlen = 9;
		}
		else
		{
			dma_addr_t dma_addr;
			dma_addr = pci_map_single(c->pdev,
					       SCpnt->request_buffer,
					       SCpnt->request_bufflen,
					       scsi_to_pci_dma_dir(SCpnt->sc_data_direction));
			if(dma_addr == 0)
				BUG();	/* How to handle ?? */
			SCpnt->SCp.ptr = (char *)(unsigned long) dma_addr;
			i2o_raw_writel(0xD0000000|direction|SCpnt->request_bufflen, mptr++);
			i2o_raw_writel(dma_addr, mptr++);
		}
	}
	
	/*
	 *	Stick the headers on 
	 */

	i2o_raw_writel(reqlen<<16 | SGL_OFFSET_10, msg);
	
	/* Queue the message */
	i2o_post_message(c,m);
	
	atomic_inc(&queue_depth);
	
	if(atomic_read(&queue_depth)> max_qd)
	{
		max_qd=atomic_read(&queue_depth);
		printk("Queue depth now %d.\n", max_qd);
	}
	
	mb();
	dprintk(("Issued %ld\n", current_command->serial_number));
	
	return 0;
}

/**
 *	i2o_scsi_abort	-	abort a running command
 *	@SCpnt: command to abort
 *
 *	Ask the I2O controller to abort a command. This is an asynchrnous
 *	process and oru callback handler will see the command complete
 *	with an aborted message if it succeeds. 
 *
 *	Locks: no locks are held or needed
 */
 
int i2o_scsi_abort(Scsi_Cmnd * SCpnt)
{
	struct i2o_controller *c;
	struct Scsi_Host *host;
	struct i2o_scsi_host *hostdata;
	unsigned long msg;
	u32 m;
	int tid;
	unsigned long timeout;
	
	printk(KERN_WARNING "i2o_scsi: Aborting command block.\n");
	
	host = SCpnt->device->host;
	hostdata = (struct i2o_scsi_host *)host->hostdata;
	tid = hostdata->task[SCpnt->device->id][SCpnt->device->lun];
	if(tid==-1)
	{
		printk(KERN_ERR "i2o_scsi: Impossible command to abort!\n");
		return FAILED;
	}
	c = hostdata->controller;

	spin_unlock_irq(host->host_lock);
		
	timeout = jiffies+2*HZ;
	do
	{
		m = le32_to_cpu(I2O_POST_READ32(c));
		if(m != 0xFFFFFFFF)
			break;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
		mb();
	}
	while(time_before(jiffies, timeout));
	
	msg = c->mem_offset + m;
	
	i2o_raw_writel(FIVE_WORD_MSG_SIZE, msg);
	i2o_raw_writel(I2O_CMD_SCSI_ABORT<<24|HOST_TID<<12|tid, msg+4);
	i2o_raw_writel(scsi_context, msg+8);
	i2o_raw_writel(0, msg+12);	/* Not needed for an abort */
	i2o_raw_writel((u32)SCpnt, msg+16);	
	wmb();
	i2o_post_message(c,m);
	wmb();
	
	spin_lock_irq(host->host_lock);
	return SUCCESS;
}

/**
 *	i2o_scsi_bus_reset		-	Issue a SCSI reset
 *	@SCpnt: the command that caused the reset
 *
 *	Perform a SCSI bus reset operation. In I2O this is just a message
 *	we pass. I2O can do clever multi-initiator and shared reset stuff
 *	but we don't support this.
 *
 *	Locks: called with no lock held, requires no locks.
 */
 
static int i2o_scsi_bus_reset(Scsi_Cmnd * SCpnt)
{
	int tid;
	struct i2o_controller *c;
	struct Scsi_Host *host;
	struct i2o_scsi_host *hostdata;
	u32 m;
	unsigned long msg;
	unsigned long timeout;

	
	/*
	 *	Find the TID for the bus
	 */

	
	host = SCpnt->device->host;

	spin_unlock_irq(host->host_lock);

	printk(KERN_WARNING "i2o_scsi: Attempting to reset the bus.\n");

	hostdata = (struct i2o_scsi_host *)host->hostdata;
	tid = hostdata->bus_task;
	c = hostdata->controller;

	/*
	 *	Now send a SCSI reset request. Any remaining commands
	 *	will be aborted by the IOP. We need to catch the reply
	 *	possibly ?
	 */

	timeout = jiffies+2*HZ;
	do
	{
		m = le32_to_cpu(I2O_POST_READ32(c));
		if(m != 0xFFFFFFFF)
			break;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
		mb();
	}
	while(time_before(jiffies, timeout));
	
	
	msg = c->mem_offset + m;
	i2o_raw_writel(FOUR_WORD_MSG_SIZE|SGL_OFFSET_0, msg);
	i2o_raw_writel(I2O_CMD_SCSI_BUSRESET<<24|HOST_TID<<12|tid, msg+4);
	i2o_raw_writel(scsi_context|0x80000000, msg+8);
	/* We use the top bit to split controller and unit transactions */
	/* Now store unit,tid so we can tie the completion back to a specific device */
	__raw_writel(c->unit << 16 | tid, msg+12);
	wmb();

	/* We want the command to complete after we return */	
	spin_lock_irq(host->host_lock);
	i2o_post_message(c,m);

	/* Should we wait for the reset to complete ? */	
	return SUCCESS;
}

/**
 *	i2o_scsi_host_reset	-	host reset callback
 *	@SCpnt: command causing the reset
 *
 *	An I2O controller can be many things at once. While we can
 *	reset a controller the potential mess from doing so is vast, and
 *	it's better to simply hold on and pray
 */
 
static int i2o_scsi_host_reset(Scsi_Cmnd * SCpnt)
{
	return FAILED;
}

/**
 *	i2o_scsi_device_reset	-	device reset callback
 *	@SCpnt: command causing the reset
 *
 *	I2O does not (AFAIK) support doing a device reset
 */
 
static int i2o_scsi_device_reset(Scsi_Cmnd * SCpnt)
{
	return FAILED;
}

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
 
static int i2o_scsi_bios_param(struct scsi_device * sdev,
		struct block_device *dev, sector_t capacity, int *ip)
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

MODULE_AUTHOR("Red Hat Software");
MODULE_LICENSE("GPL");


static Scsi_Host_Template driver_template = {
	.proc_name		= "i2o_scsi",
	.name			= "I2O SCSI Layer",
	.detect			= i2o_scsi_detect,
	.release		= i2o_scsi_release,
	.info			= i2o_scsi_info,
	.queuecommand		= i2o_scsi_queuecommand,
	.eh_abort_handler	= i2o_scsi_abort,
	.eh_bus_reset_handler	= i2o_scsi_bus_reset,
	.eh_device_reset_handler= i2o_scsi_device_reset,
	.eh_host_reset_handler	= i2o_scsi_host_reset,
	.bios_param		= i2o_scsi_bios_param,
	.can_queue		= I2O_SCSI_CAN_QUEUE,
	.this_id		= 15,
	.sg_tablesize		= 8,
	.cmd_per_lun		= 6,
	.use_clustering		= ENABLE_CLUSTERING,
};

#include "../../scsi/scsi_module.c"
