/*
 * I2O Random Block Storage Class OSM
 *
 * (C) Copyright 1999 Red Hat Software
 *	
 * Written by Alan Cox, Building Number Three Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This is a beta test release. Most of the good code was taken
 * from the nbd driver by Pavel Machek, who in turn took some of it
 * from loop.c. Isn't free software great for reusability 8)
 *
 * Fixes/additions:
 *	Steve Ralston:	
 *		Multiple device handling error fixes,
 *		Added a queue depth.
 *	Alan Cox:	
 *		FC920 has an rmw bug. Dont or in the end marker.
 *		Removed queue walk, fixed for 64bitness.
 *	Deepak Saxena:
 *		Independent queues per IOP
 *		Support for dynamic device creation/deletion
 *		Code cleanup	
 *    		Support for larger I/Os through merge* functions 
 *       	(taken from DAC960 driver)
 *	Boji T Kannanthanam:
 *		Reduced the timeout during RAID 5 creation. 
 *		This is to prevent race condition when a RAID volume
 *		is created and immediately deleted.
 *
 *	To do:
 *		Serial number scanning to find duplicates for FC multipathing
 *		Remove the random timeout in the code needed for RAID 5
 *			volume creation.
 */

#include <linux/major.h>

#include <linux/module.h>

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/ioctl.h>
#include <linux/i2o.h>
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/malloc.h>
#include <linux/hdreg.h>

#include <linux/notifier.h>
#include <linux/reboot.h>

#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <linux/smp_lock.h>
#include <linux/wait.h>

#define MAJOR_NR I2O_MAJOR

#include <linux/blk.h>

#define MAX_I2OB	16

#define MAX_I2OB_DEPTH	128
#define MAX_I2OB_RETRIES 4

//#define DRIVERDEBUG
#ifdef DRIVERDEBUG
#define DEBUG( s )
#else
#define DEBUG( s ) printk( s ) 
#endif

/*
 * Events that this OSM is interested in
 */
#define I2OB_EVENT_MASK		(I2O_EVT_IND_BSA_VOLUME_LOAD |	\
				 I2O_EVT_IND_BSA_VOLUME_UNLOAD | \
				 I2O_EVT_IND_BSA_VOLUME_UNLOAD_REQ | \
				 I2O_EVT_IND_BSA_CAPACITY_CHANGE)


/*
 * I2O Block Error Codes - should be in a header file really...
 */
#define I2O_BSA_DSC_SUCCESS             0x0000
#define I2O_BSA_DSC_MEDIA_ERROR         0x0001
#define I2O_BSA_DSC_ACCESS_ERROR        0x0002
#define I2O_BSA_DSC_DEVICE_FAILURE      0x0003
#define I2O_BSA_DSC_DEVICE_NOT_READY    0x0004
#define I2O_BSA_DSC_MEDIA_NOT_PRESENT   0x0005
#define I2O_BSA_DSC_MEDIA_LOCKED        0x0006
#define I2O_BSA_DSC_MEDIA_FAILURE       0x0007
#define I2O_BSA_DSC_PROTOCOL_FAILURE    0x0008
#define I2O_BSA_DSC_BUS_FAILURE         0x0009
#define I2O_BSA_DSC_ACCESS_VIOLATION    0x000A
#define I2O_BSA_DSC_WRITE_PROTECTED     0x000B
#define I2O_BSA_DSC_DEVICE_RESET        0x000C
#define I2O_BSA_DSC_VOLUME_CHANGED      0x000D
#define I2O_BSA_DSC_TIMEOUT             0x000E

/*
 *	Some of these can be made smaller later
 */

static int i2ob_blksizes[MAX_I2OB<<4];
static int i2ob_hardsizes[MAX_I2OB<<4];
static int i2ob_sizes[MAX_I2OB<<4];
static int i2ob_media_change_flag[MAX_I2OB];
static u32 i2ob_max_sectors[MAX_I2OB<<4];

static int i2ob_context;

/*
 * I2O Block device descriptor 
 */
struct i2ob_device
{
	struct i2o_controller *controller;
	struct i2o_device *i2odev;
	int unit;
	int tid;
	int flags;
	int refcnt;
	struct request *head, *tail;
	request_queue_t *req_queue;
	int max_segments;
	int done_flag;
};

/*
 *	FIXME:
 *	We should cache align these to avoid ping-ponging lines on SMP
 *	boxes under heavy I/O load...
 */
struct i2ob_request
{
	struct i2ob_request *next;
	struct request *req;
	int num;
};

/*
 * Per IOP requst queue information
 *
 * We have a separate requeust_queue_t per IOP so that a heavilly
 * loaded I2O block device on an IOP does not starve block devices
 * across all I2O controllers.
 * 
 */
struct i2ob_iop_queue
{
	atomic_t queue_depth;
	struct i2ob_request request_queue[MAX_I2OB_DEPTH];
	struct i2ob_request *i2ob_qhead;
	request_queue_t req_queue;
};
static struct i2ob_iop_queue *i2ob_queues[MAX_I2O_CONTROLLERS] = {NULL};

/*
 *	Each I2O disk is one of these.
 */

static struct i2ob_device i2ob_dev[MAX_I2OB<<4];
static int i2ob_dev_count = 0;
static struct hd_struct i2ob[MAX_I2OB<<4];
static struct gendisk i2ob_gendisk;	/* Declared later */

/*
 * Mutex and spin lock for event handling synchronization
 * evt_msg contains the last event.
 */
DECLARE_MUTEX(i2ob_evt_sem);
static spinlock_t i2ob_evt_lock = SPIN_LOCK_UNLOCKED;
static unsigned int evt_msg[MSG_FRAME_SIZE>>2];
DECLARE_WAIT_QUEUE_HEAD(i2ob_evt_wait);

static struct timer_list i2ob_timer;
static int i2ob_timer_started = 0;

static void i2o_block_reply(struct i2o_handler *, struct i2o_controller *,
	 struct i2o_message *);
static void i2ob_new_device(struct i2o_controller *, struct i2o_device *);
static void i2ob_del_device(struct i2o_controller *, struct i2o_device *);
static void i2ob_reboot_event(void);
static int i2ob_install_device(struct i2o_controller *, struct i2o_device *, int);
static void i2ob_end_request(struct request *);
static void i2ob_request(request_queue_t *);
static int i2ob_init_iop(unsigned int);
static request_queue_t* i2ob_get_queue(kdev_t);
static int i2ob_query_device(struct i2ob_device *, int, int, void*, int);
static int do_i2ob_revalidate(kdev_t, int);
static int i2ob_evt(void *);

static int evt_pid = 0;
static int evt_running = 0;

/*
 * I2O OSM registration structure...keeps getting bigger and bigger :)
 */
static struct i2o_handler i2o_block_handler =
{
	i2o_block_reply,
	i2ob_new_device,
	i2ob_del_device,
	i2ob_reboot_event,
	"I2O Block OSM",
	0,
	I2O_CLASS_RANDOM_BLOCK_STORAGE
};

/*
 *	Get a message
 */

static u32 i2ob_get(struct i2ob_device *dev)
{
	struct i2o_controller *c=dev->controller;
   	return I2O_POST_READ32(c);
}
 
/*
 *	Turn a Linux block request into an I2O block read/write.
 */

static int i2ob_send(u32 m, struct i2ob_device *dev, struct i2ob_request *ireq, u32 base, int unit)
{
	struct i2o_controller *c = dev->controller;
	int tid = dev->tid;
	unsigned long msg;
	unsigned long mptr;
	u64 offset;
	struct request *req = ireq->req;
	struct buffer_head *bh = req->bh;
	int count = req->nr_sectors<<9;
	char *last = NULL;
	unsigned short size = 0;

	// printk(KERN_INFO "i2ob_send called\n");
	/* Map the message to a virtual address */
	msg = c->mem_offset + m;
	
	/*
	 * Build the message based on the request.
	 */
	__raw_writel(i2ob_context|(unit<<8), msg+8);
	__raw_writel(ireq->num, msg+12);
	__raw_writel(req->nr_sectors << 9, msg+20);
	
	/* This can be optimised later - just want to be sure its right for
	   starters */
	offset = ((u64)(req->sector+base)) << 9;
	__raw_writel( offset & 0xFFFFFFFF, msg+24);
	__raw_writel(offset>>32, msg+28);
	mptr=msg+32;
	
	if(req->cmd == READ)
	{
		__raw_writel(I2O_CMD_BLOCK_READ<<24|HOST_TID<<12|tid, msg+4);
		/* We don't yet do cache/readahead and other magic */
		__raw_writel(1<<16, msg+16);
		while(bh!=NULL)
		{
			if(bh->b_data == last) {
				size += bh->b_size;
				last += bh->b_size;
				if(bh->b_reqnext)
					__raw_writel(0x14000000|(size), mptr-8);
				else
					__raw_writel(0xD4000000|(size), mptr-8);
			}
			else
			{
				if(bh->b_reqnext)
					__raw_writel(0x10000000|(bh->b_size), mptr);
				else
					__raw_writel(0xD0000000|(bh->b_size), mptr);
				__raw_writel(virt_to_bus(bh->b_data), mptr+4);
				mptr += 8;	
				size = bh->b_size;
				last = bh->b_data + size;
			}

			count -= bh->b_size;
			bh = bh->b_reqnext;
		}
	}
	else if(req->cmd == WRITE)
	{
		__raw_writel(I2O_CMD_BLOCK_WRITE<<24|HOST_TID<<12|tid, msg+4);
		/* 
		 * Allow replies to come back once data is cached in the controller
		 * This allows us to handle writes quickly thus giving more of the
		 * queue to reads.
		 */
		__raw_writel(0x00000010, msg+16);
		while(bh!=NULL)
		{
			if(bh->b_data == last) {
				size += bh->b_size;
				last += bh->b_size;
				if(bh->b_reqnext)
					__raw_writel(0x14000000|(size), mptr-8);
				else
					__raw_writel(0xD4000000|(size), mptr-8);
			}
			else
			{
				if(bh->b_reqnext)
					__raw_writel(0x14000000|(bh->b_size), mptr);
				else
					__raw_writel(0xD4000000|(bh->b_size), mptr);
				__raw_writel(virt_to_bus(bh->b_data), mptr+4);
				mptr += 8;	
				size = bh->b_size;
				last = bh->b_data + size;
			}

			count -= bh->b_size;
			bh = bh->b_reqnext;
		}
	}
	__raw_writel(I2O_MESSAGE_SIZE(mptr-msg)>>2 | SGL_OFFSET_8, msg);
	
	if(req->current_nr_sectors > i2ob_max_sectors[unit])
		printk("Gathered sectors %ld.\n", 
			req->current_nr_sectors);
			
	if(count != 0)
	{
		printk(KERN_ERR "Request count botched by %d.\n", count);
	}

	i2o_post_message(c,m);
	atomic_inc(&i2ob_queues[c->unit]->queue_depth);

	return 0;
}

/*
 *	Remove a request from the _locked_ request list. We update both the
 *	list chain and if this is the last item the tail pointer. Caller
 *	must hold the lock.
 */
 
static inline void i2ob_unhook_request(struct i2ob_request *ireq, 
	unsigned int iop)
{
	ireq->next = i2ob_queues[iop]->i2ob_qhead;
	i2ob_queues[iop]->i2ob_qhead = ireq;
}

/*
 *	Request completion handler
 */
 
static inline void i2ob_end_request(struct request *req)
{
	/*
	 * Loop until all of the buffers that are linked
	 * to this request have been marked updated and
	 * unlocked.
	 */

	while (end_that_request_first( req, !req->errors, "i2o block" ));

	/*
	 * It is now ok to complete the request.
	 */
	end_that_request_last( req );
}

/*
 * Request merging functions
 */
static inline int i2ob_new_segment(request_queue_t *q, struct request *req,
				  int __max_segments)
{
	int max_segments = i2ob_dev[MINOR(req->rq_dev)].max_segments;

	if (__max_segments < max_segments)
		max_segments = __max_segments;

	if (req->nr_segments < max_segments) {
		req->nr_segments++;
		q->elevator.nr_segments++;
		return 1;
	}
	return 0;
}

static int i2ob_back_merge(request_queue_t *q, struct request *req, 
			     struct buffer_head *bh, int __max_segments)
{
	if (req->bhtail->b_data + req->bhtail->b_size == bh->b_data)
		return 1;
	return i2ob_new_segment(q, req, __max_segments);
}

static int i2ob_front_merge(request_queue_t *q, struct request *req, 
			      struct buffer_head *bh, int __max_segments)
{
	if (bh->b_data + bh->b_size == req->bh->b_data)
		return 1;
	return i2ob_new_segment(q, req, __max_segments);
}

static int i2ob_merge_requests(request_queue_t *q,
				struct request *req,
				struct request *next,
				int __max_segments)
{
	int max_segments = i2ob_dev[MINOR(req->rq_dev)].max_segments;
	int total_segments = req->nr_segments + next->nr_segments;
	int same_segment;

	if (__max_segments < max_segments)
		max_segments = __max_segments;

	same_segment = 0;
	if (req->bhtail->b_data + req->bhtail->b_size == next->bh->b_data)
	{
		total_segments--;
		same_segment = 1;
	}
    
	if (total_segments > max_segments)
		return 0;

	q->elevator.nr_segments -= same_segment;
	req->nr_segments = total_segments;
	return 1;
}


/*
 *	OSM reply handler. This gets all the message replies
 */

static void i2o_block_reply(struct i2o_handler *h, struct i2o_controller *c, struct i2o_message *msg)
{
	unsigned long flags;
	struct i2ob_request *ireq = NULL;
	u8 st;
	u32 *m = (u32 *)msg;
	u8 unit = (m[2]>>8)&0xF0;	/* low 4 bits are partition */
	struct i2ob_device *dev = &i2ob_dev[(unit&0xF0)];

	/*
	 * FAILed message
	 */
	if(m[0] & (1<<13))
	{
		/*
		 * FAILed message from controller
		 * We increment the error count and abort it
		 *
		 * In theory this will never happen.  The I2O block class
		 * speficiation states that block devices never return
		 * FAILs but instead use the REQ status field...but
		 * better be on the safe side since no one really follows
		 * the spec to the book :)
		 */
		ireq=&i2ob_queues[c->unit]->request_queue[m[3]];
		ireq->req->errors++;

		spin_lock_irqsave(&io_request_lock, flags);
		i2ob_unhook_request(ireq, c->unit);
		i2ob_end_request(ireq->req);
		spin_unlock_irqrestore(&io_request_lock, flags);
	
		/* Now flush the message by making it a NOP */
		m[0]&=0x00FFFFFF;
		m[0]|=(I2O_CMD_UTIL_NOP)<<24;
		i2o_post_message(c,virt_to_bus(m));

		return;
	}

	if(msg->function == I2O_CMD_UTIL_EVT_REGISTER)
	{
		spin_lock(&i2ob_evt_lock);
		memcpy(&evt_msg, m, msg->size);
		spin_unlock(&i2ob_evt_lock);
		wake_up_interruptible(&i2ob_evt_wait);
		return;
	}

	if(!dev->i2odev)
	{
		/*
		 * This is HACK, but Intel Integrated RAID allows user
		 * to delete a volume that is claimed, locked, and in use 
		 * by the OS. We have to check for a reply from a
		 * non-existent device and flag it as an error or the system 
		 * goes kaput...
		 */
		ireq=&i2ob_queues[c->unit]->request_queue[m[3]];
		ireq->req->errors++;
		printk(KERN_WARNING "I2O Block: Data transfer to deleted device!\n");
		spin_lock_irqsave(&io_request_lock, flags);
		i2ob_unhook_request(ireq, c->unit);
		i2ob_end_request(ireq->req);
		spin_unlock_irqrestore(&io_request_lock, flags);
		return;
	}	

	/*
	 *	Lets see what is cooking. We stuffed the
	 *	request in the context.
	 */
		 
	ireq=&i2ob_queues[c->unit]->request_queue[m[3]];
	st=m[4]>>24;

	if(st!=0)
	{
		char *bsa_errors[] = 
		{ 
			"Success", 
			"Media Error", 
			"Failure communicating to device",
			"Device Failure",
			"Device is not ready",
			"Media not present",
			"Media is locked by another user",
			"Media has failed",
			"Failure communicating to device",
			"Device bus failure",
			"Device is locked by another user",
			"Device is write protected",
			"Device has reset",
			"Volume has changed, waiting for acknowledgement"
		};
				
		printk(KERN_ERR "\n/dev/%s error: %s", dev->i2odev->dev_name, 
			bsa_errors[m[4]&0XFFFF]);
		if(m[4]&0x00FF0000)
			printk(" - DDM attempted %d retries", (m[4]>>16)&0x00FF );
		printk("\n");

		ireq->req->errors++;	
	}
	else
		ireq->req->errors = 0;

	/*
	 *	Dequeue the request. We use irqsave locks as one day we
	 *	may be running polled controllers from a BH...
	 */
	
	spin_lock_irqsave(&io_request_lock, flags);
	i2ob_unhook_request(ireq, c->unit);
	i2ob_end_request(ireq->req);
	atomic_dec(&i2ob_queues[c->unit]->queue_depth);

	/*
	 *	We may be able to do more I/O
	 */
	i2ob_request(dev->req_queue);

	spin_unlock_irqrestore(&io_request_lock, flags);
}

/* 
 * Event handler.  Needs to be a separate thread b/c we may have
 * to do things like scan a partition table, or query parameters
 * which cannot be done from an interrupt or from a bottom half.
 */
static int i2ob_evt(void *dummy)
{
	unsigned int evt;
	unsigned int flags;
	int unit;
	int i;

	lock_kernel();
	daemonize();
	unlock_kernel();

	strcpy(current->comm, "i2oblock");
	evt_running = 1;

	while(1)
	{
#warning "RACE"
		interruptible_sleep_on(&i2ob_evt_wait);
 		if(signal_pending(current)) {
			evt_running = 0;
			return 0;
		}

		printk(KERN_INFO "Doing something in i2o_block event thread\n");
		
		/*
		 * Keep another CPU/interrupt from overwriting the 
		 * message while we're reading it
		 *
		 * We stuffed the unit in the TxContext and grab the event mask
		 * None of the BSA we care about events have EventData
		 */
		spin_lock_irqsave(&i2ob_evt_lock, flags);
		unit = evt_msg[3];
		evt = evt_msg[4];
		spin_unlock_irqrestore(&i2ob_evt_lock, flags);

		switch(evt)
		{
			/*
			 * New volume loaded on same TID, so we just re-install.
			 * The TID/controller don't change as it is the same
			 * I2O device.  It's just new media that we have to
			 * rescan.
			 */
			case I2O_EVT_IND_BSA_VOLUME_LOAD:
			{
				i2ob_install_device(i2ob_dev[unit].i2odev->controller, 
					i2ob_dev[unit].i2odev, unit);
				break;
			}

			/*
			 * No media, so set all parameters to 0 and set the media
			 * change flag. The I2O device is still valid, just doesn't
			 * have media, so we don't want to clear the controller or
			 * device pointer.
			 */
			case I2O_EVT_IND_BSA_VOLUME_UNLOAD:
			{
				for(i = unit; i <= unit+15; i++)
				{
					i2ob_sizes[i] = 0;
					i2ob_hardsizes[i] = 0;
					i2ob_max_sectors[i] = 0;
					i2ob[i].nr_sects = 0;
					i2ob_gendisk.part[i].nr_sects = 0;
				}
				i2ob_media_change_flag[unit] = 1;
				break;
			}

			case I2O_EVT_IND_BSA_VOLUME_UNLOAD_REQ:
				printk(KERN_WARNING "%s: Attempt to eject locked media\n", 
					i2ob_dev[unit].i2odev->dev_name);
				break;

			/*
			 * The capacity has changed and we are going to be
			 * updating the max_sectors and other information 
			 * about this disk.  We try a revalidate first. If
			 * the block device is in use, we don't want to
			 * do that as there may be I/Os bound for the disk
			 * at the moment.  In that case we read the size 
			 * from the device and update the information ourselves
			 * and the user can later force a partition table
			 * update through an ioctl.
			 */
			case I2O_EVT_IND_BSA_CAPACITY_CHANGE:
			{
				u64 size;

				if(do_i2ob_revalidate(MKDEV(MAJOR_NR, unit),0) != -EBUSY)
					continue;

	  			if(i2ob_query_device(&i2ob_dev[unit], 0x0004, 0, &size, 8) !=0 )
					i2ob_query_device(&i2ob_dev[unit], 0x0000, 4, &size, 8);

				spin_lock_irqsave(&io_request_lock, flags);	
				i2ob_sizes[unit] = (int)(size>>10);
				i2ob_gendisk.part[unit].nr_sects = size>>9;
				i2ob[unit].nr_sects = (int)(size>>9);
				spin_unlock_irqrestore(&io_request_lock, flags);	
				break;
			}

			/*
			 * An event we didn't ask for.  Call the card manufacturer
			 * and tell them to fix their firmware :)
			 */
			default:
				printk(KERN_INFO "%s: Received event we didn't register for\n"
					KERN_INFO "   Call I2O card manufacturer\n", 
					i2ob_dev[unit].i2odev->dev_name);
				break;
		}
	};

	return 0;
}

/*
 * The timer handler will attempt to restart requests 
 * that are queued to the driver.  This handler
 * currently only gets called if the controller
 * had no more room in its inbound fifo.  
 */

static void i2ob_timer_handler(unsigned long q)
{
	unsigned long flags;

	/*
	 * We cannot touch the request queue or the timer
         * flag without holding the io_request_lock.
	 */
	spin_lock_irqsave(&io_request_lock,flags);

	/* 
	 * Clear the timer started flag so that 
	 * the timer can be queued again.
	 */
	i2ob_timer_started = 0;

	/* 
	 * Restart any requests.
	 */
	i2ob_request((request_queue_t*)q);

	/* 
	 * Free the lock.
	 */
	spin_unlock_irqrestore(&io_request_lock,flags);
}

/*
 *	The I2O block driver is listed as one of those that pulls the
 *	front entry off the queue before processing it. This is important
 *	to remember here. If we drop the io lock then CURRENT will change
 *	on us. We must unlink CURRENT in this routine before we return, if
 *	we use it.
 */
static void i2ob_request(request_queue_t *q)
{
	struct request *req;
	struct i2ob_request *ireq;
	int unit;
	struct i2ob_device *dev;
	u32 m;

	// printk(KERN_INFO "i2ob_request() called with queue %p\n", q);

	while (!list_empty(&q->queue_head)) {
		/*
		 *	On an IRQ completion if there is an inactive
		 *	request on the queue head it means it isnt yet
		 *	ready to dispatch.
		 */
		req = blkdev_entry_next_request(&q->queue_head);

		if(req->rq_status == RQ_INACTIVE)
			return;
			
		unit = MINOR(req->rq_dev);
		dev = &i2ob_dev[(unit&0xF0)];

		/* 
		 *	Queue depths probably belong with some kind of 
		 *	generic IOP commit control. Certainly its not right 
		 *	its global!  
		 */
		if(atomic_read(&i2ob_queues[dev->unit]->queue_depth)>=MAX_I2OB_DEPTH)
			break;

		/* Get a message */
		m = i2ob_get(dev);

		if(m==0xFFFFFFFF)
		{
			/* 
			 * See if the timer has already been queued.
			 */
			if (!i2ob_timer_started)
			{
				printk(KERN_ERR "i2ob: starting timer\n");

				/*
				 * Set the timer_started flag to insure
				 * that the timer is only queued once.
				 * Queing it more than once will corrupt
				 * the timer queue.
				 */
				i2ob_timer_started = 1;

				/* 
				 * Set up the timer to expire in
				 * 500ms.
				 */
				i2ob_timer.expires = jiffies + (HZ >> 1);
				i2ob_timer.data = (unsigned int)q;

				/*
				 * Start it.
				 */
				 
				add_timer(&i2ob_timer);
			}
		}

		/*
		 * Everything ok, so pull from kernel queue onto our queue
		 */
		req->errors = 0;
		blkdev_dequeue_request(req);	
		req->sem = NULL;

		ireq = i2ob_queues[dev->unit]->i2ob_qhead;
		i2ob_queues[dev->unit]->i2ob_qhead = ireq->next;
		ireq->req = req;

		i2ob_send(m, dev, ireq, i2ob[unit].start_sect, (unit&0xF0));
	}
}


/*
 *	SCSI-CAM for ioctl geometry mapping
 *	Duplicated with SCSI - this should be moved into somewhere common
 *	perhaps genhd ?
 *
 * LBA -> CHS mapping table taken from:
 *
 * "Incorporating the I2O Architecture into BIOS for Intel Architecture 
 *  Platforms" 
 *
 * This is an I2O document that is only available to I2O members,
 * not developers.
 *
 * From my understanding, this is how all the I2O cards do this
 *
 * Disk Size      | Sectors | Heads | Cylinders
 * ---------------+---------+-------+-------------------
 * 1 < X <= 528M  | 63      | 16    | X/(63 * 16 * 512)
 * 528M < X <= 1G | 63      | 32    | X/(63 * 32 * 512)
 * 1 < X <528M    | 63      | 16    | X/(63 * 16 * 512)
 * 1 < X <528M    | 63      | 16    | X/(63 * 16 * 512)
 *
 */
#define	BLOCK_SIZE_528M		1081344
#define	BLOCK_SIZE_1G		2097152
#define	BLOCK_SIZE_21G		4403200
#define	BLOCK_SIZE_42G		8806400
#define	BLOCK_SIZE_84G		17612800

static void i2o_block_biosparam(
	unsigned long capacity,
	unsigned short *cyls,
	unsigned char *hds,
	unsigned char *secs) 
{ 
	unsigned long heads, sectors, cylinders; 

	sectors = 63L;      			/* Maximize sectors per track */ 
	if(capacity <= BLOCK_SIZE_528M)
		heads = 16;
	else if(capacity <= BLOCK_SIZE_1G)
		heads = 32;
	else if(capacity <= BLOCK_SIZE_21G)
		heads = 64;
	else if(capacity <= BLOCK_SIZE_42G)
		heads = 128;
	else
		heads = 255;

	cylinders = capacity / (heads * sectors);

	*cyls = (unsigned short) cylinders;	/* Stuff return values */ 
	*secs = (unsigned char) sectors; 
	*hds  = (unsigned char) heads; 
}


/*
 *	Rescan the partition tables
 */
 
static int do_i2ob_revalidate(kdev_t dev, int maxu)
{
	int minor=MINOR(dev);
	int i;
	
	minor&=0xF0;

	i2ob_dev[minor].refcnt++;
	if(i2ob_dev[minor].refcnt>maxu+1)
	{
		i2ob_dev[minor].refcnt--;
		return -EBUSY;
	}
	
	for( i = 15; i>=0 ; i--)
	{
		int m = minor+i;
		kdev_t d = MKDEV(MAJOR_NR, m);
		struct super_block *sb = get_super(d);
		
		sync_dev(d);
		if(sb)
			invalidate_inodes(sb);
		invalidate_buffers(d);
		i2ob_gendisk.part[m].start_sect = 0;
		i2ob_gendisk.part[m].nr_sects = 0;
	}

	/*
	 *	Do a physical check and then reconfigure
	 */
	 
	i2ob_install_device(i2ob_dev[minor].controller, i2ob_dev[minor].i2odev,
		minor);
	i2ob_dev[minor].refcnt--;
	return 0;
}

/*
 *	Issue device specific ioctl calls.
 */

static int i2ob_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	struct i2ob_device *dev;
	int minor;

	/* Anyone capable of this syscall can do *real bad* things */

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!inode)
		return -EINVAL;
	minor = MINOR(inode->i_rdev);
	if (minor >= (MAX_I2OB<<4))
		return -ENODEV;

	dev = &i2ob_dev[minor];
	switch (cmd) {
		case BLKGETSIZE:
			return put_user(i2ob[minor].nr_sects, (long *) arg);

		case HDIO_GETGEO:
		{
			struct hd_geometry g;
			int u=minor&0xF0;
			i2o_block_biosparam(i2ob_sizes[u]<<1, 
				&g.cylinders, &g.heads, &g.sectors);
			g.start = i2ob[minor].start_sect;
			return copy_to_user((void *)arg,&g, sizeof(g))?-EFAULT:0;
		}
	
		case BLKRRPART:
			if(!capable(CAP_SYS_ADMIN))
				return -EACCES;
			return do_i2ob_revalidate(inode->i_rdev,1);
			
		case BLKFLSBUF:
		case BLKROSET:
		case BLKROGET:
		case BLKRASET:
		case BLKRAGET:
		case BLKPG:
			return blk_ioctl(inode->i_rdev, cmd, arg);
			
		default:
			return -EINVAL;
	}
}

/*
 *	Close the block device down
 */
 
static int i2ob_release(struct inode *inode, struct file *file)
{
	struct i2ob_device *dev;
	int minor;

	minor = MINOR(inode->i_rdev);
	if (minor >= (MAX_I2OB<<4))
		return -ENODEV;
	dev = &i2ob_dev[(minor&0xF0)];

	/*
	 * This is to deail with the case of an application
	 * opening a device and then the device dissapears while
	 * it's in use, and then the application tries to release
	 * it.  ex: Unmounting a deleted RAID volume at reboot. 
	 * If we send messages, it will just cause FAILs since
	 * the TID no longer exists.
	 */
	if(!dev->i2odev)
		return 0;

	/* Sync the device so we don't get errors */
	fsync_dev(inode->i_rdev);

	if (dev->refcnt <= 0)
		printk(KERN_ALERT "i2ob_release: refcount(%d) <= 0\n", dev->refcnt);
	dev->refcnt--;
	if(dev->refcnt==0)
	{
		/*
		 *	Flush the onboard cache on unmount
		 */
		u32 msg[5];
		int *query_done = &dev->done_flag;
		msg[0] = FIVE_WORD_MSG_SIZE|SGL_OFFSET_0;
		msg[1] = I2O_CMD_BLOCK_CFLUSH<<24|HOST_TID<<12|dev->tid;
		msg[2] = i2ob_context|0x40000000;
		msg[3] = (u32)query_done;
		msg[4] = 60<<16;
		i2o_post_wait(dev->controller, msg, 20, 2);

		/*
		 *	Unlock the media
		 */
		msg[0] = FIVE_WORD_MSG_SIZE|SGL_OFFSET_0;
		msg[1] = I2O_CMD_BLOCK_MUNLOCK<<24|HOST_TID<<12|dev->tid;
		msg[2] = i2ob_context|0x40000000;
		msg[3] = (u32)query_done;
		msg[4] = -1;
		i2o_post_wait(dev->controller, msg, 20, 2);
	
		/*
 		 * Now unclaim the device.
		 */
		if (i2o_release_device(dev->i2odev, &i2o_block_handler))
			printk(KERN_ERR "i2ob_release: controller rejected unclaim.\n");

	}
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 *	Open the block device.
 */
 
static int i2ob_open(struct inode *inode, struct file *file)
{
	int minor;
	struct i2ob_device *dev;
	
	if (!inode)
		return -EINVAL;
	minor = MINOR(inode->i_rdev);
	if (minor >= MAX_I2OB<<4)
		return -ENODEV;
	dev=&i2ob_dev[(minor&0xF0)];

	if(!dev->i2odev)	
		return -ENODEV;
	
	if(dev->refcnt++==0)
	{ 
		u32 msg[6];
		
		if(i2o_claim_device(dev->i2odev, &i2o_block_handler))
		{
			dev->refcnt--;
			printk(KERN_INFO "I2O Block: Could not open device\n");
			return -EBUSY;
		}
		
		/*
		 *	Mount the media if needed. Note that we don't use
		 *	the lock bit. Since we have to issue a lock if it
		 *	refuses a mount (quite possible) then we might as
		 *	well just send two messages out.
		 */
		msg[0] = FIVE_WORD_MSG_SIZE|SGL_OFFSET_0;		
		msg[1] = I2O_CMD_BLOCK_MMOUNT<<24|HOST_TID<<12|dev->tid;
		msg[4] = -1;
		msg[5] = 0;
		i2o_post_wait(dev->controller, msg, 24, 2);

		/*
		 *	Lock the media
		 */
		msg[0] = FIVE_WORD_MSG_SIZE|SGL_OFFSET_0;
		msg[1] = I2O_CMD_BLOCK_MLOCK<<24|HOST_TID<<12|dev->tid;
		msg[4] = -1;
		i2o_post_wait(dev->controller, msg, 20, 2);
	}		
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 *	Issue a device query
 */
 
static int i2ob_query_device(struct i2ob_device *dev, int table, 
	int field, void *buf, int buflen)
{
	return i2o_query_scalar(dev->controller, dev->tid,
		table, field, buf, buflen);
}


/*
 *	Install the I2O block device we found.
 */
 
static int i2ob_install_device(struct i2o_controller *c, struct i2o_device *d, int unit)
{
	u64 size;
	u32 blocksize;
	u32 limit;
	u8 type;
	u32 flags, status;
	struct i2ob_device *dev=&i2ob_dev[unit];
	int i;

	/*
	 * For logging purposes...
	 */
	printk(KERN_INFO "i2ob: Installing tid %d device at unit %d\n", 
			d->lct_data.tid, unit);	

	/*
	 *	Ask for the current media data. If that isn't supported
	 *	then we ask for the device capacity data
	 */
	if(i2ob_query_device(dev, 0x0004, 1, &blocksize, 4) != 0
	  || i2ob_query_device(dev, 0x0004, 0, &size, 8) !=0 )
	{
		i2ob_query_device(dev, 0x0000, 3, &blocksize, 4);
		i2ob_query_device(dev, 0x0000, 4, &size, 8);
	}
	
	i2ob_query_device(dev, 0x0000, 5, &flags, 4);
	i2ob_query_device(dev, 0x0000, 6, &status, 4);
	i2ob_sizes[unit] = (int)(size>>10);
	i2ob_hardsizes[unit] = blocksize;
	i2ob_gendisk.part[unit].nr_sects = size>>9;
	i2ob[unit].nr_sects = (int)(size>>9);

	/* Set limit based on inbound frame size */
	limit = (d->controller->status_block->inbound_frame_size - 8)/2;
	limit = limit<<9;

	/*
	 * Max number of Scatter-Gather Elements
	 */	
	i2ob_dev[unit].max_segments = 
		(d->controller->status_block->inbound_frame_size - 8)/2;

	printk(KERN_INFO "Max Segments set to %d\n", 
				i2ob_dev[unit].max_segments);
	printk(KERN_INFO "Byte limit is %d.\n", limit);
	
	for(i=unit;i<=unit+15;i++)
	{
		i2ob_max_sectors[i]=MAX_SECTORS;
		i2ob_dev[i].max_segments = 
			(d->controller->status_block->inbound_frame_size - 8)/2;
	}

	i2ob_query_device(dev, 0x0000, 0, &type, 1);
	
	sprintf(d->dev_name, "%s%c", i2ob_gendisk.major_name, 'a' + (unit>>4));

	printk(KERN_INFO "%s: ", d->dev_name);
	switch(type)
	{
		case 0: printk("Disk Storage");break;
		case 4: printk("WORM");break;
		case 5: printk("CD-ROM");break;
		case 7:	printk("Optical device");break;
		default:
			printk("Type %d", type);
	}
	if(status&(1<<10))
		printk("(RAID)");
	if(((flags & (1<<3)) && !(status & (1<<3))) ||
	   ((flags & (1<<4)) && !(status & (1<<4))))
	{
		printk(KERN_INFO " Not loaded.\n");
		return 1;
	}
	printk("- %dMb, %d byte sectors",
		(int)(size>>20), blocksize);
	if(status&(1<<0))
	{
		u32 cachesize;
		i2ob_query_device(dev, 0x0003, 0, &cachesize, 4);
		cachesize>>=10;
		if(cachesize>4095)
			printk(", %dMb cache", cachesize>>10);
		else
			printk(", %dKb cache", cachesize);
	}
	printk(".\n");
	printk(KERN_INFO "%s: Maximum sectors/read set to %d.\n", 
		d->dev_name, i2ob_max_sectors[unit]);

	/* 
	 * If this is the first I2O block device found on this IOP,
	 * we need to initialize all the queue data structures
	 * before any I/O can be performed. If it fails, this
	 * device is useless.
	 */
	if(!i2ob_queues[c->unit]) {
		if(i2ob_init_iop(c->unit))
			return 1;
	}

	/* 
	 * This will save one level of lookup/indirection in critical 
	 * code so that we can directly get the queue ptr from the
	 * device instead of having to go the IOP data structure.
	 */
	dev->req_queue = &i2ob_queues[c->unit]->req_queue;

	grok_partitions(&i2ob_gendisk, unit>>4, 1<<4, (long)(size>>9));

	/*
	 * Register for the events we're interested in and that the
	 * device actually supports.
	 */
	i2o_event_register(c, d->lct_data.tid, i2ob_context, unit, 
		(I2OB_EVENT_MASK & d->lct_data.event_capabilities));

	return 0;
}

/*
 * Initialize IOP specific queue structures.  This is called
 * once for each IOP that has a block device sitting behind it.
 */
static int i2ob_init_iop(unsigned int unit)
{
	int i;

	i2ob_queues[unit] = (struct i2ob_iop_queue*)
		kmalloc(sizeof(struct i2ob_iop_queue), GFP_ATOMIC);
	if(!i2ob_queues[unit])
	{
		printk(KERN_WARNING
			"Could not allocate request queue for I2O block device!\n");
		return -1;
	}

	for(i = 0; i< MAX_I2OB_DEPTH; i++)
	{
		i2ob_queues[unit]->request_queue[i].next = 
			&i2ob_queues[unit]->request_queue[i+1];
		i2ob_queues[unit]->request_queue[i].num = i;
	}
	
	/* Queue is MAX_I2OB + 1... */
	i2ob_queues[unit]->request_queue[i].next = NULL;
	i2ob_queues[unit]->i2ob_qhead = &i2ob_queues[unit]->request_queue[0];
	atomic_set(&i2ob_queues[unit]->queue_depth, 0);

	blk_init_queue(&i2ob_queues[unit]->req_queue, i2ob_request);
	blk_queue_headactive(&i2ob_queues[unit]->req_queue, 0);
	i2ob_queues[unit]->req_queue.back_merge_fn = i2ob_back_merge;
	i2ob_queues[unit]->req_queue.front_merge_fn = i2ob_front_merge;
	i2ob_queues[unit]->req_queue.merge_requests_fn = i2ob_merge_requests;
	i2ob_queues[unit]->req_queue.queuedata = &i2ob_queues[unit];

	return 0;
}

/*
 * Get the request queue for the given device.
 */	
static request_queue_t* i2ob_get_queue(kdev_t dev)
{
	int unit = MINOR(dev)&0xF0;

	return i2ob_dev[unit].req_queue;
}

/*
 * Probe the I2O subsytem for block class devices
 */
static void i2ob_probe(void)
{
	int i;
	int unit = 0;
	int warned = 0;
		
	for(i=0; i< MAX_I2O_CONTROLLERS; i++)
	{
		struct i2o_controller *c=i2o_find_controller(i);
		struct i2o_device *d;
	
		if(c==NULL)
			continue;

		for(d=c->devices;d!=NULL;d=d->next)
		{
			if(d->lct_data.class_id!=I2O_CLASS_RANDOM_BLOCK_STORAGE)
				continue;

			if(d->lct_data.user_tid != 0xFFF)
				continue;

			if(i2o_claim_device(d, &i2o_block_handler))
			{
				printk(KERN_WARNING "i2o_block: Controller %d, TID %d\n", c->unit,
					d->lct_data.tid);
				printk(KERN_WARNING "\tDevice refused claim! Skipping installation\n");
				continue;
			}

			if(unit<MAX_I2OB<<4)
			{
 				/*
				 * Get the device and fill in the
				 * Tid and controller.
				 */
				struct i2ob_device *dev=&i2ob_dev[unit];
				dev->i2odev = d; 
				dev->controller = c;
				dev->unit = c->unit;
				dev->tid = d->lct_data.tid;

				if(i2ob_install_device(c,d,unit))
					printk(KERN_WARNING "Could not install I2O block device\n");
				else
				{
					unit+=16;
					i2ob_dev_count++;

					/* We want to know when device goes away */
					i2o_device_notify_on(d, &i2o_block_handler);
				}
			}
			else
			{
				if(!warned++)
					printk(KERN_WARNING "i2o_block: too many device, registering only %d.\n", unit>>4);
			}
			i2o_release_device(d, &i2o_block_handler);
		}
		i2o_unlock_controller(c);
	}
}

/*
 * New device notification handler.  Called whenever a new
 * I2O block storage device is added to the system.
 * 
 * Should we spin lock around this to keep multiple devs from 
 * getting updated at the same time? 
 * 
 */
void i2ob_new_device(struct i2o_controller *c, struct i2o_device *d)
{
	struct i2ob_device *dev;
	int unit = 0;

	printk(KERN_INFO "i2o_block: New device detected\n");
	printk(KERN_INFO "   Controller %d Tid %d\n",c->unit, d->lct_data.tid);

	/* Check for available space */
	if(i2ob_dev_count>=MAX_I2OB<<4)
	{
		printk(KERN_ERR "i2o_block: No more devices allowed!\n");
		return;
	}
	for(unit = 0; unit < (MAX_I2OB<<4); unit += 16)
	{
		if(!i2ob_dev[unit].i2odev)
			break;
	}

	/*
	 * Creating a RAID 5 volume takes a little while and the UTIL_CLAIM
	 * will fail if we don't give the card enough time to do it's magic, 
	 * so we just sleep for a little while and let it do it's thing
	 */
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(3*HZ);

	if(i2o_claim_device(d, &i2o_block_handler))
	{
		printk(KERN_INFO 
			"i2o_block: Unable to claim device. Installation aborted\n");
		return;
	}

	dev = &i2ob_dev[unit];
	dev->i2odev = d; 
	dev->controller = c;
	dev->tid = d->lct_data.tid;

	if(i2ob_install_device(c,d,unit))
		printk(KERN_ERR "i2o_block: Could not install new device\n");
	else	
	{
		i2ob_dev_count++;
		i2o_device_notify_on(d, &i2o_block_handler);
	}

	i2o_release_device(d, &i2o_block_handler);
 
	return;
}

/*
 * Deleted device notification handler.  Called when a device we
 * are talking to has been deleted by the user or some other
 * mysterious fource outside the kernel.
 */
void i2ob_del_device(struct i2o_controller *c, struct i2o_device *d)
{	
	int unit = 0;
	int i = 0;
	int flags;

	spin_lock_irqsave(&io_request_lock, flags);

	/*
	 * Need to do this...we somtimes get two events from the IRTOS
	 * in a row and that causes lots of problems.
	 */
	i2o_device_notify_off(d, &i2o_block_handler);

	printk(KERN_INFO "I2O Block Device Deleted\n");

	for(unit = 0; unit < MAX_I2OB<<4; unit += 16)
	{
		if(i2ob_dev[unit].i2odev == d)
		{
			printk(KERN_INFO "  /dev/%s: Controller %d Tid %d\n", 
				d->dev_name, c->unit, d->lct_data.tid);
			break;
		}
	}
	if(unit >= MAX_I2OB<<4)
	{
		printk(KERN_ERR "i2ob_del_device called, but not in dev table!\n");
		return;
	}

	/* 
	 * This will force errors when i2ob_get_queue() is called
	 * by the kenrel.
	 */
	i2ob_dev[unit].req_queue = NULL;
	for(i = unit; i <= unit+15; i++)
	{
		i2ob_dev[i].i2odev = NULL;
		i2ob_sizes[i] = 0;
		i2ob_hardsizes[i] = 0;
		i2ob_max_sectors[i] = 0;
		i2ob[i].nr_sects = 0;
		i2ob_gendisk.part[i].nr_sects = 0;
	}
	spin_unlock_irqrestore(&io_request_lock, flags);

	/*
	 * Sync the device...this will force all outstanding I/Os
	 * to attempt to complete, thus causing error messages.
	 * We have to do this as the user could immediatelly create
	 * a new volume that gets assigned the same minor number.
	 * If there are still outstanding writes to the device,
	 * that could cause data corruption on the new volume!
	 *
	 * The truth is that deleting a volume that you are currently
	 * accessing will do _bad things_ to your system.  This 
	 * handler will keep it from crashing, but must probably 
	 * you'll have to do a 'reboot' to get the system running
	 * properly.  Deleting disks you are using is dumb.  
	 * Umount them first and all will be good!
	 *
	 * It's not this driver's job to protect the system from
	 * dumb user mistakes :)
	 */
	if(i2ob_dev[unit].refcnt)
		fsync_dev(MKDEV(MAJOR_NR,unit));

	/*
	 * Decrease usage count for module
	 */	
	while(i2ob_dev[unit].refcnt--)
		MOD_DEC_USE_COUNT;

	i2ob_dev[unit].refcnt = 0;
	
	i2ob_dev[i].tid = 0;

	/* 
	 * Do we need this?
	 * The media didn't really change...the device is just gone
	 */
	i2ob_media_change_flag[unit] = 1;

	i2ob_dev_count--;	

	return;
}

/*
 *	Have we seen a media change ?
 */
static int i2ob_media_change(kdev_t dev)
{
	int i=MINOR(dev);
	i>>=4;
	if(i2ob_media_change_flag[i])
	{
		i2ob_media_change_flag[i]=0;
		return 1;
	}
	return 0;
}

static int i2ob_revalidate(kdev_t dev)
{
	return do_i2ob_revalidate(dev, 0);
}

/*
 * Reboot notifier.  This is called by i2o_core when the system
 * shuts down.
 */
static void i2ob_reboot_event(void)
{
	int i;
	
	for(i=0;i<MAX_I2OB;i++)
	{
		struct i2ob_device *dev=&i2ob_dev[(i<<4)];
		
		if(dev->refcnt!=0)
		{
			/*
			 *	Flush the onboard cache
			 */
			u32 msg[5];
			int *query_done = &dev->done_flag;
			msg[0] = FIVE_WORD_MSG_SIZE|SGL_OFFSET_0;
			msg[1] = I2O_CMD_BLOCK_CFLUSH<<24|HOST_TID<<12|dev->tid;
			msg[2] = i2ob_context|0x40000000;
			msg[3] = (u32)query_done;
			msg[4] = 60<<16;
			i2o_post_wait(dev->controller, msg, 20, 2);

			/*
			 *	Unlock the media
			 */
			msg[0] = FIVE_WORD_MSG_SIZE|SGL_OFFSET_0;
			msg[1] = I2O_CMD_BLOCK_MUNLOCK<<24|HOST_TID<<12|dev->tid;
			msg[2] = i2ob_context|0x40000000;
			msg[3] = (u32)query_done;
			msg[4] = -1;
			i2o_post_wait(dev->controller, msg, 20, 2);
		}
	}	
}

static struct block_device_operations i2ob_fops =
{
   open:       i2ob_open,
   release:    i2ob_release,
   ioctl:         i2ob_ioctl,
   check_media_change:  i2ob_media_change,
   revalidate:    i2ob_revalidate,
};


static struct gendisk i2ob_gendisk = 
{
	MAJOR_NR,
	"i2o/hd",
	4,
	1<<4,
	i2ob,
	i2ob_sizes,
	0,
	NULL,
	NULL
};


/*
 * And here should be modules and kernel interface 
 *  (Just smiley confuses emacs :-)
 */

#ifdef MODULE
#define i2o_block_init init_module
#endif

int i2o_block_init(void)
{
	int i;

	printk(KERN_INFO "I2O Block Storage OSM v0.9\n");
	printk(KERN_INFO "   (c) Copyright 1999, 2000 Red Hat Software.\n");
	
	/*
	 *	Register the block device interfaces
	 */

	if (register_blkdev(MAJOR_NR, "i2o_block", &i2ob_fops)) {
		printk(KERN_ERR "Unable to get major number %d for i2o_block\n",
		       MAJOR_NR);
		return -EIO;
	}
#ifdef MODULE
	printk(KERN_INFO "i2o_block: registered device at major %d\n", MAJOR_NR);
#endif

	/*
	 *	Now fill in the boiler plate
	 */
	 
	blksize_size[MAJOR_NR] = i2ob_blksizes;
	hardsect_size[MAJOR_NR] = i2ob_hardsizes;
	blk_size[MAJOR_NR] = i2ob_sizes;
	max_sectors[MAJOR_NR] = i2ob_max_sectors;
	blk_dev[MAJOR_NR].queue = i2ob_get_queue;
	
	blk_init_queue(BLK_DEFAULT_QUEUE(MAJOR_NR), i2ob_request);
	blk_queue_headactive(BLK_DEFAULT_QUEUE(MAJOR_NR), 0);

	for (i = 0; i < MAX_I2OB << 4; i++) {
		i2ob_dev[i].refcnt = 0;
		i2ob_dev[i].flags = 0;
		i2ob_dev[i].controller = NULL;
		i2ob_dev[i].i2odev = NULL;
		i2ob_dev[i].tid = 0;
		i2ob_dev[i].head = NULL;
		i2ob_dev[i].tail = NULL;
		i2ob_blksizes[i] = 1024;
		i2ob_max_sectors[i] = 2;
	}
	
	/*
	 *	Set up the queue
	 */
	for(i = 0; i < MAX_I2O_CONTROLLERS; i++)
	{
		i2ob_queues[i] = NULL;
	}

	/*
	 *	Timers
	 */
	 
	init_timer(&i2ob_timer);
	i2ob_timer.function = i2ob_timer_handler;
	i2ob_timer.data = 0;
	
	/*
	 *	Register the OSM handler as we will need this to probe for
	 *	drives, geometry and other goodies.
	 */

	if(i2o_install_handler(&i2o_block_handler)<0)
	{
		unregister_blkdev(MAJOR_NR, "i2o_block");
		blk_cleanup_queue(BLK_DEFAULT_QUEUE(MAJOR_NR));
		printk(KERN_ERR "i2o_block: unable to register OSM.\n");
		return -EINVAL;
	}
	i2ob_context = i2o_block_handler.context;	 

	/*
	 * Initialize event handling thread
	 */
	init_MUTEX_LOCKED(&i2ob_evt_sem);
	evt_pid = kernel_thread(i2ob_evt, NULL, CLONE_SIGHAND);
	if(evt_pid < 0)
	{
		printk(KERN_ERR 
			"i2o_block: Could not initialize event thread.  Aborting\n");
		i2o_remove_handler(&i2o_block_handler);
		return 0;
	}

	/*
	 *	Finally see what is actually plugged in to our controllers
	 */
	for (i = 0; i < MAX_I2OB; i++)
		register_disk(&i2ob_gendisk, MKDEV(MAJOR_NR,i<<4), 1<<4,
			&i2ob_fops, 0);
	i2ob_probe();
	
	return 0;
}

#ifdef MODULE

EXPORT_NO_SYMBOLS;
MODULE_AUTHOR("Red Hat Software");
MODULE_DESCRIPTION("I2O Block Device OSM");

void cleanup_module(void)
{
	struct gendisk **gdp;
	int i;
	
	/*
	 * Unregister for updates from any devices..otherwise we still
	 * get them and the core jumps to random memory :O
	 */
	if(i2ob_dev_count) {
		struct i2o_device *d;
		for(i = 0; i < MAX_I2OB; i++)
		if((d=i2ob_dev[i<<4].i2odev)) {
			i2o_device_notify_off(d, &i2o_block_handler);
			i2o_event_register(d->controller, d->lct_data.tid, 
				i2ob_context, i<<4, 0);
		}
	}
	
	/*
	 *	Flush the OSM
	 */

	i2o_remove_handler(&i2o_block_handler);
		 
	/*
	 *	Return the block device
	 */
	if (unregister_blkdev(MAJOR_NR, "i2o_block") != 0)
		printk("i2o_block: cleanup_module failed\n");

	/*
	 * free request queue
	 */
	blk_cleanup_queue(BLK_DEFAULT_QUEUE(MAJOR_NR));

	if(evt_running) {
		i = kill_proc(evt_pid, SIGTERM, 1);
		if(!i) {
			int count = 5 * 100;
			while(evt_running && --count) {
				current->state = TASK_INTERRUPTIBLE;
				schedule_timeout(1);
			}
	
			if(!count)
				printk(KERN_ERR "Giving up on i2oblock thread...\n");	
		}
	}


	/*
	 *	Why isnt register/unregister gendisk in the kernel ???
	 */

	for (gdp = &gendisk_head; *gdp; gdp = &((*gdp)->next))
		if (*gdp == &i2ob_gendisk)
			break;

}
#endif
