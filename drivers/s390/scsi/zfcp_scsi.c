/* 
 * 
 * linux/drivers/s390/scsi/zfcp_scsi.c
 * 
 * FCP adapter driver for IBM eServer zSeries 
 * 
 * Copyright 2002 IBM Corporation 
 * Author(s): Martin Peschke <mpeschke@de.ibm.com> 
 *            Raimund Schroeder <raimund.schroeder@de.ibm.com> 
 *            Aron Zeh <arzeh@de.ibm.com> 
 *            Wolfgang Taphorn <taphorn@de.ibm.com> 
 *            Stefan Bader <stefan.bader@de.ibm.com> 
 *            Heiko Carstens <heiko.carstens@de.ibm.com> 
 * 
 * This program is free software; you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation; either version 2, or (at your option) 
 * any later version. 
 * 
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 * GNU General Public License for more details. 
 * 
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 */

#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_SCSI
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_SCSI
/* this drivers version (do not edit !!! generated and updated by cvs) */
#define ZFCP_SCSI_REVISION "$Revision: 1.38 $"

#include <linux/blkdev.h>

#include "zfcp_ext.h"

static void zfcp_scsi_slave_destroy(struct scsi_device *sdp);
static int zfcp_scsi_slave_alloc(struct scsi_device *sdp);
static int zfcp_scsi_slave_configure(struct scsi_device *sdp);
static int zfcp_scsi_queuecommand(Scsi_Cmnd *, void (*done) (Scsi_Cmnd *));
static int zfcp_scsi_eh_abort_handler(Scsi_Cmnd *);
static int zfcp_scsi_eh_device_reset_handler(Scsi_Cmnd *);
static int zfcp_scsi_eh_bus_reset_handler(Scsi_Cmnd *);
static int zfcp_scsi_eh_host_reset_handler(Scsi_Cmnd *);
static int zfcp_task_management_function(struct zfcp_unit *, u8);

static int zfcp_create_sbales_from_segment(unsigned long, int, int *,
					   int, int, int *, int *, int,
					   int, struct qdio_buffer **,
					   char);

static int zfcp_create_sbale(unsigned long, int, int *, int, int, int *,
			     int, int, int *, struct qdio_buffer **,
			     char);

static struct zfcp_unit *zfcp_scsi_determine_unit(struct zfcp_adapter *,
						  Scsi_Cmnd *);
static struct zfcp_unit *zfcp_unit_lookup(struct zfcp_adapter *, int, int, int);

static struct device_attribute *zfcp_sysfs_sdev_attrs[];

struct zfcp_data zfcp_data = {
	.scsi_host_template = {
	      name:	               ZFCP_NAME,
	      proc_name:               "dummy",
	      proc_info:               NULL,
	      detect:	               NULL,
	      slave_alloc:             zfcp_scsi_slave_alloc,
	      slave_configure:         zfcp_scsi_slave_configure,
	      slave_destroy:           zfcp_scsi_slave_destroy,
	      queuecommand:            zfcp_scsi_queuecommand,
	      eh_abort_handler:        zfcp_scsi_eh_abort_handler,
	      eh_device_reset_handler: zfcp_scsi_eh_device_reset_handler,
	      eh_bus_reset_handler:    zfcp_scsi_eh_bus_reset_handler,
	      eh_host_reset_handler:   zfcp_scsi_eh_host_reset_handler,
			               /* FIXME(openfcp): Tune */
	      can_queue:               4096,
	      this_id:	               0,
	      /*
	       * FIXME:
	       * one less? can zfcp_create_sbale cope with it?
	       */
	      sg_tablesize:            ZFCP_MAX_SBALES_PER_REQ,
	      cmd_per_lun:             1,
	      unchecked_isa_dma:       0,
	      use_clustering:          1,
	      sdev_attrs:              zfcp_sysfs_sdev_attrs,
	}
	/* rest initialised with zeros */
};

/* Find start of Response Information in FCP response unit*/
char *
zfcp_get_fcp_rsp_info_ptr(struct fcp_rsp_iu *fcp_rsp_iu)
{
	char *fcp_rsp_info_ptr;

	fcp_rsp_info_ptr =
	    (unsigned char *) fcp_rsp_iu + (sizeof (struct fcp_rsp_iu));

	return fcp_rsp_info_ptr;
}

/* Find start of Sense Information in FCP response unit*/
char *
zfcp_get_fcp_sns_info_ptr(struct fcp_rsp_iu *fcp_rsp_iu)
{
	char *fcp_sns_info_ptr;

	fcp_sns_info_ptr =
	    (unsigned char *) fcp_rsp_iu + (sizeof (struct fcp_rsp_iu));
	if (fcp_rsp_iu->validity.bits.fcp_rsp_len_valid)
		fcp_sns_info_ptr = (char *) fcp_sns_info_ptr +
		    fcp_rsp_iu->fcp_rsp_len;

	return fcp_sns_info_ptr;
}

fcp_dl_t *
zfcp_get_fcp_dl_ptr(struct fcp_cmnd_iu * fcp_cmd)
{
	int additional_length = fcp_cmd->add_fcp_cdb_length << 2;
	fcp_dl_t *fcp_dl_addr;

	fcp_dl_addr = (fcp_dl_t *)
		((unsigned char *) fcp_cmd +
		 sizeof (struct fcp_cmnd_iu) + additional_length);
	/*
	 * fcp_dl_addr = start address of fcp_cmnd structure + 
	 * size of fixed part + size of dynamically sized add_dcp_cdb field
	 * SEE FCP-2 documentation
	 */
	return fcp_dl_addr;
}

fcp_dl_t
zfcp_get_fcp_dl(struct fcp_cmnd_iu * fcp_cmd)
{
	return *zfcp_get_fcp_dl_ptr(fcp_cmd);
}

void
zfcp_set_fcp_dl(struct fcp_cmnd_iu *fcp_cmd, fcp_dl_t fcp_dl)
{
	*zfcp_get_fcp_dl_ptr(fcp_cmd) = fcp_dl;
}

/*
 * note: it's a bit-or operation not an assignment
 * regarding the specified byte
 */
static inline void
set_byte(u32 * result, char status, char pos)
{
	*result |= status << (pos * 8);
}

void
set_host_byte(u32 * result, char status)
{
	set_byte(result, status, 2);
}

void
set_driver_byte(u32 * result, char status)
{
	set_byte(result, status, 3);
}

/*
 * function:	zfcp_scsi_slave_alloc
 *
 * purpose:
 *
 * returns:
 */

static int
zfcp_scsi_slave_alloc(struct scsi_device *sdp)
{
	struct zfcp_adapter *adapter;
	struct zfcp_unit *unit;
	unsigned long flags;
	int retval = -ENODEV;

	adapter = (struct zfcp_adapter *) sdp->host->hostdata[0];
	if (!adapter)
		goto out;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	unit = zfcp_unit_lookup(adapter, sdp->channel, sdp->id, sdp->lun);
	if (unit) {
		sdp->hostdata = unit;
		unit->device = sdp;
		zfcp_unit_get(unit);
		retval = 0;
	}
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);
 out:
	return retval;
}

/*
 * function:	zfcp_scsi_slave_destroy
 *
 * purpose:
 *
 * returns:
 */

static void
zfcp_scsi_slave_destroy(struct scsi_device *sdpnt)
{
	struct zfcp_unit *unit = (struct zfcp_unit *) sdpnt->hostdata;

	if (unit) {
		sdpnt->hostdata = NULL;
		unit->device = NULL;
		zfcp_unit_put(unit);
	} else {
		ZFCP_LOG_INFO("no unit associated with SCSI device at "
			      "address 0x%lx\n", (unsigned long) sdpnt);
	}
}

/*
 * function:    zfcp_scsi_insert_into_fake_queue
 *
 * purpose:
 *		
 *
 * returns:
 *
 * FIXME:	Is the following scenario possible and - even more interesting -
 *		a problem? It reminds me of the famous 'no retry for tape' fix
 *		(no problem for disks, but what is about tapes...)
 *
 *		device is unaccessable,
 *		command A is put into the fake queue,
 *		device becomes accessable again,
 *		command B is queued to the device,
 *		fake queue timer expires
 *		command A is returned to the mid-layer
 *		command A is queued to the device
 */
void
zfcp_scsi_insert_into_fake_queue(struct zfcp_adapter *adapter,
				 Scsi_Cmnd * new_cmnd)
{
	unsigned long flags;
	Scsi_Cmnd *current_cmnd;

	ZFCP_LOG_DEBUG("Faking SCSI command:\n");
	ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
		      (char *) new_cmnd->cmnd, new_cmnd->cmd_len);

	new_cmnd->host_scribble = NULL;

	write_lock_irqsave(&adapter->fake_list_lock, flags);
	if (adapter->first_fake_cmnd == NULL) {
		adapter->first_fake_cmnd = new_cmnd;
		adapter->fake_scsi_timer.function =
		    zfcp_scsi_process_and_clear_fake_queue;
		adapter->fake_scsi_timer.data = (unsigned long) adapter;
		adapter->fake_scsi_timer.expires =
		    jiffies + ZFCP_FAKE_SCSI_COMPLETION_TIME;
		add_timer(&adapter->fake_scsi_timer);
	} else {
		for (current_cmnd = adapter->first_fake_cmnd;
		     current_cmnd->host_scribble != NULL;
		     current_cmnd =
		     (Scsi_Cmnd *) (current_cmnd->host_scribble)) ;
		current_cmnd->host_scribble = (char *) new_cmnd;
	}
	write_unlock_irqrestore(&adapter->fake_list_lock, flags);
}

/*
 * function:    zfcp_scsi_process_and_clear_fake_queue
 *
 * purpose:
 *		
 *
 * returns:
 */
void
zfcp_scsi_process_and_clear_fake_queue(unsigned long data)
{
	unsigned long flags;
	struct zfcp_adapter *adapter = (struct zfcp_adapter *) data;
	Scsi_Cmnd *cur = adapter->first_fake_cmnd;
	Scsi_Cmnd *next;

	/*
	 * We need a common lock for scsi_req on command completion
	 * as well as on command abort to avoid race conditions
	 * during completions and aborts taking place at the same time.
	 * It needs to be the outer lock as in the eh_abort_handler.
	 */
	read_lock_irqsave(&adapter->abort_lock, flags);
	write_lock(&adapter->fake_list_lock);
	while (cur) {
		next = (Scsi_Cmnd *) cur->host_scribble;
		cur->host_scribble = NULL;
		zfcp_cmd_dbf_event_scsi("clrfake", cur);
		cur->scsi_done(cur);
		cur = next;
#ifdef ZFCP_DEBUG_REQUESTS
		debug_text_event(adapter->req_dbf, 2, "fk_done:");
		debug_event(adapter->req_dbf, 2, &cur, sizeof (unsigned long));
#endif
	}
	adapter->first_fake_cmnd = NULL;
	write_unlock(&adapter->fake_list_lock);
	read_unlock_irqrestore(&adapter->abort_lock, flags);
	return;
}

void
zfcp_scsi_block_requests(struct Scsi_Host *shpnt)
{
	scsi_block_requests(shpnt);
	/* This is still somewhat racy but the best I could imagine */
	do {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(ZFCP_SCSI_HOST_FLUSH_TIMEOUT);

	} while (shpnt->host_busy || shpnt->eh_active);
}

/* 
 * Tries to associate a zfcp unit with the scsi device.
 *
 * returns:       unit pointer   if unit is found
 *                NULL           otherwise
 */
struct zfcp_unit *
zfcp_scsi_determine_unit(struct zfcp_adapter *adapter, Scsi_Cmnd * scpnt)
{
	struct zfcp_unit *unit;

	/*
	 * figure out target device
	 * (stored there by zfcp_scsi_slave_alloc)
	 * ATTENTION: assumes hostdata initialized to NULL by
	 * mid layer (see scsi_scan.c)
	 */
	unit = (struct zfcp_unit *) scpnt->device->hostdata;
	if (!unit) {
		ZFCP_LOG_DEBUG("logical unit (%i %i %i %i) not configured\n",
			       scpnt->device->host->host_no,
			       scpnt->device->channel,
			       scpnt->device->id, scpnt->device->lun);
		/*
		 * must fake SCSI command execution and scsi_done
		 * callback for non-configured logical unit
		 */
		/* return this as long as we are unable to process requests */
		set_host_byte(&scpnt->result, DID_NO_CONNECT);
		zfcp_cmd_dbf_event_scsi("notconf", scpnt);
		scpnt->scsi_done(scpnt);
#ifdef ZFCP_DEBUG_REQUESTS
		debug_text_event(adapter->req_dbf, 2, "nc_done:");
		debug_event(adapter->req_dbf, 2, &scpnt,
			    sizeof (unsigned long));
#endif				/* ZFCP_DEBUG_REQUESTS */
	}
	return unit;
}

/*
 * called from scsi midlayer to allow finetuning of a device.
 */
static int
zfcp_scsi_slave_configure(struct scsi_device *sdp)
{
	if (sdp->tagged_supported)
		scsi_adjust_queue_depth(sdp, MSG_SIMPLE_TAG, ZFCP_CMND_PER_LUN);
	else
		scsi_adjust_queue_depth(sdp, 0, 1);
	return 0;
}

/* Sends command on a round-trip using SCSI stack */
static void
zfcp_scsi_queuecommand_fake(Scsi_Cmnd * scpnt, struct zfcp_adapter *adapter)
{
	ZFCP_LOG_DEBUG("Looping SCSI IO on the adapter %s.\n",
		       zfcp_get_busid_by_adapter(adapter));
	/* 
	 * Reset everything for devices with retries, allow at least one retry
	 * for others, e.g. tape.
	 */
	scpnt->retries = 0;
	if (scpnt->allowed == 1) {
		scpnt->allowed = 2;
	}
	set_host_byte(&scpnt->result, DID_SOFT_ERROR);
	set_driver_byte(&scpnt->result, SUGGEST_RETRY);
	zfcp_scsi_insert_into_fake_queue(adapter, scpnt);
}

/* Complete a command immediately handing back DID_ERROR */
static void
zfcp_scsi_queuecommand_stop(Scsi_Cmnd * scpnt,
			    struct zfcp_adapter *adapter,
			    struct zfcp_unit *unit)
{
	/* Always pass through to upper layer */
	scpnt->retries = scpnt->allowed - 1;
	set_host_byte(&scpnt->result, DID_ERROR);
	zfcp_cmd_dbf_event_scsi("stopping", scpnt);
	/* return directly */
	scpnt->scsi_done(scpnt);
	if (adapter && unit) {
		ZFCP_LOG_INFO("Stopping SCSI IO on the unit with FCP LUN 0x%Lx "
			      "connected to the port with WWPN 0x%Lx at the "
			      "adapter %s.\n",
			      unit->fcp_lun,
			      unit->port->wwpn,
			      zfcp_get_busid_by_adapter(adapter));
#ifdef ZFCP_DEBUG_REQUESTS
		debug_text_event(adapter->req_dbf, 2, "de_done:");
		debug_event(adapter->req_dbf, 2, &scpnt,
			    sizeof (unsigned long));
#endif				/* ZFCP_DEBUG_REQUESTS */
	} else {
		ZFCP_LOG_INFO("There is no adapter registered in the zfcp "
			      "module for the SCSI host with hostnumber %d. "
			      "Stopping IO.\n", scpnt->device->host->host_no);
	}
}

/*
 * function:	zfcp_scsi_queuecommand
 *
 * purpose:	enqueues a SCSI command to the specified target device
 *
 * note:        The scsi_done midlayer function may be called directly from
 *              within queuecommand provided queuecommand returns with
 *              success (0).
 *              If it fails, it is expected that the command could not be sent
 *              and is still available for processing.
 *              As we ensure that queuecommand never fails, we have the choice 
 *              to call done directly wherever we please.
 *              Thus, any kind of send errors other than those indicating
 *              'infinite' retries will be reported directly.
 *              Retry requests are put into a list to be processed under timer 
 *              control once in a while to allow for other operations to
 *              complete in the meantime.
 *
 * returns:	0 - success, SCSI command enqueued
 *		!0 - failure, note that we never allow this to happen as the 
 *              SCSI stack would block indefinitely should a non-zero return
 *              value be reported if there are no outstanding commands
 *              (as in when the queues are down)
 */
int
zfcp_scsi_queuecommand(Scsi_Cmnd * scpnt, void (*done) (Scsi_Cmnd *))
{
	int temp_ret;
	struct zfcp_unit *unit;
	struct zfcp_adapter *adapter;

	/* reset the status for this request */
	scpnt->result = 0;
	/* save address of mid layer call back function */
	scpnt->scsi_done = done;
	/*
	 * figure out adapter
	 * (previously stored there by the driver when
	 * the adapter was registered)
	 */
	adapter = (struct zfcp_adapter *) scpnt->device->host->hostdata[0];
	/* NULL when the adapter was removed from the zfcp list */
	if (adapter == NULL) {
		zfcp_scsi_queuecommand_stop(scpnt, NULL, NULL);
		goto out;
	}

	/* set when we have a unit/port list modification */
	if (atomic_test_mask(ZFCP_STATUS_ADAPTER_QUEUECOMMAND_BLOCK,
			     &adapter->status)) {
		zfcp_scsi_queuecommand_fake(scpnt, adapter);
		goto out;
	}

	unit = zfcp_scsi_determine_unit(adapter, scpnt);
	if (unit == NULL)
		goto out;

	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &unit->status)
	    || !atomic_test_mask(ZFCP_STATUS_COMMON_RUNNING, &unit->status)) {
		zfcp_scsi_queuecommand_stop(scpnt, adapter, unit);
		goto out;
	}
	if (!atomic_test_mask(ZFCP_STATUS_COMMON_UNBLOCKED, &unit->status)) {
		ZFCP_LOG_DEBUG("adapter %s not ready or unit with LUN 0x%Lx "
			       "on the port with WWPN 0x%Lx in recovery.\n",
			       zfcp_get_busid_by_adapter(adapter),
			       unit->fcp_lun, unit->port->wwpn);
		zfcp_scsi_queuecommand_fake(scpnt, adapter);
		goto out;
	}

	atomic_inc(&adapter->scsi_reqs_active);

	temp_ret = zfcp_fsf_send_fcp_command_task(adapter,
						  unit,
						  scpnt, ZFCP_REQ_AUTO_CLEANUP);

	if (temp_ret < 0) {
		ZFCP_LOG_DEBUG("error: Could not send a Send FCP Command\n");
		atomic_dec(&adapter->scsi_reqs_active);
		wake_up(&adapter->scsi_reqs_active_wq);
		zfcp_scsi_queuecommand_fake(scpnt, adapter);
	} else {
#ifdef ZFCP_DEBUG_REQUESTS
		debug_text_event(adapter->req_dbf, 3, "q_scpnt");
		debug_event(adapter->req_dbf, 3, &scpnt,
			    sizeof (unsigned long));
#endif				/* ZFCP_DEBUG_REQUESTS */
	}
 out:
	return 0;
}

/*
 * function:    zfcp_unit_lookup
 *
 * purpose:
 *
 * returns:
 *
 * context:	
 */
static struct zfcp_unit *
zfcp_unit_lookup(struct zfcp_adapter *adapter, int channel, int id, int lun)
{
	struct zfcp_port *port;
	struct zfcp_unit *unit, *retval = NULL;

	list_for_each_entry(port, &adapter->port_list_head, list) {
		if (id != port->scsi_id)
			continue;
		list_for_each_entry(unit, &port->unit_list_head, list) {
			if (lun == unit->scsi_lun) {
				retval = unit;
				goto out;
			}
		}
	}
 out:
	return retval;
}

/*
 * function:    zfcp_scsi_potential_abort_on_fake
 *
 * purpose:
 *
 * returns:     0 - no fake request aborted
 *              1 - fake request was aborted
 *
 * context:	both the adapter->abort_lock and the 
 *              adapter->fake_list_lock are assumed to be held write lock
 *              irqsave
 */
int
zfcp_scsi_potential_abort_on_fake(struct zfcp_adapter *adapter,
				  Scsi_Cmnd * cmnd)
{
	Scsi_Cmnd *cur = adapter->first_fake_cmnd;
	Scsi_Cmnd *pre = NULL;
	int retval = 0;

	while (cur) {
		if (cur == cmnd) {
			if (pre)
				pre->host_scribble = cur->host_scribble;
			else
				adapter->first_fake_cmnd =
				    (Scsi_Cmnd *) cur->host_scribble;
			cur->host_scribble = NULL;
			if (!adapter->first_fake_cmnd)
				del_timer(&adapter->fake_scsi_timer);
			retval = 1;
			break;
		}
		pre = cur;
		cur = (Scsi_Cmnd *) cur->host_scribble;
	}
	return retval;
}

/*
 * function:	zfcp_scsi_eh_abort_handler
 *
 * purpose:	tries to abort the specified (timed out) SCSI command
 *
 * note: 	We do not need to care for a SCSI command which completes
 *		normally but late during this abort routine runs.
 *		We are allowed to return late commands to the SCSI stack.
 *		It tracks the state of commands and will handle late commands.
 *		(Usually, the normal completion of late commands is ignored with
 *		respect to the running abort operation. Grep for 'done_late'
 *		in the SCSI stacks sources.)
 *
 * returns:	SUCCESS	- command has been aborted and cleaned up in internal
 *			  bookkeeping,
 *			  SCSI stack won't be called for aborted command
 *		FAILED	- otherwise
 */
int
zfcp_scsi_eh_abort_handler(Scsi_Cmnd * scpnt)
{
	int retval = SUCCESS;
	struct zfcp_fsf_req *new_fsf_req, *old_fsf_req;
	struct zfcp_adapter *adapter;
	struct zfcp_unit *unit;
	struct zfcp_port *port;
	struct Scsi_Host *scsi_host;
	union zfcp_req_data *req_data = NULL;
	unsigned long flags;
	u32 status = 0;

	adapter = (struct zfcp_adapter *) scpnt->device->host->hostdata[0];
	scsi_host = scpnt->device->host;
	unit = (struct zfcp_unit *) scpnt->device->hostdata;
	port = unit->port;

#ifdef ZFCP_DEBUG_ABORTS
	/* the components of a abort_dbf record (fixed size record) */
	u64 dbf_scsi_cmnd = (unsigned long) scpnt;
	char dbf_opcode[ZFCP_ABORT_DBF_LENGTH];
	wwn_t dbf_wwn = port->wwpn;
	fcp_lun_t dbf_fcp_lun = unit->fcp_lun;
	u64 dbf_retries = scpnt->retries;
	u64 dbf_allowed = scpnt->allowed;
	u64 dbf_timeout = 0;
	u64 dbf_fsf_req = 0;
	u64 dbf_fsf_status = 0;
	u64 dbf_fsf_qual[2] = { 0, 0 };
	char dbf_result[ZFCP_ABORT_DBF_LENGTH] = { "##undef" };

	memset(dbf_opcode, 0, ZFCP_ABORT_DBF_LENGTH);
	memcpy(dbf_opcode,
	       scpnt->cmnd,
	       min(scpnt->cmd_len, (unsigned char) ZFCP_ABORT_DBF_LENGTH));
#endif

	 /*TRACE*/
	    ZFCP_LOG_INFO
	    ("Aborting for adapter=0x%lx, busid=%s, scsi_cmnd=0x%lx\n",
	     (unsigned long) adapter, zfcp_get_busid_by_adapter(adapter),
	     (unsigned long) scpnt);

	spin_unlock_irq(scsi_host->host_lock);

	/*
	 * Race condition between normal (late) completion and abort has
	 * to be avoided.
	 * The entirity of all accesses to scsi_req have to be atomic.
	 * scsi_req is usually part of the fsf_req (for requests which
	 * are not faked) and thus we block the release of fsf_req
	 * as long as we need to access scsi_req.
	 * For faked commands we use the same lock even if they are not
	 * put into the fsf_req queue. This makes implementation
	 * easier. 
	 */
	write_lock_irqsave(&adapter->abort_lock, flags);

	/*
	 * Check if we deal with a faked command, which we may just forget
	 * about from now on
	 */
	write_lock(&adapter->fake_list_lock);
	/* only need to go through list if there are faked requests */
	if (adapter->first_fake_cmnd != NULL) {
		if (zfcp_scsi_potential_abort_on_fake(adapter, scpnt)) {
			write_unlock(&adapter->fake_list_lock);
			write_unlock_irqrestore(&adapter->abort_lock, flags);
			ZFCP_LOG_INFO("A faked command was aborted\n");
			retval = SUCCESS;
			strncpy(dbf_result, "##faked", ZFCP_ABORT_DBF_LENGTH);
			goto out;
		}
	}
	write_unlock(&adapter->fake_list_lock);

	/*
	 * Check whether command has just completed and can not be aborted.
	 * Even if the command has just been completed late, we can access
	 * scpnt since the SCSI stack does not release it at least until
	 * this routine returns. (scpnt is parameter passed to this routine
	 * and must not disappear during abort even on late completion.)
	 */
	req_data = (union zfcp_req_data *) scpnt->host_scribble;
	/* DEBUG */
	ZFCP_LOG_DEBUG("req_data=0x%lx\n", (unsigned long) req_data);
	if (!req_data) {
		ZFCP_LOG_DEBUG("late command completion overtook abort\n");
		/*
		 * That's it.
		 * Do not initiate abort but return SUCCESS.
		 */
		write_unlock_irqrestore(&adapter->abort_lock, flags);
		retval = SUCCESS;
		strncpy(dbf_result, "##late1", ZFCP_ABORT_DBF_LENGTH);
		goto out;
	}

	/* Figure out which fsf_req needs to be aborted. */
	old_fsf_req = req_data->send_fcp_command_task.fsf_req;
#ifdef ZFCP_DEBUG_ABORTS
	dbf_fsf_req = (unsigned long) old_fsf_req;
	dbf_timeout =
	    (jiffies - req_data->send_fcp_command_task.start_jiffies) / HZ;
#endif
	/* DEBUG */
	ZFCP_LOG_DEBUG("old_fsf_req=0x%lx\n", (unsigned long) old_fsf_req);
	if (!old_fsf_req) {
		write_unlock_irqrestore(&adapter->abort_lock, flags);
		ZFCP_LOG_NORMAL("bug: No old fsf request found.\n");
		ZFCP_LOG_NORMAL("req_data:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
			      (char *) req_data, sizeof (union zfcp_req_data));
		ZFCP_LOG_NORMAL("scsi_cmnd:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
			      (char *) scpnt, sizeof (struct scsi_cmnd));
		retval = FAILED;
		strncpy(dbf_result, "##bug:r", ZFCP_ABORT_DBF_LENGTH);
		goto out;
	}
	old_fsf_req->data.send_fcp_command_task.scsi_cmnd = NULL;
	/* mark old request as being aborted */
	old_fsf_req->status |= ZFCP_STATUS_FSFREQ_ABORTING;
	/*
	 * We have to collect all information (e.g. unit) needed by 
	 * zfcp_fsf_abort_fcp_command before calling that routine
	 * since that routine is not allowed to access
	 * fsf_req which it is going to abort.
	 * This is because of we need to release fsf_req_list_lock
	 * before calling zfcp_fsf_abort_fcp_command.
	 * Since this lock will not be held, fsf_req may complete
	 * late and may be released meanwhile.
	 */
	ZFCP_LOG_DEBUG("unit=0x%lx, unit_fcp_lun=0x%Lx\n",
		       (unsigned long) unit, unit->fcp_lun);

	/*
	 * The 'Abort FCP Command' routine may block (call schedule)
	 * because it may wait for a free SBAL.
	 * That's why we must release the lock and enable the
	 * interrupts before.
	 * On the other hand we do not need the lock anymore since
	 * all critical accesses to scsi_req are done.
	 */
	write_unlock_irqrestore(&adapter->abort_lock, flags);
	/* call FSF routine which does the abort */
	new_fsf_req = zfcp_fsf_abort_fcp_command((unsigned long) old_fsf_req,
						 adapter,
						 unit, ZFCP_WAIT_FOR_SBAL);
	ZFCP_LOG_DEBUG("new_fsf_req=0x%lx\n", (unsigned long) new_fsf_req);
	if (!new_fsf_req) {
		retval = FAILED;
		ZFCP_LOG_DEBUG("warning: Could not abort SCSI command "
			       "at 0x%lx\n", (unsigned long) scpnt);
		strncpy(dbf_result, "##nores", ZFCP_ABORT_DBF_LENGTH);
		goto out;
	}

	/* wait for completion of abort */
	ZFCP_LOG_DEBUG("Waiting for cleanup....\n");
#ifdef ZFCP_DEBUG_ABORTS
	/*
	 * FIXME:
	 * copying zfcp_fsf_req_wait_and_cleanup code is not really nice
	 */
	__wait_event(new_fsf_req->completion_wq,
		     new_fsf_req->status & ZFCP_STATUS_FSFREQ_COMPLETED);
	status = new_fsf_req->status;
	dbf_fsf_status = new_fsf_req->qtcb->header.fsf_status;
	/*
	 * Ralphs special debug load provides timestamps in the FSF
	 * status qualifier. This might be specified later if being
	 * useful for debugging aborts.
	 */
	dbf_fsf_qual[0] =
	    *(u64 *) & new_fsf_req->qtcb->header.fsf_status_qual.word[0];
	dbf_fsf_qual[1] =
	    *(u64 *) & new_fsf_req->qtcb->header.fsf_status_qual.word[2];
	zfcp_fsf_req_cleanup(new_fsf_req);
#else
	retval = zfcp_fsf_req_wait_and_cleanup(new_fsf_req,
					       ZFCP_UNINTERRUPTIBLE, &status);
#endif
	ZFCP_LOG_DEBUG("Waiting for cleanup complete, status=0x%x\n", status);
	/* status should be valid since signals were not permitted */
	if (status & ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED) {
		retval = SUCCESS;
		strncpy(dbf_result, "##succ", ZFCP_ABORT_DBF_LENGTH);
	} else if (status & ZFCP_STATUS_FSFREQ_ABORTNOTNEEDED) {
		retval = SUCCESS;
		strncpy(dbf_result, "##late2", ZFCP_ABORT_DBF_LENGTH);
	} else {
		retval = FAILED;
		strncpy(dbf_result, "##fail", ZFCP_ABORT_DBF_LENGTH);
	}

      out:
#ifdef ZFCP_DEBUG_ABORTS
	debug_event(adapter->abort_dbf, 1, &dbf_scsi_cmnd, sizeof (u64));
	debug_event(adapter->abort_dbf, 1, &dbf_opcode, ZFCP_ABORT_DBF_LENGTH);
	debug_event(adapter->abort_dbf, 1, &dbf_wwn, sizeof (wwn_t));
	debug_event(adapter->abort_dbf, 1, &dbf_fcp_lun, sizeof (fcp_lun_t));
	debug_event(adapter->abort_dbf, 1, &dbf_retries, sizeof (u64));
	debug_event(adapter->abort_dbf, 1, &dbf_allowed, sizeof (u64));
	debug_event(adapter->abort_dbf, 1, &dbf_timeout, sizeof (u64));
	debug_event(adapter->abort_dbf, 1, &dbf_fsf_req, sizeof (u64));
	debug_event(adapter->abort_dbf, 1, &dbf_fsf_status, sizeof (u64));
	debug_event(adapter->abort_dbf, 1, &dbf_fsf_qual[0], sizeof (u64));
	debug_event(adapter->abort_dbf, 1, &dbf_fsf_qual[1], sizeof (u64));
	debug_text_event(adapter->abort_dbf, 1, dbf_result);
#endif
	spin_lock_irq(scsi_host->host_lock);
	return retval;
}

/*
 * function:	zfcp_scsi_eh_device_reset_handler
 *
 * purpose:
 *
 * returns:
 */
int
zfcp_scsi_eh_device_reset_handler(Scsi_Cmnd * scpnt)
{
	int retval;
	struct zfcp_unit *unit = (struct zfcp_unit *) scpnt->device->hostdata;
	struct Scsi_Host *scsi_host = scpnt->device->host;

	spin_unlock_irq(scsi_host->host_lock);

	/*
	 * We should not be called to reset a target which we 'sent' faked SCSI
	 * commands since the abort of faked SCSI commands should always
	 * succeed (simply delete timer). 
	 */
	if (!unit) {
		ZFCP_LOG_NORMAL("bug: Tried to reset a non existant unit.\n");
		retval = SUCCESS;
		goto out;
	}
	ZFCP_LOG_NORMAL("Resetting Device fcp_lun=0x%Lx\n", unit->fcp_lun);

	/*
	 * If we do not know whether the unit supports 'logical unit reset'
	 * then try 'logical unit reset' and proceed with 'target reset'
	 * if 'logical unit reset' fails.
	 * If the unit is known not to support 'logical unit reset' then
	 * skip 'logical unit reset' and try 'target reset' immediately.
	 */
	if (!atomic_test_mask(ZFCP_STATUS_UNIT_NOTSUPPUNITRESET,
			      &unit->status)) {
		retval =
		    zfcp_task_management_function(unit, LOGICAL_UNIT_RESET);
		if (retval) {
			ZFCP_LOG_DEBUG
			    ("logical unit reset failed (unit=0x%lx)\n",
			     (unsigned long) unit);
			if (retval == -ENOTSUPP)
				atomic_set_mask
				    (ZFCP_STATUS_UNIT_NOTSUPPUNITRESET,
				     &unit->status);
			/* fall through and try 'target reset' next */
		} else {
			ZFCP_LOG_DEBUG
			    ("logical unit reset succeeded (unit=0x%lx)\n",
			     (unsigned long) unit);
			/* avoid 'target reset' */
			retval = SUCCESS;
			goto out;
		}
	}
	retval = zfcp_task_management_function(unit, TARGET_RESET);
	if (retval) {
		ZFCP_LOG_DEBUG("target reset failed (unit=0x%lx)\n",
			       (unsigned long) unit);
		retval = FAILED;
	} else {
		ZFCP_LOG_DEBUG("target reset succeeded (unit=0x%lx)\n",
			       (unsigned long) unit);
		retval = SUCCESS;
	}
 out:
	spin_lock_irq(scsi_host->host_lock);
	return retval;
}

static int
zfcp_task_management_function(struct zfcp_unit *unit, u8 tm_flags)
{
	struct zfcp_adapter *adapter = unit->port->adapter;
	int retval;
	int status;
	struct zfcp_fsf_req *fsf_req;

	/* issue task management function */
	fsf_req = zfcp_fsf_send_fcp_command_task_management
	    (adapter, unit, tm_flags, ZFCP_WAIT_FOR_SBAL);
	if (!fsf_req) {
		ZFCP_LOG_INFO("error: Out of resources. Could not create a "
			      "task management (abort, reset, etc) request "
			      "for the unit with FCP-LUN 0x%Lx connected to "
			      "the port with WWPN 0x%Lx connected to "
			      "the adapter %s.\n",
			      unit->fcp_lun,
			      unit->port->wwpn,
			      zfcp_get_busid_by_adapter(adapter));
		retval = -ENOMEM;
		goto out;
	}

	retval = zfcp_fsf_req_wait_and_cleanup(fsf_req,
					       ZFCP_UNINTERRUPTIBLE, &status);
	/*
	 * check completion status of task management function
	 * (status should always be valid since no signals permitted)
	 */
	if (status & ZFCP_STATUS_FSFREQ_TMFUNCFAILED)
		retval = -EIO;
	else if (status & ZFCP_STATUS_FSFREQ_TMFUNCNOTSUPP)
		retval = -ENOTSUPP;
	else
		retval = 0;
 out:
	return retval;
}

/*
 * function:	zfcp_scsi_eh_bus_reset_handler
 *
 * purpose:
 *
 * returns:
 */
int
zfcp_scsi_eh_bus_reset_handler(Scsi_Cmnd * scpnt)
{
	int retval = 0;
	struct zfcp_unit *unit;
	struct Scsi_Host *scsi_host = scpnt->device->host;

	spin_unlock_irq(scsi_host->host_lock);

	unit = (struct zfcp_unit *) scpnt->device->hostdata;
	 /*DEBUG*/
	    ZFCP_LOG_NORMAL("Resetting because of problems with "
			    "unit=0x%lx, unit_fcp_lun=0x%Lx\n",
			    (unsigned long) unit, unit->fcp_lun);
	zfcp_erp_adapter_reopen(unit->port->adapter, 0);
	zfcp_erp_wait(unit->port->adapter);
	retval = SUCCESS;

	spin_lock_irq(scsi_host->host_lock);
	return retval;
}

/*
 * function:	zfcp_scsi_eh_host_reset_handler
 *
 * purpose:
 *
 * returns:
 */
int
zfcp_scsi_eh_host_reset_handler(Scsi_Cmnd * scpnt)
{
	int retval = 0;
	struct zfcp_unit *unit;
	struct Scsi_Host *scsi_host = scpnt->device->host;

	spin_unlock_irq(scsi_host->host_lock);

	unit = (struct zfcp_unit *) scpnt->device->hostdata;
	 /*DEBUG*/
	    ZFCP_LOG_NORMAL("Resetting because of problems with "
			    "unit=0x%lx, unit_fcp_lun=0x%Lx\n",
			    (unsigned long) unit, unit->fcp_lun);
	zfcp_erp_adapter_reopen(unit->port->adapter, 0);
	zfcp_erp_wait(unit->port->adapter);
	retval = SUCCESS;

	spin_lock_irq(scsi_host->host_lock);
	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
int
zfcp_adapter_scsi_register(struct zfcp_adapter *adapter)
{
	int retval = 0;
	static unsigned int unique_id = 0;

	/* register adapter as SCSI host with mid layer of SCSI stack */
	adapter->scsi_host = scsi_host_alloc(&zfcp_data.scsi_host_template,
					     sizeof (struct zfcp_adapter *));
	if (!adapter->scsi_host) {
		ZFCP_LOG_NORMAL("error: Not enough free memory. "
				"Could not register adapter %s "
				"with the SCSI-stack.\n",
				zfcp_get_busid_by_adapter(adapter));
		retval = -EIO;
		goto out;
	}
	atomic_set_mask(ZFCP_STATUS_ADAPTER_REGISTERED, &adapter->status);
	ZFCP_LOG_DEBUG("host registered, scsi_host at 0x%lx\n",
		       (unsigned long) adapter->scsi_host);

	/* tell the SCSI stack some characteristics of this adapter */
	adapter->scsi_host->max_id = adapter->max_scsi_id + 1;
	adapter->scsi_host->max_lun = adapter->max_scsi_lun + 1;
	adapter->scsi_host->max_channel = 0;
	adapter->scsi_host->unique_id = unique_id++;	/* FIXME */
	adapter->scsi_host->max_cmd_len = ZFCP_MAX_SCSI_CMND_LENGTH;
	/*
	 * save a pointer to our own adapter data structure within
	 * hostdata field of SCSI host data structure
	 */
	adapter->scsi_host->hostdata[0] = (unsigned long) adapter;

	scsi_add_host(adapter->scsi_host, &adapter->ccw_device->dev);
 out:
	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
void
zfcp_adapter_scsi_unregister(struct zfcp_adapter *adapter)
{
	struct Scsi_Host *shost;

	shost = adapter->scsi_host;
	if (!shost)
		return;
	scsi_remove_host(shost);
	scsi_host_put(shost);

	adapter->scsi_host = NULL;
	return;
}


/**
 * zfcp_create_sbales_from_segment - creates SBALEs
 * @addr:          begin of this buffer segment
 * @length_seg:	   length of this buffer segment
 * @length_total:  total length of buffer
 * @length_min:    roll back if generated buffer smaller than this
 * @length_max:	   sum of all SBALEs (count) not larger than this
 * @buffer_index:  position of current BUFFER
 * @buffere_index: position of current BUFFERE
 * @buffer_first:  first BUFFER used for this buffer
 * @buffer_last:   last BUFFER in request queue allowed
 * @buffer:        begin of SBAL array of request queue
 * @sbtype:        storage-block type
 */
static int
zfcp_create_sbales_from_segment(unsigned long addr, int length_seg,
				int *length_total, int length_min,
				int length_max, int *buffer_index,
				int *buffere_index, int buffer_first,
				int buffer_last, struct qdio_buffer *buffer[],
				char sbtype)
{
	int retval = 0;
	int length = 0;

	ZFCP_LOG_TRACE
	    ("SCSI data buffer segment with %i bytes from 0x%lx to 0x%lx\n",
	     length_seg, addr, (addr + length_seg) - 1);

	if (!length_seg)
		goto out;

	if (addr & (PAGE_SIZE - 1)) {
		length =
		    min((int) (PAGE_SIZE - (addr & (PAGE_SIZE - 1))),
			length_seg);
		ZFCP_LOG_TRACE
		    ("address 0x%lx not on page boundary, length=0x%x\n",
		     (unsigned long) addr, length);
		retval =
		    zfcp_create_sbale(addr, length, length_total, length_min,
				      length_max, buffer_index, buffer_first,
				      buffer_last, buffere_index, buffer,
				      sbtype);
		if (retval) {
			/* no resources */
			goto out;
		}
		addr += length;
		length = length_seg - length;
	} else
		length = length_seg;

	while (length > 0) {
		retval = zfcp_create_sbale(addr, min((int) PAGE_SIZE, length),
					   length_total, length_min, length_max,
					   buffer_index, buffer_first,
					   buffer_last, buffere_index, buffer,
					   sbtype);
		if (*buffere_index > ZFCP_LAST_SBALE_PER_SBAL)
			ZFCP_LOG_NORMAL("bug: Filling output buffers with SCSI "
					"data failed. Index ran out of bounds. "
					"(debug info %d)\n", *buffere_index);
		if (retval) {
			/* no resources */
			goto out;
		}
		length -= PAGE_SIZE;
		addr += PAGE_SIZE;
	}
 out:
	return retval;
}

/**
 * zfcp_create_sbale - creates a single SBALE
 * @addr:          begin of this buffer segment
 * @length:        length of this buffer segment
 * @length_total:  total length of buffer
 * @length_min:    roll back if generated buffer smaller than this
 * @length_max:    sum of all SBALEs (count) not larger than this
 * @buffer_index:  position of current BUFFER
 * @buffer_first:  first BUFFER used for this buffer
 * @buffer_last:   last BUFFER allowed for this buffer
 * @buffere_index: position of current BUFFERE of current BUFFER
 * @buffer:        begin of SBAL array of request queue
 * @sbtype:        storage-block type
 */
static int
zfcp_create_sbale(unsigned long addr, int length, int *length_total,
		  int length_min, int length_max, int *buffer_index,
		  int buffer_first, int buffer_last, int *buffere_index,
		  struct qdio_buffer *buffer[], char sbtype)
{
	int retval = 0;
	int length_real, residual;
	int buffers_used;

	volatile struct qdio_buffer_element *buffere =
	    &(buffer[*buffer_index]->element[*buffere_index]);

	/* check whether we hit the limit */
	residual = length_max - *length_total;
	if (residual == 0) {
		ZFCP_LOG_TRACE("skip remaining %i bytes since length_max hit\n",
			       length);
		goto out;
	}
	length_real = min(length, residual);

	/*
	 * figure out next BUFFERE
	 * (first BUFFERE of first BUFFER is skipped - 
	 * this is ok since it is reserved for the QTCB)
	 */
	if (*buffere_index == ZFCP_LAST_SBALE_PER_SBAL) {
		/* last BUFFERE in this BUFFER */
		buffere->flags |= SBAL_FLAGS_LAST_ENTRY;
		/* need further BUFFER */
		if (*buffer_index == buffer_last) {
			/* queue full or last allowed BUFFER */
			buffers_used = (buffer_last - buffer_first) + 1;
			/* avoid modulo operation on negative value */
			buffers_used += QDIO_MAX_BUFFERS_PER_Q;
			buffers_used %= QDIO_MAX_BUFFERS_PER_Q;
			ZFCP_LOG_DEBUG("reached limit of number of BUFFERs "
				       "allowed for this request\n");
			/* FIXME (design) - This check is wrong and enforces the
			 * use of one SBALE less than possible 
			 */
			if ((*length_total < length_min)
			    || (buffers_used < ZFCP_MAX_SBALS_PER_REQ)) {
				ZFCP_LOG_DEBUG("Rolling back SCSI command as "
					       "there are insufficient buffers "
					       "to cover the minimum required "
					       "amount of data\n");
				/*
				 * roll back complete list of BUFFERs generated
				 * from the scatter-gather list associated
				 * with this SCSI command
				 */
				zfcp_qdio_zero_sbals(buffer,
						     buffer_first,
						     buffers_used);
				*length_total = 0;
			} else {
				/* DEBUG */
				ZFCP_LOG_NORMAL("Not enough buffers available. "
						"Can only transfer %i bytes of "
						"data\n",
						*length_total);
			}
			retval = -ENOMEM;
			goto out;
		} else {	/* *buffer_index != buffer_last */
			/* chain BUFFERs */
			*buffere_index = 0;
			buffere =
			    &(buffer[*buffer_index]->element[*buffere_index]);
			buffere->flags |= SBAL_FLAGS0_MORE_SBALS;
			(*buffer_index)++;
			*buffer_index %= QDIO_MAX_BUFFERS_PER_Q;
			buffere =
			    &(buffer[*buffer_index]->element[*buffere_index]);
			buffere->flags |= sbtype;
			ZFCP_LOG_DEBUG
			    ("Chaining previous BUFFER %i to BUFFER %i\n",
			     ((*buffer_index !=
			       0) ? *buffer_index - 1 : QDIO_MAX_BUFFERS_PER_Q -
			      1), *buffer_index);
		}
	} else { /* *buffere_index != (QDIO_MAX_ELEMENTS_PER_BUFFER - 1) */
		(*buffere_index)++;
		buffere = &(buffer[*buffer_index]->element[*buffere_index]);
	}

	/* ok, found a place for this piece, put it there */
	buffere->addr = (void *) addr;
	buffere->length = length_real;

#ifdef ZFCP_STAT_REQSIZES
	if (sbtype == SBAL_FLAGS0_TYPE_READ)
		zfcp_statistics_inc(&zfcp_data.read_sg_head, length_real);
	else
		zfcp_statistics_inc(&zfcp_data.write_sg_head, length_real);
#endif

	ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_TRACE, (char *) addr, length_real);
	ZFCP_LOG_TRACE("BUFFER no %i (0x%lx) BUFFERE no %i (0x%lx): BUFFERE "
		       "data addr 0x%lx, BUFFERE length %i, BUFFER type %i\n",
		       *buffer_index,
		       (unsigned long) &buffer[*buffer_index], *buffere_index,
		       (unsigned long) buffere, addr, length_real, sbtype);
	*length_total += length_real;
 out:
	return retval;
}

/*
 * function:    zfcp_create_sbals_from_sg
 *
 * purpose:	walks through scatter-gather list of specified SCSI command
 *		and creates a corresponding list of SBALs
 *
 * returns:	size of generated buffer in bytes 
 *
 * context:	
 */
int
zfcp_create_sbals_from_sg(struct zfcp_fsf_req *fsf_req, Scsi_Cmnd * scpnt,
			  char sbtype,	/* storage-block type */
			  int length_min, /* roll back if generated buffer */
			  int buffer_max) /* max numbers of BUFFERs */
{
	int length_total = 0;
	int buffer_index = 0;
	int buffer_last = 0;
	int buffere_index = 1;	/* elements 0 and 1 are req-id and qtcb */
	volatile struct qdio_buffer_element *buffere = NULL;
	struct zfcp_qdio_queue *req_q = NULL;
	int length_max = scpnt->request_bufflen;

	req_q = &fsf_req->adapter->request_queue;

	buffer_index = req_q->free_index;
	buffer_last = req_q->free_index +
	    min(buffer_max, atomic_read(&req_q->free_count)) - 1;
	buffer_last %= QDIO_MAX_BUFFERS_PER_Q;

	ZFCP_LOG_TRACE
	    ("total SCSI data buffer size is (scpnt->request_bufflen) %i\n",
	     scpnt->request_bufflen);
	ZFCP_LOG_TRACE
	    ("BUFFERs from (buffer_index)%i to (buffer_last)%i available\n",
	     buffer_index, buffer_last);
	ZFCP_LOG_TRACE("buffer_max=%d, req_q->free_count=%d\n", buffer_max,
		       atomic_read(&req_q->free_count));

	if (scpnt->use_sg) {
		int sg_index;
		struct scatterlist *list
		    = (struct scatterlist *) scpnt->request_buffer;

		ZFCP_LOG_DEBUG("%i (scpnt->use_sg) scatter-gather segments\n",
			       scpnt->use_sg);

		//                length_max+=0x2100;

#ifdef ZFCP_STAT_REQSIZES
		if (sbtype == SBAL_FLAGS0_TYPE_READ)
			zfcp_statistics_inc(&zfcp_data.read_sguse_head,
					    scpnt->use_sg);
		else
			zfcp_statistics_inc(&zfcp_data.write_sguse_head,
					    scpnt->use_sg);
#endif

		for (sg_index = 0; sg_index < scpnt->use_sg; sg_index++, list++)
		{
			if (zfcp_create_sbales_from_segment(
				    (page_to_pfn (list->page) << PAGE_SHIFT) +
				    list->offset,
				    list->length,
				    &length_total,
				    length_min,
				    length_max,
				    &buffer_index,
				    &buffere_index,
				    req_q->free_index,
				    buffer_last,
				    req_q->buffer,
				    sbtype))
				break;
		}
	} else {
		ZFCP_LOG_DEBUG("no scatter-gather list\n");
#ifdef ZFCP_STAT_REQSIZES
		if (sbtype == SBAL_FLAGS0_TYPE_READ)
			zfcp_statistics_inc(&zfcp_data.read_sguse_head, 1);
		else
			zfcp_statistics_inc(&zfcp_data.write_sguse_head, 1);
#endif
		zfcp_create_sbales_from_segment(
			(unsigned long) scpnt->request_buffer,
			scpnt->request_bufflen,
			&length_total,
			length_min,
			length_max,
			&buffer_index,
			&buffere_index,
			req_q->free_index,
			buffer_last,
			req_q->buffer,
			sbtype);
	}

	fsf_req->sbal_index = req_q->free_index;

	if (buffer_index >= fsf_req->sbal_index) {
		fsf_req->sbal_count = (buffer_index - fsf_req->sbal_index) + 1;
	} else {
		fsf_req->sbal_count =
		    (QDIO_MAX_BUFFERS_PER_Q - fsf_req->sbal_index) +
		    buffer_index + 1;
	}
	/* HACK */
	if ((scpnt->request_bufflen != 0) && (length_total == 0))
		goto out;

#ifdef ZFCP_STAT_REQSIZES
	if (sbtype == SBAL_FLAGS0_TYPE_READ)
		zfcp_statistics_inc(&zfcp_data.read_req_head, length_total);
	else
		zfcp_statistics_inc(&zfcp_data.write_req_head, length_total);
#endif

	buffere = &(req_q->buffer[buffer_index]->element[buffere_index]);
	buffere->flags |= SBAL_FLAGS_LAST_ENTRY;
 out:
	ZFCP_LOG_DEBUG("%i BUFFER(s) from %i to %i needed\n",
		       fsf_req->sbal_count, fsf_req->sbal_index, buffer_index);
	ZFCP_LOG_TRACE("total QDIO data buffer size is %i\n", length_total);

	return length_total;
}

void
zfcp_fsf_start_scsi_er_timer(struct zfcp_adapter *adapter)
{
	adapter->scsi_er_timer.function = zfcp_fsf_scsi_er_timeout_handler;
	adapter->scsi_er_timer.data = (unsigned long) adapter;
	adapter->scsi_er_timer.expires = jiffies + ZFCP_SCSI_ER_TIMEOUT;
	add_timer(&adapter->scsi_er_timer);
}

/**
 * zfcp_sysfs_hba_id_show - display hba_id of scsi device
 * @dev: pointer to belonging device
 * @buf: pointer to input buffer
 *
 * "hba_id" attribute of a scsi device. Displays hba_id (bus_id)
 * of the adapter belonging to a scsi device.
 */
static ssize_t
zfcp_sysfs_hba_id_show(struct device *dev, char *buf)
{
	struct scsi_device *sdev;
	struct zfcp_unit *unit;

	sdev = to_scsi_device(dev);
	unit = (struct zfcp_unit *) sdev->hostdata;
	return sprintf(buf, "%s\n", zfcp_get_busid_by_unit(unit));
}

static DEVICE_ATTR(hba_id, S_IRUGO, zfcp_sysfs_hba_id_show, NULL);

/**
 * zfcp_sysfs_wwpn_show - display wwpn of scsi device
 * @dev: pointer to belonging device
 * @buf: pointer to input buffer
 *
 * "wwpn" attribute of a scsi device. Displays wwpn of the port
 * belonging to a scsi device.
 */
static ssize_t
zfcp_sysfs_wwpn_show(struct device *dev, char *buf)
{
	struct scsi_device *sdev;
	struct zfcp_unit *unit;

	sdev = to_scsi_device(dev);
	unit = (struct zfcp_unit *) sdev->hostdata;
	return sprintf(buf, "0x%016llx\n", unit->port->wwpn);
}

static DEVICE_ATTR(wwpn, S_IRUGO, zfcp_sysfs_wwpn_show, NULL);

/**
 * zfcp_sysfs_fcp_lun_show - display fcp lun of scsi device
 * @dev: pointer to belonging device
 * @buf: pointer to input buffer
 *
 * "fcp_lun" attribute of a scsi device. Displays fcp_lun of the unit
 * belonging to a scsi device.
 */
static ssize_t
zfcp_sysfs_fcp_lun_show(struct device *dev, char *buf)
{
	struct scsi_device *sdev;
	struct zfcp_unit *unit;

	sdev = to_scsi_device(dev);
	unit = (struct zfcp_unit *) sdev->hostdata;
	return sprintf(buf, "0x%016llx\n", unit->fcp_lun);
}

static DEVICE_ATTR(fcp_lun, S_IRUGO, zfcp_sysfs_fcp_lun_show, NULL);

static struct device_attribute *zfcp_sysfs_sdev_attrs[] = {
	&dev_attr_fcp_lun,
	&dev_attr_wwpn,
	&dev_attr_hba_id,
	NULL
};

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
