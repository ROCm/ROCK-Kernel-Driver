/*
 *  scsi.c Copyright (C) 1992 Drew Eckhardt
 *         Copyright (C) 1993, 1994, 1995, 1999 Eric Youngdale
 *
 *  generic mid-level SCSI driver
 *      Initial versions: Drew Eckhardt
 *      Subsequent revisions: Eric Youngdale
 *
 *  <drew@colorado.edu>
 *
 *  Bug correction thanks go to :
 *      Rik Faith <faith@cs.unc.edu>
 *      Tommy Thorn <tthorn>
 *      Thomas Wuensche <tw@fgb1.fgb.mw.tu-muenchen.de>
 *
 *  Modified by Eric Youngdale eric@andante.org or ericy@gnu.ai.mit.edu to
 *  add scatter-gather, multiple outstanding request, and other
 *  enhancements.
 *
 *  Native multichannel, wide scsi, /proc/scsi and hot plugging
 *  support added by Michael Neuffer <mike@i-connect.net>
 *
 *  Added request_module("scsi_hostadapter") for kerneld:
 *  (Put an "alias scsi_hostadapter your_hostadapter" in /etc/modules.conf)
 *  Bjorn Ekwall  <bj0rn@blox.se>
 *  (changed to kmod)
 *
 *  Major improvements to the timeout, abort, and reset processing,
 *  as well as performance modifications for large queue depths by
 *  Leonard N. Zubkoff <lnz@dandelion.com>
 *
 *  Converted cli() code to spinlocks, Ingo Molnar
 *
 *  Jiffies wrap fixes (host->resetting), 3 Dec 1998 Andrea Arcangeli
 *
 *  out_of_space hacks, D. Gilbert (dpg) 990608
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/blk.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/completion.h>
#include <linux/devfs_fs_kernel.h>

#define __KERNEL_SYSCALLS__

#include <linux/unistd.h>
#include <linux/spinlock.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/uaccess.h>

#include "scsi.h"
#include "hosts.h"

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif


/*
 * Definitions and constants.
 */

#define MIN_RESET_DELAY (2*HZ)

/* Do not call reset on error if we just did a reset within 15 sec. */
#define MIN_RESET_PERIOD (15*HZ)

/*
 * Macro to determine the size of SCSI command. This macro takes vendor
 * unique commands into account. SCSI commands in groups 6 and 7 are
 * vendor unique and we will depend upon the command length being
 * supplied correctly in cmd_len.
 */
#define CDB_SIZE(SCpnt)	((((SCpnt->cmnd[0] >> 5) & 7) < 6) ? \
				COMMAND_SIZE(SCpnt->cmnd[0]) : SCpnt->cmd_len)

/*
 * Data declarations.
 */
unsigned long scsi_pid;
Scsi_Cmnd *last_cmnd;
static unsigned long serial_number;

static struct list_head done_q[NR_CPUS] __cacheline_aligned;

/*
 * List of all highlevel drivers.
 */
LIST_HEAD(scsi_devicelist);
static DECLARE_RWSEM(scsi_devicelist_mutex);

/*
 * Note - the initial logging level can be set here to log events at boot time.
 * After the system is up, you may enable logging via the /proc interface.
 */
unsigned int scsi_logging_level;

const char *const scsi_device_types[MAX_SCSI_DEVICE_CODE] =
{
	"Direct-Access    ",
	"Sequential-Access",
	"Printer          ",
	"Processor        ",
	"WORM             ",
	"CD-ROM           ",
	"Scanner          ",
	"Optical Device   ",
	"Medium Changer   ",
	"Communications   ",
	"Unknown          ",
	"Unknown          ",
	"Unknown          ",
	"Enclosure        ",
};

static const char * const spaces = "                "; /* 16 of them */

static unsigned scsi_default_dev_flags;
LIST_HEAD(scsi_dev_info_list);

/* 
 * Function prototypes.
 */
extern void scsi_times_out(Scsi_Cmnd * SCpnt);

#ifdef MODULE
MODULE_PARM(scsi_logging_level, "i");
MODULE_PARM_DESC(scsi_logging_level, "SCSI logging level; should be zero or nonzero");

#else

static int __init scsi_logging_setup(char *str)
{
	int tmp;

	if (get_option(&str, &tmp) == 1) {
		scsi_logging_level = (tmp ? ~0 : 0);
		return 1;
	} else {
		printk(KERN_INFO "scsi_logging_setup : usage scsi_logging_level=n "
		       "(n should be 0 or non-zero)\n");
		return 0;
	}
}

__setup("scsi_logging=", scsi_logging_setup);

#endif

/*
 * Function:    scsi_allocate_request
 *
 * Purpose:     Allocate a request descriptor.
 *
 * Arguments:   device    - device for which we want a request
 *
 * Lock status: No locks assumed to be held.  This function is SMP-safe.
 *
 * Returns:     Pointer to request block.
 *
 * Notes:       With the new queueing code, it becomes important
 *              to track the difference between a command and a
 *              request.  A request is a pending item in the queue that
 *              has not yet reached the top of the queue.
 */

Scsi_Request *scsi_allocate_request(Scsi_Device * device)
{
  	Scsi_Request *SRpnt = NULL;
        const int offset = ALIGN(sizeof(Scsi_Request), 4);
        const int size = offset + sizeof(struct request);
  
  	if (!device)
  		panic("No device passed to scsi_allocate_request().\n");
  
        SRpnt = (Scsi_Request *) kmalloc(size, GFP_ATOMIC);
	if( SRpnt == NULL )
	{
		return NULL;
	}
	memset(SRpnt, 0, size);
        SRpnt->sr_request = (struct request *)(((char *)SRpnt) + offset);
	SRpnt->sr_device = device;
	SRpnt->sr_host = device->host;
	SRpnt->sr_magic = SCSI_REQ_MAGIC;
	SRpnt->sr_data_direction = SCSI_DATA_UNKNOWN;

	return SRpnt;
}

/*
 * Function:    scsi_release_request
 *
 * Purpose:     Release a request descriptor.
 *
 * Arguments:   device    - device for which we want a request
 *
 * Lock status: No locks assumed to be held.  This function is SMP-safe.
 *
 * Returns:     Pointer to request block.
 *
 * Notes:       With the new queueing code, it becomes important
 *              to track the difference between a command and a
 *              request.  A request is a pending item in the queue that
 *              has not yet reached the top of the queue.  We still need
 *              to free a request when we are done with it, of course.
 */
void scsi_release_request(Scsi_Request * req)
{
	if( req->sr_command != NULL )
	{
    		request_queue_t *q = req->sr_device->request_queue;
		scsi_put_command(req->sr_command);
		req->sr_command = NULL;
		scsi_queue_next_request(q, NULL);
	}

	kfree(req);
}

struct scsi_host_cmd_pool {
	kmem_cache_t	*slab;
	unsigned int	users;
	char		*name;
	unsigned int	slab_flags;
	unsigned int	gfp_mask;
};

static struct scsi_host_cmd_pool scsi_cmd_pool = {
	.name		= "scsi_cmd_cache",
	.slab_flags	= SLAB_HWCACHE_ALIGN,
};

static struct scsi_host_cmd_pool scsi_cmd_dma_pool = {
	.name		= "scsi_cmd_cache(DMA)",
	.slab_flags	= SLAB_HWCACHE_ALIGN|SLAB_CACHE_DMA,
	.gfp_mask	= __GFP_DMA,
};

static DECLARE_MUTEX(host_cmd_pool_mutex);

static struct scsi_cmnd *__scsi_get_command(struct Scsi_Host *shost,
					    int gfp_mask)
{
	struct scsi_cmnd *cmd;

	cmd = kmem_cache_alloc(shost->cmd_pool->slab,
			gfp_mask | shost->cmd_pool->gfp_mask);

	if (unlikely(!cmd)) {
		unsigned long flags;

		spin_lock_irqsave(&shost->free_list_lock, flags);
		if (likely(!list_empty(&shost->free_list))) {
			cmd = list_entry(shost->free_list.next,
					 struct scsi_cmnd, list);
			list_del_init(&cmd->list);
		}
		spin_unlock_irqrestore(&shost->free_list_lock, flags);
	}

	return cmd;
}

/*
 * Function:	scsi_get_command()
 *
 * Purpose:	Allocate and setup a scsi command block
 *
 * Arguments:	dev	- parent scsi device
 *		gfp_mask- allocator flags
 *
 * Returns:	The allocated scsi command structure.
 */
struct scsi_cmnd *scsi_get_command(struct scsi_device *dev, int gfp_mask)
{
	struct scsi_cmnd *cmd = __scsi_get_command(dev->host, gfp_mask);

	if (likely(cmd != NULL)) {
		unsigned long flags;

		memset(cmd, 0, sizeof(*cmd));
		cmd->device = dev;
		cmd->state = SCSI_STATE_UNUSED;
		cmd->owner = SCSI_OWNER_NOBODY;
		init_timer(&cmd->eh_timeout);
		INIT_LIST_HEAD(&cmd->list);
		spin_lock_irqsave(&dev->list_lock, flags);
		list_add_tail(&cmd->list, &dev->cmd_list);
		spin_unlock_irqrestore(&dev->list_lock, flags);
	}

	return cmd;
}				

/*
 * Function:	scsi_put_command()
 *
 * Purpose:	Free a scsi command block
 *
 * Arguments:	cmd	- command block to free
 *
 * Returns:	Nothing.
 *
 * Notes:	The command must not belong to any lists.
 */
void scsi_put_command(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *shost = cmd->device->host;
	unsigned long flags;
	
	/* serious error if the command hasn't come from a device list */
	spin_lock_irqsave(&cmd->device->list_lock, flags);
	BUG_ON(list_empty(&cmd->list));
	list_del_init(&cmd->list);
	spin_unlock(&cmd->device->list_lock);
	/* changing locks here, don't need to restore the irq state */
	spin_lock(&shost->free_list_lock);
	if (unlikely(list_empty(&shost->free_list))) {
		list_add(&cmd->list, &shost->free_list);
		cmd = NULL;
	}
	spin_unlock_irqrestore(&shost->free_list_lock, flags);

	if (likely(cmd != NULL))
		kmem_cache_free(shost->cmd_pool->slab, cmd);
}

/*
 * Function:	scsi_setup_command_freelist()
 *
 * Purpose:	Setup the command freelist for a scsi host.
 *
 * Arguments:	shost	- host to allocate the freelist for.
 *
 * Returns:	Nothing.
 */
int scsi_setup_command_freelist(struct Scsi_Host *shost)
{
	struct scsi_host_cmd_pool *pool;
	struct scsi_cmnd *cmd;

	spin_lock_init(&shost->free_list_lock);
	INIT_LIST_HEAD(&shost->free_list);

	/*
	 * Select a command slab for this host and create it if not
	 * yet existant.
	 */
	down(&host_cmd_pool_mutex);
	pool = (shost->unchecked_isa_dma ? &scsi_cmd_dma_pool : &scsi_cmd_pool);
	if (!pool->users) {
		pool->slab = kmem_cache_create(pool->name,
				sizeof(struct scsi_cmnd), 0,
				pool->slab_flags, NULL, NULL);
		if (!pool->slab)
			goto fail;
	}

	pool->users++;
	shost->cmd_pool = pool;
	up(&host_cmd_pool_mutex);

	/*
	 * Get one backup command for this host.
	 */
	cmd = kmem_cache_alloc(shost->cmd_pool->slab,
			GFP_KERNEL | shost->cmd_pool->gfp_mask);
	if (!cmd)
		goto fail2;
	list_add(&cmd->list, &shost->free_list);		
	return 0;

 fail2:
	if (!--pool->users)
		kmem_cache_destroy(pool->slab);
	return -ENOMEM;
 fail:
	up(&host_cmd_pool_mutex);
	return -ENOMEM;

}

/*
 * Function:	scsi_destroy_command_freelist()
 *
 * Purpose:	Release the command freelist for a scsi host.
 *
 * Arguments:	shost	- host that's freelist is going to be destroyed
 */
void scsi_destroy_command_freelist(struct Scsi_Host *shost)
{
	while (!list_empty(&shost->free_list)) {
		struct scsi_cmnd *cmd;

		cmd = list_entry(shost->free_list.next, struct scsi_cmnd, list);
		list_del_init(&cmd->list);
		kmem_cache_free(shost->cmd_pool->slab, cmd);
	}

	down(&host_cmd_pool_mutex);
	if (!--shost->cmd_pool->users)
		kmem_cache_destroy(shost->cmd_pool->slab);
	up(&host_cmd_pool_mutex);
}

/*
 * Function:    scsi_dispatch_command
 *
 * Purpose:     Dispatch a command to the low-level driver.
 *
 * Arguments:   SCpnt - command block we are dispatching.
 *
 * Notes:
 */
int scsi_dispatch_cmd(Scsi_Cmnd * SCpnt)
{
#ifdef DEBUG_DELAY
	unsigned long clock;
#endif
	struct Scsi_Host *host;
	int rtn = 0;
	unsigned long flags = 0;
	unsigned long timeout;

#if DEBUG
	unsigned long *ret = 0;
#ifdef __mips__
	__asm__ __volatile__("move\t%0,$31":"=r"(ret));
#else
	ret = __builtin_return_address(0);
#endif
#endif

	host = SCpnt->device->host;

	/* Assign a unique nonzero serial_number. */
	if (++serial_number == 0)
		serial_number = 1;
	SCpnt->serial_number = serial_number;
	SCpnt->pid = scsi_pid++;
	/* 
	 * If SCSI-2 or lower, store the LUN value in cmnd.
	 */
	if (SCpnt->device->scsi_level <= SCSI_2)
		SCpnt->cmnd[1] = (SCpnt->cmnd[1] & 0x1f) |
			(SCpnt->device->lun << 5 & 0xe0);

	/*
	 * We will wait MIN_RESET_DELAY clock ticks after the last reset so
	 * we can avoid the drive not being ready.
	 */
	timeout = host->last_reset + MIN_RESET_DELAY;

	if (host->resetting && time_before(jiffies, timeout)) {
		int ticks_remaining = timeout - jiffies;
		/*
		 * NOTE: This may be executed from within an interrupt
		 * handler!  This is bad, but for now, it'll do.  The irq
		 * level of the interrupt handler has been masked out by the
		 * platform dependent interrupt handling code already, so the
		 * sti() here will not cause another call to the SCSI host's
		 * interrupt handler (assuming there is one irq-level per
		 * host).
		 */
		while (--ticks_remaining >= 0)
			mdelay(1 + 999 / HZ);
		host->resetting = 0;
	}

	scsi_add_timer(SCpnt, SCpnt->timeout_per_command, scsi_times_out);

	/*
	 * We will use a queued command if possible, otherwise we will emulate the
	 * queuing and calling of completion function ourselves.
	 */
	SCSI_LOG_MLQUEUE(3, printk("scsi_dispatch_cmnd (host = %d, channel = %d, target = %d, "
	       "command = %p, buffer = %p, \nbufflen = %d, done = %p)\n",
	SCpnt->device->host->host_no, SCpnt->device->channel, SCpnt->device->id, SCpnt->cmnd,
			    SCpnt->buffer, SCpnt->bufflen, SCpnt->done));

	SCpnt->state = SCSI_STATE_QUEUED;
	SCpnt->owner = SCSI_OWNER_LOWLEVEL;
	if (host->can_queue) {
		SCSI_LOG_MLQUEUE(3, printk("queuecommand : routine at %p\n",
					   host->hostt->queuecommand));
		/*
		 * Before we queue this command, check if the command
		 * length exceeds what the host adapter can handle.
		 */
		if (CDB_SIZE(SCpnt) <= SCpnt->device->host->max_cmd_len) {
			spin_lock_irqsave(host->host_lock, flags);
			rtn = host->hostt->queuecommand(SCpnt, scsi_done);
			spin_unlock_irqrestore(host->host_lock, flags);
			if (rtn != 0) {
				scsi_queue_insert(SCpnt, rtn == SCSI_MLQUEUE_DEVICE_BUSY ? rtn : SCSI_MLQUEUE_HOST_BUSY);
				SCSI_LOG_MLQUEUE(3,
				   printk("queuecommand : request rejected\n"));                                
			}
		} else {
			SCSI_LOG_MLQUEUE(3,
				printk("queuecommand : command too long.\n"));
			SCpnt->result = (DID_ABORT << 16);
			spin_lock_irqsave(host->host_lock, flags);
			scsi_done(SCpnt);
			spin_unlock_irqrestore(host->host_lock, flags);
			rtn = 1;
		}
	} else {
		int temp;

		SCSI_LOG_MLQUEUE(3, printk("command() :  routine at %p\n", host->hostt->command));
                spin_lock_irqsave(host->host_lock, flags);
		temp = host->hostt->command(SCpnt);
		SCpnt->result = temp;
#ifdef DEBUG_DELAY
                spin_unlock_irqrestore(host->host_lock, flags);
		clock = jiffies + 4 * HZ;
		while (time_before(jiffies, clock)) {
			barrier();
			cpu_relax();
		}
		printk("done(host = %d, result = %04x) : routine at %p\n",
		       host->host_no, temp, host->hostt->command);
                spin_lock_irqsave(host->host_lock, flags);
#endif
		scsi_done(SCpnt);
                spin_unlock_irqrestore(host->host_lock, flags);
	}
	SCSI_LOG_MLQUEUE(3, printk("leaving scsi_dispatch_cmnd()\n"));
	return rtn;
}

/*
 * Function:    scsi_init_cmd_from_req
 *
 * Purpose:     Queue a SCSI command
 * Purpose:     Initialize a Scsi_Cmnd from a Scsi_Request
 *
 * Arguments:   SCpnt     - command descriptor.
 *              SRpnt     - Request from the queue.
 *
 * Lock status: None needed.
 *
 * Returns:     Nothing.
 *
 * Notes:       Mainly transfer data from the request structure to the
 *              command structure.  The request structure is allocated
 *              using the normal memory allocator, and requests can pile
 *              up to more or less any depth.  The command structure represents
 *              a consumable resource, as these are allocated into a pool
 *              when the SCSI subsystem initializes.  The preallocation is
 *              required so that in low-memory situations a disk I/O request
 *              won't cause the memory manager to try and write out a page.
 *              The request structure is generally used by ioctls and character
 *              devices.
 */
void scsi_init_cmd_from_req(Scsi_Cmnd * SCpnt, Scsi_Request * SRpnt)
{
	struct Scsi_Host *host = SCpnt->device->host;

	SCpnt->owner = SCSI_OWNER_MIDLEVEL;
	SRpnt->sr_command = SCpnt;

	if (!host) {
		panic("Invalid or not present host.\n");
	}

	SCpnt->cmd_len = SRpnt->sr_cmd_len;
	SCpnt->use_sg = SRpnt->sr_use_sg;

        SCpnt->request = SRpnt->sr_request;
	memcpy((void *) SCpnt->data_cmnd, (const void *) SRpnt->sr_cmnd, 
	       sizeof(SCpnt->data_cmnd));
	SCpnt->reset_chain = NULL;
	SCpnt->serial_number = 0;
	SCpnt->serial_number_at_timeout = 0;
	SCpnt->bufflen = SRpnt->sr_bufflen;
	SCpnt->buffer = SRpnt->sr_buffer;
	SCpnt->flags = 0;
	SCpnt->retries = 0;
	SCpnt->allowed = SRpnt->sr_allowed;
	SCpnt->done = SRpnt->sr_done;
	SCpnt->timeout_per_command = SRpnt->sr_timeout_per_command;

	SCpnt->sc_data_direction = SRpnt->sr_data_direction;

	SCpnt->sglist_len = SRpnt->sr_sglist_len;
	SCpnt->underflow = SRpnt->sr_underflow;

	SCpnt->sc_request = SRpnt;

	memcpy((void *) SCpnt->cmnd, (const void *) SRpnt->sr_cmnd, 
	       sizeof(SCpnt->cmnd));
	/* Zero the sense buffer.  Some host adapters automatically request
	 * sense on error.  0 is not a valid sense code.
	 */
	memset((void *) SCpnt->sense_buffer, 0, sizeof SCpnt->sense_buffer);
	SCpnt->request_buffer = SRpnt->sr_buffer;
	SCpnt->request_bufflen = SRpnt->sr_bufflen;
	SCpnt->old_use_sg = SCpnt->use_sg;
	if (SCpnt->cmd_len == 0)
		SCpnt->cmd_len = COMMAND_SIZE(SCpnt->cmnd[0]);
	SCpnt->old_cmd_len = SCpnt->cmd_len;
	SCpnt->sc_old_data_direction = SCpnt->sc_data_direction;
	SCpnt->old_underflow = SCpnt->underflow;

	/* Start the timer ticking.  */

	SCpnt->internal_timeout = NORMAL_TIMEOUT;
	SCpnt->abort_reason = 0;
	SCpnt->result = 0;

	SCSI_LOG_MLQUEUE(3, printk("Leaving scsi_init_cmd_from_req()\n"));
}

/**
 * scsi_done - Enqueue the finished SCSI command into the done queue.
 * @cmd: The SCSI Command for which a low-level device driver (LLDD) gives
 * ownership back to SCSI Core -- i.e. the LLDD has finished with it.
 *
 * This function is the mid-level's (SCSI Core) interrupt routine, which
 * regains ownership of the SCSI command (de facto) from a LLDD, and enqueues
 * the command to the done queue for further processing.
 *
 * This is the producer of the done queue who enqueues at the tail.
 *
 * This function is interrupt context safe.
 */
void scsi_done(struct scsi_cmnd *cmd)
{
	int cpu;
	unsigned long flags;
	struct list_head *pdone_q;

	/*
	 * We don't have to worry about this one timing out any more.
	 * If we are unable to remove the timer, then the command
	 * has already timed out.  In which case, we have no choice but to
	 * let the timeout function run, as we have no idea where in fact
	 * that function could really be.  It might be on another processor,
	 * etc, etc.
	 */
	if (!scsi_delete_timer(cmd))
		return;

	/* Set the serial numbers back to zero */
	cmd->serial_number = 0;
	cmd->serial_number_at_timeout = 0;
	cmd->state = SCSI_STATE_BHQUEUE;
	cmd->owner = SCSI_OWNER_BH_HANDLER;

	/*
	 * Next, enqueue the command into the done queue.
	 * It is a per-CPU queue, so we just disable local interrupts
	 * and need no spinlock.
	 */
	local_irq_save(flags);

	cpu = smp_processor_id();
	pdone_q = &done_q[cpu];
	list_add_tail(&cmd->eh_entry, pdone_q);
	cpu_raise_softirq(cpu, SCSI_SOFTIRQ);

	local_irq_restore(flags);
}

/**
 * scsi_softirq - Perform post-interrupt processing of finished SCSI commands.
 *
 * This is the consumer of the done queue.
 *
 * This is called with all interrupts enabled.  This should reduce
 * interrupt latency, stack depth, and reentrancy of the low-level
 * drivers.
 */
static void scsi_softirq(struct softirq_action *h)
{
	LIST_HEAD(local_q);

	local_irq_disable();
	list_splice_init(&done_q[smp_processor_id()], &local_q);
	local_irq_enable();

	while (!list_empty(&local_q)) {
		struct scsi_cmnd *cmd = list_entry(local_q.next,
						   struct scsi_cmnd, eh_entry);
		list_del_init(&cmd->eh_entry);

		switch (scsi_decide_disposition(cmd)) {
		case SUCCESS:
			/*
			 * Add to BH queue.
			 */
			SCSI_LOG_MLCOMPLETE(3,
					    printk("Command finished %d %d "
						   "0x%x\n",
					   cmd->device->host->host_busy,
					   cmd->device->host->host_failed,
						   cmd->result));

			scsi_finish_command(cmd);
			break;
		case NEEDS_RETRY:
			/*
			 * We only come in here if we want to retry a
			 * command.  The test to see whether the
			 * command should be retried should be keeping
			 * track of the number of tries, so we don't
			 * end up looping, of course.
			 */
			SCSI_LOG_MLCOMPLETE(3, printk("Command needs retry "
						      "%d %d 0x%x\n",
					      cmd->device->host->host_busy,
					      cmd->device->host->host_failed,
						      cmd->result));

			scsi_retry_command(cmd);
			break;
		case ADD_TO_MLQUEUE:
			/* 
			 * This typically happens for a QUEUE_FULL
			 * message - typically only when the queue
			 * depth is only approximate for a given
			 * device.  Adding a command to the queue for
			 * the device will prevent further commands
			 * from being sent to the device, so we
			 * shouldn't end up with tons of things being
			 * sent down that shouldn't be.
			 */
			SCSI_LOG_MLCOMPLETE(3, printk("Command rejected as "
						      "device queue full, "
						      "put on ml queue %p\n",
						      cmd));
			scsi_queue_insert(cmd, SCSI_MLQUEUE_DEVICE_BUSY);
			break;
		default:
			/*
			 * Here we have a fatal error of some sort.
			 * Turn it over to the error handler.
			 */
			SCSI_LOG_MLCOMPLETE(3,
					    printk("Command failed %p %x "
						   "busy=%d failed=%d\n",
						   cmd, cmd->result,
					   cmd->device->host->host_busy,
					   cmd->device->host->host_failed));

			/*
			 * Dump the sense information too.
			 */
			if ((status_byte(cmd->result)&CHECK_CONDITION) != 0) {
				SCSI_LOG_MLCOMPLETE(3, print_sense("bh", cmd));
			}

			if (!scsi_eh_scmd_add(cmd, 0)) {
				/*
				 * We only get here if the error
				 * recovery thread has died.
				 */
				scsi_finish_command(cmd);
			}
		}
	}
}

/*
 * Function:    scsi_retry_command
 *
 * Purpose:     Send a command back to the low level to be retried.
 *
 * Notes:       This command is always executed in the context of the
 *              bottom half handler, or the error handler thread. Low
 *              level drivers should not become re-entrant as a result of
 *              this.
 */
int scsi_retry_command(Scsi_Cmnd * SCpnt)
{
	/*
	 * Restore the SCSI command state.
	 */
	scsi_setup_cmd_retry(SCpnt);

        /*
         * Zero the sense information from the last time we tried
         * this command.
         */
	memset((void *) SCpnt->sense_buffer, 0, sizeof SCpnt->sense_buffer);

	return scsi_dispatch_cmd(SCpnt);
}

/*
 * Function:    scsi_finish_command
 *
 * Purpose:     Pass command off to upper layer for finishing of I/O
 *              request, waking processes that are waiting on results,
 *              etc.
 */
void scsi_finish_command(Scsi_Cmnd * SCpnt)
{
	struct Scsi_Host *host;
	Scsi_Device *device;
	Scsi_Request * SRpnt;
	unsigned long flags;

	host = SCpnt->device->host;
	device = SCpnt->device;

        /*
         * We need to protect the decrement, as otherwise a race condition
         * would exist.  Fiddling with SCpnt isn't a problem as the
         * design only allows a single SCpnt to be active in only
         * one execution context, but the device and host structures are
         * shared.
         */
	scsi_host_busy_dec_and_test(host, device);
	spin_lock_irqsave(SCpnt->device->request_queue->queue_lock, flags);
	SCpnt->device->device_busy--;
	spin_unlock_irqrestore(SCpnt->device->request_queue->queue_lock, flags);

        /*
         * Clear the flags which say that the device/host is no longer
         * capable of accepting new commands.  These are set in scsi_queue.c
         * for both the queue full condition on a device, and for a
         * host full condition on the host.
         */
        host->host_blocked = 0;
        device->device_blocked = 0;

	/*
	 * If we have valid sense information, then some kind of recovery
	 * must have taken place.  Make a note of this.
	 */
	if (SCSI_SENSE_VALID(SCpnt)) {
		SCpnt->result |= (DRIVER_SENSE << 24);
	}
	SCSI_LOG_MLCOMPLETE(3, printk("Notifying upper driver of completion for device %d %x\n",
				      SCpnt->device->id, SCpnt->result));

	SCpnt->owner = SCSI_OWNER_HIGHLEVEL;
	SCpnt->state = SCSI_STATE_FINISHED;

	/* We can get here with use_sg=0, causing a panic in the upper level (DB) */
	SCpnt->use_sg = SCpnt->old_use_sg;

       /*
	* If there is an associated request structure, copy the data over before we call the
	* completion function.
	*/
	SRpnt = SCpnt->sc_request;
	if( SRpnt != NULL ) {
	       SRpnt->sr_result = SRpnt->sr_command->result;
	       if( SRpnt->sr_result != 0 ) {
		       memcpy(SRpnt->sr_sense_buffer,
			      SRpnt->sr_command->sense_buffer,
			      sizeof(SRpnt->sr_sense_buffer));
	       }
	}

	SCpnt->done(SCpnt);
}

/*
 * Function:	scsi_adjust_queue_depth()
 *
 * Purpose:	Allow low level drivers to tell us to change the queue depth
 * 		on a specific SCSI device
 *
 * Arguments:	SDpnt	- SCSI Device in question
 * 		tagged	- Do we use tagged queueing (non-0) or do we treat
 * 			  this device as an untagged device (0)
 * 		tags	- Number of tags allowed if tagged queueing enabled,
 * 			  or number of commands the low level driver can
 * 			  queue up in non-tagged mode (as per cmd_per_lun).
 *
 * Returns:	Nothing
 *
 * Lock Status:	None held on entry
 *
 * Notes:	Low level drivers may call this at any time and we will do
 * 		the right thing depending on whether or not the device is
 * 		currently active and whether or not it even has the
 * 		command blocks built yet.
 */
void scsi_adjust_queue_depth(Scsi_Device *SDpnt, int tagged, int tags)
{
	static spinlock_t device_request_lock = SPIN_LOCK_UNLOCKED;
	unsigned long flags;

	/*
	 * refuse to set tagged depth to an unworkable size
	 */
	if(tags <= 0)
		return;
	/*
	 * Limit max queue depth on a single lun to 256 for now.  Remember,
	 * we allocate a struct scsi_command for each of these and keep it
	 * around forever.  Too deep of a depth just wastes memory.
	 */
	if(tags > 256)
		return;

	spin_lock_irqsave(&device_request_lock, flags);
	SDpnt->queue_depth = tags;
	switch(tagged) {
		case MSG_ORDERED_TAG:
			SDpnt->ordered_tags = 1;
			SDpnt->simple_tags = 1;
			break;
		case MSG_SIMPLE_TAG:
			SDpnt->ordered_tags = 0;
			SDpnt->simple_tags = 1;
			break;
		default:
			printk(KERN_WARNING "(scsi%d:%d:%d:%d) "
				"scsi_adjust_queue_depth, bad queue type, "
				"disabled\n", SDpnt->host->host_no,
				SDpnt->channel, SDpnt->id, SDpnt->lun); 
		case 0:
			SDpnt->ordered_tags = SDpnt->simple_tags = 0;
			SDpnt->queue_depth = tags;
			break;
	}
	spin_unlock_irqrestore(&device_request_lock, flags);
}

/*
 * Function:	scsi_track_queue_full()
 *
 * Purpose:	This function will track successive QUEUE_FULL events on a
 * 		specific SCSI device to determine if and when there is a
 * 		need to adjust the queue depth on the device.
 *
 * Arguments:	SDpnt	- SCSI Device in question
 * 		depth	- Current number of outstanding SCSI commands on
 * 			  this device, not counting the one returned as
 * 			  QUEUE_FULL.
 *
 * Returns:	0 - No change needed
 * 		>0 - Adjust queue depth to this new depth
 * 		-1 - Drop back to untagged operation using host->cmd_per_lun
 * 			as the untagged command depth
 *
 * Lock Status:	None held on entry
 *
 * Notes:	Low level drivers may call this at any time and we will do
 * 		"The Right Thing."  We are interrupt context safe.
 */
int scsi_track_queue_full(Scsi_Device *SDptr, int depth)
{
	if((jiffies >> 4) != SDptr->last_queue_full_time) {
		SDptr->last_queue_full_time = (jiffies >> 4);
		if(SDptr->last_queue_full_depth == depth)
			SDptr->last_queue_full_count++;
		else {
			SDptr->last_queue_full_count = 1;
			SDptr->last_queue_full_depth = depth;
		}
		if(SDptr->last_queue_full_count > 10) {
			if(SDptr->last_queue_full_depth < 8) {
				/* Drop back to untagged */
				scsi_adjust_queue_depth(SDptr, 0 /* untagged */,
						SDptr->host->cmd_per_lun);
				return -1;
			}
			if(SDptr->ordered_tags)
				scsi_adjust_queue_depth(SDptr, MSG_ORDERED_TAG,
						depth);
			else
				scsi_adjust_queue_depth(SDptr, MSG_SIMPLE_TAG,
						depth);
			return depth;
		}
	}
	return 0;
}


/*
 * scsi_strcpy_devinfo: called from scsi_dev_info_list_add to copy into
 * devinfo vendor and model strings.
 */
static void scsi_strcpy_devinfo(char *name, char *to, size_t to_length,
				char *from, int compatible)
{
	size_t from_length;

	from_length = strlen(from);
	strncpy(to, from, min(to_length, from_length));
	if (from_length < to_length) {
		if (compatible) {
			/*
			 * NUL terminate the string if it is short.
			 */
			to[from_length] = '\0';
		} else {
			/* 
			 * space pad the string if it is short. 
			 */
			strncpy(&to[from_length], spaces,
				to_length - from_length);
		}
	}
	if (from_length > to_length)
		 printk(KERN_WARNING "%s: %s string '%s' is too long\n",
			__FUNCTION__, name, from);
}

/**
 * scsi_dev_info_list_add: add one dev_info list entry.
 * @vendor:	vendor string
 * @model:	model (product) string
 * @strflags:	integer string
 * @flag:	if strflags NULL, use this flag value
 *
 * Description:
 * 	Create and add one dev_info entry for @vendor, @model, @strflags or
 * 	@flag. If @compatible, add to the tail of the list, do not space
 * 	pad, and set devinfo->compatible. The scsi_static_device_list entries
 * 	are added with @compatible 1 and @clfags NULL.
 *
 * Returns: 0 OK, -error on failure.
 **/
static int scsi_dev_info_list_add(int compatible, char *vendor, char *model,
			    char *strflags, int flags)
{
	struct scsi_dev_info_list *devinfo;

	devinfo = kmalloc(sizeof(*devinfo), GFP_KERNEL);
	if (!devinfo) {
		printk(KERN_ERR "%s: no memory\n", __FUNCTION__);
		return -ENOMEM;
	}

	scsi_strcpy_devinfo("vendor", devinfo->vendor, sizeof(devinfo->vendor),
			    vendor, compatible);
	scsi_strcpy_devinfo("model", devinfo->model, sizeof(devinfo->model),
			    model, compatible);

	if (strflags)
		devinfo->flags = simple_strtoul(strflags, NULL, 0);
	else
		devinfo->flags = flags;

	devinfo->compatible = compatible;

	if (compatible)
		list_add_tail(&devinfo->dev_info_list, &scsi_dev_info_list);
	else
		list_add(&devinfo->dev_info_list, &scsi_dev_info_list);

	return 0;
}

/**
 * scsi_dev_info_list_add_str: parse dev_list and add to the
 * scsi_dev_info_list.
 * @dev_list:	string of device flags to add
 *
 * Description:
 * 	Parse dev_list, and add entries to the scsi_dev_info_list.
 * 	dev_list is of the form "vendor:product:flag,vendor:product:flag".
 * 	dev_list is modified via strsep. Can be called for command line
 * 	addition, for proc or mabye a sysfs interface.
 *
 * Returns: 0 if OK, -error on failure.
 **/
int scsi_dev_info_list_add_str (char *dev_list)
{
	char *vendor, *model, *strflags, *next;
	char *next_check;
	int res = 0;

	next = dev_list;
	if (next && next[0] == '"') {
		/*
		 * Ignore both the leading and trailing quote.
		 */
		next++;
		next_check = ",\"";
	} else {
		next_check = ",";
	}

	/*
	 * For the leading and trailing '"' case, the for loop comes
	 * through the last time with vendor[0] == '\0'.
	 */
	for (vendor = strsep(&next, ":"); vendor && (vendor[0] != '\0')
	     && (res == 0); vendor = strsep(&next, ":")) {
		strflags = NULL;
		model = strsep(&next, ":");
		if (model)
			strflags = strsep(&next, next_check);
		if (!model || !strflags) {
			printk(KERN_ERR "%s: bad dev info string '%s' '%s'"
			       " '%s'\n", __FUNCTION__, vendor, model,
			       strflags);
			res = -EINVAL;
		} else
			res = scsi_dev_info_list_add(0 /* compatible */, vendor,
						     model, strflags, 0);
	}
	return res;
}

/**
 * scsi_dev_info_list_delete: called from scsi.c:exit_scsi to remove
 * 	the scsi_dev_info_list.
 **/
static void scsi_dev_info_list_delete (void)
{
	struct list_head *lh, *lh_next;
	struct scsi_dev_info_list *devinfo;

	list_for_each_safe(lh, lh_next, &scsi_dev_info_list) {
		devinfo = list_entry(lh, struct scsi_dev_info_list,
				     dev_info_list);
		kfree(devinfo);
	}
}

/**
 * scsi_dev_list_init: set up the dynamic device list.
 * @dev_list:	string of device flags to add
 *
 * Description:
 * 	Add command line @dev_list entries, then add
 * 	scsi_static_device_list entries to the scsi device info list.
 **/
static int scsi_dev_info_list_init (char *dev_list)
{
	int error, i;

	error = scsi_dev_info_list_add_str(dev_list);
	if (error)
		return error;

	for (i = 0; scsi_static_device_list[i].vendor != NULL; i++) {
		error = scsi_dev_info_list_add(1 /* compatibile */,
				scsi_static_device_list[i].vendor,
				scsi_static_device_list[i].model,
				NULL,
				scsi_static_device_list[i].flags);
		if (error)
			break;
	}

	if (error)
		scsi_dev_info_list_delete();
	return error;
}

/**
 * get_device_flags - get device specific flags from the dynamic device
 * list. Called during scan time.
 * @vendor:	vendor name
 * @model:	model name
 *
 * Description:
 *     Search the scsi_dev_info_list for an entry matching @vendor and
 *     @model, if found, return the matching flags value, else return
 *     scsi_default_dev_flags.
 **/
int scsi_get_device_flags(unsigned char *vendor, unsigned char *model)
{
	struct scsi_dev_info_list *devinfo;

	list_for_each_entry(devinfo, &scsi_dev_info_list, dev_info_list) {
		if (devinfo->compatible) {
			/*
			 * Behave like the older version of get_device_flags.
			 */
			size_t max;
			/*
			 * XXX why skip leading spaces? If an odd INQUIRY
			 * value, that should have been part of the
			 * scsi_static_device_list[] entry, such as "  FOO"
			 * rather than "FOO". Since this code is already
			 * here, and we don't know what device it is
			 * trying to work with, leave it as-is.
			 */
			max = 8;	/* max length of vendor */
			while ((max > 0) && *vendor == ' ') {
				max--;
				vendor++;
			}
			/*
			 * XXX removing the following strlen() would be
			 * good, using it means that for a an entry not in
			 * the list, we scan every byte of every vendor
			 * listed in scsi_static_device_list[], and never match
			 * a single one (and still have to compare at
			 * least the first byte of each vendor).
			 */
			if (memcmp(devinfo->vendor, vendor,
				    min(max, strlen(devinfo->vendor))))
				continue;
			/*
			 * Skip spaces again.
			 */
			max = 16;	/* max length of model */
			while ((max > 0) && *model == ' ') {
				max--;
				model++;
			}
			if (memcmp(devinfo->model, model,
				   min(max, strlen(devinfo->model))))
				continue;
			return devinfo->flags;
		} else {
			if (!memcmp(devinfo->vendor, vendor,
				     sizeof(devinfo->vendor)) &&
			     !memcmp(devinfo->model, model,
				      sizeof(devinfo->model)))
				return devinfo->flags;
		}
	}
	return scsi_default_dev_flags;
}

int scsi_attach_device(struct scsi_device *sdev)
{
	struct Scsi_Device_Template *sdt;

	down_read(&scsi_devicelist_mutex);
	list_for_each_entry(sdt, &scsi_devicelist, list) {
		if (!try_module_get(sdt->module))
			continue;
		(*sdt->attach)(sdev);
		module_put(sdt->module);
	}
	up_read(&scsi_devicelist_mutex);
	return 0;
}

void scsi_detach_device(struct scsi_device *sdev)
{
	struct Scsi_Device_Template *sdt;

	down_read(&scsi_devicelist_mutex);
	list_for_each_entry(sdt, &scsi_devicelist, list) {
		if (!try_module_get(sdt->module))
			continue;
		(*sdt->detach)(sdev);
		module_put(sdt->module);
	}
	up_read(&scsi_devicelist_mutex);
}

void scsi_rescan_device(struct scsi_device *sdev)
{
	struct Scsi_Device_Template *sdt;

	down_read(&scsi_devicelist_mutex);
	list_for_each_entry(sdt, &scsi_devicelist, list) {
		if (!try_module_get(sdt->module))
			continue;
		if (*sdt->rescan)
			(*sdt->rescan)(sdev);
		module_put(sdt->module);
	}
	up_read(&scsi_devicelist_mutex);
}

int scsi_device_get(struct scsi_device *sdev)
{
	if (!try_module_get(sdev->host->hostt->module))
		return -ENXIO;

	sdev->access_count++;
	return 0;
}

void scsi_device_put(struct scsi_device *sdev)
{
	sdev->access_count--;
	module_put(sdev->host->hostt->module);
}

/**
 * scsi_set_device_offline - set scsi_device offline
 * @sdev:	pointer to struct scsi_device to offline. 
 *
 * Locks:	host_lock held on entry.
 **/
void scsi_set_device_offline(struct scsi_device *sdev)
{
	struct scsi_cmnd *scmd;
	LIST_HEAD(active_list);
	struct list_head *lh, *lh_sf;
	unsigned long flags;

	sdev->online = FALSE;

	spin_lock_irqsave(&sdev->list_lock, flags);
	list_for_each_entry(scmd, &sdev->cmd_list, list) {
		if (scmd->request && scmd->request->rq_status != RQ_INACTIVE) {
			/*
			 * If we are unable to remove the timer, it means
			 * that the command has already timed out or
			 * finished.
			 */
			if (!scsi_delete_timer(scmd)) {
				continue;
			}
			list_add_tail(&scmd->eh_entry, &active_list);
		}
	}
	spin_unlock_irqrestore(&sdev->list_lock, flags);

	if (!list_empty(&active_list)) {
		list_for_each_safe(lh, lh_sf, &active_list) {
			scmd = list_entry(lh, struct scsi_cmnd, eh_entry);
			scsi_eh_scmd_add(scmd, SCSI_EH_CANCEL_CMD);
		}
	} else {
		/* FIXME: Send online state change hotplug event */
	}
}

/*
 * Function:	scsi_slave_attach()
 *
 * Purpose:	Called from the upper level driver attach to handle common
 * 		attach code.
 *
 * Arguments:	sdev - scsi_device to attach
 *
 * Returns:	1 on error, 0 on succes
 *
 * Lock Status:	Protected via scsi_devicelist_mutex.
 */
int scsi_slave_attach(struct scsi_device *sdev)
{
	sdev->attached++;
	return 0;
}

/*
 * Function:	scsi_slave_detach()
 *
 * Purpose:	Called from the upper level driver attach to handle common
 * 		detach code.
 *
 * Arguments:	sdev - struct scsi_device to detach
 *
 * Lock Status:	Protected via scsi_devicelist_mutex.
 */
void scsi_slave_detach(struct scsi_device *sdev)
{
	sdev->attached--;
}
/*
 * This entry point should be called by a loadable module if it is trying
 * add a high level scsi driver to the system.
 *
 * This entry point is called from the upper level module's module_init()
 * routine.  That implies that when this function is called, the
 * scsi_mod module is locked down because of upper module layering and
 * that the high level driver module is locked down by being in it's
 * init routine.  So, the *only* thing we have to do to protect adds 
 * we perform in this function is to make sure that all call's
 * to the high level driver's attach() and detach() call in points, other
 * than via scsi_register_device and scsi_unregister_device which are in
 * the module_init and module_exit code respectively and therefore already
 * locked down by the kernel module loader, are wrapped by try_module_get()
 * and module_put() to avoid races on device adds and removes.
 */
int scsi_register_device(struct Scsi_Device_Template *tpnt)
{
	Scsi_Device *SDpnt;
	struct Scsi_Host *shpnt;

#ifdef CONFIG_KMOD
	if (scsi_host_get_next(NULL) == NULL)
		request_module("scsi_hostadapter");
#endif

	if (!list_empty(&tpnt->list))
		return 1;

	down_write(&scsi_devicelist_mutex);
	list_add_tail(&tpnt->list, &scsi_devicelist);
	up_write(&scsi_devicelist_mutex);

	scsi_upper_driver_register(tpnt);

	for (shpnt = scsi_host_get_next(NULL); shpnt;
	     shpnt = scsi_host_get_next(shpnt)) 
		list_for_each_entry (SDpnt, &shpnt->my_devices, siblings)
			(*tpnt->attach) (SDpnt);

	return 0;
}

int scsi_unregister_device(struct Scsi_Device_Template *tpnt)
{
	Scsi_Device *SDpnt;
	struct Scsi_Host *shpnt;

	/*
	 * Next, detach the devices from the driver.
	 */
	for (shpnt = scsi_host_get_next(NULL); shpnt;
	     shpnt = scsi_host_get_next(shpnt)) {
		list_for_each_entry(SDpnt, &shpnt->my_devices, siblings)
			(*tpnt->detach) (SDpnt);
	}

	/*
	 * Extract the template from the linked list.
	 */
	down_write(&scsi_devicelist_mutex);
	list_del(&tpnt->list);
	up_write(&scsi_devicelist_mutex);

	scsi_upper_driver_unregister(tpnt);
	return 0;
}

static char *scsi_dev_flags;
MODULE_PARM(scsi_dev_flags, "s");
MODULE_PARM_DESC(scsi_dev_flags,
	 "Given scsi_dev_flags=vendor:model:flags, add a black/white list"
	 " entry for vendor and model with an integer value of flags"
	 " to the scsi device info list");
MODULE_PARM(scsi_default_dev_flags, "i");
MODULE_PARM_DESC(scsi_default_dev_flags,
		 "scsi default device flag integer value");
MODULE_DESCRIPTION("SCSI core");
MODULE_LICENSE("GPL");

#ifndef MODULE

int __init setup_scsi_dev_flags(char *str)
{
	scsi_dev_flags = str;
	return 1;
}
__setup("scsi_dev_flags=", setup_scsi_dev_flags);

static int __init setup_scsi_default_dev_flags(char *str)
{
	unsigned int tmp;
	if (get_option(&str, &tmp) == 1) {
		scsi_default_dev_flags = tmp;
		printk(KERN_WARNING "%s %d\n", __FUNCTION__,
		       scsi_default_dev_flags);
		return 1;
	} else {
		printk(KERN_WARNING "%s: usage scsi_default_dev_flags=intr\n",
		       __FUNCTION__);
		return 0;
	}
}
__setup("scsi_default_dev_flags=", setup_scsi_default_dev_flags);

#endif

static int __init init_scsi(void)
{
	int error, i;

	error = scsi_init_queue();
	if (error)
		return error;
	error = scsi_init_procfs();
	if (error)
		goto cleanup_queue;
	error = scsi_dev_info_list_init(scsi_dev_flags);
	if (error)
		goto cleanup_procfs;
	error = scsi_sysfs_register();
	if (error)
		goto cleanup_devlist;

	for (i = 0; i < NR_CPUS; i++)
		INIT_LIST_HEAD(&done_q[i]);

	scsi_host_init();
	devfs_mk_dir("scsi");
	open_softirq(SCSI_SOFTIRQ, scsi_softirq, NULL);
	printk(KERN_NOTICE "SCSI subsystem initialized\n");
	return 0;

cleanup_devlist:
	scsi_dev_info_list_delete();
cleanup_procfs:
	scsi_exit_procfs();
cleanup_queue:
	scsi_exit_queue();
	printk(KERN_ERR "SCSI subsystem failed to initialize, error = %d\n",
	       -error);
	return error;
}

static void __exit exit_scsi(void)
{
	scsi_sysfs_unregister();
	scsi_dev_info_list_delete();
	devfs_remove("scsi");
	scsi_exit_procfs();
	scsi_exit_queue();
}

subsys_initcall(init_scsi);
module_exit(exit_scsi);
