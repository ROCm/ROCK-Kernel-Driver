/*
 *  scsi_lib.c Copyright (C) 1999 Eric Youngdale
 *
 *  SCSI queueing library.
 *      Initial versions: Eric Youngdale (eric@andante.org).
 *                        Based upon conversations with large numbers
 *                        of people at Linux Expo.
 */

#include <linux/bio.h>
#include <linux/blk.h>
#include <linux/completion.h>
#include <linux/kernel.h>
#include <linux/mempool.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/pci.h>

#include "scsi.h"
#include "hosts.h"


#define SG_MEMPOOL_NR		5
#define SG_MEMPOOL_SIZE		32

struct scsi_host_sg_pool {
	size_t		size;
	char		*name; 
	kmem_cache_t	*slab;
	mempool_t	*pool;
};

#define SP(x) { x, "sgpool-" #x } 
struct scsi_host_sg_pool scsi_sg_pools[SG_MEMPOOL_NR] = { 
	SP(8), SP(16), SP(32), SP(64), SP(MAX_PHYS_SEGMENTS)
}; 	
#undef SP


/*
 * Function:    scsi_insert_special_req()
 *
 * Purpose:     Insert pre-formed request into request queue.
 *
 * Arguments:   sreq	- request that is ready to be queued.
 *              at_head	- boolean.  True if we should insert at head
 *                        of queue, false if we should insert at tail.
 *
 * Lock status: Assumed that lock is not held upon entry.
 *
 * Returns:     Nothing
 *
 * Notes:       This function is called from character device and from
 *              ioctl types of functions where the caller knows exactly
 *              what SCSI command needs to be issued.   The idea is that
 *              we merely inject the command into the queue (at the head
 *              for now), and then call the queue request function to actually
 *              process it.
 */
int scsi_insert_special_req(struct scsi_request *sreq, int at_head)
{
	/*
	 * Because users of this function are apt to reuse requests with no
	 * modification, we have to sanitise the request flags here
	 */
	sreq->sr_request->flags &= ~REQ_DONTPREP;
	blk_insert_request(sreq->sr_device->request_queue, sreq->sr_request,
		       	   at_head, sreq);
	return 0;
}

/*
 * Function:    scsi_queue_insert()
 *
 * Purpose:     Insert a command in the midlevel queue.
 *
 * Arguments:   cmd    - command that we are adding to queue.
 *              reason - why we are inserting command to queue.
 *
 * Lock status: Assumed that lock is not held upon entry.
 *
 * Returns:     Nothing.
 *
 * Notes:       We do this for one of two cases.  Either the host is busy
 *              and it cannot accept any more commands for the time being,
 *              or the device returned QUEUE_FULL and can accept no more
 *              commands.
 * Notes:       This could be called either from an interrupt context or a
 *              normal process context.
 */
int scsi_queue_insert(struct scsi_cmnd *cmd, int reason)
{
	struct Scsi_Host *host = cmd->device->host;
	struct scsi_device *device = cmd->device;
	unsigned long flags;

	SCSI_LOG_MLQUEUE(1,
		 printk("Inserting command %p into mlqueue\n", cmd));

	/*
	 * We are inserting the command into the ml queue.  First, we
	 * cancel the timer, so it doesn't time out.
	 */
	scsi_delete_timer(cmd);

	/*
	 * Next, set the appropriate busy bit for the device/host.
	 *
	 * If the host/device isn't busy, assume that something actually
	 * completed, and that we should be able to queue a command now.
	 *
	 * Note that the prior mid-layer assumption that any host could
	 * always queue at least one command is now broken.  The mid-layer
	 * will implement a user specifiable stall (see
	 * scsi_host.max_host_blocked and scsi_device.max_device_blocked)
	 * if a command is requeued with no other commands outstanding
	 * either for the device or for the host.
	 */
	if (reason == SCSI_MLQUEUE_HOST_BUSY)
		host->host_blocked = host->max_host_blocked;
	else if (reason == SCSI_MLQUEUE_DEVICE_BUSY)
		device->device_blocked = device->max_device_blocked;

	/*
	 * Register the fact that we own the thing for now.
	 */
	cmd->state = SCSI_STATE_MLQUEUE;
	cmd->owner = SCSI_OWNER_MIDLEVEL;

	/*
	 * Decrement the counters, since these commands are no longer
	 * active on the host/device.
	 */
	spin_lock_irqsave(device->request_queue->queue_lock, flags);
	device->device_busy--;
	spin_unlock_irqrestore(device->request_queue->queue_lock, flags);
	scsi_host_busy_dec_and_test(host, device);

	/*
	 * Insert this command at the head of the queue for it's device.
	 * It will go before all other commands that are already in the queue.
	 *
	 * NOTE: there is magic here about the way the queue is plugged if
	 * we have no outstanding commands.
	 * 
	 * Although this *doesn't* plug the queue, it does call the request
	 * function.  The SCSI request function detects the blocked condition
	 * and plugs the queue appropriately.
	 */
	blk_insert_request(device->request_queue, cmd->request, 1, cmd);
	return 0;
}

/*
 * Function:    scsi_do_req
 *
 * Purpose:     Queue a SCSI request
 *
 * Arguments:   sreq	  - command descriptor.
 *              cmnd      - actual SCSI command to be performed.
 *              buffer    - data buffer.
 *              bufflen   - size of data buffer.
 *              done      - completion function to be run.
 *              timeout   - how long to let it run before timeout.
 *              retries   - number of retries we allow.
 *
 * Lock status: No locks held upon entry.
 *
 * Returns:     Nothing.
 *
 * Notes:	This function is only used for queueing requests for things
 *		like ioctls and character device requests - this is because
 *		we essentially just inject a request into the queue for the
 *		device.
 */
void scsi_do_req(struct scsi_request *sreq, const void *cmnd,
		 void *buffer, unsigned bufflen,
		 void (*done)(struct scsi_cmnd *),
		 int timeout, int retries)
{
	struct request_queue *q;

	/*
	 * If the upper level driver is reusing these things, then
	 * we should release the low-level block now.  Another one will
	 * be allocated later when this request is getting queued.
	 */
	if (sreq->sr_command) {
		q = sreq->sr_command->device->request_queue;
		scsi_put_command(sreq->sr_command);
		sreq->sr_command = NULL;
		scsi_queue_next_request(q, NULL);
	}

	/*
	 * Our own function scsi_done (which marks the host as not busy,
	 * disables the timeout counter, etc) will be called by us or by the
	 * scsi_hosts[host].queuecommand() function needs to also call
	 * the completion function for the high level driver.
	 */
	memcpy(sreq->sr_cmnd, cmnd, sizeof(sreq->sr_cmnd));
	sreq->sr_bufflen = bufflen;
	sreq->sr_buffer = buffer;
	sreq->sr_allowed = retries;
	sreq->sr_done = done;
	sreq->sr_timeout_per_command = timeout;

	if (sreq->sr_cmd_len == 0)
		sreq->sr_cmd_len = COMMAND_SIZE(sreq->sr_cmnd[0]);

	/*
	 * At this point, we merely set up the command, stick it in the normal
	 * request queue, and return.  Eventually that request will come to the
	 * top of the list, and will be dispatched.
	 */
	scsi_insert_special_req(sreq, 0);
}
 
static void scsi_wait_done(struct scsi_cmnd *cmd)
{
	struct request *req = cmd->request;
	struct request_queue *q = cmd->device->request_queue;
	unsigned long flags;

	req->rq_status = RQ_SCSI_DONE;	/* Busy, but indicate request done */

	spin_lock_irqsave(q->queue_lock, flags);
	if (blk_rq_tagged(req))
		blk_queue_end_tag(q, req);
	spin_unlock_irqrestore(q->queue_lock, flags);

	if (req->waiting)
		complete(req->waiting);
}

void scsi_wait_req(struct scsi_request *sreq, const void *cmnd, void *buffer,
		   unsigned bufflen, int timeout, int retries)
{
	struct request_queue *q;
	DECLARE_COMPLETION(wait);
	
	sreq->sr_request->waiting = &wait;
	sreq->sr_request->rq_status = RQ_SCSI_BUSY;
	scsi_do_req(sreq, cmnd, buffer, bufflen, scsi_wait_done,
			timeout, retries);
	generic_unplug_device(sreq->sr_device->request_queue);
	wait_for_completion(&wait);
	sreq->sr_request->waiting = NULL;

	if (sreq->sr_command) {
		q = sreq->sr_command->device->request_queue;
		scsi_put_command(sreq->sr_command);
		scsi_queue_next_request(q, NULL);
		sreq->sr_command = NULL;
	}
}

/*
 * Function:    scsi_init_cmd_errh()
 *
 * Purpose:     Initialize cmd fields related to error handling.
 *
 * Arguments:   cmd	- command that is ready to be queued.
 *
 * Returns:     Nothing
 *
 * Notes:       This function has the job of initializing a number of
 *              fields related to error handling.   Typically this will
 *              be called once for each command, as required.
 */
static int scsi_init_cmd_errh(struct scsi_cmnd *cmd)
{
	cmd->owner = SCSI_OWNER_MIDLEVEL;
	cmd->reset_chain = NULL;
	cmd->serial_number = 0;
	cmd->serial_number_at_timeout = 0;
	cmd->flags = 0;
	cmd->abort_reason = 0;

	memset(cmd->sense_buffer, 0, sizeof cmd->sense_buffer);

	if (cmd->cmd_len == 0)
		cmd->cmd_len = COMMAND_SIZE(cmd->cmnd[0]);

	/*
	 * We need saved copies of a number of fields - this is because
	 * error handling may need to overwrite these with different values
	 * to run different commands, and once error handling is complete,
	 * we will need to restore these values prior to running the actual
	 * command.
	 */
	cmd->old_use_sg = cmd->use_sg;
	cmd->old_cmd_len = cmd->cmd_len;
	cmd->sc_old_data_direction = cmd->sc_data_direction;
	cmd->old_underflow = cmd->underflow;
	memcpy(cmd->data_cmnd, cmd->cmnd, sizeof(cmd->cmnd));
	cmd->buffer = cmd->request_buffer;
	cmd->bufflen = cmd->request_bufflen;
	cmd->reset_chain = NULL;
	cmd->internal_timeout = NORMAL_TIMEOUT;
	cmd->abort_reason = 0;

	return 1;
}

/*
 * Function:   scsi_setup_cmd_retry()
 *
 * Purpose:    Restore the command state for a retry
 *
 * Arguments:  cmd	- command to be restored
 *
 * Returns:    Nothing
 *
 * Notes:      Immediately prior to retrying a command, we need
 *             to restore certain fields that we saved above.
 */
void scsi_setup_cmd_retry(struct scsi_cmnd *cmd)
{
	memcpy(cmd->cmnd, cmd->data_cmnd, sizeof(cmd->data_cmnd));
	cmd->request_buffer = cmd->buffer;
	cmd->request_bufflen = cmd->bufflen;
	cmd->use_sg = cmd->old_use_sg;
	cmd->cmd_len = cmd->old_cmd_len;
	cmd->sc_data_direction = cmd->sc_old_data_direction;
	cmd->underflow = cmd->old_underflow;
}

/*
 * Called for single_lun devices on IO completion. Clear starget_sdev_user,
 * and call __blk_run_queue for all the scsi_devices on the target -
 * including current_sdev first.
 *
 * Called with *no* scsi locks held.
 */
static void scsi_single_lun_run(struct scsi_device *current_sdev)
{
	struct scsi_device *sdev;
	unsigned long flags, flags2;

	spin_lock_irqsave(current_sdev->host->host_lock, flags);
	WARN_ON(!current_sdev->sdev_target->starget_sdev_user);
	current_sdev->sdev_target->starget_sdev_user = NULL;
	spin_unlock_irqrestore(current_sdev->host->host_lock, flags);

	/*
	 * Call __blk_run_queue for all LUNs on the target, starting with
	 * current_sdev. We race with others (to set starget_sdev_user),
	 * but in most cases, we will be first. Ideally, each LU on the
	 * target would get some limited time or requests on the target.
	 */
	spin_lock_irqsave(current_sdev->request_queue->queue_lock, flags2);
	__blk_run_queue(current_sdev->request_queue);
	spin_unlock_irqrestore(current_sdev->request_queue->queue_lock, flags2);

	spin_lock_irqsave(current_sdev->host->host_lock, flags);
	if (current_sdev->sdev_target->starget_sdev_user) {
		/*
		 * After unlock, this races with anyone clearing
		 * starget_sdev_user, but we (should) always enter this
		 * function again, avoiding any problems.
		 */
		spin_unlock_irqrestore(current_sdev->host->host_lock, flags);
		return;
	}
	spin_unlock_irqrestore(current_sdev->host->host_lock, flags);

	list_for_each_entry(sdev, &current_sdev->same_target_siblings,
			    same_target_siblings) {
		spin_lock_irqsave(sdev->request_queue->queue_lock, flags2);
		__blk_run_queue(sdev->request_queue);
		spin_unlock_irqrestore(sdev->request_queue->queue_lock, flags2);
	}
}

/*
 * Function:    scsi_queue_next_request()
 *
 * Purpose:     Handle post-processing of completed commands.
 *
 * Arguments:   cmd	- command that may need to be requeued.
 *
 * Returns:     Nothing
 *
 * Notes:       After command completion, there may be blocks left
 *              over which weren't finished by the previous command
 *              this can be for a number of reasons - the main one is
 *              that a medium error occurred, and the sectors after
 *              the bad block need to be re-read.
 *
 *              If cmd is NULL, it means that the previous command
 *              was completely finished, and we should simply start
 *              a new command, if possible.
 *
 *		This is where a lot of special case code has begun to
 *		accumulate.  It doesn't really affect readability or
 *		anything, but it might be considered architecturally
 *		inelegant.  If more of these special cases start to
 *		accumulate, I am thinking along the lines of implementing
 *		an atexit() like technology that gets run when commands
 *		complete.  I am not convinced that it is worth the
 *		added overhead, however.  Right now as things stand,
 *		there are simple conditional checks, and most hosts
 *		would skip past.
 *
 *		Another possible solution would be to tailor different
 *		handler functions, sort of like what we did in scsi_merge.c.
 *		This is probably a better solution, but the number of different
 *		permutations grows as 2**N, and if too many more special cases
 *		get added, we start to get screwed.
 */
void scsi_queue_next_request(request_queue_t *q, struct scsi_cmnd *cmd)
{
	struct scsi_device *sdev;
	struct Scsi_Host *shost;
	unsigned long flags;

	if (cmd != NULL) {

		/*
		 * For some reason, we are not done with this request.
		 * This happens for I/O errors in the middle of the request,
		 * in which case we need to request the blocks that come after
		 * the bad sector.
		 */
		spin_lock_irqsave(q->queue_lock, flags);
		cmd->request->special = cmd;
		if (blk_rq_tagged(cmd->request))
			blk_queue_end_tag(q, cmd->request);

		/*
		 * set REQ_SPECIAL - we have a command
		 * clear REQ_DONTPREP - we assume the sg table has been 
		 *	nuked so we need to set it up again.
		 */
		cmd->request->flags |= REQ_SPECIAL;
		cmd->request->flags &= ~REQ_DONTPREP;
		__elv_add_request(q, cmd->request, 0, 0);
		spin_unlock_irqrestore(q->queue_lock, flags);
	}

	sdev = q->queuedata;

	if (sdev->single_lun)
		scsi_single_lun_run(sdev);

	shost = sdev->host;
	spin_lock_irqsave(shost->host_lock, flags);
	while (!list_empty(&shost->starved_list) &&
	       !shost->host_blocked && !shost->host_self_blocked &&
		!((shost->can_queue > 0) &&
		  (shost->host_busy >= shost->can_queue))) {
		/*
		 * As long as shost is accepting commands and we have
		 * starved queues, call __blk_run_queue. scsi_request_fn
		 * drops the queue_lock and can add us back to the
		 * starved_list.
		 *
		 * host_lock protects the starved_list and starved_entry.
		 * scsi_request_fn must get the host_lock before checking
		 * or modifying starved_list or starved_entry.
		 */
		sdev = list_entry(shost->starved_list.next,
					  struct scsi_device, starved_entry);
		list_del_init(&sdev->starved_entry);
		spin_unlock_irqrestore(shost->host_lock, flags);

		spin_lock_irqsave(sdev->request_queue->queue_lock, flags);
		__blk_run_queue(sdev->request_queue);
		spin_unlock_irqrestore(sdev->request_queue->queue_lock, flags);

		spin_lock_irqsave(shost->host_lock, flags);
		if (unlikely(!list_empty(&sdev->starved_entry)))
			/*
			 * sdev lost a race, and was put back on the
			 * starved list. This is unlikely but without this
			 * in theory we could loop forever.
			 */
			break;
	}
	spin_unlock_irqrestore(shost->host_lock, flags);

	spin_lock_irqsave(q->queue_lock, flags);
	__blk_run_queue(q);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

/*
 * Function:    scsi_end_request()
 *
 * Purpose:     Post-processing of completed commands called from interrupt
 *              handler or a bottom-half handler.
 *
 * Arguments:   cmd	 - command that is complete.
 *              uptodate - 1 if I/O indicates success, 0 for I/O error.
 *              sectors  - number of sectors we want to mark.
 *		requeue  - indicates whether we should requeue leftovers.
 *		frequeue - indicates that if we release the command block
 *			   that the queue request function should be called.
 *
 * Lock status: Assumed that lock is not held upon entry.
 *
 * Returns:     Nothing
 *
 * Notes:       This is called for block device requests in order to
 *              mark some number of sectors as complete.
 * 
 *		We are guaranteeing that the request queue will be goosed
 *		at some point during this call.
 */
static struct scsi_cmnd *scsi_end_request(struct scsi_cmnd *cmd, int uptodate,
					  int sectors, int requeue)
{
	request_queue_t *q = cmd->device->request_queue;
	struct request *req = cmd->request;
	unsigned long flags;

	/*
	 * If there are blocks left over at the end, set up the command
	 * to queue the remainder of them.
	 */
	if (end_that_request_first(req, uptodate, sectors)) {
		if (requeue) {
			/*
			 * Bleah.  Leftovers again.  Stick the leftovers in
			 * the front of the queue, and goose the queue again.
			 */
			scsi_queue_next_request(q, cmd);
		}
		return cmd;
	}

	add_disk_randomness(req->rq_disk);

	spin_lock_irqsave(q->queue_lock, flags);
	if (blk_rq_tagged(req))
		blk_queue_end_tag(q, req);
	end_that_request_last(req);
	spin_unlock_irqrestore(q->queue_lock, flags);

	/*
	 * This will goose the queue request function at the end, so we don't
	 * need to worry about launching another command.
	 */
	scsi_put_command(cmd);
	scsi_queue_next_request(q, NULL);
	return NULL;
}

static struct scatterlist *scsi_alloc_sgtable(struct scsi_cmnd *cmd, int gfp_mask)
{
	struct scsi_host_sg_pool *sgp;
	struct scatterlist *sgl;

	BUG_ON(!cmd->use_sg);

	switch (cmd->use_sg) {
	case 1 ... 8:
		cmd->sglist_len = 0;
		break;
	case 9 ... 16:
		cmd->sglist_len = 1;
		break;
	case 17 ... 32:
		cmd->sglist_len = 2;
		break;
	case 33 ... 64:
		cmd->sglist_len = 3;
		break;
	case 65 ... MAX_PHYS_SEGMENTS:
		cmd->sglist_len = 4;
		break;
	default:
		return NULL;
	}

	sgp = scsi_sg_pools + cmd->sglist_len;
	sgl = mempool_alloc(sgp->pool, gfp_mask);
	if (sgl)
		memset(sgl, 0, sgp->size);
	return sgl;
}

static void scsi_free_sgtable(struct scatterlist *sgl, int index)
{
	struct scsi_host_sg_pool *sgp;

	BUG_ON(index > SG_MEMPOOL_NR);

	sgp = scsi_sg_pools + index;
	mempool_free(sgl, sgp->pool);
}

/*
 * Function:    scsi_release_buffers()
 *
 * Purpose:     Completion processing for block device I/O requests.
 *
 * Arguments:   cmd	- command that we are bailing.
 *
 * Lock status: Assumed that no lock is held upon entry.
 *
 * Returns:     Nothing
 *
 * Notes:       In the event that an upper level driver rejects a
 *		command, we must release resources allocated during
 *		the __init_io() function.  Primarily this would involve
 *		the scatter-gather table, and potentially any bounce
 *		buffers.
 */
static void scsi_release_buffers(struct scsi_cmnd *cmd)
{
	struct request *req = cmd->request;

	/*
	 * Free up any indirection buffers we allocated for DMA purposes. 
	 */
	if (cmd->use_sg)
		scsi_free_sgtable(cmd->request_buffer, cmd->sglist_len);
	else if (cmd->request_buffer != req->buffer)
		kfree(cmd->request_buffer);

	/*
	 * Zero these out.  They now point to freed memory, and it is
	 * dangerous to hang onto the pointers.
	 */
	cmd->buffer  = NULL;
	cmd->bufflen = 0;
	cmd->request_buffer = NULL;
	cmd->request_bufflen = 0;
}

/*
 * Function:    scsi_get_request_dev()
 *
 * Purpose:     Find the upper-level driver that is responsible for this
 *              request
 *
 * Arguments:   request   - I/O request we are preparing to queue.
 *
 * Lock status: No locks assumed to be held, but as it happens the
 *              q->queue_lock is held when this is called.
 *
 * Returns:     Nothing
 *
 * Notes:       The requests in the request queue may have originated
 *              from any block device driver.  We need to find out which
 *              one so that we can later form the appropriate command.
 */
static struct Scsi_Device_Template *scsi_get_request_dev(struct request *req)
{
	struct gendisk *p = req->rq_disk;
	return p ? *(struct Scsi_Device_Template **)p->private_data : NULL;
}

/*
 * Function:    scsi_io_completion()
 *
 * Purpose:     Completion processing for block device I/O requests.
 *
 * Arguments:   cmd   - command that is finished.
 *
 * Lock status: Assumed that no lock is held upon entry.
 *
 * Returns:     Nothing
 *
 * Notes:       This function is matched in terms of capabilities to
 *              the function that created the scatter-gather list.
 *              In other words, if there are no bounce buffers
 *              (the normal case for most drivers), we don't need
 *              the logic to deal with cleaning up afterwards.
 */
void scsi_io_completion(struct scsi_cmnd *cmd, int good_sectors,
			int block_sectors)
{
	int result = cmd->result;
	int this_count = cmd->bufflen >> 9;
	request_queue_t *q = cmd->device->request_queue;
	struct request *req = cmd->request;
	int clear_errors = 1;

	/*
	 * We must do one of several things here:
	 *
	 *	Call scsi_end_request.  This will finish off the specified
	 *	number of sectors.  If we are done, the command block will
	 *	be released, and the queue function will be goosed.  If we
	 *	are not done, then scsi_end_request will directly goose
	 *	the queue.
	 *
	 *	We can just use scsi_queue_next_request() here.  This
	 *	would be used if we just wanted to retry, for example.
	 *
	 */
	/*
	 * Free up any indirection buffers we allocated for DMA purposes. 
	 * For the case of a READ, we need to copy the data out of the
	 * bounce buffer and into the real buffer.
	 */
	if (cmd->use_sg)
		scsi_free_sgtable(cmd->buffer, cmd->sglist_len);
	else if (cmd->buffer != req->buffer) {
		if (rq_data_dir(req) == READ) {
			unsigned long flags;
			char *to = bio_kmap_irq(req->bio, &flags);
			memcpy(to, cmd->buffer, cmd->bufflen);
			bio_kunmap_irq(to, &flags);
		}
		kfree(cmd->buffer);
	}

	if (blk_pc_request(req)) { /* SG_IO ioctl from block level */
		req->errors = (driver_byte(result) & DRIVER_SENSE) ?
			      (CHECK_CONDITION << 1) : (result & 0xff);
		if (result) {
			clear_errors = 0;
			if (cmd->sense_buffer[0] & 0x70) {
				int len = 8 + cmd->sense_buffer[7];

				if (len > SCSI_SENSE_BUFFERSIZE)
					len = SCSI_SENSE_BUFFERSIZE;
				memcpy(req->sense, cmd->sense_buffer,  len);
				req->sense_len = len;
			}
		} else
			req->data_len -= cmd->bufflen;
	}

	/*
	 * Zero these out.  They now point to freed memory, and it is
	 * dangerous to hang onto the pointers.
	 */
	cmd->buffer  = NULL;
	cmd->bufflen = 0;
	cmd->request_buffer = NULL;
	cmd->request_bufflen = 0;

	/*
	 * Next deal with any sectors which we were able to correctly
	 * handle.
	 */
	if (good_sectors >= 0) {
		SCSI_LOG_HLCOMPLETE(1, printk("%ld sectors total, %d sectors done.\n",
					      req->nr_sectors, good_sectors));
		SCSI_LOG_HLCOMPLETE(1, printk("use_sg is %d\n", cmd->use_sg));

		if (clear_errors)
			req->errors = 0;
		/*
		 * If multiple sectors are requested in one buffer, then
		 * they will have been finished off by the first command.
		 * If not, then we have a multi-buffer command.
		 *
		 * If block_sectors != 0, it means we had a medium error
		 * of some sort, and that we want to mark some number of
		 * sectors as not uptodate.  Thus we want to inhibit
		 * requeueing right here - we will requeue down below
		 * when we handle the bad sectors.
		 */
		cmd = scsi_end_request(cmd, 1, good_sectors, result == 0);

		/*
		 * If the command completed without error, then either finish off the
		 * rest of the command, or start a new one.
		 */
		if (result == 0 || cmd == NULL ) {
			return;
		}
	}
	/*
	 * Now, if we were good little boys and girls, Santa left us a request
	 * sense buffer.  We can extract information from this, so we
	 * can choose a block to remap, etc.
	 */
	if (driver_byte(result) != 0) {
		if (suggestion(result) == SUGGEST_REMAP) {
#ifdef REMAP
			/*
			 * Not yet implemented.  A read will fail after being remapped,
			 * a write will call the strategy routine again.
			 */
			if (cmd->device->remap) {
				result = 0;
			}
#endif
		}
		if ((cmd->sense_buffer[0] & 0x7f) == 0x70) {
			/*
			 * If the device is in the process of becoming ready,
			 * retry.
			 */
			if (cmd->sense_buffer[12] == 0x04 &&
			    cmd->sense_buffer[13] == 0x01) {
				scsi_queue_next_request(q, cmd);
				return;
			}
			if ((cmd->sense_buffer[2] & 0xf) == UNIT_ATTENTION) {
				if (cmd->device->removable) {
					/* detected disc change.  set a bit 
					 * and quietly refuse further access.
		 			 */
					cmd->device->changed = 1;
					cmd = scsi_end_request(cmd, 0,
							this_count, 1);
					return;
				} else {
					/*
				 	* Must have been a power glitch, or a
				 	* bus reset.  Could not have been a
				 	* media change, so we just retry the
				 	* request and see what happens.  
				 	*/
					scsi_queue_next_request(q, cmd);
					return;
				}
			}
		}
		/* If we had an ILLEGAL REQUEST returned, then we may have
		 * performed an unsupported command.  The only thing this should be
		 * would be a ten byte read where only a six byte read was supported.
		 * Also, on a system where READ CAPACITY failed, we have have read
		 * past the end of the disk.
		 */

		switch (cmd->sense_buffer[2]) {
		case ILLEGAL_REQUEST:
			if (cmd->device->ten) {
				cmd->device->ten = 0;
				/*
				 * This will cause a retry with a 6-byte
				 * command.
				 */
				scsi_queue_next_request(q, cmd);
				result = 0;
			} else {
				cmd = scsi_end_request(cmd, 0, this_count, 1);
				return;
			}
			break;
		case NOT_READY:
			printk(KERN_INFO "Device %s not ready.\n",
			       req->rq_disk ? req->rq_disk->disk_name : "");
			cmd = scsi_end_request(cmd, 0, this_count, 1);
			return;
			break;
		case MEDIUM_ERROR:
		case VOLUME_OVERFLOW:
			printk("scsi%d: ERROR on channel %d, id %d, lun %d, CDB: ",
			       cmd->device->host->host_no, (int) cmd->device->channel,
			       (int) cmd->device->id, (int) cmd->device->lun);
			print_command(cmd->data_cmnd);
			print_sense("", cmd);
			cmd = scsi_end_request(cmd, 0, block_sectors, 1);
			return;
		default:
			break;
		}
	}			/* driver byte != 0 */
	if (host_byte(result) == DID_RESET) {
		/*
		 * Third party bus reset or reset for error
		 * recovery reasons.  Just retry the request
		 * and see what happens.  
		 */
		scsi_queue_next_request(q, cmd);
		return;
	}
	if (result) {
		struct Scsi_Device_Template *sdt;

		sdt = scsi_get_request_dev(cmd->request);
		printk("SCSI %s error : <%d %d %d %d> return code = 0x%x\n",
		       (sdt ? sdt->name : "device"),
		       cmd->device->host->host_no,
		       cmd->device->channel,
		       cmd->device->id,
		       cmd->device->lun, result);

		if (driver_byte(result) & DRIVER_SENSE)
			print_sense("", cmd);
		/*
		 * Mark a single buffer as not uptodate.  Queue the remainder.
		 * We sometimes get this cruft in the event that a medium error
		 * isn't properly reported.
		 */
		cmd = scsi_end_request(cmd, 0, req->current_nr_sectors, 1);
		return;
	}
}

/*
 * Function:    scsi_init_io()
 *
 * Purpose:     SCSI I/O initialize function.
 *
 * Arguments:   cmd   - Command descriptor we wish to initialize
 *
 * Returns:     0 on success
 *		BLKPREP_DEFER if the failure is retryable
 *		BLKPREP_KILL if the failure is fatal
 */
static int scsi_init_io(struct scsi_cmnd *cmd)
{
	struct request     *req = cmd->request;
	struct scatterlist *sgpnt;
	int		   count;

	/*
	 * if this is a rq->data based REQ_BLOCK_PC, setup for a non-sg xfer
	 */
	if ((req->flags & REQ_BLOCK_PC) && !req->bio) {
		cmd->request_bufflen = req->data_len;
		cmd->request_buffer = req->data;
		req->buffer = req->data;
		cmd->use_sg = 0;
		return 0;
	}

	/*
	 * we used to not use scatter-gather for single segment request,
	 * but now we do (it makes highmem I/O easier to support without
	 * kmapping pages)
	 */
	cmd->use_sg = req->nr_phys_segments;

	/*
	 * if sg table allocation fails, requeue request later.
	 */
	sgpnt = scsi_alloc_sgtable(cmd, GFP_ATOMIC);
	if (unlikely(!sgpnt)) {
		req->flags |= REQ_SPECIAL;
		return BLKPREP_DEFER;
	}

	cmd->request_buffer = (char *) sgpnt;
	cmd->request_bufflen = req->nr_sectors << 9;
	if (blk_pc_request(req))
		cmd->request_bufflen = req->data_len;
	req->buffer = NULL;

	/* 
	 * Next, walk the list, and fill in the addresses and sizes of
	 * each segment.
	 */
	count = blk_rq_map_sg(req->q, req, cmd->request_buffer);

	/*
	 * mapped well, send it off
	 */
	if (likely(count <= cmd->use_sg)) {
		cmd->use_sg = count;
		return 0;
	}

	printk(KERN_ERR "Incorrect number of segments after building list\n");
	printk(KERN_ERR "counted %d, received %d\n", count, cmd->use_sg);
	printk(KERN_ERR "req nr_sec %lu, cur_nr_sec %u\n", req->nr_sectors,
			req->current_nr_sectors);

	/* release the command and kill it */
	scsi_put_command(cmd);
	return BLKPREP_KILL;
}

static int scsi_prep_fn(struct request_queue *q, struct request *req)
{
	struct Scsi_Device_Template *sdt;
	struct scsi_device *sdev = q->queuedata;
	struct scsi_cmnd *cmd;

	/*
	 * Find the actual device driver associated with this command.
	 * The SPECIAL requests are things like character device or
	 * ioctls, which did not originate from ll_rw_blk.  Note that
	 * the special field is also used to indicate the cmd for
	 * the remainder of a partially fulfilled request that can 
	 * come up when there is a medium error.  We have to treat
	 * these two cases differently.  We differentiate by looking
	 * at request->cmd, as this tells us the real story.
	 */
	if (req->flags & REQ_SPECIAL) {
		struct scsi_request *sreq = req->special;

		if (sreq->sr_magic == SCSI_REQ_MAGIC) {
			cmd = scsi_get_command(sreq->sr_device, GFP_ATOMIC);
			if (unlikely(!cmd))
				goto defer;
			scsi_init_cmd_from_req(cmd, sreq);
		} else
			cmd = req->special;
	} else if (req->flags & (REQ_CMD | REQ_BLOCK_PC)) {
		/*
		 * Now try and find a command block that we can use.
		 */
		if (!req->special) {
			cmd = scsi_get_command(sdev, GFP_ATOMIC);
			if (unlikely(!cmd))
				goto defer;
		} else
			cmd = req->special;
		
		/* pull a tag out of the request if we have one */
		cmd->tag = req->tag;
	} else {
		blk_dump_rq_flags(req, "SCSI bad req");
		return BLKPREP_KILL;
	}
	
	/* note the overloading of req->special.  When the tag
	 * is active it always means cmd.  If the tag goes
	 * back for re-queueing, it may be reset */
	req->special = cmd;
	cmd->request = req;
	
	/*
	 * FIXME: drop the lock here because the functions below
	 * expect to be called without the queue lock held.  Also,
	 * previously, we dequeued the request before dropping the
	 * lock.  We hope REQ_STARTED prevents anything untoward from
	 * happening now.
	 */
	if (req->flags & (REQ_CMD | REQ_BLOCK_PC)) {
		int ret;

		/*
		 * This will do a couple of things:
		 *  1) Fill in the actual SCSI command.
		 *  2) Fill in any other upper-level specific fields
		 * (timeout).
		 *
		 * If this returns 0, it means that the request failed
		 * (reading past end of disk, reading offline device,
		 * etc).   This won't actually talk to the device, but
		 * some kinds of consistency checking may cause the	
		 * request to be rejected immediately.
		 */
		sdt = scsi_get_request_dev(req);
		BUG_ON(!sdt);

		/* 
		 * This sets up the scatter-gather table (allocating if
		 * required).
		 */
		ret = scsi_init_io(cmd);
		if (ret)	/* BLKPREP_KILL return also releases the command */
			return ret;
		
		/*
		 * Initialize the actual SCSI command for this request.
		 */
		if (unlikely(!sdt->init_command(cmd))) {
			scsi_release_buffers(cmd);
			scsi_put_command(cmd);
			return BLKPREP_KILL;
		}
	}

	/*
	 * The request is now prepped, no need to come back here
	 */
	req->flags |= REQ_DONTPREP;
	return BLKPREP_OK;

 defer:
	/* If we defer, the elv_next_request() returns NULL, but the
	 * queue must be restarted, so we plug here if no returning
	 * command will automatically do that. */
	if (sdev->device_busy == 0)
		blk_plug_device(q);
	return BLKPREP_DEFER;
}

/*
 * scsi_dev_queue_ready: if we can send requests to sdev, return 1 else
 * return 0.
 *
 * Called with the queue_lock held.
 */
static inline int scsi_dev_queue_ready(struct request_queue *q,
				  struct scsi_device *sdev)
{
	if (sdev->device_busy >= sdev->queue_depth)
		return 0;
	if (sdev->device_busy == 0 && sdev->device_blocked) {
		/*
		 * unblock after device_blocked iterates to zero
		 */
		if (--sdev->device_blocked == 0) {
			SCSI_LOG_MLQUEUE(3,
				printk("scsi%d (%d:%d) unblocking device at"
				       " zero depth\n", sdev->host->host_no,
				       sdev->id, sdev->lun));
		} else {
			blk_plug_device(q);
			return 0;
		}
	}
	if (sdev->device_blocked)
		return 0;

	return 1;
}

/*
 * scsi_host_queue_ready: if we can send requests to shost, return 1 else
 * return 0. We must end up running the queue again whenever 0 is
 * returned, else IO can hang.
 *
 * Called with host_lock held.
 */
static inline int scsi_host_queue_ready(struct request_queue *q,
				   struct Scsi_Host *shost,
				   struct scsi_device *sdev)
{
	if (shost->in_recovery)
		return 0;
	if (shost->host_busy == 0 && shost->host_blocked) {
		/*
		 * unblock after host_blocked iterates to zero
		 */
		if (--shost->host_blocked == 0) {
			SCSI_LOG_MLQUEUE(3,
				printk("scsi%d unblocking host at zero depth\n",
					shost->host_no));
		} else {
			blk_plug_device(q);
			return 0;
		}
	}
	if (!list_empty(&sdev->starved_entry))
		return 0;
	if ((shost->can_queue > 0 && shost->host_busy >= shost->can_queue) ||
	    shost->host_blocked || shost->host_self_blocked) {
		list_add_tail(&sdev->starved_entry, &shost->starved_list);
		return 0;
	}

	return 1;
}

/*
 * Function:    scsi_request_fn()
 *
 * Purpose:     Main strategy routine for SCSI.
 *
 * Arguments:   q       - Pointer to actual queue.
 *
 * Returns:     Nothing
 *
 * Lock status: IO request lock assumed to be held when called.
 */
static void scsi_request_fn(request_queue_t *q)
{
	struct scsi_device *sdev = q->queuedata;
	struct Scsi_Host *shost = sdev->host;
	struct scsi_cmnd *cmd;
	struct request *req;
	unsigned long flags;

	/*
	 * To start with, we keep looping until the queue is empty, or until
	 * the host is no longer able to accept any more requests.
	 */
	for (;;) {
		if (blk_queue_plugged(q))
			goto completed;

		/*
		 * get next queueable request.  We do this early to make sure
		 * that the request is fully prepared even if we cannot 
		 * accept it.
		 */
		req = elv_next_request(q);

		if (!req)
			goto completed;

		if (!scsi_dev_queue_ready(q, sdev))
			goto completed;

		/*
		 * Remove the request from the request list.
		 */
		if (!(blk_queue_tagged(q) && (blk_queue_start_tag(q, req) == 0)))
			blkdev_dequeue_request(req);

		sdev->device_busy++;
		spin_unlock_irq(q->queue_lock);

		spin_lock_irqsave(shost->host_lock, flags);
		if (!scsi_host_queue_ready(q, shost, sdev))
			goto host_lock_held;

		if (sdev->single_lun) {
			if (sdev->sdev_target->starget_sdev_user &&
			    (sdev->sdev_target->starget_sdev_user != sdev))
				goto host_lock_held;
			else
				sdev->sdev_target->starget_sdev_user = sdev;
		}

		shost->host_busy++;
		spin_unlock_irqrestore(shost->host_lock, flags);

		cmd = req->special;

		/*
		 * Should be impossible for a correctly prepared request
		 * please mail the stack trace to linux-scsi@vger.kernel.org
		 */
		BUG_ON(!cmd);

		/*
		 * Finally, initialize any error handling parameters, and set up
		 * the timers for timeouts.
		 */
		scsi_init_cmd_errh(cmd);

		/*
		 * Dispatch the command to the low-level driver.
		 */
		scsi_dispatch_cmd(cmd);

		/*
		 * Now we need to grab the lock again.  We are about to mess
		 * with the request queue and try to find another command.
		 */
		spin_lock_irq(q->queue_lock);
	}
completed:
	return;

host_lock_held:
	spin_unlock_irqrestore(shost->host_lock, flags);
	/*
	 * lock q, handle tag, requeue req, and decrement device_busy. We
	 * must return with queue_lock held.
	 *
	 * Decrementing device_busy without checking it is OK, as all such
	 * cases (host limits or settings) should run the queue at some
	 * later time.
	 */
	spin_lock_irq(q->queue_lock);
	if (blk_rq_tagged(req))
		blk_queue_end_tag(q, req);
	__elv_add_request(q, req, 0, 0);
	sdev->device_busy--;
}

u64 scsi_calculate_bounce_limit(struct Scsi_Host *shost)
{
	if (shost->highmem_io) {
		struct device *host_dev = scsi_get_device(shost);

		if (PCI_DMA_BUS_IS_PHYS && host_dev && host_dev->dma_mask)
			return *host_dev->dma_mask;

		/*
		 * Platforms with virtual-DMA translation
 		 * hardware have no practical limit.
		 */
		return BLK_BOUNCE_ANY;
	} else if (shost->unchecked_isa_dma)
		return BLK_BOUNCE_ISA;

	return BLK_BOUNCE_HIGH;
}

request_queue_t *scsi_alloc_queue(struct scsi_device *sdev)
{
	request_queue_t *q;
	struct Scsi_Host *shost;

	q = kmalloc(sizeof(*q), GFP_ATOMIC);
	if (!q)
		return NULL;
	memset(q, 0, sizeof(*q));

	/*
	 * XXX move host code to scsi_register
	 */
	shost = sdev->host;
	if (!shost->max_sectors) {
		/*
		 * Driver imposes no hard sector transfer limit.
		 * start at machine infinity initially.
		 */
		shost->max_sectors = SCSI_DEFAULT_MAX_SECTORS;
	}

	blk_init_queue(q, scsi_request_fn, &sdev->sdev_lock);
	blk_queue_prep_rq(q, scsi_prep_fn);

	blk_queue_max_hw_segments(q, shost->sg_tablesize);
	blk_queue_max_phys_segments(q, MAX_PHYS_SEGMENTS);
	blk_queue_max_sectors(q, shost->max_sectors);
	blk_queue_bounce_limit(q, scsi_calculate_bounce_limit(shost));

	if (!shost->use_clustering)
		clear_bit(QUEUE_FLAG_CLUSTER, &q->queue_flags);

	return q;
}

void scsi_free_queue(request_queue_t *q)
{
	blk_cleanup_queue(q);
	kfree(q);
}

/*
 * Function:    scsi_block_requests()
 *
 * Purpose:     Utility function used by low-level drivers to prevent further
 *		commands from being queued to the device.
 *
 * Arguments:   shost       - Host in question
 *
 * Returns:     Nothing
 *
 * Lock status: No locks are assumed held.
 *
 * Notes:       There is no timer nor any other means by which the requests
 *		get unblocked other than the low-level driver calling
 *		scsi_unblock_requests().
 */
void scsi_block_requests(struct Scsi_Host *shost)
{
	shost->host_self_blocked = 1;
}

/*
 * Function:    scsi_unblock_requests()
 *
 * Purpose:     Utility function used by low-level drivers to allow further
 *		commands from being queued to the device.
 *
 * Arguments:   shost       - Host in question
 *
 * Returns:     Nothing
 *
 * Lock status: No locks are assumed held.
 *
 * Notes:       There is no timer nor any other means by which the requests
 *		get unblocked other than the low-level driver calling
 *		scsi_unblock_requests().
 *
 *		This is done as an API function so that changes to the
 *		internals of the scsi mid-layer won't require wholesale
 *		changes to drivers that use this feature.
 */
void scsi_unblock_requests(struct Scsi_Host *shost)
{
	struct scsi_device *sdev;

	shost->host_self_blocked = 0;

	/*
	 * Now that we are unblocked, try to start the queues.
	 */
	list_for_each_entry(sdev, &shost->my_devices, siblings)
		scsi_queue_next_request(sdev->request_queue, NULL);
}

/*
 * Function:    scsi_report_bus_reset()
 *
 * Purpose:     Utility function used by low-level drivers to report that
 *		they have observed a bus reset on the bus being handled.
 *
 * Arguments:   shost       - Host in question
 *		channel     - channel on which reset was observed.
 *
 * Returns:     Nothing
 *
 * Lock status: No locks are assumed held.
 *
 * Notes:       This only needs to be called if the reset is one which
 *		originates from an unknown location.  Resets originated
 *		by the mid-level itself don't need to call this, but there
 *		should be no harm.
 *
 *		The main purpose of this is to make sure that a CHECK_CONDITION
 *		is properly treated.
 */
void scsi_report_bus_reset(struct Scsi_Host *shost, int channel)
{
	struct scsi_device *sdev;

	list_for_each_entry(sdev, &shost->my_devices, siblings) {
		if (channel == sdev->channel) {
			sdev->was_reset = 1;
			sdev->expecting_cc_ua = 1;
		}
	}
}

int __init scsi_init_queue(void)
{
	int i;

	for (i = 0; i < SG_MEMPOOL_NR; i++) {
		struct scsi_host_sg_pool *sgp = scsi_sg_pools + i;
		int size = sgp->size * sizeof(struct scatterlist);

		sgp->slab = kmem_cache_create(sgp->name, size, 0,
				SLAB_HWCACHE_ALIGN, NULL, NULL);
		if (!sgp->slab) {
			printk(KERN_ERR "SCSI: can't init sg slab %s\n",
					sgp->name);
		}

		sgp->pool = mempool_create(SG_MEMPOOL_SIZE,
				mempool_alloc_slab, mempool_free_slab,
				sgp->slab);
		if (!sgp->pool) {
			printk(KERN_ERR "SCSI: can't init sg mempool %s\n",
					sgp->name);
		}
	}

	return 0;
}

void scsi_exit_queue(void)
{
	int i;

	for (i = 0; i < SG_MEMPOOL_NR; i++) {
		struct scsi_host_sg_pool *sgp = scsi_sg_pools + i;
		mempool_destroy(sgp->pool);
		kmem_cache_destroy(sgp->slab);
	}
}
