/*
 *  scsi_error.c Copyright (C) 1997 Eric Youngdale
 *
 *  SCSI error/timeout handling
 *      Initial versions: Eric Youngdale.  Based upon conversations with
 *                        Leonard Zubkoff and David Miller at Linux Expo, 
 *                        ideas originating from all over the place.
 *
 *	Restructured scsi_unjam_host and associated functions.
 *	September 04, 2002 Mike Anderson (andmike@us.ibm.com)
 */

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
#include <linux/smp_lock.h>

#define __KERNEL_SYSCALLS__

#include <linux/unistd.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/dma.h>

#include "scsi.h"
#include "hosts.h"

/*
 * We must always allow SHUTDOWN_SIGS.  Even if we are not a module,
 * the host drivers that we are using may be loaded as modules, and
 * when we unload these,  we need to ensure that the error handler thread
 * can be shut down.
 *
 * Note - when we unload a module, we send a SIGHUP.  We mustn't
 * enable SIGTERM, as this is how the init shuts things down when you
 * go to single-user mode.  For that matter, init also sends SIGKILL,
 * so we mustn't enable that one either.  We use SIGHUP instead.  Other
 * options would be SIGPWR, I suppose.
 */
#define SHUTDOWN_SIGS	(sigmask(SIGHUP))

#ifdef DEBUG
#define SENSE_TIMEOUT SCSI_TIMEOUT
#else
#define SENSE_TIMEOUT (10*HZ)
#endif

/*
 * These should *probably* be handled by the host itself.
 * Since it is allowed to sleep, it probably should.
 */
#define BUS_RESET_SETTLE_TIME   5*HZ
#define HOST_RESET_SETTLE_TIME  10*HZ

/**
 * scsi_add_timer - Start timeout timer for a single scsi command.
 * @scmd:	scsi command that is about to start running.
 * @timeout:	amount of time to allow this command to run.
 * @complete:	timeout function to call if timer isn't canceled.
 *
 * Notes:
 *    This should be turned into an inline function.  Each scsi command
 *    has it's own timer, and as it is added to the queue, we set up the
 *    timer.  When the command completes, we cancel the timer.  Pretty
 *    simple, really, especially compared to the old way of handling this
 *    crap.
 **/
void scsi_add_timer(Scsi_Cmnd *scmd, int timeout, void (*complete)
		    (Scsi_Cmnd *))
{

	/*
	 * If the clock was already running for this command, then
	 * first delete the timer.  The timer handling code gets rather
	 * confused if we don't do this.
	 */
	if (scmd->eh_timeout.function != NULL) {
		del_timer(&scmd->eh_timeout);
	}
	scmd->eh_timeout.data = (unsigned long) scmd;
	scmd->eh_timeout.expires = jiffies + timeout;
	scmd->eh_timeout.function = (void (*)(unsigned long)) complete;

	SCSI_LOG_ERROR_RECOVERY(5, printk("Adding timer for command %p at"
					  "%d (%p)\n", scmd, timeout,
					  complete));

	add_timer(&scmd->eh_timeout);

}

/**
 * scsi_delete_timer - Delete/cancel timer for a given function.
 * @scmd:	Cmd that we are canceling timer for
 *
 * Notes:
 *     This should be turned into an inline function.
 *
 * Return value:
 *     1 if we were able to detach the timer.  0 if we blew it, and the
 *     timer function has already started to run.
 **/
int scsi_delete_timer(Scsi_Cmnd *scmd)
{
	int rtn;

	rtn = del_timer(&scmd->eh_timeout);

	SCSI_LOG_ERROR_RECOVERY(5, printk("Clearing timer for command %p"
					 " %d\n", scmd, rtn));

	scmd->eh_timeout.data = (unsigned long) NULL;
	scmd->eh_timeout.function = NULL;

	return rtn;
}

/**
 * scsi_times_out - Timeout function for normal scsi commands.
 * @scmd:	Cmd that is timing out.
 *
 * Notes:
 *     We do not need to lock this.  There is the potential for a race
 *     only in that the normal completion handling might run, but if the
 *     normal completion function determines that the timer has already
 *     fired, then it mustn't do anything.
 **/
void scsi_times_out(Scsi_Cmnd *scmd)
{
	/* Set the serial_number_at_timeout to the current serial_number */
	scmd->serial_number_at_timeout = scmd->serial_number;

	scsi_eh_eflags_set(scmd, SCSI_EH_CMD_TIMEOUT | SCSI_EH_CMD_ERR);

	if( scmd->host->eh_wait == NULL ) {
		panic("Error handler thread not present at %p %p %s %d",
		      scmd, scmd->host, __FILE__, __LINE__);
	}

	scsi_host_failed_inc_and_test(scmd->host);

	SCSI_LOG_TIMEOUT(3, printk("Command timed out active=%d busy=%d "
				   "failed=%d\n",
				   atomic_read(&scmd->host->host_active),
				   scmd->host->host_busy,
				   scmd->host->host_failed));
}

/**
 * scsi_block_when_processing_errors - Prevent cmds from being queued.
 * @sdev:	Device on which we are performing recovery.
 *
 * Description:
 *     We block until the host is out of error recovery, and then check to
 *     see whether the host or the device is offline.
 *
 * Return value:
 *     FALSE when dev was taken offline by error recovery. TRUE OK to
 *     proceed.
 **/
int scsi_block_when_processing_errors(Scsi_Device *sdev)
{

	SCSI_SLEEP(&sdev->host->host_wait, sdev->host->in_recovery);

	SCSI_LOG_ERROR_RECOVERY(5, printk("Open returning %d\n",
					  sdev->online));

	return sdev->online;
}

#if CONFIG_SCSI_LOGGING
/**
 * scsi_eh_prt_fail_stats - Log info on failures.
 * @sc_list:	List for failed cmds.
 * @shost:	scsi host being recovered.
 **/
static void scsi_eh_prt_fail_stats(Scsi_Cmnd *sc_list, struct Scsi_Host *shost)
{
	Scsi_Cmnd *scmd;
	Scsi_Device *sdev;
	int total_failures = 0;
	int cmd_failed = 0;
	int cmd_timed_out = 0;
	int devices_failed = 0;


	for (sdev = shost->host_queue; sdev; sdev = sdev->next) {
		for (scmd = sc_list; scmd; scmd = scmd->bh_next) {
			if (scmd->device == sdev) {
				++total_failures;
				if (scsi_eh_eflags_chk(scmd,
						       SCSI_EH_CMD_TIMEOUT))
					++cmd_timed_out;
				else
					++cmd_failed;
			}
		}

		if (cmd_timed_out || cmd_failed) {
			SCSI_LOG_ERROR_RECOVERY(3,
				printk("scsi_eh: %d:%d:%d:%d cmds failed: %d,"
				       "timedout: %d\n",
				       shost->host_no, sdev->channel,
				       sdev->id, sdev->lun,
				       cmd_failed, cmd_timed_out));
			cmd_timed_out = 0;
			cmd_failed = 0;
			++devices_failed;
		}
	}

	SCSI_LOG_ERROR_RECOVERY(2, printk("Total of %d commands on %d "
					  "devices require eh work\n",
				  total_failures, devices_failed));
}
#endif

/**
 * scsi_eh_get_failed - Gather failed cmds.
 * @sc_list:	A pointer to a list for failed cmds.
 * @shost:	Scsi host being recovered.
 *
 * XXX Add opaque interator for device / shost. Investigate direct
 * addition to per eh list on error allowing skipping of this step.
 **/
static void scsi_eh_get_failed(Scsi_Cmnd **sc_list, struct Scsi_Host *shost)
{
	int found;
	Scsi_Device *sdev;
	Scsi_Cmnd *scmd;

	for (found = 0, sdev = shost->host_queue; sdev; sdev = sdev->next) {
		for (scmd = sdev->device_queue; scmd; scmd = scmd->next) {
			if (scsi_eh_eflags_chk(scmd, SCSI_EH_CMD_ERR)) {
				scmd->bh_next = *sc_list;
				*sc_list = scmd;
				found++;
			} else {
				/*
				 * FIXME Verify how this can happen and if
				 * this is still needed??
				 */
			    if (scmd->state != SCSI_STATE_INITIALIZING
			    && scmd->state != SCSI_STATE_UNUSED) {
				/*
				 * Rats.  Something is still floating
				 * around out there This could be the
				 * result of the fact that the upper level
				 * drivers are still frobbing commands
				 * that might have succeeded.  There are
				 * two outcomes. One is that the command
				 * block will eventually be freed, and the
				 * other one is that the command will be
				 * queued and will be finished along the
				 * way.
				 */
				SCSI_LOG_ERROR_RECOVERY(1, printk("Error hdlr "
							  "prematurely woken "
							  "cmds still active "
							  "(%p %x %d)\n",
					       scmd, scmd->state,
					       scmd->target));
				}
			}
		}
	}

	SCSI_LOG_ERROR_RECOVERY(1, scsi_eh_prt_fail_stats(*sc_list, shost));

	BUG_ON(shost->host_failed != found);
}

/**
 * scsi_check_sense - Examine scsi cmd sense
 * @scmd:	Cmd to have sense checked.
 **/
static int scsi_check_sense(Scsi_Cmnd *scmd)
{
	if (!SCSI_SENSE_VALID(scmd)) {
		return FAILED;
	}
	if (scmd->sense_buffer[2] & 0xe0)
		return SUCCESS;

	switch (scmd->sense_buffer[2] & 0xf) {
	case NO_SENSE:
		return SUCCESS;
	case RECOVERED_ERROR:
		return /* soft_error */ SUCCESS;

	case ABORTED_COMMAND:
		return NEEDS_RETRY;
	case NOT_READY:
	case UNIT_ATTENTION:
		/*
		 * if we are expecting a cc/ua because of a bus reset that we
		 * performed, treat this just as a retry.  otherwise this is
		 * information that we should pass up to the upper-level driver
		 * so that we can deal with it there.
		 */
		if (scmd->device->expecting_cc_ua) {
			scmd->device->expecting_cc_ua = 0;
			return NEEDS_RETRY;
		}
		/*
		 * if the device is in the process of becoming ready, we 
		 * should retry.
		 */
		if ((scmd->sense_buffer[12] == 0x04) &&
			(scmd->sense_buffer[13] == 0x01)) {
			return NEEDS_RETRY;
		}
		return SUCCESS;

		/* these three are not supported */
	case COPY_ABORTED:
	case VOLUME_OVERFLOW:
	case MISCOMPARE:
		return SUCCESS;

	case MEDIUM_ERROR:
		return NEEDS_RETRY;

	case ILLEGAL_REQUEST:
	case BLANK_CHECK:
	case DATA_PROTECT:
	case HARDWARE_ERROR:
	default:
		return SUCCESS;
	}
}

/**
 * scsi_eh_completed_normally - Disposition a eh cmd on return from LLD.
 * @scmd:	SCSI cmd to examine.
 *
 * Notes:
 *    This is *only* called when we are examining the status of commands
 *    queued during error recovery.  the main difference here is that we
 *    don't allow for the possibility of retries here, and we are a lot
 *    more restrictive about what we consider acceptable.
 **/
static int scsi_eh_completed_normally(Scsi_Cmnd *scmd)
{
	int rtn;

	/*
	 * first check the host byte, to see if there is anything in there
	 * that would indicate what we need to do.
	 */
	if (host_byte(scmd->result) == DID_RESET) {
		if (scmd->flags & IS_RESETTING) {
			/*
			 * ok, this is normal.  we don't know whether in fact
			 * the command in question really needs to be rerun
			 * or not - if this was the original data command then
			 * the answer is yes, otherwise we just flag it as
			 * SUCCESS.
			 */
			scmd->flags &= ~IS_RESETTING;
			goto maybe_retry;
		}
		/*
		 * rats.  we are already in the error handler, so we now
		 * get to try and figure out what to do next.  if the sense
		 * is valid, we have a pretty good idea of what to do.
		 * if not, we mark it as FAILED.
		 */
		rtn = scsi_check_sense(scmd);
		if (rtn == NEEDS_RETRY)
			goto maybe_retry;
		return rtn;
	}
	if (host_byte(scmd->result) != DID_OK) {
		return FAILED;
	}
	/*
	 * next, check the message byte.
	 */
	if (msg_byte(scmd->result) != COMMAND_COMPLETE) {
		return FAILED;
	}
	/*
	 * now, check the status byte to see if this indicates
	 * anything special.
	 */
	switch (status_byte(scmd->result)) {
	case GOOD:
	case COMMAND_TERMINATED:
		return SUCCESS;
	case CHECK_CONDITION:
		rtn = scsi_check_sense(scmd);
		if (rtn == NEEDS_RETRY)
			goto maybe_retry;
		return rtn;
	case CONDITION_GOOD:
	case INTERMEDIATE_GOOD:
	case INTERMEDIATE_C_GOOD:
		/*
		 * who knows?  FIXME(eric)
		 */
		return SUCCESS;
	case BUSY:
	case QUEUE_FULL:
	case RESERVATION_CONFLICT:
	default:
		return FAILED;
	}
	return FAILED;

 maybe_retry:
	if ((++scmd->retries) < scmd->allowed) {
		return NEEDS_RETRY;
	} else {
		/* no more retries - report this one back to upper level */
		return SUCCESS;
	}
}

/**
 * scsi_eh_times_out - timeout function for error handling.
 * @scmd:	Cmd that is timing out.
 *
 * Notes:
 *    During error handling, the kernel thread will be sleeping waiting
 *    for some action to complete on the device.  our only job is to
 *    record that it timed out, and to wake up the thread.
 **/
static void scsi_eh_times_out(Scsi_Cmnd *scmd)
{
	scsi_eh_eflags_set(scmd, SCSI_EH_REC_TIMEOUT);
	SCSI_LOG_ERROR_RECOVERY(3, printk("in scsi_eh_times_out %p\n", scmd));

	if (scmd->host->eh_action != NULL)
		up(scmd->host->eh_action);
	else
		printk("missing scsi error handler thread\n");
}

/**
 * scsi_eh_done - Completion function for error handling.
 * @scmd:	Cmd that is done.
 **/
static void scsi_eh_done(Scsi_Cmnd *scmd)
{
	int     rtn;

	/*
	 * if the timeout handler is already running, then just set the
	 * flag which says we finished late, and return.  we have no
	 * way of stopping the timeout handler from running, so we must
	 * always defer to it.
	 */
	rtn = del_timer(&scmd->eh_timeout);
	if (!rtn) {
		return;
	}

	scmd->request->rq_status = RQ_SCSI_DONE;

	scmd->owner = SCSI_OWNER_ERROR_HANDLER;

	SCSI_LOG_ERROR_RECOVERY(3, printk("in eh_done %p result:%x\n", scmd,
					  scmd->result));

	if (scmd->host->eh_action != NULL)
		up(scmd->host->eh_action);
}

/**
 * scsi_send_eh_cmnd  - send a cmd to a device as part of error recovery.
 * @scmd:	SCSI Cmd to send.
 * @timeout:	Timeout for cmd.
 *
 * Notes:
 *    The initialization of the structures is quite a bit different in
 *    this case, and furthermore, there is a different completion handler
 *    vs scsi_dispatch_cmd.
 * Return value:
 *    SUCCESS/FAILED
 **/
static int scsi_send_eh_cmnd(Scsi_Cmnd *scmd, int timeout)
{
	unsigned long flags;
	struct Scsi_Host *host = scmd->host;
	int rtn = SUCCESS;

	ASSERT_LOCK(host->host_lock, 0);

retry:
	/*
	 * we will use a queued command if possible, otherwise we will
	 * emulate the queuing and calling of completion function ourselves.
	 */
	scmd->owner = SCSI_OWNER_LOWLEVEL;

	if (host->can_queue) {
		DECLARE_MUTEX_LOCKED(sem);

		scsi_add_timer(scmd, timeout, scsi_eh_times_out);

		/*
		 * set up the semaphore so we wait for the command to complete.
		 */
		scmd->host->eh_action = &sem;
		scmd->request->rq_status = RQ_SCSI_BUSY;

		spin_lock_irqsave(scmd->host->host_lock, flags);
		host->hostt->queuecommand(scmd, scsi_eh_done);
		spin_unlock_irqrestore(scmd->host->host_lock, flags);

		down(&sem);

		scmd->host->eh_action = NULL;

		/*
		 * see if timeout.  if so, tell the host to forget about it.
		 * in other words, we don't want a callback any more.
		 */
		if (scsi_eh_eflags_chk(scmd, SCSI_EH_REC_TIMEOUT)) {
			scsi_eh_eflags_clr(scmd,  SCSI_EH_REC_TIMEOUT);
                        scmd->owner = SCSI_OWNER_LOWLEVEL;

			/*
			 * as far as the low level driver is
			 * concerned, this command is still active, so
			 * we must give the low level driver a chance
			 * to abort it. (db) 
			 *
			 * FIXME(eric) - we are not tracking whether we could
			 * abort a timed out command or not.  not sure how
			 * we should treat them differently anyways.
			 */
			spin_lock_irqsave(scmd->host->host_lock, flags);
			if (scmd->host->hostt->eh_abort_handler)
				scmd->host->hostt->eh_abort_handler(scmd);
			spin_unlock_irqrestore(scmd->host->host_lock, flags);
			
			scmd->request->rq_status = RQ_SCSI_DONE;
			scmd->owner = SCSI_OWNER_ERROR_HANDLER;
			
			rtn = FAILED;
		}
		SCSI_LOG_ERROR_RECOVERY(3, printk("%s: %p rtn:%x\n",
						  __FUNCTION__, scmd,
						  rtn));
	} else {
		int temp;

		/*
		 * we damn well had better never use this code.  there is no
		 * timeout protection here, since we would end up waiting in
		 * the actual low level driver, we don't know how to wake it up.
		 */
		spin_lock_irqsave(host->host_lock, flags);
		temp = host->hostt->command(scmd);
		spin_unlock_irqrestore(host->host_lock, flags);

		scmd->result = temp;
		/* fall through to code below to examine status. */
	}

	/*
	 * now examine the actual status codes to see whether the command
	 * actually did complete normally.
	 */
	if (rtn == SUCCESS) {
		int ret = scsi_eh_completed_normally(scmd);
		SCSI_LOG_ERROR_RECOVERY(3,
			printk("%s: scsi_eh_completed_normally %x\n",
			       __FUNCTION__, ret));
		switch (ret) {
		case SUCCESS:
			break;
		case NEEDS_RETRY:
			goto retry;
		case FAILED:
		default:
			rtn = FAILED;
			break;
		}
	}

	return rtn;
}

/**
 * scsi_request_sense - Request sense data from a particular target.
 * @scmd:	SCSI cmd for request sense.
 *
 * Notes:
 *    Some hosts automatically obtain this information, others require
 *    that we obtain it on our own. This function will *not* return until
 *    the command either times out, or it completes.
 **/
static int scsi_request_sense(Scsi_Cmnd *scmd)
{
	static unsigned char generic_sense[6] =
	{REQUEST_SENSE, 0, 0, 0, 255, 0};
	unsigned char scsi_result0[256], *scsi_result = NULL;
	int saved_result;
	int rtn;

	memcpy((void *) scmd->cmnd, (void *) generic_sense,
	       sizeof(generic_sense));

	if (scmd->device->scsi_level <= SCSI_2)
		scmd->cmnd[1] = scmd->lun << 5;

	scsi_result = (!scmd->host->hostt->unchecked_isa_dma)
	    ? &scsi_result0[0] : kmalloc(512, GFP_ATOMIC | GFP_DMA);

	if (scsi_result == NULL) {
		printk("cannot allocate scsi_result in scsi_request_sense.\n");
		return FAILED;
	}
	/*
	 * zero the sense buffer.  some host adapters automatically always
	 * request sense, so it is not a good idea that
	 * scmd->request_buffer and scmd->sense_buffer point to the same
	 * address (db).  0 is not a valid sense code. 
	 */
	memset((void *) scmd->sense_buffer, 0, sizeof(scmd->sense_buffer));
	memset((void *) scsi_result, 0, 256);

	saved_result = scmd->result;
	scmd->request_buffer = scsi_result;
	scmd->request_bufflen = 256;
	scmd->use_sg = 0;
	scmd->cmd_len = COMMAND_SIZE(scmd->cmnd[0]);
	scmd->sc_data_direction = SCSI_DATA_READ;
	scmd->underflow = 0;

	rtn = scsi_send_eh_cmnd(scmd, SENSE_TIMEOUT);

	/* last chance to have valid sense data */
	if (!SCSI_SENSE_VALID(scmd))
		memcpy((void *) scmd->sense_buffer,
		       scmd->request_buffer,
		       sizeof(scmd->sense_buffer));

	if (scsi_result != &scsi_result0[0] && scsi_result != NULL)
		kfree(scsi_result);

	/*
	 * when we eventually call scsi_finish, we really wish to complete
	 * the original request, so let's restore the original data. (db)
	 */
	memcpy((void *) scmd->cmnd, (void *) scmd->data_cmnd,
	       sizeof(scmd->data_cmnd));
	scmd->result = saved_result;
	scmd->request_buffer = scmd->buffer;
	scmd->request_bufflen = scmd->bufflen;
	scmd->use_sg = scmd->old_use_sg;
	scmd->cmd_len = scmd->old_cmd_len;
	scmd->sc_data_direction = scmd->sc_old_data_direction;
	scmd->underflow = scmd->old_underflow;

	/*
	 * hey, we are done.  let's look to see what happened.
	 */
	return rtn;
}

/**
 * scsi_eh_retry_cmd - Retry the original command
 * @scmd:	Original failed SCSI cmd.
 *
 * Notes:
 *    This function will *not* return until the command either times out,
 *    or it completes.
 **/
static int scsi_eh_retry_cmd(Scsi_Cmnd *scmd)
{
	memcpy((void *) scmd->cmnd, (void *) scmd->data_cmnd,
	       sizeof(scmd->data_cmnd));
	scmd->request_buffer = scmd->buffer;
	scmd->request_bufflen = scmd->bufflen;
	scmd->use_sg = scmd->old_use_sg;
	scmd->cmd_len = scmd->old_cmd_len;
	scmd->sc_data_direction = scmd->sc_old_data_direction;
	scmd->underflow = scmd->old_underflow;

	return scsi_send_eh_cmnd(scmd, scmd->timeout_per_command);
}

/**
 * scsi_eh_finish_cmd - Handle a cmd that eh is finished with.
 * @scmd:	Original SCSI cmd that eh has finished.
 * @shost:	SCSI host that cmd originally failed on.
 *
 * Notes:
 *    We don't want to use the normal command completion while we are are
 *    still handling errors - it may cause other commands to be queued,
 *    and that would disturb what we are doing.  thus we really want to
 *    keep a list of pending commands for final completion, and once we
 *    are ready to leave error handling we handle completion for real.
 **/
static void scsi_eh_finish_cmd(Scsi_Cmnd *scmd, struct Scsi_Host *shost)
{
	shost->host_failed--;
	scmd->state = SCSI_STATE_BHQUEUE;
	scsi_eh_eflags_clr_all(scmd);

	/*
	 * set this back so that the upper level can correctly free up
	 * things.
	 */
	scmd->use_sg = scmd->old_use_sg;
	scmd->sc_data_direction = scmd->sc_old_data_direction;
	scmd->underflow = scmd->old_underflow;
}

/**
 * scsi_eh_get_sense - Get device sense data.
 * @sc_todo:	list of cmds that have failed.
 * @shost:	scsi host being recovered.
 *
 * Description:
 *    See if we need to request sense information.  if so, then get it
 *    now, so we have a better idea of what to do.  
 *
 *
 * Notes:
 *    This has the unfortunate side effect that if a shost adapter does
 *    not automatically request sense information, that we end up shutting
 *    it down before we request it.  All shosts should be doing this
 *    anyways, so for now all I have to say is tough noogies if you end up
 *    in here.  On second thought, this is probably a good idea.  We
 *    *really* want to give authors an incentive to automatically request
 *    this.
 *
 *    In 2.5 this capability will be going away.
 **/
static int scsi_eh_get_sense(Scsi_Cmnd *sc_todo, struct Scsi_Host *shost)
{
	int rtn;
	Scsi_Cmnd *scmd;

	SCSI_LOG_ERROR_RECOVERY(3, printk("%s: checking to see if we need"
					  " to request sense\n",
					  __FUNCTION__));

	for (scmd = sc_todo; scmd; scmd = scmd->bh_next) {
		if (!scsi_eh_eflags_chk(scmd, SCSI_EH_CMD_FAILED) ||
		    SCSI_SENSE_VALID(scmd))
			continue;

		SCSI_LOG_ERROR_RECOVERY(2, printk("%s: requesting sense"
						  "for %d\n", __FUNCTION__,
						  scmd->target));
		rtn = scsi_request_sense(scmd);
		if (rtn != SUCCESS)
			continue;

		SCSI_LOG_ERROR_RECOVERY(3, printk("sense requested for %p"
						  "- result %x\n", scmd,
						  scmd->result));
		SCSI_LOG_ERROR_RECOVERY(3, print_sense("bh", scmd));

		rtn = scsi_decide_disposition(scmd);

		/*
		 * if the result was normal, then just pass it along to the
		 * upper level.
		 */
		if (rtn == SUCCESS)
			scsi_eh_finish_cmd(scmd, shost);
		if (rtn != NEEDS_RETRY)
			continue;

		/*
		 * we only come in here if we want to retry a
		 * command.  the test to see whether the command
		 * should be retried should be keeping track of the
		 * number of tries, so we don't end up looping, of
		 * course.
		 */
		scmd->state = NEEDS_RETRY;
		rtn = scsi_eh_retry_cmd(scmd);
		if (rtn != SUCCESS)
			continue;

		/*
		 * we eventually hand this one back to the top level.
		 */
		scsi_eh_finish_cmd(scmd, shost);
	}

	return shost->host_failed;
}

/**
 * scsi_try_to_abort_cmd - Ask host to abort a running command.
 * @scmd:	SCSI cmd to abort from Lower Level.
 *
 * Notes:
 *    This function will not return until the user's completion function
 *    has been called.  there is no timeout on this operation.  if the
 *    author of the low-level driver wishes this operation to be timed,
 *    they can provide this facility themselves.  helper functions in
 *    scsi_error.c can be supplied to make this easier to do.
 **/
static int scsi_try_to_abort_cmd(Scsi_Cmnd *scmd)
{
	int rtn = FAILED;
	unsigned long flags;

	if (scmd->host->hostt->eh_abort_handler == NULL) {
		return rtn;
	}
	/*
	 * scsi_done was called just after the command timed out and before
	 * we had a chance to process it. (db)
	 */
	if (scmd->serial_number == 0)
		return SUCCESS;

	scmd->owner = SCSI_OWNER_LOWLEVEL;

	spin_lock_irqsave(scmd->host->host_lock, flags);
	rtn = scmd->host->hostt->eh_abort_handler(scmd);
	spin_unlock_irqrestore(scmd->host->host_lock, flags);
	return rtn;
}

/**
 * scsi_eh_tur - Send TUR to device.
 * @scmd:	Scsi cmd to send TUR
 *
 * Return value:
 *    0 - Device is ready. 1 - Device NOT ready.
 **/
static int scsi_eh_tur(Scsi_Cmnd *scmd)
{
	static unsigned char tur_command[6] =
	{TEST_UNIT_READY, 0, 0, 0, 0, 0};
	int rtn;

	memcpy((void *) scmd->cmnd, (void *) tur_command,
	       sizeof(tur_command));

	if (scmd->device->scsi_level <= SCSI_2)
		scmd->cmnd[1] = scmd->lun << 5;

	/*
	 * zero the sense buffer.  the scsi spec mandates that any
	 * untransferred sense data should be interpreted as being zero.
	 */
	memset((void *) scmd->sense_buffer, 0, sizeof(scmd->sense_buffer));

	scmd->request_buffer = NULL;
	scmd->request_bufflen = 0;
	scmd->use_sg = 0;
	scmd->cmd_len = COMMAND_SIZE(scmd->cmnd[0]);
	scmd->underflow = 0;
	scmd->sc_data_direction = SCSI_DATA_NONE;

	rtn = scsi_send_eh_cmnd(scmd, SENSE_TIMEOUT);

	/*
	 * when we eventually call scsi_finish, we really wish to complete
	 * the original request, so let's restore the original data. (db)
	 */
	memcpy((void *) scmd->cmnd, (void *) scmd->data_cmnd,
	       sizeof(scmd->data_cmnd));
	scmd->request_buffer = scmd->buffer;
	scmd->request_bufflen = scmd->bufflen;
	scmd->use_sg = scmd->old_use_sg;
	scmd->cmd_len = scmd->old_cmd_len;
	scmd->sc_data_direction = scmd->sc_old_data_direction;
	scmd->underflow = scmd->old_underflow;

	/*
	 * hey, we are done.  let's look to see what happened.
	 */
	SCSI_LOG_ERROR_RECOVERY(3,
		printk("%s: scmd %p rtn %x\n",
		__FUNCTION__, scmd, rtn));
	if ((rtn == SUCCESS) && scmd->result) {
		if (((driver_byte(scmd->result) & DRIVER_SENSE) ||
		     (status_byte(scmd->result) & CHECK_CONDITION)) &&
		    (SCSI_SENSE_VALID(scmd))) {
			if (((scmd->sense_buffer[2] & 0xf) != NOT_READY) &&
			    ((scmd->sense_buffer[2] & 0xf) != UNIT_ATTENTION) &&
			    ((scmd->sense_buffer[2] & 0xf) != ILLEGAL_REQUEST)) {
				return 0;
			}
		}
	}
	return 1;
}

/**
 * scsi_eh_abort_cmd - abort a timed-out cmd.
 * @sc_todo:	A list of cmds that have failed.
 * @shost:	scsi host being recovered.
 *
 * Decription:
 *    Try and see whether or not it makes sense to try and abort the
 *    running command.  this only works out to be the case if we have one
 *    command that has timed out.  if the command simply failed, it makes
 *    no sense to try and abort the command, since as far as the shost
 *    adapter is concerned, it isn't running.
 **/
static int scsi_eh_abort_cmd(Scsi_Cmnd *sc_todo, struct Scsi_Host *shost)
{

	int rtn;
	Scsi_Cmnd *scmd;

	SCSI_LOG_ERROR_RECOVERY(3, printk("%s: checking to see if we need"
					  " to abort cmd\n", __FUNCTION__));

	for (scmd = sc_todo; scmd; scmd = scmd->bh_next) {
		if (!scsi_eh_eflags_chk(scmd, SCSI_EH_CMD_TIMEOUT))
			continue;

		rtn = scsi_try_to_abort_cmd(scmd);
		if (rtn == SUCCESS) {
			if (scsi_eh_tur(scmd)) {
				rtn = scsi_eh_retry_cmd(scmd);
				if (rtn == SUCCESS)
					scsi_eh_finish_cmd(scmd, shost);
			}
		}
	}
	return shost->host_failed;
}

/**
 * scsi_try_bus_device_reset - Ask host to perform a BDR on a dev
 * @scmd:	SCSI cmd used to send BDR	
 *
 * Notes:
 *    There is no timeout for this operation.  if this operation is
 *    unreliable for a given host, then the host itself needs to put a
 *    timer on it, and set the host back to a consistent state prior to
 *    returning.
 **/
static int scsi_try_bus_device_reset(Scsi_Cmnd *scmd)
{
	unsigned long flags;
	int rtn = FAILED;

	if (scmd->host->hostt->eh_device_reset_handler == NULL) {
		return rtn;
	}
	scmd->owner = SCSI_OWNER_LOWLEVEL;

	spin_lock_irqsave(scmd->host->host_lock, flags);
	rtn = scmd->host->hostt->eh_device_reset_handler(scmd);
	spin_unlock_irqrestore(scmd->host->host_lock, flags);

	return rtn;
}

/**
 * scsi_eh_bus_device_reset - send bdr is needed
 * @sc_todo:	a list of cmds that have failed.
 * @shost:	scsi host being recovered.
 *
 * Notes:
 *    Try a bus device reset.  still, look to see whether we have multiple
 *    devices that are jammed or not - if we have multiple devices, it
 *    makes no sense to try bus_device_reset - we really would need to try
 *    a bus_reset instead. 
 **/
static int scsi_eh_bus_device_reset(Scsi_Cmnd *sc_todo, struct Scsi_Host *shost)
{
	int rtn;
	Scsi_Cmnd *scmd;
	Scsi_Device *sdev;

	SCSI_LOG_ERROR_RECOVERY(3, printk("%s: Trying BDR\n", __FUNCTION__));

	for (sdev = shost->host_queue; sdev; sdev = sdev->next) {
		for (scmd = sc_todo; scmd; scmd = scmd->bh_next)
			if ((scmd->device == sdev) &&
			    scsi_eh_eflags_chk(scmd, SCSI_EH_CMD_ERR))
				break;

		if (!scmd)
			continue;

		/*
		 * ok, we have a device that is having problems.  try and send
		 * a bus device reset to it.
		 */
		rtn = scsi_try_bus_device_reset(scmd);
		if ((rtn == SUCCESS) && (scsi_eh_tur(scmd)))
				for (scmd = sc_todo; scmd; scmd = scmd->bh_next)
					if ((scmd->device == sdev) &&
					    scsi_eh_eflags_chk(scmd, SCSI_EH_CMD_ERR)) {
						rtn = scsi_eh_retry_cmd(scmd);
						if (rtn == SUCCESS)
							scsi_eh_finish_cmd(scmd, shost);
					}
	}

	return shost->host_failed;
}

/**
 * scsi_try_bus_reset - ask host to perform a bus reset
 * @scmd:	SCSI cmd to send bus reset.
 **/
static int scsi_try_bus_reset(Scsi_Cmnd *scmd)
{
	unsigned long flags;
	int rtn;
	Scsi_Device *sdev;

	SCSI_LOG_ERROR_RECOVERY(3, printk("%s: Snd Bus RST\n",
					  __FUNCTION__));
	scmd->owner = SCSI_OWNER_LOWLEVEL;
	scmd->serial_number_at_timeout = scmd->serial_number;

	if (scmd->host->hostt->eh_bus_reset_handler == NULL)
		return FAILED;

	spin_lock_irqsave(scmd->host->host_lock, flags);
	rtn = scmd->host->hostt->eh_bus_reset_handler(scmd);
	spin_unlock_irqrestore(scmd->host->host_lock, flags);

	if (rtn == SUCCESS) {
		scsi_sleep(BUS_RESET_SETTLE_TIME);
		/*
		 * Mark all affected devices to expect a unit attention.
		 */
		for (sdev = scmd->host->host_queue; sdev; sdev = sdev->next)
			if (scmd->channel == sdev->channel) {
				sdev->was_reset = 1;
				sdev->expecting_cc_ua = 1;
			}
	}
	return rtn;
}

/**
 * scsi_try_host_reset - ask host adapter to reset itself
 * @scmd:	SCSI cmd to send hsot reset.
 **/
static int scsi_try_host_reset(Scsi_Cmnd *scmd)
{
	unsigned long flags;
	int rtn;
	 Scsi_Device *sdev;

	SCSI_LOG_ERROR_RECOVERY(3, printk("%s: Snd Host RST\n",
					  __FUNCTION__));
	scmd->owner = SCSI_OWNER_LOWLEVEL;
	scmd->serial_number_at_timeout = scmd->serial_number;

	if (scmd->host->hostt->eh_host_reset_handler == NULL)
		return FAILED;

	spin_lock_irqsave(scmd->host->host_lock, flags);
	rtn = scmd->host->hostt->eh_host_reset_handler(scmd);
	spin_unlock_irqrestore(scmd->host->host_lock, flags);

	if (rtn == SUCCESS) {
		scsi_sleep(HOST_RESET_SETTLE_TIME);
		/*
		 * Mark all affected devices to expect a unit attention.
		 */
		for (sdev = scmd->host->host_queue; sdev; sdev = sdev->next)
			if (scmd->channel == sdev->channel) {
				sdev->was_reset = 1;
				sdev->expecting_cc_ua = 1;
			}
	}
	return rtn;
}

/**
 * scsi_eh_bus_host_reset - send a bus reset and on failure try host reset
 * @sc_todo:	a list of cmds that have failed.
 * @shost:	scsi host being recovered.
 **/
static int scsi_eh_bus_host_reset(Scsi_Cmnd *sc_todo, struct Scsi_Host *shost)
{
	int rtn;
	Scsi_Cmnd *scmd;
	Scsi_Cmnd *chan_scmd;
	unsigned int channel;

	/*
	 * if we ended up here, we have serious problems.  the only thing left
	 * to try is a full bus reset.  if someone has grabbed the bus and isn't
	 * letting go, then perhaps this will help.
	 */
	SCSI_LOG_ERROR_RECOVERY(3, printk("%s: Try Bus/Host RST\n",
					  __FUNCTION__));

	/* 
	 * we really want to loop over the various channels, and do this on
	 * a channel by channel basis.  we should also check to see if any
	 * of the failed commands are on soft_reset devices, and if so, skip
	 * the reset.  
	 */

	for (channel = 0; channel <= shost->max_channel; channel++) {
		for (scmd = sc_todo; scmd; scmd = scmd->bh_next) {
			if (!scsi_eh_eflags_chk(scmd, SCSI_EH_CMD_ERR))
				continue;
			if (channel == scmd->channel) {
				chan_scmd = scmd;
				break;
				/*
				 * FIXME add back in some support for
				 * soft_reset devices.
				 */
			}
		}

		if (!scmd)
			continue;

		/*
		 * we now know that we are able to perform a reset for the
		 * channel that scmd points to.
		 */
		rtn = scsi_try_bus_reset(scmd);
		if (rtn != SUCCESS)
			rtn = scsi_try_host_reset(scmd);

		if (rtn == SUCCESS) {
			for (scmd = sc_todo; scmd; scmd = scmd->bh_next) {
				if (!scsi_eh_eflags_chk(scmd, SCSI_EH_CMD_ERR)
				    || channel != scmd->channel)
					continue;
				if (scsi_eh_tur(scmd)) {
					rtn = scsi_eh_retry_cmd(scmd);

					if (rtn == SUCCESS)
						scsi_eh_finish_cmd(scmd, shost);
				}
			}
		}

	}
	return shost->host_failed;
}

/**
 * scsi_eh_offline_sdevs - offline scsi devices that fail to recover
 * @sc_todo:	a list of cmds that have failed.
 * @shost:	scsi host being recovered.
 *
 **/
static void scsi_eh_offline_sdevs(Scsi_Cmnd *sc_todo, struct Scsi_Host *shost)
{
	Scsi_Cmnd *scmd;

	for (scmd = sc_todo; scmd; scmd = scmd->bh_next) {
		if (!scsi_eh_eflags_chk(scmd, SCSI_EH_CMD_ERR))
			continue;

		printk(KERN_INFO "%s: Device set offline - not"
				"ready or command retry failed"
				"after error recovery: host"
				"%d channel %d id %d lun %d\n",
				__FUNCTION__, shost->host_no,
				scmd->device->channel,
				scmd->device->id,
				scmd->device->lun);
		scmd->device->online = FALSE;
		scsi_eh_finish_cmd(scmd, shost);
	}
	return;
}

/**
 * scsi_sleep_done - timer function for scsi_sleep
 * @sem:	semphore to signal
 *
 **/
static
void scsi_sleep_done(struct semaphore *sem)
{
	if (sem != NULL) {
		up(sem);
	}
}

/**
 * scsi_sleep - sleep for specified timeout
 * @timeout:	timeout value
 *
 **/
void scsi_sleep(int timeout)
{
	DECLARE_MUTEX_LOCKED(sem);
	struct timer_list timer;

	init_timer(&timer);
	timer.data = (unsigned long) &sem;
	timer.expires = jiffies + timeout;
	timer.function = (void (*)(unsigned long)) scsi_sleep_done;

	SCSI_LOG_ERROR_RECOVERY(5, printk("sleeping for timer tics %d\n",
					  timeout));

	add_timer(&timer);

	down(&sem);
	del_timer(&timer);
}

/**
 * scsi_decide_disposition - Disposition a cmd on return from LLD.
 * @scmd:	SCSI cmd to examine.
 *
 * Notes:
 *    This is *only* called when we are examining the status after sending
 *    out the actual data command.  any commands that are queued for error
 *    recovery (i.e. test_unit_ready) do *not* come through here.
 *
 *    When this routine returns failed, it means the error handler thread
 *    is woken.  in cases where the error code indicates an error that
 *    doesn't require the error handler read (i.e. we don't need to
 *    abort/reset), then this function should return SUCCESS.
 **/
int scsi_decide_disposition(Scsi_Cmnd *scmd)
{
	int rtn;

	/*
	 * if the device is offline, then we clearly just pass the result back
	 * up to the top level.
	 */
	if (scmd->device->online == FALSE) {
		SCSI_LOG_ERROR_RECOVERY(5, printk("%s: device offline - report"
						  "as SUCCESS\n",
						  __FUNCTION__));
		return SUCCESS;
	}
	/*
	 * first check the host byte, to see if there is anything in there
	 * that would indicate what we need to do.
	 */

	switch (host_byte(scmd->result)) {
	case DID_PASSTHROUGH:
		/*
		 * no matter what, pass this through to the upper layer.
		 * nuke this special code so that it looks like we are saying
		 * did_ok.
		 */
		scmd->result &= 0xff00ffff;
		return SUCCESS;
	case DID_OK:
		/*
		 * looks good.  drop through, and check the next byte.
		 */
		break;
	case DID_NO_CONNECT:
	case DID_BAD_TARGET:
	case DID_ABORT:
		/*
		 * note - this means that we just report the status back
		 * to the top level driver, not that we actually think
		 * that it indicates SUCCESS.
		 */
		return SUCCESS;
		/*
		 * when the low level driver returns did_soft_error,
		 * it is responsible for keeping an internal retry counter 
		 * in order to avoid endless loops (db)
		 *
		 * actually this is a bug in this function here.  we should
		 * be mindful of the maximum number of retries specified
		 * and not get stuck in a loop.
		 */
	case DID_SOFT_ERROR:
		goto maybe_retry;

	case DID_ERROR:
		if (msg_byte(scmd->result) == COMMAND_COMPLETE &&
		    status_byte(scmd->result) == RESERVATION_CONFLICT)
			/*
			 * execute reservation conflict processing code
			 * lower down
			 */
			break;
		/* fallthrough */

	case DID_BUS_BUSY:
	case DID_PARITY:
		goto maybe_retry;
	case DID_TIME_OUT:
		/*
		 * when we scan the bus, we get timeout messages for
		 * these commands if there is no device available.
		 * other hosts report did_no_connect for the same thing.
		 */
		if ((scmd->cmnd[0] == TEST_UNIT_READY ||
		     scmd->cmnd[0] == INQUIRY)) {
			return SUCCESS;
		} else {
			return FAILED;
		}
	case DID_RESET:
		/*
		 * in the normal case where we haven't initiated a reset,
		 * this is a failure.
		 */
		if (scmd->flags & IS_RESETTING) {
			scmd->flags &= ~IS_RESETTING;
			goto maybe_retry;
		}
		return SUCCESS;
	default:
		return FAILED;
	}

	/*
	 * next, check the message byte.
	 */
	if (msg_byte(scmd->result) != COMMAND_COMPLETE) {
		return FAILED;
	}
	/*
	 * now, check the status byte to see if this indicates anything special.
	 */
	switch (status_byte(scmd->result)) {
	case QUEUE_FULL:
		/*
		 * the case of trying to send too many commands to a
		 * tagged queueing device.
		 */
		return ADD_TO_MLQUEUE;
	case GOOD:
	case COMMAND_TERMINATED:
		return SUCCESS;
	case CHECK_CONDITION:
		rtn = scsi_check_sense(scmd);
		if (rtn == NEEDS_RETRY) {
			goto maybe_retry;
		}
		return rtn;
	case CONDITION_GOOD:
	case INTERMEDIATE_GOOD:
	case INTERMEDIATE_C_GOOD:
		/*
		 * who knows?  FIXME(eric)
		 */
		return SUCCESS;
	case BUSY:
		goto maybe_retry;

	case RESERVATION_CONFLICT:
		printk("scsi%d (%d,%d,%d) : reservation conflict\n", 
		       scmd->host->host_no, scmd->channel,
		       scmd->device->id, scmd->device->lun);
		return SUCCESS; /* causes immediate i/o error */
	default:
		return FAILED;
	}
	return FAILED;

      maybe_retry:

	if ((++scmd->retries) < scmd->allowed) {
		return NEEDS_RETRY;
	} else {
		/*
		 * no more retries - report this one back to upper level.
		 */
		return SUCCESS;
	}
}

/**
 * scsi_restart_operations - restart io operations to the specified host.
 * @shost:	Host we are restarting.
 *
 * Notes:
 *    When we entered the error handler, we blocked all further i/o to
 *    this device.  we need to 'reverse' this process.
 **/
static void scsi_restart_operations(struct Scsi_Host *shost)
{
	Scsi_Device *sdev;
	unsigned long flags;

	ASSERT_LOCK(shost->host_lock, 0);

	/*
	 * next free up anything directly waiting upon the host.  this
	 * will be requests for character device operations, and also for
	 * ioctls to queued block devices.
	 */
	SCSI_LOG_ERROR_RECOVERY(3, printk("%s: waking up host to restart\n",
					  __FUNCTION__));

	wake_up(&shost->host_wait);

	/*
	 * finally we need to re-initiate requests that may be pending.  we will
	 * have had everything blocked while error handling is taking place, and
	 * now that error recovery is done, we will need to ensure that these
	 * requests are started.
	 */
	spin_lock_irqsave(shost->host_lock, flags);
	for (sdev = shost->host_queue; sdev; sdev = sdev->next) {
		request_queue_t *q = &sdev->request_queue;

		if ((shost->can_queue > 0 &&
		     (shost->host_busy >= shost->can_queue))
		    || (shost->host_blocked)
		    || (shost->host_self_blocked)
		    || (sdev->device_blocked)) {
			break;
		}

		q->request_fn(q);
	}
	spin_unlock_irqrestore(shost->host_lock, flags);
}

/**
 * scsi_unjam_host - Attempt to fix a host which has a cmd that failed.
 * @shost:	Host to unjam.
 *
 * Notes:
 *    When we come in here, we *know* that all commands on the bus have
 *    either completed, failed or timed out.  we also know that no further
 *    commands are being sent to the host, so things are relatively quiet
 *    and we have freedom to fiddle with things as we wish.
 *
 *    This is only the *default* implementation.  it is possible for
 *    individual drivers to supply their own version of this function, and
 *    if the maintainer wishes to do this, it is strongly suggested that
 *    this function be taken as a template and modified.  this function
 *    was designed to correctly handle problems for about 95% of the
 *    different cases out there, and it should always provide at least a
 *    reasonable amount of error recovery.
 *
 *    Any command marked 'failed' or 'timeout' must eventually have
 *    scsi_finish_cmd() called for it.  we do all of the retry stuff
 *    here, so when we restart the host after we return it should have an
 *    empty queue.
 **/
static void scsi_unjam_host(struct Scsi_Host *shost)
{
	Scsi_Cmnd *sc_todo = NULL;
	Scsi_Cmnd *scmd;

	/*
	 * Is this assert really ok anymore (andmike). Should we at least
	 * be using spin_lock_unlocked.
	 */
	ASSERT_LOCK(shost->host_lock, 0);

	scsi_eh_get_failed(&sc_todo, shost);

	if (scsi_eh_get_sense(sc_todo, shost))
		if (scsi_eh_abort_cmd(sc_todo, shost))
			if (scsi_eh_bus_device_reset(sc_todo, shost))
				if(scsi_eh_bus_host_reset(sc_todo, shost))
					scsi_eh_offline_sdevs(sc_todo, shost);

	BUG_ON(shost->host_failed);


	/*
	 * We are currently holding these things in a linked list - we
	 * didn't put them in the bottom half queue because we wanted to
	 * keep things quiet while we were working on recovery, and
	 * passing them up to the top level could easily cause the top
	 * level to try and queue something else again.
	 *
	 * start by marking that the host is no longer in error recovery.
	 */
	shost->in_recovery = 0;

	/*
	 * take the list of commands, and stick them in the bottom half queue.
	 * the current implementation of scsi_done will do this for us - if need
	 * be we can create a special version of this function to do the
	 * same job for us.
	 */
	for (scmd = sc_todo; scmd; scmd = sc_todo) {
		sc_todo = scmd->bh_next;
		scmd->bh_next = NULL;
		/*
		 * Oh, this is a vile hack.  scsi_done() expects a timer
		 * to be running on the command.  If there isn't, it assumes
		 * that the command has actually timed out, and a timer
		 * handler is running.  That may well be how we got into
		 * this fix, but right now things are stable.  We add
		 * a timer back again so that we can report completion.
		 * scsi_done() will immediately remove said timer from
		 * the command, and then process it.
		 */
		scsi_add_timer(scmd, 100, scsi_eh_times_out);
		scsi_done(scmd);
	}

}

/**
 * scsi_error_handler - Handle errors/timeouts of SCSI cmds.
 * @data:	Host for which we are running.
 *
 * Notes:
 *    This is always run in the context of a kernel thread.  The idea is
 *    that we start this thing up when the kernel starts up (one per host
 *    that we detect), and it immediately goes to sleep and waits for some
 *    event (i.e. failure).  When this takes place, we have the job of
 *    trying to unjam the bus and restarting things.
 **/
void scsi_error_handler(void *data)
{
	struct Scsi_Host *shost = (struct Scsi_Host *) data;
	int rtn;
	DECLARE_MUTEX_LOCKED(sem);

	/*
	 * We only listen to signals if the HA was loaded as a module.
	 * If the HA was compiled into the kernel, then we don't listen
	 * to any signals.
	 */
	siginitsetinv(&current->blocked, SHUTDOWN_SIGS);

	lock_kernel();

	/*
	 *    Flush resources
	 */

	daemonize();

	/*
	 * Set the name of this process.
	 */

	sprintf(current->comm, "scsi_eh_%d", shost->host_no);

	shost->eh_wait = &sem;
	shost->ehandler = current;

	unlock_kernel();

	/*
	 * Wake up the thread that created us.
	 */
	SCSI_LOG_ERROR_RECOVERY(3, printk("Wake up parent %d\n",
					  shost->eh_notify->count.counter));

	up(shost->eh_notify);

	while (1) {
		/*
		 * If we get a signal, it means we are supposed to go
		 * away and die.  This typically happens if the user is
		 * trying to unload a module.
		 */
		SCSI_LOG_ERROR_RECOVERY(1, printk("Error handler sleeping\n"));

		/*
		 * Note - we always use down_interruptible with the semaphore
		 * even if the module was loaded as part of the kernel.  The
		 * reason is that down() will cause this thread to be counted
		 * in the load average as a running process, and down
		 * interruptible doesn't.  Given that we need to allow this
		 * thread to die if the driver was loaded as a module, using
		 * semaphores isn't unreasonable.
		 */
		down_interruptible(&sem);
		if (signal_pending(current))
			break;

		SCSI_LOG_ERROR_RECOVERY(1, printk("Error handler waking up\n"));

		shost->eh_active = 1;

		/*
		 * We have a host that is failing for some reason.  Figure out
		 * what we need to do to get it up and online again (if we can).
		 * If we fail, we end up taking the thing offline.
		 */
		if (shost->hostt->eh_strategy_handler != NULL) {
			rtn = shost->hostt->eh_strategy_handler(shost);
		} else {
			scsi_unjam_host(shost);
		}

		shost->eh_active = 0;

		/*
		 * Note - if the above fails completely, the action is to take
		 * individual devices offline and flush the queue of any
		 * outstanding requests that may have been pending.  When we
		 * restart, we restart any I/O to any other devices on the bus
		 * which are still online.
		 */
		scsi_restart_operations(shost);

	}

	SCSI_LOG_ERROR_RECOVERY(1, printk("Error handler exiting\n"));

	/*
	 * Make sure that nobody tries to wake us up again.
	 */
	shost->eh_wait = NULL;

	/*
	 * Knock this down too.  From this point on, the host is flying
	 * without a pilot.  If this is because the module is being unloaded,
	 * that's fine.  If the user sent a signal to this thing, we are
	 * potentially in real danger.
	 */
	shost->in_recovery = 0;
	shost->eh_active = 0;
	shost->ehandler = NULL;

	/*
	 * If anyone is waiting for us to exit (i.e. someone trying to unload
	 * a driver), then wake up that process to let them know we are on
	 * the way out the door.  This may be overkill - I *think* that we
	 * could probably just unload the driver and send the signal, and when
	 * the error handling thread wakes up that it would just exit without
	 * needing to touch any memory associated with the driver itself.
	 */
	if (shost->eh_notify != NULL)
		up(shost->eh_notify);
}

/**
 * scsi_new_reset - Send reset to a bus or device at any phase.
 * @scmd:	Cmd to send reset with (usually a dummy)
 * @flag:	Reset type.
 *
 * Description:
 *    This is used by the SCSI Generic driver to provide Bus/Device reset
 *    capability.
 *
 * Return value:
 *    SUCCESS/FAILED.
 **/
int scsi_new_reset(Scsi_Cmnd *scmd, int flag)
{
	int rtn;

	switch(flag) {
	case SCSI_TRY_RESET_DEVICE:
		rtn = scsi_try_bus_device_reset(scmd);
		if (rtn == SUCCESS)
			break;
		/* FALLTHROUGH */
	case SCSI_TRY_RESET_BUS:
		rtn = scsi_try_bus_reset(scmd);
		if (rtn == SUCCESS)
			break;
		/* FALLTHROUGH */
	case SCSI_TRY_RESET_HOST:
		rtn = scsi_try_host_reset(scmd);
		break;
	default:
		rtn = FAILED;
	}

	return rtn;
}
