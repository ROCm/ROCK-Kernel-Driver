/*
 *
 * linux/drivers/s390/scsi/zfcp_fsf.c
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

/* this drivers version (do not edit !!! generated and updated by cvs) */
#define ZFCP_FSF_C_REVISION "$Revision: 1.12 $"

#include "zfcp_ext.h"

static int zfcp_fsf_exchange_config_data_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_open_port_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_close_port_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_close_physical_port_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_open_unit_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_close_unit_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_send_fcp_command_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_send_fcp_command_task_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_send_fcp_command_task_management_handler(
	struct zfcp_fsf_req *);
static int zfcp_fsf_abort_fcp_command_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_send_generic_handler(struct zfcp_fsf_req *);
static int zfcp_fsf_status_read_handler(struct zfcp_fsf_req *);
static inline int zfcp_fsf_req_create_sbal_check(
	unsigned long *, struct zfcp_qdio_queue *, int);
static struct zfcp_fsf_req *zfcp_fsf_req_get(int, mempool_t *);
static struct zfcp_fsf_req *zfcp_fsf_req_alloc(struct zfcp_adapter *, u32, int);
static int zfcp_fsf_req_send(struct zfcp_fsf_req *, struct timer_list *);
static int zfcp_fsf_protstatus_eval(struct zfcp_fsf_req *);
static int zfcp_fsf_fsfstatus_eval(struct zfcp_fsf_req *);
static int zfcp_fsf_fsfstatus_qual_eval(struct zfcp_fsf_req *);
static int zfcp_fsf_req_dispatch(struct zfcp_fsf_req *);
static void zfcp_fsf_req_dismiss(struct zfcp_fsf_req *);

/* association between FSF command and FSF QTCB type */
static u32 fsf_qtcb_type[] = {
	[FSF_QTCB_FCP_CMND] =             FSF_IO_COMMAND,
	[FSF_QTCB_ABORT_FCP_CMND] =       FSF_SUPPORT_COMMAND,
	[FSF_QTCB_OPEN_PORT_WITH_DID] =   FSF_SUPPORT_COMMAND,
	[FSF_QTCB_OPEN_LUN] =             FSF_SUPPORT_COMMAND,
	[FSF_QTCB_CLOSE_LUN] =            FSF_SUPPORT_COMMAND,
	[FSF_QTCB_CLOSE_PORT] =           FSF_SUPPORT_COMMAND,
	[FSF_QTCB_CLOSE_PHYSICAL_PORT] =  FSF_SUPPORT_COMMAND,
	[FSF_QTCB_SEND_ELS] =             FSF_SUPPORT_COMMAND,
	[FSF_QTCB_SEND_GENERIC] =         FSF_SUPPORT_COMMAND,
	[FSF_QTCB_EXCHANGE_CONFIG_DATA] = FSF_CONFIG_COMMAND
};

/****************************************************************/
/*************** FSF related Functions  *************************/
/****************************************************************/

#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_FSF
#define ZFCP_LOG_AREA_PREFIX		ZFCP_LOG_AREA_PREFIX_FSF

/*
 * function:	zfcp_fsf_req_alloc
 *
 * purpose:     Obtains an fsf_req and potentially a qtcb (for all but 
 *              unsolicited requests) via helper functions
 *              Does some initial fsf request set-up.
 *              
 * returns:	pointer to allocated fsf_req if successfull
 *              NULL otherwise
 *
 * locks:       none
 *
 */
static struct zfcp_fsf_req *
zfcp_fsf_req_alloc(struct zfcp_adapter *adapter, u32 fsf_cmd, int kmalloc_flags)
{
	struct zfcp_fsf_req *fsf_req = NULL;

	switch (fsf_cmd) {

	case FSF_QTCB_FCP_CMND:
	case FSF_QTCB_ABORT_FCP_CMND:
		fsf_req = zfcp_fsf_req_get(kmalloc_flags,
					   adapter->pool.fcp_command_fsf);
		if (fsf_req && (fsf_req->status & ZFCP_STATUS_FSFREQ_POOL)) {
			/*
			 * watch low mem buffer
			 * Note: If the command is reset or aborted, two
			 * timeouts (this and the SCSI ER one) will be started
			 * for the command. There is no problem however as
			 * the first expired timer will call adapter_reopen
			 * which will delete the other 
			 */
			adapter->pool.fcp_command_fsf_timer.expires =
			    jiffies + ZFCP_ERP_SCSI_LOW_MEM_TIMEOUT;
			add_timer(&adapter->pool.fcp_command_fsf_timer);
		}
#ifdef ZFCP_DEBUG_REQUESTS
		debug_text_event(adapter->req_dbf, 5, "fsfa_fcp");
		if (fsf_req && (fsf_req->status & ZFCP_STATUS_FSFREQ_POOL))
			debug_text_event(adapter->req_dbf, 5, "fsfa_pl");
#endif /* ZFCP_DEBUG_REQUESTS */
		break;

	case FSF_QTCB_OPEN_PORT_WITH_DID:
	case FSF_QTCB_OPEN_LUN:
	case FSF_QTCB_CLOSE_LUN:
	case FSF_QTCB_CLOSE_PORT:
	case FSF_QTCB_CLOSE_PHYSICAL_PORT:
	case FSF_QTCB_SEND_ELS:
	case FSF_QTCB_EXCHANGE_CONFIG_DATA:
	case FSF_QTCB_SEND_GENERIC:
		fsf_req =
		    zfcp_fsf_req_get(kmalloc_flags, adapter->pool.erp_fsf);
#ifdef ZFCP_DEBUG_REQUESTS
		debug_text_event(adapter->req_dbf, 5, "fsfa_erp");
		if (fsf_req && (fsf_req->status & ZFCP_STATUS_FSFREQ_POOL))
			debug_text_event(adapter->req_dbf, 5, "fsfa_pl");
#endif				/* ZFCP_DEBUG_REQUESTS */
		break;

	case FSF_QTCB_UNSOLICITED_STATUS:
		fsf_req =
		    mempool_alloc(adapter->pool.status_read_fsf, GFP_ATOMIC);
		if (fsf_req) {
			memset(fsf_req, 0, sizeof (struct zfcp_fsf_req));
			fsf_req->status |= ZFCP_STATUS_FSFREQ_POOL;
		} else
			ZFCP_LOG_NORMAL("bug: could not find free fsf_req\n");
#ifdef ZFCP_DEBUG_REQUESTS
		debug_text_event(adapter->req_dbf, 5, "fsfa_sr");
		debug_text_event(adapter->req_dbf, 5, "fsfa_pl");
#endif				/* ZFCP_DEBUG_REQUESTS */
		break;

	default:
		ZFCP_LOG_NORMAL("bug: An attempt to send an unsupported "
				"command has been detected. "
				"(debug info 0x%x)\n", fsf_cmd);
	}			//switch(fsf_cmd)

	if (!fsf_req) {
		ZFCP_LOG_DEBUG("error: Out of memory. Allocation of FSF "
			       "request structure failed\n");
	} else {
		ZFCP_LOG_TRACE("FSF request allocated at 0x%lx, "
			       "adapter 0x%lx (%s)\n",
			       (unsigned long) fsf_req,
			       (unsigned long) adapter,
			       zfcp_get_busid_by_adapter(adapter));
	}

#ifdef ZFCP_DEBUG_REQUESTS
	debug_event(adapter->req_dbf, 5, &fsf_req, sizeof (unsigned long));
	if (fsf_req->qtcb)
		debug_event(adapter->req_dbf, 5, &fsf_req->qtcb,
			    sizeof (unsigned long));
#endif				/* ZFCP_DEBUG_REQUESTS */

	return fsf_req;
}

/*
 * function:	zfcp_fsf_req_free
 *
 * purpose:     Frees the memory of an fsf_req (and potentially a qtcb) or
 *              returns it into the pool via helper functions.
 *
 * returns:     sod all
 *
 * locks:       none
 */
void
zfcp_fsf_req_free(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_adapter *adapter = fsf_req->adapter;

	switch (fsf_req->fsf_command) {

	case FSF_QTCB_FCP_CMND:
	case FSF_QTCB_ABORT_FCP_CMND:
		if (fsf_req->status & ZFCP_STATUS_FSFREQ_POOL) {
			del_timer(&adapter->pool.fcp_command_fsf_timer);
			mempool_free(fsf_req, adapter->pool.fcp_command_fsf);
		} else
			kfree(fsf_req);
		break;

	case FSF_QTCB_OPEN_PORT_WITH_DID:
	case FSF_QTCB_OPEN_LUN:
	case FSF_QTCB_CLOSE_LUN:
	case FSF_QTCB_CLOSE_PORT:
	case FSF_QTCB_CLOSE_PHYSICAL_PORT:
	case FSF_QTCB_SEND_ELS:
	case FSF_QTCB_EXCHANGE_CONFIG_DATA:
	case FSF_QTCB_SEND_GENERIC:
		if (fsf_req->status & ZFCP_STATUS_FSFREQ_POOL)
			mempool_free(fsf_req, adapter->pool.erp_fsf);
		else
			kfree(fsf_req);
		break;

	case FSF_QTCB_UNSOLICITED_STATUS:
		mempool_free(fsf_req, adapter->pool.status_read_fsf);
		break;
	}
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 *
 * note: qdio queues shall be down (no ongoing inbound processing)
 */
int
zfcp_fsf_req_dismiss_all(struct zfcp_adapter *adapter)
{
	int retval = 0;
	struct zfcp_fsf_req *fsf_req, *tmp;

	list_for_each_entry_safe(fsf_req, tmp, &adapter->fsf_req_list_head,
				 list)
	    zfcp_fsf_req_dismiss(fsf_req);
	/* wait_event_timeout? */
	while (!list_empty(&adapter->fsf_req_list_head)) {
		ZFCP_LOG_DEBUG("fsf req list of adapter %s not yet empty\n",
			       zfcp_get_busid_by_adapter(adapter));
		/* wait for woken intiators to clean up their requests */
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(ZFCP_FSFREQ_CLEANUP_TIMEOUT);
	}

	/* consistency check */
	if (atomic_read(&adapter->fsf_reqs_active)) {
		ZFCP_LOG_NORMAL("bug: There are still %d FSF requests pending "
				"on adapter %s after cleanup.\n",
				atomic_read(&adapter->fsf_reqs_active),
				zfcp_get_busid_by_adapter(adapter));
		atomic_set(&adapter->fsf_reqs_active, 0);
	}

	return retval;
}

/*
 * function:	
 *
 * purpose:	
 *
 * returns:
 */
static void
zfcp_fsf_req_dismiss(struct zfcp_fsf_req *fsf_req)
{
	fsf_req->status |= ZFCP_STATUS_FSFREQ_DISMISSED;
	zfcp_fsf_req_complete(fsf_req);
}

/*
 * function:    zfcp_fsf_req_complete
 *
 * purpose:	Updates active counts and timers for openfcp-reqs
 *              May cleanup request after req_eval returns
 *
 * returns:	0 - success
 *		!0 - failure
 *
 * context:	
 */
int
zfcp_fsf_req_complete(struct zfcp_fsf_req *fsf_req)
{
	int retval = 0;
	int cleanup;
	struct zfcp_adapter *adapter = fsf_req->adapter;

	/* do some statistics */
	atomic_dec(&adapter->fsf_reqs_active);

	if (fsf_req->fsf_command == FSF_QTCB_UNSOLICITED_STATUS) {
		ZFCP_LOG_DEBUG("Status read response received\n");
		/*
		 * Note: all cleanup handling is done in the callchain of
		 * the function call-chain below.
		 */
		zfcp_fsf_status_read_handler(fsf_req);
		goto out;
	} else
		zfcp_fsf_protstatus_eval(fsf_req);

	/*
	 * fsf_req may be deleted due to waking up functions, so 
	 * cleanup is saved here and used later 
	 */
	if (fsf_req->status & ZFCP_STATUS_FSFREQ_CLEANUP)
		cleanup = 1;
	else
		cleanup = 0;

	fsf_req->status |= ZFCP_STATUS_FSFREQ_COMPLETED;

	/* cleanup request if requested by initiator */
	if (cleanup) {
		ZFCP_LOG_TRACE("removing FSF request 0x%lx\n",
			       (unsigned long) fsf_req);
		/*
		 * lock must not be held here since it will be
		 * grabed by the called routine, too
		 */
		zfcp_fsf_req_cleanup(fsf_req);
	} else {
		/* notify initiator waiting for the requests completion */
		ZFCP_LOG_TRACE("waking initiator of FSF request 0x%lx\n",
			       (unsigned long) fsf_req);
		wake_up(&fsf_req->completion_wq);
	}

 out:
	return retval;
}

/*
 * function:    zfcp_fsf_protstatus_eval
 *
 * purpose:	evaluates the QTCB of the finished FSF request
 *		and initiates appropriate actions
 *		(usually calling FSF command specific handlers)
 *
 * returns:	
 *
 * context:	
 *
 * locks:
 */
static int
zfcp_fsf_protstatus_eval(struct zfcp_fsf_req *fsf_req)
{
	int retval = 0;
	struct zfcp_adapter *adapter = fsf_req->adapter;

	ZFCP_LOG_DEBUG("QTCB is at 0x%lx\n", (unsigned long) fsf_req->qtcb);

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_DISMISSED) {
		ZFCP_LOG_DEBUG("fsf_req 0x%lx has been dismissed\n",
			       (unsigned long) fsf_req);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			ZFCP_STATUS_FSFREQ_RETRY; /* only for SCSI cmnds. */
		zfcp_cmd_dbf_event_fsf("dismiss", fsf_req, NULL, 0);
		goto skip_protstatus;
	}

	/* log additional information provided by FSF (if any) */
	if (fsf_req->qtcb->header.log_length) {
		/* do not trust them ;-) */
		if (fsf_req->qtcb->header.log_start > ZFCP_QTCB_SIZE) {
			ZFCP_LOG_NORMAL
			    ("bug: ULP (FSF logging) log data starts "
			     "beyond end of packet header. Ignored. "
			     "(start=%i, size=%li)\n",
			     fsf_req->qtcb->header.log_start, ZFCP_QTCB_SIZE);
			goto forget_log;
		}
		if ((fsf_req->qtcb->header.log_start +
		     fsf_req->qtcb->header.log_length)
		    > ZFCP_QTCB_SIZE) {
			ZFCP_LOG_NORMAL("bug: ULP (FSF logging) log data ends "
					"beyond end of packet header. Ignored. "
					"(start=%i, length=%i, size=%li)\n",
					fsf_req->qtcb->header.log_start,
					fsf_req->qtcb->header.log_length,
					ZFCP_QTCB_SIZE);
			goto forget_log;
		}
		ZFCP_LOG_TRACE("ULP log data: \n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_TRACE,
			      (char *) fsf_req->qtcb +
			      fsf_req->qtcb->header.log_start,
			      fsf_req->qtcb->header.log_length);
	}
 forget_log:

	/* evaluate FSF Protocol Status */
	switch (fsf_req->qtcb->prefix.prot_status) {

	case FSF_PROT_GOOD:
		ZFCP_LOG_TRACE("FSF_PROT_GOOD\n");
		break;

	case FSF_PROT_FSF_STATUS_PRESENTED:
		ZFCP_LOG_TRACE("FSF_PROT_FSF_STATUS_PRESENTED\n");
		break;

	case FSF_PROT_QTCB_VERSION_ERROR:
		ZFCP_LOG_FLAGS(0, "FSF_PROT_QTCB_VERSION_ERROR\n");
		/* DEBUG */
		ZFCP_LOG_NORMAL("fsf_req=0x%lx, qtcb=0x%lx (0x%lx, 0x%lx)\n",
				(unsigned long) fsf_req,
				(unsigned long) fsf_req->qtcb,
				((unsigned long) fsf_req) & 0xFFFFFF00,
				(unsigned
				 long) ((struct zfcp_fsf_req
					 *) (((unsigned long) fsf_req) &
					     0xFFFFFF00))->qtcb);
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
			      (char *) (((unsigned long) fsf_req) & 0xFFFFFF00),
			      sizeof (struct zfcp_fsf_req));
		ZFCP_LOG_NORMAL("error: The adapter %s contains "
				"microcode of version 0x%x, the device driver "
				"only supports 0x%x. Aborting.\n",
				zfcp_get_busid_by_adapter(adapter),
				fsf_req->qtcb->prefix.prot_status_qual.
				version_error.fsf_version, ZFCP_QTCB_VERSION);
		/* stop operation for this adapter */
		debug_text_exception(adapter->erp_dbf, 0, "prot_ver_err");
		zfcp_erp_adapter_shutdown(adapter, 0);
		zfcp_cmd_dbf_event_fsf("qverserr", fsf_req,
				       &fsf_req->qtcb->prefix.prot_status_qual,
				       sizeof (union fsf_prot_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PROT_SEQ_NUMB_ERROR:
		ZFCP_LOG_FLAGS(0, "FSF_PROT_SEQ_NUMB_ERROR\n");
		ZFCP_LOG_NORMAL("bug: Sequence number mismatch between "
				"driver (0x%x) and adapter %s (0x%x). "
				"Restarting all operations on this adapter.\n",
				fsf_req->qtcb->prefix.req_seq_no,
				zfcp_get_busid_by_adapter(adapter),
				fsf_req->qtcb->prefix.prot_status_qual.
				sequence_error.exp_req_seq_no);
#ifdef ZFCP_DEBUG_REQUESTS
		debug_text_event(adapter->req_dbf, 1, "exp_seq!");
		debug_event(adapter->req_dbf, 1,
			    &fsf_req->qtcb->prefix.prot_status_qual.
			    sequence_error.exp_req_seq_no, 4);
		debug_text_event(adapter->req_dbf, 1, "qtcb_seq!");
		debug_exception(adapter->req_dbf, 1,
				&fsf_req->qtcb->prefix.req_seq_no, 4);
#endif				/* ZFCP_DEBUG_REQUESTS */
		debug_text_exception(adapter->erp_dbf, 0, "prot_seq_err");
		/* restart operation on this adapter */
		zfcp_erp_adapter_reopen(adapter, 0);
		zfcp_cmd_dbf_event_fsf("seqnoerr", fsf_req,
				       &fsf_req->qtcb->prefix.prot_status_qual,
				       sizeof (union fsf_prot_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_RETRY;
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PROT_UNSUPP_QTCB_TYPE:
		ZFCP_LOG_FLAGS(0, "FSF_PROT_UNSUP_QTCB_TYPE\n");
		ZFCP_LOG_NORMAL("error: Packet header type used by the "
				"device driver is incompatible with "
				"that used on adapter %s. "
				"Stopping all operations on this adapter.\n",
				zfcp_get_busid_by_adapter(adapter));
		ZFCP_LOG_NORMAL("fsf_req=0x%lx, qtcb=0x%lx (0x%lx, 0x%lx)\n",
				(unsigned long) fsf_req,
				(unsigned long) fsf_req->qtcb,
				((unsigned long) fsf_req) & 0xFFFFFF00,
				(unsigned long) (
					(struct zfcp_fsf_req *) (
						((unsigned long)
						 fsf_req) & 0xFFFFFF00))->qtcb);
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
			      (char *) (((unsigned long) fsf_req) & 0xFFFFFF00),
			      sizeof (struct zfcp_fsf_req));
		debug_text_exception(adapter->erp_dbf, 0, "prot_unsup_qtcb");
		zfcp_erp_adapter_shutdown(adapter, 0);
		zfcp_cmd_dbf_event_fsf("unsqtcbt", fsf_req,
				       &fsf_req->qtcb->prefix.prot_status_qual,
				       sizeof (union fsf_prot_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PROT_HOST_CONNECTION_INITIALIZING:
		ZFCP_LOG_FLAGS(1, "FSF_PROT_HOST_CONNECTION_INITIALIZING\n");
		zfcp_cmd_dbf_event_fsf("hconinit", fsf_req,
				       &fsf_req->qtcb->prefix.prot_status_qual,
				       sizeof (union fsf_prot_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		atomic_set_mask(ZFCP_STATUS_ADAPTER_HOST_CON_INIT,
				&(adapter->status));
		debug_text_event(adapter->erp_dbf, 4, "prot_con_init");
		break;

	case FSF_PROT_DUPLICATE_REQUEST_ID:
		ZFCP_LOG_FLAGS(0, "FSF_PROT_DUPLICATE_REQUEST_IDS\n");
		if (fsf_req->qtcb) {
			ZFCP_LOG_NORMAL("bug: The request identifier  0x%Lx "
					"to the adapter %s is ambiguous. "
					"Stopping all operations on this "
					"adapter.\n",
					*(unsigned long long *)
					(&fsf_req->qtcb->bottom.support.
					 req_handle),
					zfcp_get_busid_by_adapter(adapter));
		} else {
			ZFCP_LOG_NORMAL("bug: The request identifier  0x%lx "
					"to the adapter %s is ambiguous. "
					"Stopping all operations on this "
					"adapter. "
					"(bug: got this for an unsolicited "
					"status read request)\n",
					(unsigned long) fsf_req,
					zfcp_get_busid_by_adapter(adapter));
		}
		debug_text_exception(adapter->erp_dbf, 0, "prot_dup_id");
		zfcp_erp_adapter_shutdown(adapter, 0);
		zfcp_cmd_dbf_event_fsf("dupreqid", fsf_req,
				       &fsf_req->qtcb->prefix.prot_status_qual,
				       sizeof (union fsf_prot_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PROT_LINK_DOWN:
		ZFCP_LOG_FLAGS(1, "FSF_PROT_LINK_DOWN\n");
		/*
		 * 'test and set' is not atomic here -
		 * it's ok as long as calls to our response queue handler
		 * (and thus execution of this code here) are serialized
		 * by the qdio module
		 */
		if (!atomic_test_mask(ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED,
				      &adapter->status)) {
			switch (fsf_req->qtcb->prefix.prot_status_qual.
				locallink_error.code) {
			case FSF_PSQ_LINK_NOLIGHT:
				ZFCP_LOG_INFO("The local link to adapter %s "
					      "is down (no light detected).\n",
					      zfcp_get_busid_by_adapter(
						      adapter));
				break;
			case FSF_PSQ_LINK_WRAPPLUG:
				ZFCP_LOG_INFO("The local link to adapter %s "
					      "is down (wrap plug detected).\n",
					      zfcp_get_busid_by_adapter(
						      adapter));
				break;
			case FSF_PSQ_LINK_NOFCP:
				ZFCP_LOG_INFO("The local link to adapter %s "
					      "is down (adjacent node on "
					      "link does not support FCP).\n",
					      zfcp_get_busid_by_adapter(
						      adapter));
				break;
			default:
				ZFCP_LOG_INFO("The local link to adapter %s "
					      "is down "
					      "(warning: unknown reason "
					      "code).\n",
					      zfcp_get_busid_by_adapter(
						      adapter));
				break;

			}
			/*
			 * Due to the 'erp failed' flag the adapter won't
			 * be recovered but will be just set to 'blocked'
			 * state. All subordinary devices will have state
			 * 'blocked' and 'erp failed', too.
			 * Thus the adapter is still able to provide
			 * 'link up' status without being flooded with
			 * requests.
			 * (note: even 'close port' is not permitted)
			 */
			ZFCP_LOG_INFO("Stopping all operations for adapter "
				      "%s.\n",
				      zfcp_get_busid_by_adapter(adapter));
			atomic_set_mask(ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED |
					ZFCP_STATUS_COMMON_ERP_FAILED,
					&adapter->status);
			zfcp_erp_adapter_reopen(adapter, 0);
			debug_text_event(adapter->erp_dbf, 1, "prot_link_down");
		}
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PROT_REEST_QUEUE:
		ZFCP_LOG_FLAGS(1, "FSF_PROT_REEST_QUEUE\n");
		debug_text_event(adapter->erp_dbf, 1, "prot_reest_queue");
		ZFCP_LOG_INFO("The local link to adapter with "
			      "%s was re-plugged. "
			      "Re-starting operations on this adapter.\n",
			      zfcp_get_busid_by_adapter(adapter));
		/* All ports should be marked as ready to run again */
		zfcp_erp_modify_adapter_status(adapter,
					       ZFCP_STATUS_COMMON_RUNNING,
					       ZFCP_SET);
		zfcp_erp_adapter_reopen(adapter,
					ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED
					| ZFCP_STATUS_COMMON_ERP_FAILED);
		zfcp_cmd_dbf_event_fsf("reestque", fsf_req,
				       &fsf_req->qtcb->prefix.prot_status_qual,
				       sizeof (union fsf_prot_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PROT_ERROR_STATE:
		ZFCP_LOG_FLAGS(0, "FSF_PROT_ERROR_STATE\n");
		ZFCP_LOG_NORMAL("error: The adapter %s "
				"has entered the error state. "
				"Restarting all operations on this "
				"adapter.\n",
				zfcp_get_busid_by_adapter(adapter));
		debug_text_event(adapter->erp_dbf, 0, "prot_err_sta");
		/* restart operation on this adapter */
		zfcp_erp_adapter_reopen(adapter, 0);
		zfcp_cmd_dbf_event_fsf("proterrs", fsf_req,
				       &fsf_req->qtcb->prefix.prot_status_qual,
				       sizeof (union fsf_prot_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_RETRY;
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	default:
		ZFCP_LOG_NORMAL("bug: Transfer protocol status information "
				"provided by the adapter %s "
				"is not compatible with the device driver. "
				"Stopping all operations on this adapter. "
				"(debug info 0x%x).\n",
				zfcp_get_busid_by_adapter(adapter),
				fsf_req->qtcb->prefix.prot_status);
		ZFCP_LOG_NORMAL("fsf_req=0x%lx, qtcb=0x%lx (0x%lx, 0x%lx)\n",
				(unsigned long) fsf_req,
				(unsigned long) fsf_req->qtcb,
				((unsigned long) fsf_req) & 0xFFFFFF00,
				(unsigned
				 long) ((struct zfcp_fsf_req
					 *) (((unsigned long) fsf_req) &
					     0xFFFFFF00))->qtcb);
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
			      (char *) (((unsigned long) fsf_req) & 0xFFFFFF00),
			      sizeof (struct zfcp_fsf_req));
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL, (char *) fsf_req->qtcb,
			      ZFCP_QTCB_SIZE);
		debug_text_event(adapter->erp_dbf, 0, "prot_inval:");
		debug_exception(adapter->erp_dbf, 0,
				&fsf_req->qtcb->prefix.prot_status,
				sizeof (u32));
		zfcp_erp_adapter_shutdown(adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
	}

 skip_protstatus:
	/*
	 * always call specific handlers to give them a chance to do
	 * something meaningful even in error cases
	 */
	zfcp_fsf_fsfstatus_eval(fsf_req);
	return retval;
}

/*
 * function:	zfcp_fsf_fsfstatus_eval
 *
 * purpose:	evaluates FSF status of completed FSF request
 *		and acts accordingly
 *
 * returns:
 */
static int
zfcp_fsf_fsfstatus_eval(struct zfcp_fsf_req *fsf_req)
{
	int retval = 0;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		goto skip_fsfstatus;
	}

	/* evaluate FSF Status */
	switch (fsf_req->qtcb->header.fsf_status) {
	case FSF_UNKNOWN_COMMAND:
		ZFCP_LOG_FLAGS(0, "FSF_UNKNOWN_COMMAND\n");
		ZFCP_LOG_NORMAL("bug: Command issued by the device driver is "
				"not known by the adapter %s "
				"Stopping all operations on this adapter. "
				"(debug info 0x%x).\n",
				zfcp_get_busid_by_adapter(fsf_req->adapter),
				fsf_req->qtcb->header.fsf_command);
		debug_text_exception(fsf_req->adapter->erp_dbf, 0,
				     "fsf_s_unknown");
		zfcp_erp_adapter_shutdown(fsf_req->adapter, 0);
		zfcp_cmd_dbf_event_fsf("unknownc", fsf_req,
				       &fsf_req->qtcb->header.fsf_status_qual,
				       sizeof (union fsf_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_FCP_RSP_AVAILABLE:
		ZFCP_LOG_FLAGS(2, "FSF_FCP_RSP_AVAILABLE\n");
		ZFCP_LOG_DEBUG("FCP Sense data will be presented to the "
			       "SCSI stack.\n");
		debug_text_event(fsf_req->adapter->erp_dbf, 3, "fsf_s_rsp");
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		ZFCP_LOG_FLAGS(2, "FSF_ADAPTER_STATUS_AVAILABLE\n");
		debug_text_event(fsf_req->adapter->erp_dbf, 2, "fsf_s_astatus");
		zfcp_fsf_fsfstatus_qual_eval(fsf_req);
		break;

	default:
		break;
	}

 skip_fsfstatus:
	/*
	 * always call specific handlers to give them a chance to do
	 * something meaningful even in error cases
	 */
	zfcp_fsf_req_dispatch(fsf_req);

	return retval;
}

/*
 * function:	zfcp_fsf_fsfstatus_qual_eval
 *
 * purpose:	evaluates FSF status-qualifier of completed FSF request
 *		and acts accordingly
 *
 * returns:
 */
static int
zfcp_fsf_fsfstatus_qual_eval(struct zfcp_fsf_req *fsf_req)
{
	int retval = 0;

	switch (fsf_req->qtcb->header.fsf_status_qual.word[0]) {
	case FSF_SQ_FCP_RSP_AVAILABLE:
		ZFCP_LOG_FLAGS(2, "FSF_SQ_FCP_RSP_AVAILABLE\n");
		debug_text_event(fsf_req->adapter->erp_dbf, 4, "fsf_sq_rsp");
		break;
	case FSF_SQ_RETRY_IF_POSSIBLE:
		ZFCP_LOG_FLAGS(2, "FSF_SQ_RETRY_IF_POSSIBLE\n");
		/* The SCSI-stack may now issue retries or escalate */
		debug_text_event(fsf_req->adapter->erp_dbf, 2, "fsf_sq_retry");
		zfcp_cmd_dbf_event_fsf("sqretry", fsf_req,
				       &fsf_req->qtcb->header.fsf_status_qual,
				       sizeof (union fsf_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_SQ_COMMAND_ABORTED:
		ZFCP_LOG_FLAGS(2, "FSF_SQ_COMMAND_ABORTED\n");
		/* Carry the aborted state on to upper layer */
		debug_text_event(fsf_req->adapter->erp_dbf, 2, "fsf_sq_abort");
		zfcp_cmd_dbf_event_fsf("sqabort", fsf_req,
				       &fsf_req->qtcb->header.fsf_status_qual,
				       sizeof (union fsf_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ABORTED;
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_SQ_NO_RECOM:
		ZFCP_LOG_FLAGS(0, "FSF_SQ_NO_RECOM\n");
		debug_text_exception(fsf_req->adapter->erp_dbf, 0,
				     "fsf_sq_no_rec");
		ZFCP_LOG_NORMAL("bug: No recommendation could be given for a"
				"problem on the adapter %s "
				"Stopping all operations on this adapter. ",
				zfcp_get_busid_by_adapter(fsf_req->adapter));
		zfcp_erp_adapter_shutdown(fsf_req->adapter, 0);
		zfcp_cmd_dbf_event_fsf("sqnrecom", fsf_req,
				       &fsf_req->qtcb->header.fsf_status_qual,
				       sizeof (union fsf_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_SQ_ULP_PROGRAMMING_ERROR:
		ZFCP_LOG_FLAGS(0, "FSF_SQ_ULP_PROGRAMMING_ERROR\n");
		ZFCP_LOG_NORMAL("bug: An illegal amount of data was attempted "
				"to be sent to the adapter %s "
				"Stopping all operations on this adapter. ",
				zfcp_get_busid_by_adapter(fsf_req->adapter));
		debug_text_exception(fsf_req->adapter->erp_dbf, 0,
				     "fsf_sq_ulp_err");
		zfcp_erp_adapter_shutdown(fsf_req->adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
	case FSF_SQ_NO_RETRY_POSSIBLE:
	case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
		/* dealt with in the respective functions */
		break;
	default:
		ZFCP_LOG_NORMAL("bug: Additional status info could "
				"not be interpreted properly.\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
			      (char *) &fsf_req->qtcb->header.fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 0, "fsf_sq_inval:");
		debug_exception(fsf_req->adapter->erp_dbf, 0,
				&fsf_req->qtcb->header.fsf_status_qual.word[0],
				sizeof (u32));
		zfcp_cmd_dbf_event_fsf("squndef", fsf_req,
				       &fsf_req->qtcb->header.fsf_status_qual,
				       sizeof (union fsf_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;
	}

	return retval;
}

/*
 * function:	zfcp_fsf_req_dispatch
 *
 * purpose:	calls the appropriate command specific handler
 *
 * returns:	
 */
static int
zfcp_fsf_req_dispatch(struct zfcp_fsf_req *fsf_req)
{
	int retval = 0;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		ZFCP_LOG_TRACE("fsf_req=0x%lx, QTCB=0x%lx\n",
			       (unsigned long) fsf_req,
			       (unsigned long) (fsf_req->qtcb));
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_TRACE,
			      (char *) fsf_req->qtcb, ZFCP_QTCB_SIZE);
	}

	switch (fsf_req->fsf_command) {

	case FSF_QTCB_FCP_CMND:
		ZFCP_LOG_FLAGS(3, "FSF_QTCB_FCP_CMND\n");
		zfcp_fsf_send_fcp_command_handler(fsf_req);
		break;

	case FSF_QTCB_ABORT_FCP_CMND:
		ZFCP_LOG_FLAGS(2, "FSF_QTCB_ABORT_FCP_CMND\n");
		zfcp_fsf_abort_fcp_command_handler(fsf_req);
		break;

	case FSF_QTCB_SEND_GENERIC:
		ZFCP_LOG_FLAGS(2, "FSF_QTCB_SEND_GENERIC\n");
		zfcp_fsf_send_generic_handler(fsf_req);
		zfcp_erp_fsf_req_handler(fsf_req);
		break;

	case FSF_QTCB_OPEN_PORT_WITH_DID:
		ZFCP_LOG_FLAGS(2, "FSF_QTCB_OPEN_PORT_WITH_DID\n");
		zfcp_fsf_open_port_handler(fsf_req);
		zfcp_erp_fsf_req_handler(fsf_req);
		break;

	case FSF_QTCB_OPEN_LUN:
		ZFCP_LOG_FLAGS(2, "FSF_QTCB_OPEN_LUN\n");
		zfcp_fsf_open_unit_handler(fsf_req);
		zfcp_erp_fsf_req_handler(fsf_req);
		break;

	case FSF_QTCB_CLOSE_LUN:
		ZFCP_LOG_FLAGS(2, "FSF_QTCB_CLOSE_LUN\n");
		zfcp_fsf_close_unit_handler(fsf_req);
		zfcp_erp_fsf_req_handler(fsf_req);
		break;

	case FSF_QTCB_CLOSE_PORT:
		ZFCP_LOG_FLAGS(2, "FSF_QTCB_CLOSE_PORT\n");
		zfcp_fsf_close_port_handler(fsf_req);
		zfcp_erp_fsf_req_handler(fsf_req);
		break;

	case FSF_QTCB_CLOSE_PHYSICAL_PORT:
		ZFCP_LOG_FLAGS(2, "FSF_QTCB_CLOSE_PHYSICAL_PORT\n");
		zfcp_fsf_close_physical_port_handler(fsf_req);
		zfcp_erp_fsf_req_handler(fsf_req);
		break;

	case FSF_QTCB_EXCHANGE_CONFIG_DATA:
		ZFCP_LOG_FLAGS(2, "FSF_QTCB_EXCHANGE_CONFIG_DATA\n");
		zfcp_fsf_exchange_config_data_handler(fsf_req);
		zfcp_erp_fsf_req_handler(fsf_req);
		break;

	default:
		ZFCP_LOG_FLAGS(2, "FSF_QTCB_UNKNOWN\n");
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		ZFCP_LOG_NORMAL("bug: Command issued by the device driver is "
				"not supported by the adapter %s "
				"(debug info 0x%lx 0x%x).\n",
				zfcp_get_busid_by_adapter(fsf_req->adapter),
				(unsigned long) fsf_req, fsf_req->fsf_command);
		if (fsf_req->fsf_command != fsf_req->qtcb->header.fsf_command)
			ZFCP_LOG_NORMAL
			    ("bug: Command issued by the device driver differs "
			     "from the command returned by the adapter %s "
			     "(debug info 0x%x, 0x%x).\n",
			     zfcp_get_busid_by_adapter(fsf_req->adapter),
			     fsf_req->fsf_command,
			     fsf_req->qtcb->header.fsf_command);
	}

	return retval;
}

/*
 * function:    zfcp_fsf_status_read
 *
 * purpose:	initiates a Status Read command at the specified adapter
 *
 * returns:
 */
int
zfcp_fsf_status_read(struct zfcp_adapter *adapter, int req_flags)
{
	struct zfcp_fsf_req *fsf_req;
	struct fsf_status_read_buffer *status_buffer;
	unsigned long lock_flags;
	volatile struct qdio_buffer_element *buffere;
	struct zfcp_qdio_queue *req_queue = &adapter->request_queue;
	int retval = 0;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(adapter,
				     FSF_QTCB_UNSOLICITED_STATUS,
				     &lock_flags, req_flags, &fsf_req);
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Out of resources. Could not create an "
			      "unsolicited status buffer for "
			      "the adapter %s.\n",
			      zfcp_get_busid_by_adapter(adapter));
		goto failed_req_create;
	}

	status_buffer =
	    mempool_alloc(adapter->pool.status_read_buf, GFP_ATOMIC);
	if (!status_buffer) {
		ZFCP_LOG_NORMAL("bug: could not get some buffer\n");
		goto failed_buf;
	}
	memset(status_buffer, 0, sizeof (struct fsf_status_read_buffer));
	fsf_req->data.status_read.buffer = status_buffer;

	/* insert pointer to respective buffer */
	buffere = req_queue->buffer[fsf_req->sbal_index]->element;
	buffere[2].addr = (void *) status_buffer;
	buffere[2].length = sizeof (struct fsf_status_read_buffer);

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(fsf_req, NULL);
	if (retval) {
		ZFCP_LOG_DEBUG("error: Could not set-up unsolicited status "
			       "environment.\n");
		goto failed_req_send;
	}

	ZFCP_LOG_TRACE("Status Read request initiated "
		       "(adapter busid=%s)\n",
		       zfcp_get_busid_by_adapter(adapter));
#ifdef ZFCP_DEBUG_REQUESTS
	debug_text_event(adapter->req_dbf, 1, "unso");
#endif
	goto out;

 failed_req_send:
	mempool_free(status_buffer, adapter->pool.status_read_buf);

 failed_buf:
	zfcp_fsf_req_free(fsf_req);
 failed_req_create:
 out:
	write_unlock_irqrestore(&adapter->request_queue.queue_lock, lock_flags);
	return retval;
}

static int
zfcp_fsf_status_read_port_closed(struct zfcp_fsf_req *fsf_req)
{
	struct fsf_status_read_buffer *status_buffer;
	struct zfcp_adapter *adapter;
	struct zfcp_port *port;
	unsigned long flags;

	status_buffer = fsf_req->data.status_read.buffer;
	adapter = fsf_req->adapter;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	list_for_each_entry(port, &adapter->port_list_head, list)
	    if (port->d_id == (status_buffer->d_id & ZFCP_DID_MASK))
		break;
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);

	if (!port || (port->d_id != (status_buffer->d_id & ZFCP_DID_MASK))) {
		ZFCP_LOG_NORMAL("bug: Re-open port indication received for the "
				"non-existing port with DID 0x%3.3x, on "
				"the adapter %s. Ignored.\n",
				status_buffer->d_id & ZFCP_DID_MASK,
				zfcp_get_busid_by_adapter(adapter));
		goto out;
	}

	switch (status_buffer->status_subtype) {

	case FSF_STATUS_READ_SUB_CLOSE_PHYS_PORT:
		ZFCP_LOG_FLAGS(2, "FSF_STATUS_READ_SUB_CLOSE_PHYS_PORT\n");
		debug_text_event(adapter->erp_dbf, 3, "unsol_pc_phys:");
		zfcp_erp_port_reopen(port, 0);
		break;

	case FSF_STATUS_READ_SUB_ERROR_PORT:
		ZFCP_LOG_FLAGS(1, "FSF_STATUS_READ_SUB_ERROR_PORT\n");
		debug_text_event(adapter->erp_dbf, 1, "unsol_pc_err:");
		zfcp_erp_port_shutdown(port, 0);
		break;

	default:
		debug_text_event(adapter->erp_dbf, 0, "unsol_unk_sub:");
		debug_exception(adapter->erp_dbf, 0,
				&status_buffer->status_subtype, sizeof (u32));
		ZFCP_LOG_NORMAL("bug: Undefined status subtype received "
				"for a re-open indication on the port with "
				"DID 0x%3.3x, on the adapter "
				"%s. Ignored. (debug info 0x%x)\n",
				status_buffer->d_id,
				zfcp_get_busid_by_adapter(adapter),
				status_buffer->status_subtype);
	}
 out:
	return 0;
}

/*
 * function:    zfcp_fsf_status_read_handler
 *
 * purpose:	is called for finished Open Port command
 *
 * returns:	
 */
static int
zfcp_fsf_status_read_handler(struct zfcp_fsf_req *fsf_req)
{
	int retval = 0;
	struct zfcp_adapter *adapter = fsf_req->adapter;
	struct fsf_status_read_buffer *status_buffer =
	    fsf_req->data.status_read.buffer;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_DISMISSED) {
		mempool_free(status_buffer, adapter->pool.status_read_buf);
		zfcp_fsf_req_cleanup(fsf_req);
		goto out;
	}

	switch (status_buffer->status_type) {

	case FSF_STATUS_READ_PORT_CLOSED:
		ZFCP_LOG_FLAGS(1, "FSF_STATUS_READ_PORT_CLOSED\n");
		debug_text_event(adapter->erp_dbf, 3, "unsol_pclosed:");
		debug_event(adapter->erp_dbf, 3,
			    &status_buffer->d_id, sizeof (u32));
		zfcp_fsf_status_read_port_closed(fsf_req);
		break;

	case FSF_STATUS_READ_INCOMING_ELS:
		ZFCP_LOG_FLAGS(1, "FSF_STATUS_READ_INCOMING_ELS\n");
		debug_text_event(adapter->erp_dbf, 3, "unsol_els:");
		zfcp_fsf_incoming_els(fsf_req);
		break;

	case FSF_STATUS_READ_BIT_ERROR_THRESHOLD:
		ZFCP_LOG_FLAGS(1, "FSF_STATUS_READ_BIT_ERROR_THRESHOLD\n");
		debug_text_event(adapter->erp_dbf, 3, "unsol_bit_err:");
		ZFCP_LOG_NORMAL("Bit error threshold data received:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
			      (char *) status_buffer,
			      sizeof (struct fsf_status_read_buffer));
		break;

	case FSF_STATUS_READ_LINK_DOWN:
		ZFCP_LOG_FLAGS(1, "FSF_STATUS_READ_LINK_DOWN\n");
		/* Unneccessary, ignoring.... */
		break;

	case FSF_STATUS_READ_LINK_UP:
		ZFCP_LOG_FLAGS(1, "FSF_STATUS_READ_LINK_UP\n");
		debug_text_event(adapter->erp_dbf, 2, "unsol_link_up:");
		ZFCP_LOG_INFO("The local link to the adapter %s "
			      "was re-plugged. "
			      "Re-starting operations on this adapter..\n",
			      zfcp_get_busid_by_adapter(adapter));
		/* All ports should be marked as ready to run again */
		zfcp_erp_modify_adapter_status(adapter,
					       ZFCP_STATUS_COMMON_RUNNING,
					       ZFCP_SET);
		zfcp_erp_adapter_reopen(adapter,
					ZFCP_STATUS_ADAPTER_LINK_UNPLUGGED
					| ZFCP_STATUS_COMMON_ERP_FAILED);
		break;

	default:
		debug_text_event(adapter->erp_dbf, 0, "unsol_unknown:");
		debug_exception(adapter->erp_dbf, 0,
				&status_buffer->status_type, sizeof (u32));
		ZFCP_LOG_NORMAL("bug: An unsolicited status packet of unknown "
				"type was received by the zfcp-driver "
				"(debug info 0x%x)\n",
				status_buffer->status_type);
		ZFCP_LOG_DEBUG("Dump of status_read_buffer 0x%lx:\n",
			       (unsigned long) status_buffer);
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      (char *) status_buffer,
			      sizeof (struct fsf_status_read_buffer));
		break;
	}
	mempool_free(status_buffer, adapter->pool.status_read_buf);
	zfcp_fsf_req_cleanup(fsf_req);
	/*
	 * recycle buffer and start new request repeat until outbound
	 * queue is empty or adapter shutdown is requested
	 */
	/*
	 * FIXME(qdio):
	 * we may wait in the req_create for 5s during shutdown, so
	 * qdio_cleanup will have to wait at least that long before returning
	 * with failure to allow us a proper cleanup under all circumstances
	 */
	/*
	 * FIXME:
	 * allocation failure possible? (Is this code needed?)
	 */
	retval = zfcp_fsf_status_read(adapter, 0);
	if (retval < 0) {
		ZFCP_LOG_INFO("Outbound queue busy. "
			      "Could not create use an "
			      "unsolicited status read request for "
			      "the adapter %s.\n",
			      zfcp_get_busid_by_adapter(adapter));
		/* temporary fix to avoid status read buffer shortage */
		adapter->status_read_failed++;
		if ((ZFCP_STATUS_READS_RECOM - adapter->status_read_failed)
		    < ZFCP_STATUS_READ_FAILED_THRESHOLD) {
			ZFCP_LOG_INFO("restart adapter due to status read "
				      "buffer shortage (busid %s)\n",
				      zfcp_get_busid_by_adapter(adapter));
			zfcp_erp_adapter_reopen(adapter, 0);
		}
	}
 out:
	return retval;
}

/*
 * function:    zfcp_fsf_abort_fcp_command
 *
 * purpose:	tells FSF to abort a running SCSI command
 *
 * returns:	address of initiated FSF request
 *		NULL - request could not be initiated
 *
 * FIXME(design): should be watched by a timeout !!! 
 * FIXME(design) shouldn't this be modified to return an int
 *               also...don't know how though
 */
struct zfcp_fsf_req *
zfcp_fsf_abort_fcp_command(unsigned long old_req_id,
			   struct zfcp_adapter *adapter,
			   struct zfcp_unit *unit, int req_flags)
{
	struct zfcp_fsf_req *new_fsf_req = NULL;
	int retval = 0;
	unsigned long lock_flags;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(adapter, FSF_QTCB_ABORT_FCP_CMND,
				     &lock_flags, req_flags, &new_fsf_req);
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Out of resources. Could not create an "
			      "abort command request on the device with "
			      "the FCP-LUN 0x%Lx connected to "
			      "the port with WWPN 0x%Lx connected to "
			      "the adapter %s.\n",
			      unit->fcp_lun,
			      unit->port->wwpn,
			      zfcp_get_busid_by_adapter(adapter));
		goto out;
	}

	new_fsf_req->data.abort_fcp_command.unit = unit;

	/* set handles of unit and its parent port in QTCB */
	new_fsf_req->qtcb->header.lun_handle = unit->handle;
	new_fsf_req->qtcb->header.port_handle = unit->port->handle;

	/* set handle of request which should be aborted */
	new_fsf_req->qtcb->bottom.support.req_handle = (u64) old_req_id;

	/* start QDIO request for this FSF request */

	zfcp_fsf_start_scsi_er_timer(adapter);
	retval = zfcp_fsf_req_send(new_fsf_req, NULL);
	if (retval) {
		del_timer(&adapter->scsi_er_timer);
		ZFCP_LOG_INFO("error: Could not send an abort command request "
			      "for a command on the adapter %s, "
			      "port WWPN 0x%Lx and unit LUN 0x%Lx\n",
			      zfcp_get_busid_by_adapter(adapter),
			      unit->port->wwpn, unit->fcp_lun);
		zfcp_fsf_req_free(new_fsf_req);
		new_fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_DEBUG("Abort FCP Command request initiated "
		       "(adapter busid=%s, port d_id=0x%x, "
		       "unit fcp_lun=0x%Lx, old_req_id=0x%lx)\n",
		       zfcp_get_busid_by_adapter(adapter),
		       (unsigned int) unit->port->d_id,
		       unit->fcp_lun, old_req_id);
 out:
	write_unlock_irqrestore(&adapter->request_queue.queue_lock, lock_flags);
	return new_fsf_req;
}

/*
 * function:    zfcp_fsf_abort_fcp_command_handler
 *
 * purpose:	is called for finished Abort FCP Command request
 *
 * returns:	
 */
static int
zfcp_fsf_abort_fcp_command_handler(struct zfcp_fsf_req *new_fsf_req)
{
	int retval = -EINVAL;
	struct zfcp_unit *unit = new_fsf_req->data.abort_fcp_command.unit;
	unsigned char status_qual =
	    new_fsf_req->qtcb->header.fsf_status_qual.word[0];

	del_timer(&new_fsf_req->adapter->scsi_er_timer);

	if (new_fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* do not set ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED */
		goto skip_fsfstatus;
	}

	/* evaluate FSF status in QTCB */
	switch (new_fsf_req->qtcb->header.fsf_status) {

	case FSF_PORT_HANDLE_NOT_VALID:
		if (status_qual >> 4 != status_qual % 0xf) {
			ZFCP_LOG_FLAGS(2, "FSF_PORT_HANDLE_NOT_VALID\n");
			debug_text_event(new_fsf_req->adapter->erp_dbf, 3,
					 "fsf_s_phand_nv0");
			/*
			 * In this case a command that was sent prior to a port
			 * reopen was aborted (handles are different). This is
			 * fine.
			 */
		} else {
			ZFCP_LOG_FLAGS(1, "FSF_PORT_HANDLE_NOT_VALID\n");
			ZFCP_LOG_INFO("Temporary port identifier (handle) 0x%x "
				      "for the port with WWPN 0x%Lx connected "
				      "to the adapter %s is not valid. This "
				      "may happen occasionally.\n",
				      unit->port->handle,
				      unit->port->wwpn,
				      zfcp_get_busid_by_unit(unit));
			ZFCP_LOG_INFO("status qualifier:\n");
			ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_INFO,
				      (char *) &new_fsf_req->qtcb->header.
				      fsf_status_qual,
				      sizeof (union fsf_status_qual));
			/* Let's hope this sorts out the mess */
			debug_text_event(new_fsf_req->adapter->erp_dbf, 1,
					 "fsf_s_phand_nv1");
			zfcp_erp_adapter_reopen(unit->port->adapter, 0);
			new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		}
		break;

	case FSF_LUN_HANDLE_NOT_VALID:
		if (status_qual >> 4 != status_qual % 0xf) {
			/* 2 */
			ZFCP_LOG_FLAGS(0, "FSF_LUN_HANDLE_NOT_VALID\n");
			debug_text_event(new_fsf_req->adapter->erp_dbf, 3,
					 "fsf_s_lhand_nv0");
			/*
			 * In this case a command that was sent prior to a unit
			 * reopen was aborted (handles are different).
			 * This is fine.
			 */
		} else {
			ZFCP_LOG_FLAGS(1, "FSF_LUN_HANDLE_NOT_VALID\n");
			ZFCP_LOG_INFO
			    ("Warning: Temporary LUN identifier (handle) 0x%x "
			     "of the logical unit with FCP-LUN 0x%Lx at "
			     "the remote port with WWPN 0x%Lx connected "
			     "to the adapter %s is "
			     "not valid. This may happen in rare cases."
			     "Trying to re-establish link.\n",
			     unit->handle,
			     unit->fcp_lun,
			     unit->port->wwpn,
			     zfcp_get_busid_by_unit(unit));
			ZFCP_LOG_DEBUG("Status qualifier data:\n");
			ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
				      (char *) &new_fsf_req->qtcb->header.
				      fsf_status_qual,
				      sizeof (union fsf_status_qual));
			/* Let's hope this sorts out the mess */
			debug_text_event(new_fsf_req->adapter->erp_dbf, 1,
					 "fsf_s_lhand_nv1");
			zfcp_erp_port_reopen(unit->port, 0);
			new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		}
		break;

	case FSF_FCP_COMMAND_DOES_NOT_EXIST:
		ZFCP_LOG_FLAGS(2, "FSF_FCP_COMMAND_DOES_NOT_EXIST\n");
		retval = 0;
#ifdef ZFCP_DEBUG_REQUESTS
		/*
		 * debug feature area which records
		 * fsf request sequence numbers
		 */
		debug_text_event(new_fsf_req->adapter->req_dbf, 3, "no_exist");
		debug_event(new_fsf_req->adapter->req_dbf, 3,
			    &new_fsf_req->qtcb->bottom.support.req_handle,
			    sizeof (unsigned long));
#endif				/* ZFCP_DEBUG_REQUESTS */
		debug_text_event(new_fsf_req->adapter->erp_dbf, 3,
				 "fsf_s_no_exist");
		new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ABORTNOTNEEDED;
		break;

	case FSF_PORT_BOXED:
		/* 2 */
		ZFCP_LOG_FLAGS(0, "FSF_PORT_BOXED\n");
		ZFCP_LOG_DEBUG("The remote port "
			       "with WWPN 0x%Lx on the adapter %s "
			       "needs to be reopened\n",
			       unit->port->wwpn, zfcp_get_busid_by_unit(unit));
		debug_text_event(new_fsf_req->adapter->erp_dbf, 2,
				 "fsf_s_pboxed");
		zfcp_erp_port_reopen(unit->port, 0);
		new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR
		    | ZFCP_STATUS_FSFREQ_RETRY;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		/* 2 */
		ZFCP_LOG_FLAGS(0, "FSF_ADAPTER_STATUS_AVAILABLE\n");
		switch (new_fsf_req->qtcb->header.fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			ZFCP_LOG_FLAGS(2,
				       "FSF_SQ_INVOKE_LINK_TEST_PROCEDURE\n");
			debug_text_event(new_fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ltest");
			/* reopening link to port */
			zfcp_erp_port_reopen(unit->port, 0);
			new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			ZFCP_LOG_FLAGS(2,
				       "FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED\n");
			/* SCSI stack will escalate */
			debug_text_event(new_fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ulp");
			new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		default:
			ZFCP_LOG_NORMAL
			    ("bug: Wrong status qualifier 0x%x arrived.\n",
			     new_fsf_req->qtcb->header.fsf_status_qual.word[0]);
			debug_text_event(new_fsf_req->adapter->erp_dbf, 0,
					 "fsf_sq_inval:");
			debug_exception(new_fsf_req->adapter->erp_dbf, 0,
					&new_fsf_req->qtcb->header.
					fsf_status_qual.word[0], sizeof (u32));
			break;
		}
		break;

	case FSF_GOOD:
		/* 3 */
		ZFCP_LOG_FLAGS(0, "FSF_GOOD\n");
		retval = 0;
		new_fsf_req->status |= ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED;
		break;

	default:
		ZFCP_LOG_NORMAL("bug: An unknown FSF Status was presented "
				"(debug info 0x%x)\n",
				new_fsf_req->qtcb->header.fsf_status);
		debug_text_event(new_fsf_req->adapter->erp_dbf, 0,
				 "fsf_s_inval:");
		debug_exception(new_fsf_req->adapter->erp_dbf, 0,
				&new_fsf_req->qtcb->header.fsf_status,
				sizeof (u32));
		break;
	}
 skip_fsfstatus:
	return retval;
}

/*
 * function:    zfcp_fsf_send_generic
 *
 * purpose:	sends a FC request according to FC-GS-3
 *
 * returns:	address of initiated FSF request
 *		NULL - request could not be initiated 
 */
int
zfcp_fsf_send_generic(struct zfcp_fsf_req *fsf_req, unsigned char timeout,
		      unsigned long *lock_flags, struct timer_list *timer)
{
	int retval = 0;
	struct qdio_buffer *buffer;
	volatile struct qdio_buffer_element *buffer_element = NULL;
	struct zfcp_port *port = fsf_req->data.send_generic.port;
	struct zfcp_adapter *adapter = port->adapter;

	/* put buffers to the 2 SBALEs after the QTCB */
	buffer = (adapter->request_queue.buffer[fsf_req->sbal_index]);
	buffer_element = &(buffer->element[2]);
	buffer_element->addr = fsf_req->data.send_generic.outbuf;
	buffer_element->length = fsf_req->data.send_generic.outbuf_length;
	buffer_element++;
	buffer_element->addr = fsf_req->data.send_generic.inbuf;
	buffer_element->length = fsf_req->data.send_generic.inbuf_length;
	buffer_element->flags |= SBAL_FLAGS_LAST_ENTRY;

	/* settings in QTCB */
	fsf_req->qtcb->header.port_handle = port->handle;
	fsf_req->qtcb->bottom.support.service_class = adapter->fc_service_class;
	fsf_req->qtcb->bottom.support.timeout = timeout;

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(fsf_req, timer);
	if (retval) {
		ZFCP_LOG_DEBUG("error: Out of resources. could not send a "
			       "generic services "
			       "command via the adapter %s, port "
			       "WWPN 0x%Lx\n",
			       zfcp_get_busid_by_adapter(adapter), port->wwpn);
		/*
		 * fsf_req structure will be cleaned up by higher layer handler
		 */
		goto out;
	}

	ZFCP_LOG_DEBUG("Send Generic request initiated "
		       "(adapter busido=%s, port d_id=0x%x)\n",
		       zfcp_get_busid_by_adapter(adapter),
		       (unsigned int) port->d_id);
 out:
	return retval;
}

/*
 * function:    zfcp_fsf_send_generic_handler
 *
 * purpose:	is called for finished Send Generic request
 *
 * returns:	
 */
static int
zfcp_fsf_send_generic_handler(struct zfcp_fsf_req *fsf_req)
{
	int retval = -EINVAL;
	struct zfcp_port *port = fsf_req->data.send_generic.port;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* do not set ZFCP_STATUS_FSFREQ_ABORTSUCCEEDED */
		goto skip_fsfstatus;
	}

	/* evaluate FSF status in QTCB */
	switch (fsf_req->qtcb->header.fsf_status) {

	case FSF_PORT_HANDLE_NOT_VALID:
		ZFCP_LOG_FLAGS(1, "FSF_PORT_HANDLE_NOT_VALID\n");
		ZFCP_LOG_DEBUG("Temporary port identifier (handle) 0x%x "
			       "for the port with WWPN 0x%Lx connected to "
			       "the adapter %s is "
			       "not valid. This may happen occasionally.\n",
			       port->handle,
			       port->wwpn, zfcp_get_busid_by_port(port));
		ZFCP_LOG_INFO("status qualifier:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_INFO,
			      (char *) &fsf_req->qtcb->header.fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_phandle_nv");
		zfcp_erp_adapter_reopen(port->adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_SERVICE_CLASS_NOT_SUPPORTED:
		ZFCP_LOG_FLAGS(0, "FSF_SERVICE_CLASS_NOT_SUPPORTED\n");
		if (fsf_req->adapter->fc_service_class <= 3) {
			ZFCP_LOG_NORMAL("error: The adapter %s does "
					"not support fibre-channel class %d.\n",
					zfcp_get_busid_by_port(port),
					fsf_req->adapter->fc_service_class);
		} else {
			ZFCP_LOG_NORMAL
			    ("bug: The fibre channel class at the adapter "
			     "%s is invalid. " "(debug info %d)\n",
			     zfcp_get_busid_by_port(port),
			     fsf_req->adapter->fc_service_class);
		}
		/* stop operation for this adapter */
		debug_text_exception(fsf_req->adapter->erp_dbf, 0,
				     "fsf_s_class_nsup");
		zfcp_erp_adapter_shutdown(port->adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_GENERIC_COMMAND_REJECTED:
		ZFCP_LOG_FLAGS(1, "FSF_GENERIC_COMMAND_REJECTED\n");
		ZFCP_LOG_INFO("warning: The port with WWPN 0x%Lx connected to "
			      "the adapter %s is"
			      "rejected a generic services command.\n",
			      port->wwpn, zfcp_get_busid_by_port(port));
		ZFCP_LOG_INFO("status qualifier:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_INFO,
			      (char *) &fsf_req->qtcb->header.fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_gcom_rej");
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_REQUEST_BUF_NOT_VALID:
		ZFCP_LOG_FLAGS(1, "FSF_REQUEST_BUF_NOT_VALID\n");
		ZFCP_LOG_NORMAL("error: The port with WWPN 0x%Lx connected to "
				"the adapter %s is"
				"rejected a generic services command "
				"due to invalid request buffer.\n",
				port->wwpn, zfcp_get_busid_by_port(port));
		debug_text_event(fsf_req->adapter->erp_dbf, 1, "fsf_s_reqiv");
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_RESPONSE_BUF_NOT_VALID:
		ZFCP_LOG_FLAGS(1, "FSF_RESPONSE_BUF_NOT_VALID\n");
		ZFCP_LOG_NORMAL("error: The port with WWPN 0x%Lx connected to "
				"the adapter %s is"
				"rejected a generic services command "
				"due to invalid response buffer.\n",
				port->wwpn, zfcp_get_busid_by_port(port));
		debug_text_event(fsf_req->adapter->erp_dbf, 1, "fsf_s_resiv");
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PORT_BOXED:
		ZFCP_LOG_FLAGS(2, "FSF_PORT_BOXED\n");
		ZFCP_LOG_DEBUG("The remote port "
			       "with WWPN 0x%Lx on the adapter %s "
			       "needs to be reopened\n",
			       port->wwpn, zfcp_get_busid_by_port(port));
		debug_text_event(fsf_req->adapter->erp_dbf, 2, "fsf_s_pboxed");
		zfcp_erp_port_reopen(port, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR
		    | ZFCP_STATUS_FSFREQ_RETRY;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		ZFCP_LOG_FLAGS(2, "FSF_ADAPTER_STATUS_AVAILABLE\n");
		switch (fsf_req->qtcb->header.fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			ZFCP_LOG_FLAGS(2,
				       "FSF_SQ_INVOKE_LINK_TEST_PROCEDURE\n");
			/* reopening link to port */
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ltest");
			zfcp_erp_port_forced_reopen(port, 0);
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			/* ERP strategy will escalate */
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ulp");
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;

		default:
			ZFCP_LOG_NORMAL
			    ("bug: Wrong status qualifier 0x%x arrived.\n",
			     fsf_req->qtcb->header.fsf_status_qual.word[0]);
			break;
		}
		break;

	case FSF_GOOD:
		ZFCP_LOG_FLAGS(2, "FSF_GOOD\n");
		retval = 0;
		break;

	default:
		ZFCP_LOG_NORMAL("bug: An unknown FSF Status was presented "
				"(debug info 0x%x)\n",
				fsf_req->qtcb->header.fsf_status);
		debug_text_event(fsf_req->adapter->erp_dbf, 0, "fsf_sq_inval:");
		debug_exception(fsf_req->adapter->erp_dbf, 0,
				&fsf_req->qtcb->header.fsf_status_qual.word[0],
				sizeof (u32));
		break;
	}
 skip_fsfstatus:
	/* callback */
	(fsf_req->data.send_generic.handler)(fsf_req);
	return retval;
}

/*
 * function:
 *
 * purpose:
 *
 * returns:	address of initiated FSF request
 *		NULL - request could not be initiated
 */
int
zfcp_fsf_exchange_config_data(struct zfcp_erp_action *erp_action)
{
	int retval = 0;
	unsigned long lock_flags;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(erp_action->adapter,
				     FSF_QTCB_EXCHANGE_CONFIG_DATA,
				     &lock_flags,
				     ZFCP_REQ_AUTO_CLEANUP,
				     &(erp_action->fsf_req));
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Out of resources. Could not create an "
			      "exchange configuration data request for"
			      "the adapter %s.\n",
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		goto out;
	}

	erp_action->fsf_req->erp_action = erp_action;
	/* no information from us to adapter, set nothing */

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(erp_action->fsf_req, &erp_action->timer);
	if (retval) {
		ZFCP_LOG_INFO
		    ("error: Could not send an exchange configuration data "
		     "command on the adapter %s\n",
		     zfcp_get_busid_by_adapter(erp_action->adapter));
		zfcp_fsf_req_free(erp_action->fsf_req);
		erp_action->fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_DEBUG("Exchange Configuration Data request initiated "
		       "(adapter busid=%s)\n",
		       zfcp_get_busid_by_adapter(erp_action->adapter));

 out:
	write_unlock_irqrestore(&erp_action->adapter->request_queue.queue_lock,
				lock_flags);
	return retval;
}

/*
 * function:    zfcp_fsf_exchange_config_data_handler
 *
 * purpose:     is called for finished Exchange Configuration Data command
 *
 * returns:
 */
static int
zfcp_fsf_exchange_config_data_handler(struct zfcp_fsf_req *fsf_req) {
	int retval = -EIO;
	struct fsf_qtcb_bottom_config *bottom;
	struct zfcp_adapter *adapter = fsf_req->adapter;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* don't set any value, stay with the old (unitialized) ones */
		goto skip_fsfstatus;
	}

	/* evaluate FSF status in QTCB */
	switch (fsf_req->qtcb->header.fsf_status) {

	case FSF_GOOD:
		ZFCP_LOG_FLAGS(2, "FSF_GOOD\n");
		bottom = &fsf_req->qtcb->bottom.config;
		/* only log QTCB versions for now */
		ZFCP_LOG_DEBUG("low QTCB version 0x%x of FSF, "
			       "high QTCB version 0x%x of FSF, \n",
			       bottom->low_qtcb_version,
			       bottom->high_qtcb_version);
		adapter->wwnn = bottom->nport_serv_param.wwnn;
		adapter->wwpn = bottom->nport_serv_param.wwpn;
		adapter->s_id = bottom->s_id & ZFCP_DID_MASK;
		adapter->hydra_version = bottom->adapter_type;
		adapter->fsf_lic_version = bottom->lic_version;
		adapter->fc_topology = bottom->fc_topology;
		adapter->fc_link_speed = bottom->fc_link_speed;
		ZFCP_LOG_INFO("The adapter %s reported "
			      "the following characteristics:\n"
			      "WWNN 0x%16.16Lx, "
			      "WWPN 0x%16.16Lx, "
			      "S_ID 0x%6.6x,\n"
			      "adapter version 0x%x, "
			      "LIC version 0x%x, "
			      "FC link speed %d Gb/s\n",
			      zfcp_get_busid_by_adapter(adapter),
			      adapter->wwnn,
			      adapter->wwpn,
			      (unsigned int) adapter->s_id,
			      adapter->hydra_version,
			      adapter->fsf_lic_version,
			      adapter->fc_link_speed);
		if (ZFCP_QTCB_VERSION < bottom->low_qtcb_version) {
			ZFCP_LOG_NORMAL("error: the adapter %s "
					"only supports newer control block "
					"versions in comparison to this device "
					"driver (try updated device driver)\n",
					zfcp_get_busid_by_adapter(adapter));
			debug_text_event(fsf_req->adapter->erp_dbf, 0,
					 "low_qtcb_ver");
			zfcp_erp_adapter_shutdown(adapter, 0);
			goto skip_fsfstatus;
		}
		if (ZFCP_QTCB_VERSION > bottom->high_qtcb_version) {
			ZFCP_LOG_NORMAL("error: the adapter %s "
					"only supports older control block "
					"versions than this device driver uses"
					"(consider a microcode upgrade)\n",
					zfcp_get_busid_by_adapter(adapter));
			debug_text_event(fsf_req->adapter->erp_dbf, 0,
					 "high_qtcb_ver");
			zfcp_erp_adapter_shutdown(adapter, 0);
			goto skip_fsfstatus;
		}
		switch (adapter->fc_topology) {
		case FSF_TOPO_P2P:
			ZFCP_LOG_FLAGS(1, "FSF_TOPO_P2P\n");
			ZFCP_LOG_NORMAL("error: Point-to-point fibre-channel "
					"configuration detected "
					"at the adapter %s, not "
					"supported, shutting down adapter\n",
					zfcp_get_busid_by_adapter(adapter));
			debug_text_event(fsf_req->adapter->erp_dbf, 0,
					 "top-p-to-p");
			zfcp_erp_adapter_shutdown(adapter, 0);
			goto skip_fsfstatus;
		case FSF_TOPO_AL:
			ZFCP_LOG_FLAGS(1, "FSF_TOPO_AL\n");
			ZFCP_LOG_NORMAL("error: Arbitrated loop fibre-channel "
					"topology detected "
					"at the adapter %s, not "
					"supported, shutting down adapter\n",
					zfcp_get_busid_by_adapter(adapter));
			debug_text_event(fsf_req->adapter->erp_dbf, 0,
					 "top-al");
			zfcp_erp_adapter_shutdown(adapter, 0);
			goto skip_fsfstatus;
		case FSF_TOPO_FABRIC:
			ZFCP_LOG_FLAGS(1, "FSF_TOPO_FABRIC\n");
			ZFCP_LOG_INFO("Switched fabric fibre-channel "
				      "network detected "
				      "at the adapter %s.\n",
				      zfcp_get_busid_by_adapter(adapter));
			break;
		default:
			ZFCP_LOG_NORMAL("bug: The fibre-channel topology "
					"reported by the exchange "
					"configuration command for "
					"the adapter %s is not "
					"of a type known to the zfcp "
					"driver, shutting down adapter\n",
					zfcp_get_busid_by_adapter(adapter));
			debug_text_exception(fsf_req->adapter->erp_dbf, 0,
					     "unknown-topo");
			zfcp_erp_adapter_shutdown(adapter, 0);
			goto skip_fsfstatus;
		}
		if (bottom->max_qtcb_size < ZFCP_QTCB_SIZE) {
			ZFCP_LOG_NORMAL("bug: Maximum QTCB size (%d bytes) "
					"allowed by the adapter %s "
					"is lower than the minimum "
					"required by the driver (%ld bytes).\n",
					bottom->max_qtcb_size,
					zfcp_get_busid_by_adapter(adapter),
					ZFCP_QTCB_SIZE);
			debug_text_event(fsf_req->adapter->erp_dbf, 0,
					 "qtcb-size");
			debug_event(fsf_req->adapter->erp_dbf, 0,
				    &bottom->max_qtcb_size, sizeof (u32));
			zfcp_erp_adapter_shutdown(adapter, 0);
			goto skip_fsfstatus;
		}
		atomic_set_mask(ZFCP_STATUS_ADAPTER_XCONFIG_OK,
				&adapter->status);
		retval = 0;
		break;
	default:
		/* retval is -EIO by default */
		debug_text_event(fsf_req->adapter->erp_dbf, 0, "fsf-stat-ng");
		debug_event(fsf_req->adapter->erp_dbf, 0,
			    &fsf_req->qtcb->header.fsf_status, sizeof (u32));
		zfcp_erp_adapter_shutdown(adapter, 0);
	}
 skip_fsfstatus:
	return retval;
}

/*
 * function:    zfcp_fsf_open_port
 *
 * purpose:	
 *
 * returns:	address of initiated FSF request
 *		NULL - request could not be initiated 
 */
int
zfcp_fsf_open_port(struct zfcp_erp_action *erp_action)
{
	int retval = 0;
	unsigned long lock_flags;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(erp_action->adapter,
				     FSF_QTCB_OPEN_PORT_WITH_DID,
				     &lock_flags,
				     ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
				     &(erp_action->fsf_req));
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Out of resources. Could not create an "
			      "open port request for "
			      "the port with WWPN 0x%Lx connected to "
			      "the adapter %s.\n",
			      erp_action->port->wwpn,
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		goto out;
	}

	erp_action->fsf_req->qtcb->bottom.support.d_id = erp_action->port->d_id;
	atomic_set_mask(ZFCP_STATUS_COMMON_OPENING, &erp_action->port->status);
	erp_action->fsf_req->data.open_port.port = erp_action->port;
	erp_action->fsf_req->erp_action = erp_action;

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(erp_action->fsf_req, &erp_action->timer);
	if (retval) {
		ZFCP_LOG_INFO("error: Could not send an "
			      "open port request for "
			      "the port with WWPN 0x%Lx connected to "
			      "the adapter %s.\n",
			      erp_action->port->wwpn,
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		zfcp_fsf_req_free(erp_action->fsf_req);
		erp_action->fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_DEBUG("Open Port request initiated "
		       "(adapter busid=%s, port wwpn=0x%Lx)\n",
		       zfcp_get_busid_by_adapter(erp_action->adapter),
		       erp_action->port->wwpn);
 out:
	write_unlock_irqrestore(&erp_action->adapter->request_queue.queue_lock,
				lock_flags);
	return retval;
}

/*
 * function:    zfcp_fsf_open_port_handler
 *
 * purpose:	is called for finished Open Port command
 *
 * returns:	
 */
static int
zfcp_fsf_open_port_handler(struct zfcp_fsf_req *fsf_req)
{
	int retval = -EINVAL;
	struct zfcp_port *port;
	struct fsf_plogi *plogi;

	port = fsf_req->data.open_port.port;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* don't change port status in our bookkeeping */
		goto skip_fsfstatus;
	}

	/* evaluate FSF status in QTCB */
	switch (fsf_req->qtcb->header.fsf_status) {

	case FSF_PORT_ALREADY_OPEN:
		ZFCP_LOG_FLAGS(0, "FSF_PORT_ALREADY_OPEN\n");
		ZFCP_LOG_NORMAL("bug: The remote port with WWPN=0x%Lx "
				"connected to the adapter %s "
				"is already open.\n",
				port->wwpn, zfcp_get_busid_by_port(port));
		debug_text_exception(fsf_req->adapter->erp_dbf, 0,
				     "fsf_s_popen");
		/*
		 * This is a bug, however operation should continue normally
		 * if it is simply ignored
		 */
		break;

	case FSF_MAXIMUM_NUMBER_OF_PORTS_EXCEEDED:
		ZFCP_LOG_FLAGS(1, "FSF_MAXIMUM_NUMBER_OF_PORTS_EXCEEDED\n");
		ZFCP_LOG_INFO("error: The FSF adapter is out of resources. "
			      "The remote port with WWPN=0x%Lx "
			      "connected to the adapter %s "
			      "could not be opened. Disabling it.\n",
			      port->wwpn, zfcp_get_busid_by_port(port));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_max_ports");
		zfcp_erp_port_failed(port);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		ZFCP_LOG_FLAGS(2, "FSF_ADAPTER_STATUS_AVAILABLE\n");
		switch (fsf_req->qtcb->header.fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			ZFCP_LOG_FLAGS(2,
				       "FSF_SQ_INVOKE_LINK_TEST_PROCEDURE\n");
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ltest");
			/* ERP strategy will escalate */
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			/* ERP strategy will escalate */
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ulp");
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_NO_RETRY_POSSIBLE:
			ZFCP_LOG_FLAGS(0, "FSF_SQ_NO_RETRY_POSSIBLE\n");
			ZFCP_LOG_NORMAL("The remote port with WWPN=0x%Lx "
					"connected to the adapter %s "
					"could not be opened. Disabling it.\n",
					port->wwpn,
					zfcp_get_busid_by_port(port));
			debug_text_exception(fsf_req->adapter->erp_dbf, 0,
					     "fsf_sq_no_retry");
			zfcp_erp_port_failed(port);
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		default:
			ZFCP_LOG_NORMAL
			    ("bug: Wrong status qualifier 0x%x arrived.\n",
			     fsf_req->qtcb->header.fsf_status_qual.word[0]);
			debug_text_event(fsf_req->adapter->erp_dbf, 0,
					 "fsf_sq_inval:");
			debug_exception(
				fsf_req->adapter->erp_dbf, 0,
				&fsf_req->qtcb->header.fsf_status_qual.word[0],
				sizeof (u32));
			break;
		}
		break;

	case FSF_GOOD:
		ZFCP_LOG_FLAGS(3, "FSF_GOOD\n");
		/* save port handle assigned by FSF */
		port->handle = fsf_req->qtcb->header.port_handle;
		ZFCP_LOG_INFO("The remote port (WWPN=0x%Lx) via adapter "
			      "(busid=%s) was opened, it's "
			      "port handle is 0x%x\n",
			      port->wwpn,
			      zfcp_get_busid_by_port(port),
			      port->handle);
		/* mark port as open */
		atomic_set_mask(ZFCP_STATUS_COMMON_OPEN |
				ZFCP_STATUS_PORT_PHYS_OPEN, &port->status);
		retval = 0;
		/* check whether D_ID has changed during open */
		plogi = (struct fsf_plogi *) fsf_req->qtcb->bottom.support.els;
		if (!atomic_test_mask(ZFCP_STATUS_PORT_NO_WWPN, &port->status))
		{
			if (fsf_req->qtcb->bottom.support.els1_length <
			    ((((unsigned long) &plogi->serv_param.wwpn) -
			      ((unsigned long) plogi)) + sizeof (u64))) {
				ZFCP_LOG_INFO(
					"warning: insufficient length of "
					"PLOGI payload (%i)\n",
					fsf_req->qtcb->bottom.support.els1_length);
				debug_text_event(fsf_req->adapter->erp_dbf, 0,
						 "fsf_s_short_plogi:");
				/* skip sanity check and assume wwpn is ok */
			} else {
				if (plogi->serv_param.wwpn != port->wwpn) {
					ZFCP_LOG_INFO("warning: D_ID of port "
						      "with WWPN 0x%Lx changed "
						      "during open\n", port->wwpn);
					debug_text_event(
						fsf_req->adapter->erp_dbf, 0,
						"fsf_s_did_change:");
					atomic_clear_mask(
						ZFCP_STATUS_PORT_DID_DID,
						&port->status);
				} else
					port->wwnn = plogi->serv_param.wwnn;
			}
		}
		break;

	default:
		ZFCP_LOG_NORMAL("bug: An unknown FSF Status was presented "
				"(debug info 0x%x)\n",
				fsf_req->qtcb->header.fsf_status);
		debug_text_event(fsf_req->adapter->erp_dbf, 0, "fsf_s_inval:");
		debug_exception(fsf_req->adapter->erp_dbf, 0,
				&fsf_req->qtcb->header.fsf_status,
				sizeof (u32));
		break;
	}

 skip_fsfstatus:
	atomic_clear_mask(ZFCP_STATUS_COMMON_OPENING, &port->status);
	return retval;
}

/*
 * function:    zfcp_fsf_close_port
 *
 * purpose:     submit FSF command "close port"
 *
 * returns:     address of initiated FSF request
 *              NULL - request could not be initiated
 */
int
zfcp_fsf_close_port(struct zfcp_erp_action *erp_action)
{
	int retval = 0;
	unsigned long lock_flags;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(erp_action->adapter,
				     FSF_QTCB_CLOSE_PORT,
				     &lock_flags,
				     ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
				     &(erp_action->fsf_req));
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Out of resources. Could not create a "
			      "close port request for WWPN 0x%Lx connected to "
			      "the adapter %s.\n",
			      erp_action->port->wwpn,
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		goto out;
	}

	atomic_set_mask(ZFCP_STATUS_COMMON_CLOSING, &erp_action->port->status);
	erp_action->fsf_req->data.close_port.port = erp_action->port;
	erp_action->fsf_req->erp_action = erp_action;
	erp_action->fsf_req->qtcb->header.port_handle =
	    erp_action->port->handle;

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(erp_action->fsf_req, &erp_action->timer);
	if (retval) {
		ZFCP_LOG_INFO("error: Could not send a "
			      "close port request for WWPN 0x%Lx connected to "
			      "the adapter %s.\n",
			      erp_action->port->wwpn,
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		zfcp_fsf_req_free(erp_action->fsf_req);
		erp_action->fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_TRACE("Close Port request initiated "
		       "(adapter busid=%s, port wwpn=0x%Lx)\n",
		       zfcp_get_busid_by_adapter(erp_action->adapter),
		       erp_action->port->wwpn);
 out:
	write_unlock_irqrestore(&erp_action->adapter->request_queue.queue_lock,
				lock_flags);
	return retval;
}

/*
 * function:    zfcp_fsf_close_port_handler
 *
 * purpose:     is called for finished Close Port FSF command
 *
 * returns:
 */
static int
zfcp_fsf_close_port_handler(struct zfcp_fsf_req *fsf_req)
{
	int retval = -EINVAL;
	struct zfcp_port *port;

	port = fsf_req->data.close_port.port;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* don't change port status in our bookkeeping */
		goto skip_fsfstatus;
	}

	/* evaluate FSF status in QTCB */
	switch (fsf_req->qtcb->header.fsf_status) {

	case FSF_PORT_HANDLE_NOT_VALID:
		ZFCP_LOG_FLAGS(1, "FSF_PORT_HANDLE_NOT_VALID\n");
		ZFCP_LOG_INFO("Temporary port identifier (handle) 0x%x "
			      "for the port with WWPN 0x%Lx connected to "
			      "the adapter %s is"
			      "not valid. This may happen occasionally.\n",
			      port->handle,
			      port->wwpn, zfcp_get_busid_by_port(port));
		ZFCP_LOG_DEBUG("status qualifier:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      (char *) &fsf_req->qtcb->header.fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_phand_nv");
		zfcp_erp_adapter_reopen(port->adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		ZFCP_LOG_FLAGS(2, "FSF_ADAPTER_STATUS_AVAILABLE\n");
		/* Note: FSF has actually closed the port in this case.
		 * The status code is just daft. Fingers crossed for a change
		 */
		retval = 0;
		break;

	case FSF_GOOD:
		ZFCP_LOG_FLAGS(3, "FSF_GOOD\n");
		ZFCP_LOG_TRACE("remote port (WWPN=0x%Lx) via adapter "
			       "(busid=%s) closed, port handle 0x%x\n",
			       port->wwpn,
			       zfcp_get_busid_by_port(port),
			       port->handle);
		zfcp_erp_modify_port_status(port,
					    ZFCP_STATUS_COMMON_OPEN,
					    ZFCP_CLEAR);
		retval = 0;
		break;

	default:
		ZFCP_LOG_NORMAL("bug: An unknown FSF Status was presented "
				"(debug info 0x%x)\n",
				fsf_req->qtcb->header.fsf_status);
		debug_text_event(fsf_req->adapter->erp_dbf, 0, "fsf_s_inval:");
		debug_exception(fsf_req->adapter->erp_dbf, 0,
				&fsf_req->qtcb->header.fsf_status,
				sizeof (u32));
		break;
	}

 skip_fsfstatus:
	atomic_clear_mask(ZFCP_STATUS_COMMON_CLOSING, &port->status);
	return retval;
}

/*
 * function:    zfcp_fsf_close_physical_port
 *
 * purpose:     submit FSF command "close physical port"
 *
 * returns:     address of initiated FSF request
 *              NULL - request could not be initiated
 */
int
zfcp_fsf_close_physical_port(struct zfcp_erp_action *erp_action)
{
	int retval = 0;
	unsigned long lock_flags;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(erp_action->adapter,
				     FSF_QTCB_CLOSE_PHYSICAL_PORT,
				     &lock_flags,
				     ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
				     &erp_action->fsf_req);
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Out of resources. Could not create a "
			      "close physical port request for "
			      "the port with WWPN 0x%Lx connected to "
			      "the adapter %s.\n",
			      erp_action->port->wwpn,
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		goto out;
	}

	/* mark port as being closed */
	atomic_set_mask(ZFCP_STATUS_PORT_PHYS_CLOSING,
			&erp_action->port->status);
	/* save a pointer to this port */
	erp_action->fsf_req->data.close_physical_port.port = erp_action->port;
	/* port to be closeed */
	erp_action->fsf_req->qtcb->header.port_handle =
	    erp_action->port->handle;
	erp_action->fsf_req->erp_action = erp_action;

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(erp_action->fsf_req, &erp_action->timer);
	if (retval) {
		ZFCP_LOG_INFO("error: Could not send an close physical port "
			      "request for the port with WWPN 0x%Lx connected "
			      "to the adapter %s.\n",
			      erp_action->port->wwpn,
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		zfcp_fsf_req_free(erp_action->fsf_req);
		erp_action->fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_TRACE("Close Physical Port request initiated "
		       "(adapter busid=%s, port wwpn=0x%Lx)\n",
		       zfcp_get_busid_by_adapter(erp_action->adapter),
		       erp_action->port->wwpn);
 out:
	write_unlock_irqrestore(&erp_action->adapter->request_queue.queue_lock,
				lock_flags);
	return retval;
}

/*
 * function:    zfcp_fsf_close_physical_port_handler
 *
 * purpose:     is called for finished Close Physical Port FSF command
 *
 * returns:
 */
static int
zfcp_fsf_close_physical_port_handler(struct zfcp_fsf_req *fsf_req)
{
	int retval = -EINVAL;
	struct zfcp_port *port;
	struct zfcp_unit *unit;

	port = fsf_req->data.close_physical_port.port;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* don't change port status in our bookkeeping */
		goto skip_fsfstatus;
	}

	/* evaluate FSF status in QTCB */
	switch (fsf_req->qtcb->header.fsf_status) {

	case FSF_PORT_HANDLE_NOT_VALID:
		ZFCP_LOG_FLAGS(1, "FSF_PORT_HANDLE_NOT_VALID\n");
		ZFCP_LOG_INFO("Temporary port identifier (handle) 0x%x "
			      "for the port with WWPN 0x%Lx connected to "
			      "the adapter %s is not valid. This may happen "
			      "occasionally.\n",
			      port->handle,
			      port->wwpn,
			      zfcp_get_busid_by_port(port));
		ZFCP_LOG_DEBUG("status qualifier:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      (char *) &fsf_req->qtcb->header.fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_phand_nv");
		zfcp_erp_adapter_reopen(port->adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PORT_BOXED:
		ZFCP_LOG_FLAGS(2, "FSF_PORT_BOXED\n");
		ZFCP_LOG_DEBUG("The remote port with WWPN 0x%Lx on the adapter "
			       "%s needs to be reopened but it was attempted "
			       "to close it physically.\n",
			       port->wwpn,
			       zfcp_get_busid_by_port(port));
		debug_text_event(fsf_req->adapter->erp_dbf, 1, "fsf_s_pboxed");
		zfcp_erp_port_reopen(port, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			ZFCP_STATUS_FSFREQ_RETRY;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		ZFCP_LOG_FLAGS(2, "FSF_ADAPTER_STATUS_AVAILABLE\n");
		switch (fsf_req->qtcb->header.fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			ZFCP_LOG_FLAGS(2,
				       "FSF_SQ_INVOKE_LINK_TEST_PROCEDURE\n");
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ltest");
			/* This will now be escalated by ERP */
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			ZFCP_LOG_FLAGS(2,
				       "FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED\n");
			/* ERP strategy will escalate */
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ulp");
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		default:
			ZFCP_LOG_NORMAL
			    ("bug: Wrong status qualifier 0x%x arrived.\n",
			     fsf_req->qtcb->header.fsf_status_qual.word[0]);
			debug_text_event(fsf_req->adapter->erp_dbf, 0,
					 "fsf_sq_inval:");
			debug_exception(
				fsf_req->adapter->erp_dbf, 0,
				&fsf_req->qtcb->header.fsf_status_qual.word[0],
				sizeof (u32));
			break;
		}
		break;

	case FSF_GOOD:
		ZFCP_LOG_FLAGS(3, "FSF_GOOD\n");
		ZFCP_LOG_DEBUG("Remote port (WWPN=0x%Lx) via adapter "
			       "(busid=%s) physically closed, "
			       "port handle 0x%x\n",
			       port->wwpn,
			       zfcp_get_busid_by_port(port), port->handle);
		/* can't use generic zfcp_erp_modify_port_status because
		 * ZFCP_STATUS_COMMON_OPEN must not be reset for the port
		 */
		atomic_clear_mask(ZFCP_STATUS_PORT_PHYS_OPEN, &port->status);
		list_for_each_entry(unit, &port->unit_list_head, list)
		    atomic_clear_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status);
		retval = 0;
		break;

	default:
		ZFCP_LOG_NORMAL("bug: An unknown FSF Status was presented "
				"(debug info 0x%x)\n",
				fsf_req->qtcb->header.fsf_status);
		debug_text_event(fsf_req->adapter->erp_dbf, 0, "fsf_s_inval:");
		debug_exception(fsf_req->adapter->erp_dbf, 0,
				&fsf_req->qtcb->header.fsf_status,
				sizeof (u32));
		break;
	}

 skip_fsfstatus:
	atomic_clear_mask(ZFCP_STATUS_PORT_PHYS_CLOSING, &port->status);
	return retval;
}

/*
 * function:    zfcp_fsf_open_unit
 *
 * purpose:
 *
 * returns:
 *
 * assumptions:	This routine does not check whether the associated
 *		remote port has already been opened. This should be
 *		done by calling routines. Otherwise some status
 *		may be presented by FSF
 */
int
zfcp_fsf_open_unit(struct zfcp_erp_action *erp_action)
{
	int retval = 0;
	unsigned long lock_flags;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(erp_action->adapter,
				     FSF_QTCB_OPEN_LUN,
				     &lock_flags,
				     ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
				     &(erp_action->fsf_req));
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Out of resources. Could not create an "
			      "open unit request for FCP-LUN 0x%Lx connected "
			      "to the port with WWPN 0x%Lx connected to "
			      "the adapter %s.\n",
			      erp_action->unit->fcp_lun,
			      erp_action->unit->port->wwpn,
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		goto out;
	}

	erp_action->fsf_req->qtcb->header.port_handle =
	    erp_action->port->handle;
	*(fcp_lun_t *) & (erp_action->fsf_req->qtcb->bottom.support.fcp_lun)
	    = erp_action->unit->fcp_lun;
	atomic_set_mask(ZFCP_STATUS_COMMON_OPENING, &erp_action->unit->status);
	erp_action->fsf_req->data.open_unit.unit = erp_action->unit;
	erp_action->fsf_req->erp_action = erp_action;

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(erp_action->fsf_req, &erp_action->timer);
	if (retval) {
		ZFCP_LOG_INFO("error: Could not send an open unit request "
			      "on the adapter %s, port WWPN 0x%Lx for "
			      "unit LUN 0x%Lx\n",
			      zfcp_get_busid_by_adapter(erp_action->adapter),
			      erp_action->port->wwpn,
			      erp_action->unit->fcp_lun);
		zfcp_fsf_req_free(erp_action->fsf_req);
		erp_action->fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_TRACE("Open LUN request initiated (adapter busid=%s, "
		       "port wwpn=0x%Lx, unit fcp_lun=0x%Lx)\n",
		       zfcp_get_busid_by_adapter(erp_action->adapter),
		       erp_action->port->wwpn, erp_action->unit->fcp_lun);
 out:
	write_unlock_irqrestore(&erp_action->adapter->request_queue.queue_lock,
				lock_flags);
	return retval;
}

/*
 * function:    zfcp_fsf_open_unit_handler
 *
 * purpose:	is called for finished Open LUN command
 *
 * returns:	
 */
static int
zfcp_fsf_open_unit_handler(struct zfcp_fsf_req *fsf_req)
{
	int retval = -EINVAL;
	struct zfcp_unit *unit;

	unit = fsf_req->data.open_unit.unit;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* don't change unit status in our bookkeeping */
		goto skip_fsfstatus;
	}

	/* evaluate FSF status in QTCB */
	switch (fsf_req->qtcb->header.fsf_status) {

	case FSF_PORT_HANDLE_NOT_VALID:
		ZFCP_LOG_FLAGS(1, "FSF_PORT_HANDLE_NOT_VALID\n");
		ZFCP_LOG_INFO("Temporary port identifier (handle) 0x%x "
			      "for the port with WWPN 0x%Lx connected to "
			      "the adapter %s is"
			      "not valid. This may happen occasionally.\n",
			      unit->port->handle,
			      unit->port->wwpn, zfcp_get_busid_by_unit(unit));
		ZFCP_LOG_DEBUG("status qualifier:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      (char *) &fsf_req->qtcb->header.fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 1, "fsf_s_ph_nv");
		zfcp_erp_adapter_reopen(unit->port->adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_LUN_ALREADY_OPEN:
		ZFCP_LOG_FLAGS(0, "FSF_LUN_ALREADY_OPEN\n");
		ZFCP_LOG_NORMAL("bug: Attempted to open the logical unit "
				"with FCP-LUN 0x%Lx at "
				"the remote port with WWPN 0x%Lx connected "
				"to the adapter %s twice.\n",
				unit->fcp_lun,
				unit->port->wwpn, zfcp_get_busid_by_unit(unit));
		debug_text_exception(fsf_req->adapter->erp_dbf, 0,
				     "fsf_s_uopen");
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PORT_BOXED:
		ZFCP_LOG_FLAGS(2, "FSF_PORT_BOXED\n");
		ZFCP_LOG_DEBUG("The remote port "
			       "with WWPN 0x%Lx on the adapter %s "
			       "needs to be reopened\n",
			       unit->port->wwpn, zfcp_get_busid_by_unit(unit));
		debug_text_event(fsf_req->adapter->erp_dbf, 2, "fsf_s_pboxed");
		zfcp_erp_port_reopen(unit->port, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			ZFCP_STATUS_FSFREQ_RETRY;
		break;

	case FSF_LUN_IN_USE:
		ZFCP_LOG_FLAGS(0, "FSF_LUN_IN_USE\n");
		ZFCP_LOG_NORMAL("error: FCP-LUN 0x%Lx at "
				"the remote port with WWPN 0x%Lx connected "
				"to the adapter %s "
				"is already owned by another operating system "
				"instance (LPAR or VM guest)\n",
				unit->fcp_lun,
				unit->port->wwpn,
				zfcp_get_busid_by_unit(unit));
		ZFCP_LOG_NORMAL("Additional sense data is presented:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
			      (char *) &fsf_req->qtcb->header.fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 2,
				 "fsf_s_l_in_use");
		zfcp_erp_unit_failed(unit);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_MAXIMUM_NUMBER_OF_LUNS_EXCEEDED:
		ZFCP_LOG_FLAGS(1, "FSF_MAXIMUM_NUMBER_OF_LUNS_EXCEEDED\n");
		ZFCP_LOG_INFO("error: The adapter ran out of resources. "
			      "There is no handle (temporary port identifier) "
			      "available for the unit with FCP-LUN 0x%Lx "
			      "at the remote port with WWPN 0x%Lx connected "
			      "to the adapter %s\n",
			      unit->fcp_lun,
			      unit->port->wwpn,
			      zfcp_get_busid_by_unit(unit));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_max_units");
		zfcp_erp_unit_failed(unit);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		ZFCP_LOG_FLAGS(2, "FSF_ADAPTER_STATUS_AVAILABLE\n");
		switch (fsf_req->qtcb->header.fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			ZFCP_LOG_FLAGS(2,
				       "FSF_SQ_INVOKE_LINK_TEST_PROCEDURE\n");
			/* Re-establish link to port */
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ltest");
			zfcp_erp_port_reopen(unit->port, 0);
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			ZFCP_LOG_FLAGS(2,
				       "FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED\n");
			/* ERP strategy will escalate */
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ulp");
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		default:
			ZFCP_LOG_NORMAL
			    ("bug: Wrong status qualifier 0x%x arrived.\n",
			     fsf_req->qtcb->header.fsf_status_qual.word[0]);
			debug_text_event(fsf_req->adapter->erp_dbf, 0,
					 "fsf_sq_inval:");
			debug_exception(
				fsf_req->adapter->erp_dbf, 0,
				&fsf_req->qtcb->header.fsf_status_qual.word[0],
				sizeof (u32));
		}
		break;

	case FSF_GOOD:
		ZFCP_LOG_FLAGS(3, "FSF_GOOD\n");
		/* save LUN handle assigned by FSF */
		unit->handle = fsf_req->qtcb->header.lun_handle;
		ZFCP_LOG_TRACE("unit (FCP_LUN=0x%Lx) of remote port "
			       "(WWPN=0x%Lx) via adapter (busid=%s) opened, "
			       "port handle 0x%x \n",
			       unit->fcp_lun,
			       unit->port->wwpn,
			       zfcp_get_busid_by_unit(unit),
			       unit->handle);
		/* mark unit as open */
		atomic_set_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status);
		retval = 0;
		break;

	default:
		ZFCP_LOG_NORMAL("bug: An unknown FSF Status was presented "
				"(debug info 0x%x)\n",
				fsf_req->qtcb->header.fsf_status);
		debug_text_event(fsf_req->adapter->erp_dbf, 0, "fsf_s_inval:");
		debug_exception(fsf_req->adapter->erp_dbf, 0,
				&fsf_req->qtcb->header.fsf_status,
				sizeof (u32));
		break;
	}

      skip_fsfstatus:
	atomic_clear_mask(ZFCP_STATUS_COMMON_OPENING, &unit->status);
	return retval;
}

/*
 * function:    zfcp_fsf_close_unit
 *
 * purpose:
 *
 * returns:	address of fsf_req - request successfully initiated
 *		NULL - 
 *
 * assumptions: This routine does not check whether the associated
 *              remote port/lun has already been opened. This should be
 *              done by calling routines. Otherwise some status
 *              may be presented by FSF
 */
int
zfcp_fsf_close_unit(struct zfcp_erp_action *erp_action)
{
	int retval = 0;
	unsigned long lock_flags;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(erp_action->adapter,
				     FSF_QTCB_CLOSE_LUN,
				     &lock_flags,
				     ZFCP_WAIT_FOR_SBAL | ZFCP_REQ_AUTO_CLEANUP,
				     &(erp_action->fsf_req));
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Out of resources. Could not create a "
			      "close unit request for FCP-LUN 0x%Lx "
			      "connected to the port with WWPN 0x%Lx connected "
			      "to the adapter %s.\n",
			      erp_action->unit->fcp_lun,
			      erp_action->port->wwpn,
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		goto out;
	}

	erp_action->fsf_req->qtcb->header.port_handle =
	    erp_action->port->handle;
	erp_action->fsf_req->qtcb->header.lun_handle = erp_action->unit->handle;
	atomic_set_mask(ZFCP_STATUS_COMMON_CLOSING, &erp_action->unit->status);
	erp_action->fsf_req->data.close_unit.unit = erp_action->unit;
	erp_action->fsf_req->erp_action = erp_action;

	/* start QDIO request for this FSF request */
	retval = zfcp_fsf_req_send(erp_action->fsf_req, &erp_action->timer);
	if (retval) {
		ZFCP_LOG_INFO("error: Could not send a close unit request for "
			      "FCP-LUN 0x%Lx connected to the port with "
			      "WWPN 0x%Lx connected to the adapter %s.\n",
			      erp_action->unit->fcp_lun,
			      erp_action->port->wwpn,
			      zfcp_get_busid_by_adapter(erp_action->adapter));
		zfcp_fsf_req_free(erp_action->fsf_req);
		erp_action->fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_TRACE("Close LUN request initiated (adapter busid=%s, "
		       "port wwpn=0x%Lx, unit fcp_lun=0x%Lx)\n",
		       zfcp_get_busid_by_adapter(erp_action->adapter),
		       erp_action->port->wwpn, erp_action->unit->fcp_lun);
 out:
	write_unlock_irqrestore(&erp_action->adapter->request_queue.queue_lock,
				lock_flags);
	return retval;
}

/*
 * function:    zfcp_fsf_close_unit_handler
 *
 * purpose:     is called for finished Close LUN FSF command
 *
 * returns:
 */
static int
zfcp_fsf_close_unit_handler(struct zfcp_fsf_req *fsf_req)
{
	int retval = -EINVAL;
	struct zfcp_unit *unit;

	unit = fsf_req->data.close_unit.unit;	/* restore unit */

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* don't change unit status in our bookkeeping */
		goto skip_fsfstatus;
	}

	/* evaluate FSF status in QTCB */
	switch (fsf_req->qtcb->header.fsf_status) {

	case FSF_PORT_HANDLE_NOT_VALID:
		ZFCP_LOG_FLAGS(1, "FSF_PORT_HANDLE_NOT_VALID\n");
		ZFCP_LOG_INFO("Temporary port identifier (handle) 0x%x "
			      "for the port with WWPN 0x%Lx connected to "
			      "the adapter %s is not valid. This may "
			      "happen in rare circumstances\n",
			      unit->port->handle,
			      unit->port->wwpn,
			      zfcp_get_busid_by_unit(unit));
		ZFCP_LOG_DEBUG("status qualifier:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      (char *) &fsf_req->qtcb->header.fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_phand_nv");
		zfcp_erp_adapter_reopen(unit->port->adapter, 0);
		zfcp_cmd_dbf_event_fsf("porthinv", fsf_req,
				       &fsf_req->qtcb->header.fsf_status_qual,
				       sizeof (union fsf_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_LUN_HANDLE_NOT_VALID:
		ZFCP_LOG_FLAGS(1, "FSF_LUN_HANDLE_NOT_VALID\n");
		ZFCP_LOG_INFO("Temporary LUN identifier (handle) 0x%x "
			      "of the logical unit with FCP-LUN 0x%Lx at "
			      "the remote port with WWPN 0x%Lx connected "
			      "to the adapter %s is "
			      "not valid. This may happen occasionally.\n",
			      unit->handle,
			      unit->fcp_lun,
			      unit->port->wwpn,
			      zfcp_get_busid_by_unit(unit));
		ZFCP_LOG_DEBUG("Status qualifier data:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      (char *) &fsf_req->qtcb->header.fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_lhand_nv");
		zfcp_erp_port_reopen(unit->port, 0);
		zfcp_cmd_dbf_event_fsf("lunhinv", fsf_req,
				       &fsf_req->qtcb->header.fsf_status_qual,
				       sizeof (union fsf_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PORT_BOXED:
		ZFCP_LOG_FLAGS(2, "FSF_PORT_BOXED\n");
		ZFCP_LOG_DEBUG("The remote port "
			       "with WWPN 0x%Lx on the adapter %s "
			       "needs to be reopened\n",
			       unit->port->wwpn,
			       zfcp_get_busid_by_unit(unit));
		debug_text_event(fsf_req->adapter->erp_dbf, 2, "fsf_s_pboxed");
		zfcp_erp_port_reopen(unit->port, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			ZFCP_STATUS_FSFREQ_RETRY;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		ZFCP_LOG_FLAGS(2, "FSF_ADAPTER_STATUS_AVAILABLE\n");
		switch (fsf_req->qtcb->header.fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			ZFCP_LOG_FLAGS(2,
				       "FSF_SQ_INVOKE_LINK_TEST_PROCEDURE\n");
			/* re-establish link to port */
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ltest");
			zfcp_erp_port_reopen(unit->port, 0);
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			ZFCP_LOG_FLAGS(2,
				       "FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED\n");
			/* ERP strategy will escalate */
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ulp");
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		default:
			ZFCP_LOG_NORMAL
			    ("bug: Wrong status qualifier 0x%x arrived.\n",
			     fsf_req->qtcb->header.fsf_status_qual.word[0]);
			debug_text_event(fsf_req->adapter->erp_dbf, 0,
					 "fsf_sq_inval:");
			debug_exception(
				fsf_req->adapter->erp_dbf, 0,
				&fsf_req->qtcb->header.fsf_status_qual.word[0],
				sizeof (u32));
			break;
		}
		break;

	case FSF_GOOD:
		ZFCP_LOG_FLAGS(3, "FSF_GOOD\n");
		ZFCP_LOG_TRACE("unit (FCP_LUN=0x%Lx) of remote port "
			       "(WWPN=0x%Lx) via adapter (busid=%s) closed, "
			       "port handle 0x%x \n",
			       unit->fcp_lun,
			       unit->port->wwpn,
			       zfcp_get_busid_by_unit(unit),
			       unit->handle);
		/* mark unit as closed */
		atomic_clear_mask(ZFCP_STATUS_COMMON_OPEN, &unit->status);
		retval = 0;
		break;

	default:
		ZFCP_LOG_NORMAL("bug: An unknown FSF Status was presented "
				"(debug info 0x%x)\n",
				fsf_req->qtcb->header.fsf_status);
		debug_text_event(fsf_req->adapter->erp_dbf, 0, "fsf_s_inval:");
		debug_exception(fsf_req->adapter->erp_dbf, 0,
				&fsf_req->qtcb->header.fsf_status,
				sizeof (u32));
		break;
	}

 skip_fsfstatus:
	atomic_clear_mask(ZFCP_STATUS_COMMON_CLOSING, &unit->status);
	return retval;
}

/*
 * function:    zfcp_fsf_send_fcp_command_task
 *
 * purpose:
 *
 * returns:
 *
 * note: we do not employ linked commands (not supported by HBA anyway)
 */
int
zfcp_fsf_send_fcp_command_task(struct zfcp_adapter *adapter,
			       struct zfcp_unit *unit,
			       Scsi_Cmnd * scsi_cmnd, int req_flags)
{
	struct zfcp_fsf_req *fsf_req = NULL;
	struct fcp_cmnd_iu *fcp_cmnd_iu;
	volatile struct qdio_buffer_element *buffere;
	unsigned int sbtype;
	unsigned long lock_flags;
	int real_bytes = 0;
	int retval = 0;

	/* setup new FSF request */

	retval = zfcp_fsf_req_create(adapter,
				     FSF_QTCB_FCP_CMND,
				     &lock_flags, req_flags, &(fsf_req));
	if (retval < 0) {
		ZFCP_LOG_DEBUG("error: Out of resources. Could not create an "
			       "FCP command request for FCP-LUN 0x%Lx "
			       "connected to the port with WWPN 0x%Lx "
			       "connected to the adapter %s.\n",
			       unit->fcp_lun,
			       unit->port->wwpn,
			       zfcp_get_busid_by_adapter(adapter));
		goto failed_req_create;
	}

	/*
	 * associate FSF request with SCSI request
	 * (need this for look up on abort)
	 */
	fsf_req->data.send_fcp_command_task.fsf_req = fsf_req;
	scsi_cmnd->host_scribble = (char *) &(fsf_req->data);

	/*
	 * associate SCSI command with FSF request
	 * (need this for look up on normal command completion)
	 */
	fsf_req->data.send_fcp_command_task.scsi_cmnd = scsi_cmnd;
#ifdef ZFCP_DEBUG_REQUESTS
	debug_text_event(adapter->req_dbf, 3, "fsf/sc");
	debug_event(adapter->req_dbf, 3, &fsf_req, sizeof (unsigned long));
	debug_event(adapter->req_dbf, 3, &scsi_cmnd, sizeof (unsigned long));
#endif				/* ZFCP_DEBUG_REQUESTS */
#ifdef ZFCP_DEBUG_ABORTS
	fsf_req->data.send_fcp_command_task.start_jiffies = jiffies;
#endif

	fsf_req->data.send_fcp_command_task.unit = unit;
	ZFCP_LOG_DEBUG("unit=0x%lx, unit_fcp_lun=0x%Lx\n",
		       (unsigned long) unit, unit->fcp_lun);

	/* set handles of unit and its parent port in QTCB */
	fsf_req->qtcb->header.lun_handle = unit->handle;
	fsf_req->qtcb->header.port_handle = unit->port->handle;

	/* FSF does not define the structure of the FCP_CMND IU */
	fcp_cmnd_iu = (struct fcp_cmnd_iu *)
	    &(fsf_req->qtcb->bottom.io.fcp_cmnd);

	/*
	 * set depending on data direction:
	 *      data direction bits in SBALE (SB Type)
	 *      data direction bits in QTCB
	 *      data direction bits in FCP_CMND IU
	 */
	switch (scsi_cmnd->sc_data_direction) {
	case SCSI_DATA_NONE:
		ZFCP_LOG_FLAGS(3, "SCSI_DATA_NONE\n");
		fsf_req->qtcb->bottom.io.data_direction = FSF_DATADIR_CMND;
		/*
		 * FIXME(qdio):
		 * what is the correct type for commands
		 * without 'real' data buffers?
		 */
		sbtype = SBAL_FLAGS0_TYPE_READ;
		break;
	case SCSI_DATA_READ:
		ZFCP_LOG_FLAGS(3, "SCSI_DATA_READ\n");
		fsf_req->qtcb->bottom.io.data_direction = FSF_DATADIR_READ;
		sbtype = SBAL_FLAGS0_TYPE_READ;
		fcp_cmnd_iu->rddata = 1;
		break;
	case SCSI_DATA_WRITE:
		ZFCP_LOG_FLAGS(3, "SCSI_DATA_WRITE\n");
		fsf_req->qtcb->bottom.io.data_direction = FSF_DATADIR_WRITE;
		sbtype = SBAL_FLAGS0_TYPE_WRITE;
		fcp_cmnd_iu->wddata = 1;
		break;
	case SCSI_DATA_UNKNOWN:
		ZFCP_LOG_FLAGS(0, "SCSI_DATA_UNKNOWN not supported\n");
	default:
		/*
		 * dummy, catch this condition earlier
		 * in zfcp_scsi_queuecommand
		 */
		goto failed_scsi_cmnd;
	}
	buffere =
	    &(adapter->request_queue.buffer[fsf_req->sbal_index]->element[0]);
	buffere->flags |= sbtype;

	/* set FC service class in QTCB (3 per default) */
	fsf_req->qtcb->bottom.io.service_class = adapter->fc_service_class;

	/* set FCP_LUN in FCP_CMND IU in QTCB */
	fcp_cmnd_iu->fcp_lun = unit->fcp_lun;

	/* set task attributes in FCP_CMND IU in QTCB */
	if (scsi_cmnd->device->simple_tags) {
		fcp_cmnd_iu->task_attribute = SIMPLE_Q;
		ZFCP_LOG_TRACE("setting SIMPLE_Q task attribute\n");
	} else {
		fcp_cmnd_iu->task_attribute = UNTAGGED;
		ZFCP_LOG_TRACE("setting UNTAGGED task attribute\n");
	}

	/* set additional length of FCP_CDB in FCP_CMND IU in QTCB, if needed */
	if (scsi_cmnd->cmd_len > FCP_CDB_LENGTH) {
		fcp_cmnd_iu->add_fcp_cdb_length
		    = (scsi_cmnd->cmd_len - FCP_CDB_LENGTH) >> 2;
		ZFCP_LOG_TRACE("SCSI CDB length is 0x%x, "
			       "additional FCP_CDB length is 0x%x "
			       "(shifted right 2 bits)\n",
			       scsi_cmnd->cmd_len,
			       fcp_cmnd_iu->add_fcp_cdb_length);
	}
	/*
	 * copy SCSI CDB (including additional length, if any) to
	 * FCP_CDB in FCP_CMND IU in QTCB
	 */
	memcpy(fcp_cmnd_iu->fcp_cdb, scsi_cmnd->cmnd, scsi_cmnd->cmd_len);

	/* FCP CMND IU length in QTCB */
	fsf_req->qtcb->bottom.io.fcp_cmnd_length =
		sizeof (struct fcp_cmnd_iu) +
		fcp_cmnd_iu->add_fcp_cdb_length + sizeof (fcp_dl_t);

	/* generate SBALEs from data buffer */
	real_bytes = zfcp_create_sbals_from_sg(fsf_req,
					       scsi_cmnd,
					       sbtype,
					       0, ZFCP_MAX_SBALS_PER_REQ);
	/* Note: >= and not = because the combined scatter-gather entries
	 * may be larger than request_bufflen according to the mailing list
	 */
	if (real_bytes >= scsi_cmnd->request_bufflen) {
		ZFCP_LOG_TRACE("Data fits\n");
	} else if (real_bytes == 0) {
		ZFCP_LOG_DEBUG("Data did not fit into available buffer(s), "
			       "waiting for more...\n");
		retval = -EIO;
		goto no_fit;
	} else {
		ZFCP_LOG_NORMAL("error: No truncation implemented but "
				"required. Shutting down unit (busid=%s, "
				"WWPN=0x%16.16Lx, FCP_LUN=0x%16.16Lx)\n",
				zfcp_get_busid_by_unit(unit),
				unit->port->wwpn,
				unit->fcp_lun);
		zfcp_erp_unit_shutdown(unit, 0);
		retval = -EINVAL;
		goto no_fit;
	}

	/* set length of FCP data length in FCP_CMND IU in QTCB */
	zfcp_set_fcp_dl(fcp_cmnd_iu, real_bytes);

	ZFCP_LOG_DEBUG("Sending SCSI command:\n");
	ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
		      (char *) scsi_cmnd->cmnd, scsi_cmnd->cmd_len);

	/*
	 * start QDIO request for this FSF request
	 *  covered by an SBALE)
	 */
	retval = zfcp_fsf_req_send(fsf_req, NULL);
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Could not send an FCP command request "
			      "for a command on the adapter %s, "
			      "port WWPN 0x%Lx and unit LUN 0x%Lx\n",
			      zfcp_get_busid_by_adapter(adapter),
			      unit->port->wwpn,
			      unit->fcp_lun);
		goto send_failed;
	}

	ZFCP_LOG_TRACE("Send FCP Command initiated (adapter busid=%s, "
		       "port wwpn=0x%Lx, unit fcp_lun=0x%Lx)\n",
		       zfcp_get_busid_by_adapter(adapter),
		       unit->port->wwpn,
		       unit->fcp_lun);
	goto success;

 send_failed:
 no_fit:
 failed_scsi_cmnd:
	/* dequeue new FSF request previously enqueued */
#ifdef ZFCP_DEBUG_REQUESTS
	debug_text_event(adapter->req_dbf, 3, "fail_sc");
	debug_event(adapter->req_dbf, 3, &scsi_cmnd, sizeof (unsigned long));
#endif				/* ZFCP_DEBUG_REQUESTS */

	zfcp_fsf_req_free(fsf_req);
	fsf_req = NULL;
 success:
 failed_req_create:
	write_unlock_irqrestore(&adapter->request_queue.queue_lock, lock_flags);
	return retval;
}

/*
 * function:    zfcp_fsf_send_fcp_command_task_management
 *
 * purpose:
 *
 * returns:
 *
 * FIXME(design): should be watched by a timeout!!!
 * FIXME(design) shouldn't this be modified to return an int
 *               also...don't know how though
 *
 */
struct zfcp_fsf_req *
zfcp_fsf_send_fcp_command_task_management(struct zfcp_adapter *adapter,
					  struct zfcp_unit *unit,
					  u8 tm_flags, int req_flags)
{
	struct zfcp_fsf_req *fsf_req = NULL;
	int retval = 0;
	struct fcp_cmnd_iu *fcp_cmnd_iu;
	unsigned long lock_flags;
	volatile struct qdio_buffer_element *buffere;

	/* setup new FSF request */
	retval = zfcp_fsf_req_create(adapter, FSF_QTCB_FCP_CMND,
				     &lock_flags, req_flags, &(fsf_req));
	if (retval < 0) {
		ZFCP_LOG_INFO("error: Out of resources. Could not create an "
			      "FCP command (task management) request for "
			      "the adapter %s, port with "
			      "WWPN 0x%Lx and FCP_LUN 0x%Lx.\n",
			      zfcp_get_busid_by_adapter(adapter),
			      unit->port->wwpn, unit->fcp_lun);
		goto out;
	}

	/*
	 * Used to decide on proper handler in the return path,
	 * could be either zfcp_fsf_send_fcp_command_task_handler or
	 * zfcp_fsf_send_fcp_command_task_management_handler */

	fsf_req->status |= ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT;

	/*
	 * hold a pointer to the unit being target of this
	 * task management request
	 */
	fsf_req->data.send_fcp_command_task_management.unit = unit;

	/* set FSF related fields in QTCB */
	fsf_req->qtcb->header.lun_handle = unit->handle;
	fsf_req->qtcb->header.port_handle = unit->port->handle;
	fsf_req->qtcb->bottom.io.data_direction = FSF_DATADIR_CMND;
	fsf_req->qtcb->bottom.io.service_class = adapter->fc_service_class;
	fsf_req->qtcb->bottom.io.fcp_cmnd_length =
		sizeof (struct fcp_cmnd_iu) + sizeof (fcp_dl_t);

	buffere =
	    &(adapter->request_queue.buffer[fsf_req->sbal_index]->element[0]);
	buffere[0].flags |= SBAL_FLAGS0_TYPE_WRITE;
	buffere[1].flags |= SBAL_FLAGS_LAST_ENTRY;

	/* set FCP related fields in FCP_CMND IU in QTCB */
	fcp_cmnd_iu = (struct fcp_cmnd_iu *)
		&(fsf_req->qtcb->bottom.io.fcp_cmnd);
	fcp_cmnd_iu->fcp_lun = unit->fcp_lun;
	fcp_cmnd_iu->task_management_flags = tm_flags;

	/* start QDIO request for this FSF request */
	zfcp_fsf_start_scsi_er_timer(adapter);
	retval = zfcp_fsf_req_send(fsf_req, NULL);
	if (retval) {
		del_timer(&adapter->scsi_er_timer);
		ZFCP_LOG_INFO("error: Could not send an FCP-command (task "
			      "management) on the adapter %s, port WWPN "
			      "0x%Lx for unit LUN 0x%Lx\n",
			      zfcp_get_busid_by_adapter(adapter),
			      unit->port->wwpn,
			      unit->fcp_lun);
		zfcp_fsf_req_free(fsf_req);
		fsf_req = NULL;
		goto out;
	}

	ZFCP_LOG_TRACE("Send FCP Command (task management function) initiated "
		       "(adapter busid=%s, port wwpn=0x%Lx, "
		       "unit fcp_lun=0x%Lx, tm_flags=0x%x)\n",
		       zfcp_get_busid_by_adapter(adapter),
		       unit->port->wwpn,
		       unit->fcp_lun,
		       tm_flags);
 out:
	write_unlock_irqrestore(&adapter->request_queue.queue_lock, lock_flags);
	return fsf_req;
}

/*
 * function:    zfcp_fsf_send_fcp_command_handler
 *
 * purpose:	is called for finished Send FCP Command
 *
 * returns:	
 */
static int
zfcp_fsf_send_fcp_command_handler(struct zfcp_fsf_req *fsf_req)
{
	int retval = -EINVAL;
	struct zfcp_unit *unit;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT)
		unit = fsf_req->data.send_fcp_command_task_management.unit;
	else
		unit = fsf_req->data.send_fcp_command_task.unit;

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		/* go directly to calls of special handlers */
		goto skip_fsfstatus;
	}

	/* evaluate FSF status in QTCB */
	switch (fsf_req->qtcb->header.fsf_status) {

	case FSF_PORT_HANDLE_NOT_VALID:
		ZFCP_LOG_FLAGS(1, "FSF_PORT_HANDLE_NOT_VALID\n");
		ZFCP_LOG_INFO("Temporary port identifier (handle) 0x%x "
			      "for the port with WWPN 0x%Lx connected to "
			      "the adapter %s is not valid.\n",
			      unit->port->handle,
			      unit->port->wwpn, zfcp_get_busid_by_unit(unit));
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      (char *) &fsf_req->qtcb->header.fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_phand_nv");
		zfcp_erp_adapter_reopen(unit->port->adapter, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_LUN_HANDLE_NOT_VALID:
		ZFCP_LOG_FLAGS(1, "FSF_LUN_HANDLE_NOT_VALID\n");
		ZFCP_LOG_INFO("Temporary LUN identifier (handle) 0x%x "
			      "of the logical unit with FCP-LUN 0x%Lx at "
			      "the remote port with WWPN 0x%Lx connected "
			      "to the adapter %s is "
			      "not valid. This may happen occasionally.\n",
			      unit->handle,
			      unit->fcp_lun,
			      unit->port->wwpn,
			      zfcp_get_busid_by_unit(unit));
		ZFCP_LOG_NORMAL("Status qualifier data:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
			      (char *) &fsf_req->qtcb->header.fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_uhand_nv");
		zfcp_erp_port_reopen(unit->port, 0);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_HANDLE_MISMATCH:
		ZFCP_LOG_FLAGS(0, "FSF_HANDLE_MISMATCH\n");
		ZFCP_LOG_NORMAL("bug: The port handle (temporary port "
				"identifier) 0x%x has changed unexpectedly. "
				"This was detected upon receiveing the "
				"response of a command send to the unit with "
				"FCP-LUN 0x%Lx at the remote port with WWPN "
				"0x%Lx connected to the adapter %s.\n",
				unit->port->handle,
				unit->fcp_lun,
				unit->port->wwpn,
				zfcp_get_busid_by_unit(unit));
		ZFCP_LOG_NORMAL("status qualifier:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
			      (char *) &fsf_req->qtcb->header.fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_hand_mis");
		zfcp_erp_adapter_reopen(unit->port->adapter, 0);
		zfcp_cmd_dbf_event_fsf("handmism",
				       fsf_req,
				       &fsf_req->qtcb->header.fsf_status_qual,
				       sizeof (union fsf_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_SERVICE_CLASS_NOT_SUPPORTED:
		ZFCP_LOG_FLAGS(0, "FSF_SERVICE_CLASS_NOT_SUPPORTED\n");
		if (fsf_req->adapter->fc_service_class <= 3) {
			ZFCP_LOG_NORMAL("error: The adapter %s does "
					"not support fibre-channel class %d.\n",
					zfcp_get_busid_by_unit(unit),
					fsf_req->adapter->fc_service_class);
		} else {
			ZFCP_LOG_NORMAL("bug: The fibre channel class at the "
					"adapter %s is invalid. "
					"(debug info %d)\n",
					zfcp_get_busid_by_unit(unit),
					fsf_req->adapter->fc_service_class);
		}
		/* stop operation for this adapter */
		debug_text_exception(fsf_req->adapter->erp_dbf, 0,
				     "fsf_s_class_nsup");
		zfcp_erp_adapter_shutdown(unit->port->adapter, 0);
		zfcp_cmd_dbf_event_fsf("unsclass",
				       fsf_req,
				       &fsf_req->qtcb->header.fsf_status_qual,
				       sizeof (union fsf_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_FCPLUN_NOT_VALID:
		ZFCP_LOG_FLAGS(0, "FSF_FCPLUN_NOT_VALID\n");
		ZFCP_LOG_NORMAL("bug: The FCP LUN 0x%Lx behind the remote port "
				"of WWPN0x%Lx via the adapter %s does not have "
				"the correct unit handle (temporary unit "
				"identifier) 0x%x\n",
				unit->fcp_lun,
				unit->port->wwpn,
				zfcp_get_busid_by_unit(unit),
				unit->handle);
		ZFCP_LOG_DEBUG("status qualifier:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
			      (char *) &fsf_req->qtcb->header.fsf_status_qual,
			      sizeof (union fsf_status_qual));
		debug_text_event(fsf_req->adapter->erp_dbf, 1,
				 "fsf_s_fcp_lun_nv");
		zfcp_erp_port_reopen(unit->port, 0);
		zfcp_cmd_dbf_event_fsf("fluninv",
				       fsf_req,
				       &fsf_req->qtcb->header.fsf_status_qual,
				       sizeof (union fsf_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_DIRECTION_INDICATOR_NOT_VALID:
		ZFCP_LOG_FLAGS(0, "FSF_DIRECTION_INDICATOR_NOT_VALID\n");
		ZFCP_LOG_INFO("bug: Invalid data direction given for the unit "
			      "with FCP LUN 0x%Lx at the remote port with "
			      "WWPN 0x%Lx via the adapter %s "
			      "(debug info %d)\n",
			      unit->fcp_lun,
			      unit->port->wwpn,
			      zfcp_get_busid_by_unit(unit),
			      fsf_req->qtcb->bottom.io.data_direction);
		/* stop operation for this adapter */
		debug_text_event(fsf_req->adapter->erp_dbf, 0,
				 "fsf_s_dir_ind_nv");
		zfcp_erp_adapter_shutdown(unit->port->adapter, 0);
		zfcp_cmd_dbf_event_fsf("dirinv",
				       fsf_req,
				       &fsf_req->qtcb->header.fsf_status_qual,
				       sizeof (union fsf_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

		/* FIXME: this should be obsolete, isn' it? */
	case FSF_INBOUND_DATA_LENGTH_NOT_VALID:
		ZFCP_LOG_FLAGS(0, "FSF_INBOUND_DATA_LENGTH_NOT_VALID\n");
		ZFCP_LOG_NORMAL("bug: An invalid inbound data length field "
				"was found in a command for the unit with "
				"FCP LUN 0x%Lx of the remote port "
				"with WWPN 0x%Lx via the adapter %s.\n",
				unit->fcp_lun,
				unit->port->wwpn, zfcp_get_busid_by_unit(unit));
		/* stop operation for this adapter */
		debug_text_event(fsf_req->adapter->erp_dbf, 0,
				 "fsf_s_in_dl_nv");
		zfcp_erp_adapter_shutdown(unit->port->adapter, 0);
		zfcp_cmd_dbf_event_fsf("idleninv",
				       fsf_req,
				       &fsf_req->qtcb->header.fsf_status_qual,
				       sizeof (union fsf_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

		/* FIXME: this should be obsolete, isn' it? */
	case FSF_OUTBOUND_DATA_LENGTH_NOT_VALID:
		ZFCP_LOG_FLAGS(0, "FSF_OUTBOUND_DATA_LENGTH_NOT_VALID\n");
		ZFCP_LOG_NORMAL("bug: An invalid outbound data length field "
				"was found in a command for the unit with "
				"FCP LUN 0x%Lx of the remote port "
				"with WWPN 0x%Lx via the adapter %s\n.",
				unit->fcp_lun,
				unit->port->wwpn,
				zfcp_get_busid_by_unit(unit));
		/* stop operation for this adapter */
		debug_text_event(fsf_req->adapter->erp_dbf, 0,
				 "fsf_s_out_dl_nv");
		zfcp_erp_adapter_shutdown(unit->port->adapter, 0);
		zfcp_cmd_dbf_event_fsf("odleninv", fsf_req,
				       &fsf_req->qtcb->header.fsf_status_qual,
				       sizeof (union fsf_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_CMND_LENGTH_NOT_VALID:
		ZFCP_LOG_FLAGS(0, "FSF_CMND_LENGTH_NOT_VALID\n");
		ZFCP_LOG_NORMAL
		    ("bug: An invalid control-data-block length field "
		     "was found in a command for the unit with "
		     "FCP LUN 0x%Lx of the remote port "
		     "with WWPN 0x%Lx via the adapter %s " "(debug info %d)\n",
		     unit->fcp_lun, unit->port->wwpn,
		     zfcp_get_busid_by_unit(unit),
		     fsf_req->qtcb->bottom.io.fcp_cmnd_length);
		/* stop operation for this adapter */
		debug_text_event(fsf_req->adapter->erp_dbf, 0,
				 "fsf_s_cmd_len_nv");
		zfcp_erp_adapter_shutdown(unit->port->adapter, 0);
		zfcp_cmd_dbf_event_fsf("cleninv",
				       fsf_req,
				       &fsf_req->qtcb->header.fsf_status_qual,
				       sizeof (union fsf_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
		break;

	case FSF_PORT_BOXED:
		ZFCP_LOG_FLAGS(2, "FSF_PORT_BOXED\n");
		ZFCP_LOG_DEBUG("The remote port "
			       "with WWPN 0x%Lx on the adapter %s "
			       "needs to be reopened\n",
			       unit->port->wwpn, zfcp_get_busid_by_unit(unit));
		debug_text_event(fsf_req->adapter->erp_dbf, 2, "fsf_s_pboxed");
		zfcp_erp_port_reopen(unit->port, 0);
		zfcp_cmd_dbf_event_fsf("portbox", fsf_req,
				       &fsf_req->qtcb->header.fsf_status_qual,
				       sizeof (union fsf_status_qual));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR |
			ZFCP_STATUS_FSFREQ_RETRY;
		break;

	case FSF_ADAPTER_STATUS_AVAILABLE:
		ZFCP_LOG_FLAGS(2, "FSF_ADAPTER_STATUS_AVAILABLE\n");
		switch (fsf_req->qtcb->header.fsf_status_qual.word[0]) {
		case FSF_SQ_INVOKE_LINK_TEST_PROCEDURE:
			ZFCP_LOG_FLAGS(2,
				       "FSF_SQ_INVOKE_LINK_TEST_PROCEDURE\n");
			/* re-establish link to port */
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ltest");
			zfcp_erp_port_reopen(unit->port, 0);
			zfcp_cmd_dbf_event_fsf(
				"sqltest",
				fsf_req,
				&fsf_req->qtcb->header.fsf_status_qual,
				sizeof (union fsf_status_qual));
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		case FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED:
			ZFCP_LOG_FLAGS(3,
				       "FSF_SQ_ULP_DEPENDENT_ERP_REQUIRED\n");
			/* FIXME(hw) need proper specs for proper action */
			/* let scsi stack deal with retries and escalation */
			debug_text_event(fsf_req->adapter->erp_dbf, 1,
					 "fsf_sq_ulp");
			zfcp_cmd_dbf_event_fsf(
				"sqdeperp",
				fsf_req,
				&fsf_req->qtcb->header.fsf_status_qual,
				sizeof (union fsf_status_qual));
			fsf_req->status |= ZFCP_STATUS_FSFREQ_ERROR;
			break;
		default:
			/* FIXME: shall we consider this a successful transfer? */
			ZFCP_LOG_NORMAL
			    ("bug: Wrong status qualifier 0x%x arrived.\n",
			     fsf_req->qtcb->header.fsf_status_qual.word[0]);
			debug_text_event(fsf_req->adapter->erp_dbf, 0,
					 "fsf_sq_inval:");
			debug_exception(
				fsf_req->adapter->erp_dbf,
				0,
				&fsf_req->qtcb->header.fsf_status_qual.word[0],
				sizeof (u32));
			break;
		}
		break;

	case FSF_GOOD:
		ZFCP_LOG_FLAGS(3, "FSF_GOOD\n");
		break;

	case FSF_FCP_RSP_AVAILABLE:
		ZFCP_LOG_FLAGS(2, "FSF_FCP_RSP_AVAILABLE\n");
		break;

	default:
		debug_text_event(fsf_req->adapter->erp_dbf, 0, "fsf_s_inval:");
		debug_exception(fsf_req->adapter->erp_dbf, 0,
				&fsf_req->qtcb->header.fsf_status,
				sizeof (u32));
		break;
	}

 skip_fsfstatus:
	if (fsf_req->status & ZFCP_STATUS_FSFREQ_TASK_MANAGEMENT) {
		retval =
		    zfcp_fsf_send_fcp_command_task_management_handler(fsf_req);
	} else {
		retval = zfcp_fsf_send_fcp_command_task_handler(fsf_req);
	}
	return retval;
}

/*
 * function:    zfcp_fsf_send_fcp_command_task_handler
 *
 * purpose:	evaluates FCP_RSP IU
 *
 * returns:	
 */
static int
zfcp_fsf_send_fcp_command_task_handler(struct zfcp_fsf_req *fsf_req)
{
	int retval = 0;
	struct zfcp_adapter *adapter = fsf_req->adapter;

	Scsi_Cmnd *scpnt;
	struct fcp_rsp_iu *fcp_rsp_iu = (struct fcp_rsp_iu *)
	    &(fsf_req->qtcb->bottom.io.fcp_rsp);
	struct fcp_cmnd_iu *fcp_cmnd_iu = (struct fcp_cmnd_iu *)
	    &(fsf_req->qtcb->bottom.io.fcp_cmnd);
	u32 sns_len;
	char *fcp_rsp_info = zfcp_get_fcp_rsp_info_ptr(fcp_rsp_iu);
	unsigned long flags;
	struct zfcp_unit *unit = fsf_req->data.send_fcp_command_task.unit;

	read_lock_irqsave(&fsf_req->adapter->abort_lock, flags);
	scpnt = fsf_req->data.send_fcp_command_task.scsi_cmnd;
	if (!scpnt) {
		ZFCP_LOG_DEBUG
		    ("Command with fsf_req 0x%lx is not associated to "
		     "a scsi command anymore. Aborted?\n",
		     (unsigned long) fsf_req);
		goto out;
	}
	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ABORTED) {
		/* FIXME: (design) mid-layer should handle DID_ABORT like
		 *        DID_SOFT_ERROR by retrying the request for devices
		 *        that allow retries.
		 */
		ZFCP_LOG_DEBUG("Setting DID_SOFT_ERROR and SUGGEST_RETRY\n");
		set_host_byte(&scpnt->result, DID_SOFT_ERROR);
		set_driver_byte(&scpnt->result, SUGGEST_RETRY);
		goto skip_fsfstatus;
	}

	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		ZFCP_LOG_DEBUG("Setting DID_ERROR\n");
		set_host_byte(&scpnt->result, DID_ERROR);
		goto skip_fsfstatus;
	}

	/* set message byte of result in SCSI command */
	scpnt->result |= COMMAND_COMPLETE << 8;

	/*
	 * copy SCSI status code of FCP_STATUS of FCP_RSP IU to status byte
	 * of result in SCSI command
	 */
	scpnt->result |= fcp_rsp_iu->scsi_status;
	if (fcp_rsp_iu->scsi_status) {
		/* DEBUG */
		ZFCP_LOG_NORMAL("status for SCSI Command:\n");
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
			      scpnt->cmnd, scpnt->cmd_len);

		ZFCP_LOG_NORMAL("SCSI status code 0x%x\n",
				fcp_rsp_iu->scsi_status);
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
			      (void *) fcp_rsp_iu, sizeof (struct fcp_rsp_iu));
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_NORMAL,
			      zfcp_get_fcp_sns_info_ptr(fcp_rsp_iu),
			      fcp_rsp_iu->fcp_sns_len);
	}

	/* check FCP_RSP_INFO */
	if (fcp_rsp_iu->validity.bits.fcp_rsp_len_valid) {
		ZFCP_LOG_DEBUG("rsp_len is valid\n");
		switch (fcp_rsp_info[3]) {
		case RSP_CODE_GOOD:
			ZFCP_LOG_FLAGS(3, "RSP_CODE_GOOD\n");
			/* ok, continue */
			ZFCP_LOG_TRACE("no failure or Task Management "
				       "Function complete\n");
			set_host_byte(&scpnt->result, DID_OK);
			break;
		case RSP_CODE_LENGTH_MISMATCH:
			ZFCP_LOG_FLAGS(0, "RSP_CODE_LENGTH_MISMATCH\n");
			/* hardware bug */
			ZFCP_LOG_NORMAL("bug: FCP response code indictates "
					" that the fibre-channel protocol data "
					"length differs from the burst length. "
					"The problem occured on the unit "
					"with FCP LUN 0x%Lx connected to the "
					"port with WWPN 0x%Lx at the "
					"adapter %s",
					unit->fcp_lun,
					unit->port->wwpn,
					zfcp_get_busid_by_unit(unit));
			/* dump SCSI CDB as prepared by zfcp */
			ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
				      (char *) &fsf_req->qtcb->
				      bottom.io.fcp_cmnd, FSF_FCP_CMND_SIZE);
			zfcp_cmd_dbf_event_fsf("clenmis", fsf_req, NULL, 0);
			set_host_byte(&scpnt->result, DID_ERROR);
			goto skip_fsfstatus;
		case RSP_CODE_FIELD_INVALID:
			ZFCP_LOG_FLAGS(0, "RSP_CODE_FIELD_INVALID\n");
			/* driver or hardware bug */
			ZFCP_LOG_NORMAL("bug: FCP response code indictates "
					"that the fibre-channel protocol data "
					"fields were incorrectly set-up. "
					"The problem occured on the unit "
					"with FCP LUN 0x%Lx connected to the "
					"port with WWPN 0x%Lx at the "
					"adapter %s",
					unit->fcp_lun,
					unit->port->wwpn,
					zfcp_get_busid_by_unit(unit));
			/* dump SCSI CDB as prepared by zfcp */
			ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
				      (char *) &fsf_req->qtcb->
				      bottom.io.fcp_cmnd, FSF_FCP_CMND_SIZE);
			set_host_byte(&scpnt->result, DID_ERROR);
			zfcp_cmd_dbf_event_fsf("codeinv", fsf_req, NULL, 0);
			goto skip_fsfstatus;
		case RSP_CODE_RO_MISMATCH:
			ZFCP_LOG_FLAGS(0, "RSP_CODE_RO_MISMATCH\n");
			/* hardware bug */
			ZFCP_LOG_NORMAL("bug: The FCP response code indicates "
					"that conflicting  values for the "
					"fibre-channel payload offset from the "
					"header were found. "
					"The problem occured on the unit "
					"with FCP LUN 0x%Lx connected to the "
					"port with WWPN 0x%Lx at the "
					"adapter %s.\n",
					unit->fcp_lun,
					unit->port->wwpn,
					zfcp_get_busid_by_unit(unit));
			/* dump SCSI CDB as prepared by zfcp */
			ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
				      (char *) &fsf_req->qtcb->
				      bottom.io.fcp_cmnd, FSF_FCP_CMND_SIZE);
			zfcp_cmd_dbf_event_fsf("codemism", fsf_req, NULL, 0);
			set_host_byte(&scpnt->result, DID_ERROR);
			goto skip_fsfstatus;
		default:
			ZFCP_LOG_NORMAL("bug: An invalid FCP response "
					"code was detected for a command. "
					"The problem occured on the unit "
					"with FCP LUN 0x%Lx connected to the "
					"port with WWPN 0x%Lx at the "
					"adapter %s "
					"(debug info 0x%x)\n",
					unit->fcp_lun,
					unit->port->wwpn,
					zfcp_get_busid_by_unit(unit),
					fcp_rsp_info[3]);
			/* dump SCSI CDB as prepared by zfcp */
			ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG,
				      (char *) &fsf_req->qtcb->
				      bottom.io.fcp_cmnd, FSF_FCP_CMND_SIZE);
			zfcp_cmd_dbf_event_fsf("undeffcp", fsf_req, NULL, 0);
			set_host_byte(&scpnt->result, DID_ERROR);
		}
	}

	/* check for sense data */
	if (fcp_rsp_iu->validity.bits.fcp_sns_len_valid) {
		sns_len = FSF_FCP_RSP_SIZE -
		    sizeof (struct fcp_rsp_iu) + fcp_rsp_iu->fcp_rsp_len;
		ZFCP_LOG_TRACE("room for %i bytes sense data in QTCB\n",
			       sns_len);
		sns_len = min(sns_len, (u32) SCSI_SENSE_BUFFERSIZE);
		ZFCP_LOG_TRACE("room for %i bytes sense data in SCSI command\n",
			       SCSI_SENSE_BUFFERSIZE);
		sns_len = min(sns_len, fcp_rsp_iu->fcp_sns_len);
		ZFCP_LOG_TRACE("scpnt->result =0x%x, command was:\n",
			       scpnt->result);
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_TRACE,
			      (void *) &scpnt->cmnd, scpnt->cmd_len);

		ZFCP_LOG_TRACE("%i bytes sense data provided by FCP\n",
			       fcp_rsp_iu->fcp_sns_len);
		memcpy(&scpnt->sense_buffer,
		       zfcp_get_fcp_sns_info_ptr(fcp_rsp_iu), sns_len);
		ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_TRACE,
			      (void *) &scpnt->sense_buffer, sns_len);
	}

	/* check for overrun */
	if (fcp_rsp_iu->validity.bits.fcp_resid_over) {
		ZFCP_LOG_INFO("A data overrun was detected for a command. "
			      "This happened for a command to the unit "
			      "with FCP LUN 0x%Lx connected to the "
			      "port with WWPN 0x%Lx at the adapter %s. "
			      "The response data length is "
			      "%d, the original length was %d.\n",
			      unit->fcp_lun,
			      unit->port->wwpn,
			      zfcp_get_busid_by_unit(unit),
			      fcp_rsp_iu->fcp_resid,
			      (int) zfcp_get_fcp_dl(fcp_cmnd_iu));
	}

	/* check for underrun */
	if (fcp_rsp_iu->validity.bits.fcp_resid_under) {
		ZFCP_LOG_DEBUG("A data underrun was detected for a command. "
			       "This happened for a command to the unit "
			       "with FCP LUN 0x%Lx connected to the "
			       "port with WWPN 0x%Lx at the adapter %s. "
			       "The response data length is "
			       "%d, the original length was %d.\n",
			       unit->fcp_lun,
			       unit->port->wwpn,
			       zfcp_get_busid_by_unit(unit),
			       fcp_rsp_iu->fcp_resid,
			       (int) zfcp_get_fcp_dl(fcp_cmnd_iu));
		/*
		 * It may not have been possible to send all data and the
		 * underrun on send may already be in scpnt->resid, so it's add
		 * not equals in the below statement.
		 */
		scpnt->resid += fcp_rsp_iu->fcp_resid;
		ZFCP_LOG_TRACE("scpnt->resid=0x%x\n", scpnt->resid);
	}

 skip_fsfstatus:
#if 0
	/*
	 * This nasty chop at the problem is not working anymore
	 * as we do not adjust the retry count anylonger in order
	 * to have a number of retries that avoids I/O errors.
	 * The manipulation of the retry count has been removed
	 * in favour of a safe tape device handling. We must not
	 * sent SCSI commands more than once to a device if no
	 * retries are permitted by the high level driver. Generally
	 * speaking, it was a mess to change retry counts. So it is
	 * fine that this sort of workaround is gone.
	 * Then, we had to face a certain number of immediate retries in case of
	 * busy and queue full conditions (see below).
	 * This is not acceptable
	 * for the latter. Queue full conditions are used
	 * by devices to indicate to a host that the host can rely
	 * on the completion (or timeout) of at least one outstanding
	 * command as a suggested trigger for command retries.
	 * Busy conditions require a different trigger since
	 * no commands are outstanding for that initiator from the
	 * devices perspective.
	 * The drawback of mapping a queue full condition to a
	 * busy condition is the chance of wasting all retries prior
	 * to the time when the device indicates that a command
	 * rejected due to a queue full condition should be re-driven.
	 * This case would lead to unnecessary I/O errors that
	 * have to be considered fatal if for example ext3's
	 * journaling would be torpedoed by such an avoidable
	 * I/O error.
	 * So, what issues are there with not mapping a queue-full
	 * condition to a busy condition?
	 * Due to the 'exclusive LUN'
	 * policy enforced by the zSeries FCP channel, this 
	 * Linux instance is the only initiator with regard to
	 * this adapter. It is safe to rely on the information
	 * 'don't disturb me now ... and btw. no other commands
	 * pending for you' (= queue full) sent by the LU,
	 * since no other Linux can use this LUN via this adapter
	 * at the same time. If there is a potential race
	 * introduced by the FCP channel by not inhibiting Linux A
	 * to give up a LU with commands pending while Linux B
	 * grabs this LU and sends commands  - thus providing
	 * an exploit at the 'exclusive LUN' policy - then this
	 * issue has to be considered a hardware problem. It should
	 * be tracked as such if it really occurs. Even if the
	 * FCP Channel spec. begs exploiters to wait for the
	 * completion of all request sent to a LU prior to
	 * closing this LU connection.
	 * This spec. statement in conjunction with
	 * the 'exclusive LUN' policy is not consistent design.
	 * Another issue is how resource constraints for SCSI commands
	 * might be handled by the FCP channel (just guessing for now).
	 * If the FCP channel would always map resource constraints,
	 * e.g. no free FC exchange ID due to I/O stress caused by
	 * other sharing Linux instances, to faked queue-full
	 * conditions then this would be a misinterpretation and
	 * violation of SCSI standards.
	 * If there are SCSI stack races as indicated below
	 * then they need to be fixed just there.
	 * Providing all issue above are not applicable or will
	 * be fixed appropriately, removing the following hack
	 * is the right thing to do.
	 */

	/*
	 * Note: This is a rather nasty chop at the problem. We cannot 
	 * risk adding to the mlqueue however as this will block the 
	 * device. If it is the last outstanding command for this host
	 * it will remain blocked indefinitely. This would be quite possible
	 * on the zSeries FCP adapter.
	 * Also, there exists a race with scsi_insert_special relying on 
	 * scsi_request_fn to recalculate some command data which may not 
	 * happen when q->plugged is true in scsi_request_fn
	 */
	if (status_byte(scpnt->result) == QUEUE_FULL) {
		ZFCP_LOG_DEBUG("Changing QUEUE_FULL to BUSY....\n");
		scpnt->result &= ~(QUEUE_FULL << 1);
		scpnt->result |= (BUSY << 1);
	}
#endif

	ZFCP_LOG_DEBUG("scpnt->result =0x%x\n", scpnt->result);

	zfcp_cmd_dbf_event_scsi("response", scpnt);

	/* cleanup pointer (need this especially for abort) */
	scpnt->host_scribble = NULL;

	/*
	 * NOTE:
	 * according to the outcome of a discussion on linux-scsi we
	 * don't need to grab the io_request_lock here since we use
	 * the new eh
	 */
	/* always call back */
	(scpnt->scsi_done) (scpnt);
	atomic_dec(&adapter->scsi_reqs_active);
	wake_up(&adapter->scsi_reqs_active_wq);
#ifdef ZFCP_DEBUG_REQUESTS
	debug_text_event(fsf_req->adapter->req_dbf, 2, "ok_done:");
	debug_event(fsf_req->adapter->req_dbf, 2, &scpnt,
		    sizeof (unsigned long));
	debug_event(fsf_req->adapter->req_dbf, 2, &scpnt->scsi_done,
		    sizeof (unsigned long));
	debug_event(fsf_req->adapter->req_dbf, 2, &fsf_req,
		    sizeof (unsigned long));
#endif /* ZFCP_DEBUG_REQUESTS */
	(scpnt->scsi_done) (scpnt);
	atomic_dec(&adapter->scsi_reqs_active);
	wake_up(&adapter->scsi_reqs_active_wq);
	/*
	 * We must hold this lock until scsi_done has been called.
	 * Otherwise we may call scsi_done after abort regarding this
	 * command has completed.
	 * Note: scsi_done must not block!
	 */
 out:
	read_unlock_irqrestore(&fsf_req->adapter->abort_lock, flags);
	return retval;
}

/*
 * function:    zfcp_fsf_send_fcp_command_task_management_handler
 *
 * purpose:	evaluates FCP_RSP IU
 *
 * returns:	
 */
static int
zfcp_fsf_send_fcp_command_task_management_handler(struct zfcp_fsf_req *fsf_req)
{
	int retval = 0;
	struct fcp_rsp_iu *fcp_rsp_iu = (struct fcp_rsp_iu *)
	    &(fsf_req->qtcb->bottom.io.fcp_rsp);
	char *fcp_rsp_info = zfcp_get_fcp_rsp_info_ptr(fcp_rsp_iu);
	struct zfcp_unit *unit =
	    fsf_req->data.send_fcp_command_task_management.unit;

	del_timer(&fsf_req->adapter->scsi_er_timer);
	if (fsf_req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		fsf_req->status |= ZFCP_STATUS_FSFREQ_TMFUNCFAILED;
		goto skip_fsfstatus;
	}

	/* check FCP_RSP_INFO */
	switch (fcp_rsp_info[3]) {
	case RSP_CODE_GOOD:
		ZFCP_LOG_FLAGS(3, "RSP_CODE_GOOD\n");
		/* ok, continue */
		ZFCP_LOG_DEBUG("no failure or Task Management "
			       "Function complete\n");
		break;
	case RSP_CODE_TASKMAN_UNSUPP:
		ZFCP_LOG_FLAGS(0, "RSP_CODE_TASKMAN_UNSUPP\n");
		ZFCP_LOG_NORMAL("bug: A reuested task management function "
				"is not supported on the target device "
				"The corresponding device is the unit with "
				"FCP LUN 0x%Lx at the port "
				"with WWPN 0x%Lx at the adapter %s\n ",
				unit->fcp_lun,
				unit->port->wwpn,
				zfcp_get_busid_by_unit(unit));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_TMFUNCNOTSUPP;
		break;
	case RSP_CODE_TASKMAN_FAILED:
		ZFCP_LOG_FLAGS(0, "RSP_CODE_TASKMAN_FAILED\n");
		ZFCP_LOG_NORMAL("bug: A reuested task management function "
				"failed to complete successfully. "
				"The corresponding device is the unit with "
				"FCP LUN 0x%Lx at the port "
				"with WWPN 0x%Lx at the adapter %s.\n",
				unit->fcp_lun,
				unit->port->wwpn,
				zfcp_get_busid_by_unit(unit));
		fsf_req->status |= ZFCP_STATUS_FSFREQ_TMFUNCFAILED;
		break;
	default:
		ZFCP_LOG_NORMAL("bug: An invalid FCP response "
				"code was detected for a command. "
				"The problem occured on the unit "
				"with FCP LUN 0x%Lx connected to the "
				"port with WWPN 0x%Lx at the adapter %s "
				"(debug info 0x%x)\n",
				unit->fcp_lun,
				unit->port->wwpn,
				zfcp_get_busid_by_unit(unit),
				fcp_rsp_info[3]);
		fsf_req->status |= ZFCP_STATUS_FSFREQ_TMFUNCFAILED;
	}

      skip_fsfstatus:
	return retval;
}

/*
 * function:    zfcp_fsf_req_wait_and_cleanup
 *
 * purpose:
 *
 * FIXME(design): signal seems to be <0 !!!
 * returns:	0	- request completed (*status is valid), cleanup succ.
 *		<0	- request completed (*status is valid), cleanup failed
 *		>0	- signal which interrupted waiting (*status invalid),
 *			  request not completed, no cleanup
 *
 *		*status is a copy of status of completed fsf_req
 */
int
zfcp_fsf_req_wait_and_cleanup(struct zfcp_fsf_req *fsf_req,
			      int interruptible, u32 * status)
{
	int retval = 0;
	int signal = 0;

	if (interruptible) {
		__wait_event_interruptible(fsf_req->completion_wq,
					   fsf_req->status &
					   ZFCP_STATUS_FSFREQ_COMPLETED,
					   signal);
		if (signal) {
			ZFCP_LOG_DEBUG("Caught signal %i while waiting for the "
				       "completion of the request at 0x%lx\n",
				       signal, (unsigned long) fsf_req);
			retval = signal;
			goto out;
		}
	} else {
		__wait_event(fsf_req->completion_wq,
			     fsf_req->status & ZFCP_STATUS_FSFREQ_COMPLETED);
	}

	*status = fsf_req->status;

	/* cleanup request */
	zfcp_fsf_req_cleanup(fsf_req);
 out:
	return retval;
}

static inline int
zfcp_fsf_req_create_sbal_check(unsigned long *flags,
			       struct zfcp_qdio_queue *queue, int needed)
{
	write_lock_irqsave(&queue->queue_lock, *flags);
	if (atomic_read(&queue->free_count) >= needed)
		return 1;
	write_unlock_irqrestore(&queue->queue_lock, *flags);
	return 0;
}

/*
 * function:    zfcp_fsf_req_create
 *
 * purpose:	create an FSF request at the specified adapter and
 *		setup common fields
 *
 * returns:	-ENOMEM if there was insufficient memory for a request
 *              -EIO if no qdio buffers could be allocate to the request
 *              -EINVAL/-EPERM on bug conditions in req_dequeue
 *              0 in success
 *
 * note:        The created request is returned by reference.
 *
 * locks:	lock of concerned request queue must not be held,
 *		but is held on completion (write, irqsave)
 */
int
zfcp_fsf_req_create(struct zfcp_adapter *adapter,
		    u32 fsf_cmd,
		    unsigned long *lock_flags,
		    int req_flags,
		    struct zfcp_fsf_req **fsf_req_p)
{
	struct zfcp_fsf_req *fsf_req = NULL;
	int retval = 0;
	struct zfcp_qdio_queue *req_queue = &adapter->request_queue;
	volatile struct qdio_buffer_element *buffere;
	unsigned long timeout;
	int condition;

	/* allocate new FSF request */
	fsf_req = zfcp_fsf_req_alloc(adapter, fsf_cmd, GFP_ATOMIC);
	if (!fsf_req) {
		ZFCP_LOG_DEBUG("error: Could not put an FSF request into"
			       "the outbound (send) queue.\n");
		retval = -ENOMEM;
		goto failed_fsf_req;
	}
	/* save pointer to "parent" adapter */
	fsf_req->adapter = adapter;

	/* initialize waitqueue which may be used to wait on 
	   this request completion */
	init_waitqueue_head(&fsf_req->completion_wq);

	/* set magics */
	fsf_req->common_magic = ZFCP_MAGIC;
	fsf_req->specific_magic = ZFCP_MAGIC_FSFREQ;

	fsf_req->fsf_command = fsf_cmd;
	if (req_flags & ZFCP_REQ_AUTO_CLEANUP)
		fsf_req->status |= ZFCP_STATUS_FSFREQ_CLEANUP;

	/* initialize QTCB */
	if (fsf_cmd != FSF_QTCB_UNSOLICITED_STATUS) {
		ZFCP_LOG_TRACE("fsf_req->qtcb=0x%lx\n",
			       (unsigned long) fsf_req->qtcb);
		fsf_req->qtcb->prefix.req_id = (unsigned long) fsf_req;
		fsf_req->qtcb->prefix.ulp_info = ZFCP_ULP_INFO_VERSION;
		fsf_req->qtcb->prefix.qtcb_type = fsf_qtcb_type[fsf_cmd];
		fsf_req->qtcb->prefix.qtcb_version = ZFCP_QTCB_VERSION;
		fsf_req->qtcb->header.req_handle = (unsigned long) fsf_req;
		fsf_req->qtcb->header.fsf_command = fsf_cmd;
		/*
		 * Request Sequence Number is set later when the request is
		 * actually sent.
		 */
	}

	/*
	 * try to get needed SBALs in request queue (get queue lock on success)
	 */
	ZFCP_LOG_TRACE("try to get free BUFFER in request queue\n");
	if (req_flags & ZFCP_WAIT_FOR_SBAL) {
		timeout = ZFCP_SBAL_TIMEOUT;
		ZFCP_WAIT_EVENT_TIMEOUT(adapter->request_wq,
					timeout,
					(condition =
					 (zfcp_fsf_req_create_sbal_check)
					 (lock_flags, req_queue, 1)));
		if (!condition) {
			retval = -EIO;
			goto failed_sbals;
		}
	} else {
		if (!zfcp_fsf_req_create_sbal_check(lock_flags, req_queue, 1)) {
			retval = -EIO;
			goto failed_sbals;
		}
	}
	fsf_req->sbal_count = 1;
	fsf_req->sbal_index = req_queue->free_index;

	ZFCP_LOG_TRACE("got %i free BUFFERs starting at index %i\n",
		       fsf_req->sbal_count, fsf_req->sbal_index);
	buffere = req_queue->buffer[fsf_req->sbal_index]->element;
	/* setup common SBALE fields */
	buffere[0].addr = fsf_req;
	buffere[0].flags |= SBAL_FLAGS0_COMMAND;
	if (fsf_cmd != FSF_QTCB_UNSOLICITED_STATUS) {
		buffere[1].addr = (void *) fsf_req->qtcb;
		buffere[1].length = ZFCP_QTCB_SIZE;
	}

	/* set specific common SBALE and QTCB fields */
	switch (fsf_cmd) {
	case FSF_QTCB_FCP_CMND:
		ZFCP_LOG_FLAGS(3, "FSF_QTCB_FCP_CMND\n");
		/*
		 * storage-block type depends on actual
		 * SCSI command and is set by calling
		 * routine according to transfer direction
		 * of data buffers associated with SCSI
		 * command
		 */
		break;
	case FSF_QTCB_ABORT_FCP_CMND:
	case FSF_QTCB_OPEN_PORT_WITH_DID:
	case FSF_QTCB_OPEN_LUN:
	case FSF_QTCB_CLOSE_LUN:
	case FSF_QTCB_CLOSE_PORT:
	case FSF_QTCB_CLOSE_PHYSICAL_PORT:
	case FSF_QTCB_SEND_ELS:	/* FIXME: ELS needs separate case */
		ZFCP_LOG_FLAGS(3, "FSF_QTCB_*\n");
		/*
		 * FIXME(qdio):
		 * what is the correct type for commands
		 * without 'real' data buffers?
		 */
		buffere[0].flags |= SBAL_FLAGS0_TYPE_READ;
		buffere[1].flags |= SBAL_FLAGS_LAST_ENTRY;
		break;
	case FSF_QTCB_EXCHANGE_CONFIG_DATA:
		ZFCP_LOG_FLAGS(3, "FSF_QTCB_EXCHANGE_CONFIG_DATA\n");
		buffere[0].flags |= SBAL_FLAGS0_TYPE_READ;
		buffere[1].flags |= SBAL_FLAGS_LAST_ENTRY;
		break;

	case FSF_QTCB_SEND_GENERIC:
		ZFCP_LOG_FLAGS(3, "FSF_QTCB_SEND_GENERIC\n");
		buffere[0].flags |= SBAL_FLAGS0_TYPE_WRITE_READ;
		break;

	case FSF_QTCB_UNSOLICITED_STATUS:
		ZFCP_LOG_FLAGS(3, "FSF_QTCB_UNSOLICITED_STATUS\n");
		buffere[0].flags |= SBAL_FLAGS0_TYPE_STATUS;
		buffere[2].flags |= SBAL_FLAGS_LAST_ENTRY;
		break;

	default:
		ZFCP_LOG_NORMAL("bug: An attempt to send an unsupported "
				"command has been detected. "
				"(debug info 0x%x)\n", fsf_cmd);
		goto unsupported_fsf_cmd;
	}

	/* yes, we did it - skip all cleanups for different failures */
	goto out;

 unsupported_fsf_cmd:

 failed_sbals:
#ifdef ZFCP_STAT_QUEUES
	atomic_inc(&zfcp_data.outbound_queue_full);
#endif
/* dequeue new FSF request previously enqueued */
	zfcp_fsf_req_free(fsf_req);
	fsf_req = NULL;

 failed_fsf_req:
	write_lock_irqsave(&req_queue->queue_lock, *lock_flags);
 out:
	*fsf_req_p = fsf_req;
	return retval;
}

/*
 * function:    zfcp_fsf_req_send
 *
 * purpose:	start transfer of FSF request via QDIO
 *
 * returns:	0 - request transfer succesfully started
 *		!0 - start of request transfer failed
 */
static int
zfcp_fsf_req_send(struct zfcp_fsf_req *fsf_req, struct timer_list *timer)
{
	int retval = 0;
	struct zfcp_adapter *adapter = fsf_req->adapter;
	struct zfcp_qdio_queue *req_queue = &adapter->request_queue;
	volatile struct qdio_buffer_element *buffere;
	int inc_seq_no = 1;
	int new_distance_from_int;
	unsigned long flags;

	u8 sbal_index = fsf_req->sbal_index;

	/* FIXME(debug): remove it later */
	buffere = &(req_queue->buffer[sbal_index]->element[0]);
	ZFCP_LOG_DEBUG("zeroeth BUFFERE flags=0x%x \n ", buffere->flags);
	buffere = &(req_queue->buffer[sbal_index]->element[1]);
	ZFCP_LOG_TRACE("HEX DUMP OF 0eth BUFFERE PAYLOAD:\n");
	ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_TRACE, (char *) buffere->addr,
		      buffere->length);

	/* set sequence counter in QTCB */
	if (fsf_req->qtcb) {
		fsf_req->qtcb->prefix.req_seq_no = adapter->fsf_req_seq_no;
		fsf_req->seq_no = adapter->fsf_req_seq_no;
		ZFCP_LOG_TRACE("FSF request 0x%lx of adapter 0x%lx gets "
			       "FSF sequence counter value of %i\n",
			       (unsigned long) fsf_req,
			       (unsigned long) adapter,
			       fsf_req->qtcb->prefix.req_seq_no);
	} else
		inc_seq_no = 0;

	/* put allocated FSF request at list tail */
	write_lock_irqsave(&adapter->fsf_req_list_lock, flags);
	list_add_tail(&fsf_req->list, &adapter->fsf_req_list_head);
	write_unlock_irqrestore(&adapter->fsf_req_list_lock, flags);

	/* figure out expiration time of timeout and start timeout */
	if (timer) {
		timer->expires += jiffies;
		add_timer(timer);
	}

	ZFCP_LOG_TRACE("request queue of adapter with busid=%s: "
		       "next free SBAL is %i, %i free SBALs\n",
		       zfcp_get_busid_by_adapter(adapter),
		       req_queue->free_index,
		       atomic_read(&req_queue->free_count));

	ZFCP_LOG_DEBUG("Calling do QDIO busid=%s, flags=0x%x, queue_no=%i, "
		       "index_in_queue=%i, count=%i, buffers=0x%lx\n",
		       zfcp_get_busid_by_adapter(adapter),
		       QDIO_FLAG_SYNC_OUTPUT,
		       0,
		       sbal_index,
		       fsf_req->sbal_count,
		       (unsigned long) &req_queue->buffer[sbal_index]);

	/*
	 * adjust the number of free SBALs in request queue as well as
	 * position of first one
	 */
	atomic_sub(fsf_req->sbal_count, &req_queue->free_count);
	ZFCP_LOG_TRACE("free_count=%d\n", atomic_read(&req_queue->free_count));
	req_queue->free_index += fsf_req->sbal_count;	  /* increase */
	req_queue->free_index %= QDIO_MAX_BUFFERS_PER_Q;  /* wrap if needed */
	new_distance_from_int = zfcp_qdio_determine_pci(req_queue, fsf_req);

	retval = do_QDIO(adapter->ccw_device,
			 QDIO_FLAG_SYNC_OUTPUT,
			 0, fsf_req->sbal_index, fsf_req->sbal_count, NULL);

	if (retval) {
		/* Queues are down..... */
		retval = -EIO;
		/*
		 * FIXME(potential race):
		 * timer might be expired (absolutely unlikely)
		 */
		if (timer)
			del_timer_sync(timer);
		write_lock_irqsave(&adapter->fsf_req_list_lock, flags);
		list_del(&fsf_req->list);
		write_unlock_irqrestore(&adapter->fsf_req_list_lock, flags);
		/*
		 * adjust the number of free SBALs in request queue as well as
		 * position of first one
		 */
		zfcp_qdio_zero_sbals(req_queue->buffer,
				     fsf_req->sbal_index, fsf_req->sbal_count);
		atomic_add(fsf_req->sbal_count, &req_queue->free_count);
		req_queue->free_index -= fsf_req->sbal_count;	 /* increase */
		req_queue->free_index += QDIO_MAX_BUFFERS_PER_Q;
		req_queue->free_index %= QDIO_MAX_BUFFERS_PER_Q; /* wrap */
		ZFCP_LOG_DEBUG
			("error: do_QDIO failed. Buffers could not be enqueued "
			 "to request queue.\n");
	} else {
		req_queue->distance_from_int = new_distance_from_int;
#ifdef ZFCP_DEBUG_REQUESTS
		debug_text_event(adapter->req_dbf, 1, "o:a/seq");
		debug_event(adapter->req_dbf, 1, &fsf_req,
			    sizeof (unsigned long));
		if (inc_seq_no) {
			debug_event(adapter->req_dbf, 1,
				    &adapter->fsf_req_seq_no, sizeof (u32));
		} else {
			debug_text_event(adapter->req_dbf, 1, "nocb");
		}
#endif				/* ZFCP_DEBUG_REQUESTS */
		/*
		 * increase FSF sequence counter -
		 * this must only be done for request successfully enqueued to
		 * QDIO this rejected requests may be cleaned up by calling
		 * routines  resulting in missing sequence counter values
		 * otherwise,
		 */
		/* Don't increase for unsolicited status */
		if (inc_seq_no) {
			adapter->fsf_req_seq_no++;
			ZFCP_LOG_TRACE
			    ("FSF sequence counter value of adapter 0x%lx "
			     "increased to %i\n", (unsigned long) adapter,
			     adapter->fsf_req_seq_no);
		}
		/* count FSF requests pending */
		atomic_inc(&adapter->fsf_reqs_active);
#ifdef ZFCP_STAT_QUEUES
		atomic_inc(&zfcp_data.outbound_total);
#endif
	}
	return retval;
}

/*
 * function:    zfcp_fsf_req_cleanup
 *
 * purpose:	cleans up an FSF request and removes it from the specified list
 *
 * returns:
 *
 * assumption:	no pending SB in SBALEs other than QTCB
 */
void
zfcp_fsf_req_cleanup(struct zfcp_fsf_req *fsf_req)
{
	struct zfcp_adapter *adapter = fsf_req->adapter;
	unsigned long flags;

	write_lock_irqsave(&adapter->fsf_req_list_lock, flags);
	list_del(&fsf_req->list);
	write_unlock_irqrestore(&adapter->fsf_req_list_lock, flags);
	zfcp_fsf_req_free(fsf_req);
}

/*
 * try to allocate fsf_req with QTCB,
 * alternately try to get hold of fsf_req+QTCB provided by the specified memory
 * pool element, this routine is called for all kinds of fsf requests other than
 * status read since status read does neither require kmalloc involvement
 * nor a QTCB
 */
static struct zfcp_fsf_req *
zfcp_fsf_req_get(int kmalloc_flags, mempool_t * pool)
{
	struct zfcp_fsf_req *fsf_req;

	fsf_req = kmalloc(ZFCP_QTCB_AND_REQ_SIZE, kmalloc_flags);
	if (fsf_req) {
		memset(fsf_req, 0, ZFCP_QTCB_AND_REQ_SIZE);
	} else {
		fsf_req = mempool_alloc(pool, kmalloc_flags);
		if (fsf_req) {
			memset(fsf_req, 0, ZFCP_QTCB_AND_REQ_SIZE);
			fsf_req->status |= ZFCP_STATUS_FSFREQ_POOL;
		}
	}
	if (fsf_req)
		fsf_req->qtcb =
		    (struct fsf_qtcb *) ((unsigned long) fsf_req +
					 sizeof (struct zfcp_fsf_req));

	return fsf_req;
}

#undef ZFCP_LOG_AREA
#undef ZFCP_LOG_AREA_PREFIX
