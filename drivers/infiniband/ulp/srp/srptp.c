/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: srptp.c 35 2004-04-09 05:34:32Z roland $
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>

#include "ib_legacy_types.h"
#include "ts_kernel_trace.h"
#include "ts_ib_core.h"
#include "srp_cmd.h"
#include "srptp.h"
#include "srp_host.h"
#include "ts_ib_cm_types.h"
#include "ts_ib_cm.h"

/*
 * Size of a SRP CMD packet.
 */
int	srp_cmd_pkt_size;

/* number of ios per connections, based on number of luns passed in */
int max_ios_per_conn;

void
cq_send_handler( tTS_IB_CQ_HANDLE cq,
                 tTS_IB_CQ_ENTRY cq_entry,
                 void *arg )
{
    srp_target_t *target = ( srp_target_t *)arg;

    switch ( cq_entry->status ) {
        case TS_IB_COMPLETION_STATUS_SUCCESS:

            if ( cq_entry->op != TS_IB_OP_SEND ) {
                TS_REPORT_FATAL( MOD_SRPTP, "Wrong Opcode" );
                return;
            }

            srp_send_done( cq_entry->work_request_id, target );
            break;

        case TS_IB_COMPLETION_STATUS_WORK_REQUEST_FLUSHED_ERROR:
            TS_REPORT_STAGE( MOD_SRPTP,
                             "Send WR_FLUSH_ERR wr_id %d",
                             cq_entry->work_request_id);

            srp_host_free_pkt_index( cq_entry->work_request_id, target );
            break;

        default:
            TS_REPORT_WARN( MOD_SRPTP,
                            "Work request failure: %d op=%d wrid=%d",
                            cq_entry->status, cq_entry->op, cq_entry->work_request_id);

		    srp_host_completion_error( cq_entry->work_request_id, target );
            break;

    } // end switch on status
}


void
cq_recv_handler( tTS_IB_CQ_HANDLE cq,
                 tTS_IB_CQ_ENTRY cq_entry,
                 void *arg )
{
    srp_target_t *target = ( srp_target_t *)arg;
    tTS_IB_CQ_ENTRY_STRUCT send_entry;
    int status;

    switch ( cq_entry->status ) {
        case TS_IB_COMPLETION_STATUS_SUCCESS:

            if ( cq_entry->op != TS_IB_OP_RECEIVE ) {
                TS_REPORT_FATAL( MOD_SRPTP, "Wrong Opcode" );
                return;
            }

            if ( target->state == TARGET_ACTIVE_CONNECTION ) {
                status = tsIbCqPoll( target->active_conn->cqs_hndl, &send_entry );
                if ( status == 0 ) {
                    cq_send_handler( NULL, &send_entry, target );
                }
            }

            srp_recv( cq_entry->work_request_id, target );
            break;

        case TS_IB_COMPLETION_STATUS_WORK_REQUEST_FLUSHED_ERROR:
            TS_REPORT_STAGE( MOD_SRPTP,
                             "Recv WR_FLUSH_ERR wr_id %d",
                             cq_entry->work_request_id);

            srp_host_free_pkt_index( cq_entry->work_request_id, target );
            break;

        default:
            TS_REPORT_WARN( MOD_SRPTP,
                            "Work request failure: %d op=%d wrid=%d",
                            cq_entry->status, cq_entry->op, cq_entry->work_request_id);

		    srp_host_completion_error( cq_entry->work_request_id, target );
            break;

    } // end switch on status
}


/*
 * Post rcv_num
 */
int srptp_post_recv( srp_pkt_t *srp_pkt )
{
    int status;
    tTS_IB_RECEIVE_PARAM_STRUCT rcv_param;

    memset( &rcv_param, 0x00, sizeof(tTS_IB_RECEIVE_PARAM ));
    rcv_param.work_request_id = srp_pkt->pkt_index;
    rcv_param.scatter_list = &srp_pkt->scatter_gather_list;
    rcv_param.num_scatter_entries = 1;
    rcv_param.device_specific = NULL;
    rcv_param.signaled = TRUE;

    status = tsIbReceive ( srp_pkt->conn->qp_hndl, &rcv_param, 1 );

    if ( status ) {
        TS_REPORT_FATAL (MOD_SRPTP, "Post Recv Failed failed");
    }

    return( status );
}


/*
 * Send an SRP packet.
 * IN: SRP work request ID for this operation, SRP packet, number of additional
 * receives to post on the connection
 */
int
srptp_post_send ( srp_pkt_t *srp_pkt )
{
    tTS_IB_SEND_PARAM_STRUCT send_param;
    int status;

    memset( &send_param, 0x00, sizeof( tTS_IB_SEND_PARAM_STRUCT ) );
    send_param.work_request_id = srp_pkt->pkt_index;
    send_param.op = TS_IB_OP_SEND;
    send_param.gather_list = &srp_pkt->scatter_gather_list;
    send_param.num_gather_entries = 1;
    send_param.signaled = TRUE;

    status = tsIbSend( srp_pkt->conn->qp_hndl, &send_param, 1 );

    if ( status ) {
        TS_REPORT_FATAL (MOD_SRPTP, "tsIbSend failed: %d", status);
    }

    return ( status );
}


/*
 * Initialize the SRPTP module
 */
int
srptp_init_module (void)
{
    int status = 0;
    int hca_index;
    srp_host_hca_params_t *hca;
    tTS_IB_FMR_POOL_PARAM_STRUCT fmr_params;
	extern Scsi_Host_Template driver_template;
	int	sg_elements = driver_template.sg_tablesize;
    int port_index;
    tTS_IB_DEVICE_PROPERTIES_STRUCT device_properties;

    tsKernelTraceLevelSet (MOD_SRPTP, srp_tracelevel);

    /*
     * cmds per lun should always be twice as much as
     * the value given to the scsi stack, in anticipation of
     * aborts for every io, add 3 to account for pre-posted
     * received descriptors( i.e. T_LOGOUTs, AEN ).
     */
    max_ios_per_conn = ( MAX_SEND_WQES * 2 ) + MAX_PREPOST_RECVS;

    TS_REPORT_STAGE(MOD_SRPTP, "max targets reported to scsi %d", max_srp_targets );
    TS_REPORT_STAGE(MOD_SRPTP, "max luns reported to scsi %d", max_luns );
    TS_REPORT_STAGE(MOD_SRPTP, "max cmds(including aborts) per lun %d", max_cmds_per_lun*2 );
    TS_REPORT_STAGE(MOD_SRPTP, "max outstanding ios per target %d", max_ios_per_conn );

    /*
     * For every HCA, the following are created
     */
    for( hca_index = 0; hca_index < MAX_HCAS; hca_index++ ) {

        hca = &hca_params[hca_index];

        hca->ca_hndl = tsIbDeviceGetByIndex( hca_index );

        if ( hca->ca_hndl == TS_IB_HANDLE_INVALID ) {
            break;
        }

        TS_REPORT_STAGE( MOD_SRPTP, "Found HCA %d %p", hca_index, hca );

        tsIbDevicePropertiesGet( hca->ca_hndl, &device_properties );

        if ( status != 0 ) {
            TS_REPORT_FATAL( MOD_SRPTP, "Property query failed %d", status );
            return( -1 );
        }

        hca->valid = TRUE;
        hca->hca_index = hca_index;
        for ( port_index = 0; port_index < MAX_LOCAL_PORTS_PER_HCA; port_index++ ) {
            /*
             * Apply IB ports mask here
             */
            hca->port[port_index].valid = TRUE;
            hca->port[port_index].hca = hca;
            hca->port[port_index].local_port = port_index + 1;
            hca->port[port_index].index = (hca_index * MAX_LOCAL_PORTS_PER_HCA) + port_index;
        }

        memset (&hca->I_PORT_ID[0], 0, 16);
        memcpy(&hca->I_PORT_ID[8], device_properties.node_guid, 8 );

        TS_REPORT_STAGE( MOD_SRPTP, "SRP Initiator GUID: %llx for hca %d",
                         be64_to_cpu(*(tUINT64 *)&hca->I_PORT_ID[8]), hca->hca_index+1 );

        status = tsIbPdCreate( hca->ca_hndl, NULL, &hca->pd_hndl );

        if (status != 0) {
            TS_REPORT_FATAL (MOD_SRPTP, "Cannot allocate protection domain: %d", status);
            return(-1);
        }

	    memset(&fmr_params,0,sizeof(tTS_IB_FMR_POOL_PARAM_STRUCT));
        fmr_params.flush_function = srp_fmr_flush_function;
        fmr_params.flush_arg = (void *)hca;
        fmr_params.max_pages_per_fmr = ((max_xfer_sectors_per_io*512/PAGE_SIZE) + 2);
        fmr_params.access = TS_IB_ACCESS_LOCAL_WRITE|
                            TS_IB_ACCESS_REMOTE_WRITE|
                            TS_IB_ACCESS_REMOTE_READ;

	    /*
	     * if the upper layer gives us virtually contiguous
	     * elements, we need one registration per IO.
	     * If they are not, the worst case is, one registration
	     * per SG element
	     */
	    if (use_srp_indirect_addressing == 0)  {
		    /*
		     * Use Direct Addressing in cmd packets.
		     */
		    srp_cmd_pkt_size = SRP_CMD_PKT_DIRECT_ADDRESS_SIZE;

        } else {
		    /*
		     * Use InDirect Addressing in cmd packets.
		     * We cache all addresses within the request packet.
		     */
		    srp_cmd_pkt_size = sizeof(srp_cmd_t) +
				4 + /* for the total length in an indirect buffer descriptor */
				/* The extra memory descriptor is for the indirect table */
				(sg_elements+1) *sizeof(srp_remote_buf_t);
	    }

        if ( srp_cmd_pkt_size < SRP_CMD_PKT_DIRECT_ADDRESS_SIZE )
		    srp_cmd_pkt_size = SRP_CMD_PKT_DIRECT_ADDRESS_SIZE;

        fmr_params.pool_size = 64 * max_cmds_per_lun * sg_elements;
        fmr_params.dirty_watermark = fmr_params.pool_size / 8;

        fmr_params.cache = FALSE;

	    TS_REPORT_STAGE(MOD_SRPTP,"Pool Create max pages 0x%x pool size 0x%x",
		    fmr_params.max_pages_per_fmr, fmr_params.pool_size);

        // fmr_params.pool_size = 256;
        // fmr_params.dirty_watermark = 256;

        status = tsIbFmrPoolCreate( hca->pd_hndl,
				                    &fmr_params,
                                    &hca->fmr_pool );

        if (status !=0) {
            TS_REPORT_WARN (MOD_SRPTP,
			    "Fast memory pool creation failed (%d) for max pages 0x%x pool size 0x%x\n"
			    "Retrying  with pool size 0x400",
				    status, fmr_params.max_pages_per_fmr, fmr_params.pool_size );

		    fmr_params.pool_size = 1024;
            fmr_params.dirty_watermark = 256;

    	    status = tsIbFmrPoolCreate( hca->pd_hndl,
				                    	&fmr_params,
                                    	&hca->fmr_pool );

		    if (status != 0) {

        	    TS_REPORT_FATAL (MOD_SRPTP,
				    "Fast memory pool creation failed (%d) for max pages 0x%x pool size 0x%x\n",
				    status, fmr_params.max_pages_per_fmr, fmr_params.pool_size );
        	    goto clean1;
		    }
        }

	} // end loop on HCAs

    return(0);

  clean1:

    return(-1);

}


/*
 * Clean up when the SRPTP module is removed.
 */
void
srptp_cleanup_module (void)
{
    srp_host_hca_params_t *hca;
    int status;
    int i;

    for( i = 0; i < MAX_HCAS; i++ ) {

        hca = &hca_params[i];

        if ( hca_params[i].valid == FALSE )
            break;

        status = tsIbFmrPoolDestroy( hca->fmr_pool );

        if (status != 0)
            TS_REPORT_STAGE (MOD_SRPTP, "Cannot destroy Fast Memory pool %d", status);

        status = tsIbPdDestroy( hca_params[i].pd_hndl );
        if (status != 0)
            TS_REPORT_STAGE (MOD_SRPTP, "Cannot free protection domain: %d", status);

    }

}


void srptp_idle_handler( srp_host_conn_t *conn,
                         tTS_IB_CM_IDLE_PARAM idle_data )
{
	int srptp_reason;
    tTS_IB_QP_HANDLE qp_hndl;
    int status;

    TS_REPORT_STAGE( MOD_SRPTP, "idle reason %d, reject reason %d",
                     idle_data->reason, idle_data->rej_reason );

    qp_hndl = conn->qp_hndl;

	/*
     * Check if this due to CONN req failing or if this
     * is the result of a disconnect while in the online
     * state
     */
    if ( idle_data->reason == TS_IB_CM_IDLE_TIME_WAIT_DONE ) {
        tTS_IB_CQ_ENTRY_STRUCT cq_entry;
        /*
         * We came from time wait, so must have previously
         * been established, we need to close this
         * SRP connection
         */
        while(1) {
            status = tsIbCqPoll( conn->cqr_hndl, &cq_entry );
            if (( status !=0 ) && ( status != -EAGAIN )) {
                TS_REPORT_WARN ( MOD_SRPTP, "cq poll failed" );
                break;
            } else if ( status == -EAGAIN ) {
                break;
            } else {
                cq_recv_handler( conn->cqr_hndl,
                                 &cq_entry,
                                 conn->target );
            }
        }

        while(1) {
            status = tsIbCqPoll( conn->cqs_hndl, &cq_entry );
            if (( status !=0 ) && ( status != -EAGAIN )) {
                TS_REPORT_WARN ( MOD_SRPTP, "cq poll failed" );
                break;
            } else if ( status == -EAGAIN ) {
                break;
            } else {
                cq_send_handler( conn->cqs_hndl,
                                 &cq_entry,
                                 conn->target );
            }
        }

        status = tsIbQpDestroy( qp_hndl );
        if ( status )
            TS_REPORT_WARN( MOD_SRPTP, "QP destroy failed" );

        srp_host_close_conn( conn );

    } else if ( idle_data->reason == TS_IB_CM_IDLE_REMOTE_REJECT ) {

        if ( idle_data->rej_reason == TS_IB_REJ_PORT_REDIRECT ) {
	        /*
	         * copy the gid of the port to which the connection
	         * should be redirected to
	         */
	        memcpy( conn->login_resp_data, idle_data->rej_info, 16 );
            conn->login_resp_len = 16;
	        srptp_reason = SRPTP_RETRY_REDIRECT_CONNECTION;

        } else if (idle_data->rej_reason == TS_IB_REJ_CONSUMER_REJECT ) {
            /*
             * Explicit reject from the target, could be the
             * centralized CM or the gateway
             */
            srptp_reason = SRPTP_HARD_REJECT;

        } else {

            srptp_reason = SRPTP_FAILURE;
        }

        status = tsIbQpDestroy( qp_hndl );
        if ( status )
            TS_REPORT_WARN( MOD_SRPTP, "QP destroy failed" );

        srp_host_connect_done( conn, srptp_reason );

    } else {
        /*
         * This is either a:
         * 1) TS_IB_CM_LOCAL_REJECT
         * 2) TS_IB_CM_REMOTE_TIMEOUT
         *
         * Also means, we were still in the login
         * process.
         */
        srptp_reason = SRPTP_FAILURE;

        status = tsIbQpDestroy( qp_hndl );
        if ( status )
            TS_REPORT_WARN( MOD_SRPTP, "QP destroy failed" );

        srp_host_connect_done( conn, srptp_reason );
    }

}


/*
 * Connection completion handler for connections requested by srptp_connect().
 * This is for the host side of SRP.
 */
tTS_IB_CM_CALLBACK_RETURN
conn_handler( tTS_IB_CM_EVENT event,
              tTS_IB_CM_COMM_ID comm_id,
              void *params,
              void *arg )
{
    srp_target_t *target = (srp_target_t *)arg;
    srp_host_conn_t *conn;

    down( &target->sema );

    conn = srp_host_find_conn( target, comm_id );
    if ( conn == NULL ) {
        TS_REPORT_WARN( MOD_SRPTP, "Unknown comm_id 0x%x for target %d",
                        comm_id, target->target_index );
        up( &target->sema );
        return( TS_SUCCESS );
    }

    TS_REPORT_STAGE( MOD_SRPTP, "SRP conn event %d for comm id 0x%x",
		             event, conn->comm_id );

    switch( event ) {

        case TS_IB_CM_REP_RECEIVED: {
            tTS_IB_CM_REP_RECEIVED_PARAM rep_param = (tTS_IB_CM_REP_RECEIVED_PARAM)params;

            memcpy( conn->login_resp_data,
                    rep_param->remote_private_data,
                    rep_param->remote_private_data_len );

            conn->login_resp_len = rep_param->remote_private_data_len;
            }
            break;


        case TS_IB_CM_ESTABLISHED:
            srp_host_connect_done( conn, SRPTP_SUCCESS );
            break;


        case TS_IB_CM_DISCONNECTED:
            srp_host_disconnect_done( conn, SRPTP_SUCCESS );
            break;

        case TS_IB_CM_IDLE:
            srptp_idle_handler( conn, (tTS_IB_CM_IDLE_PARAM)params );
            break;

        default:
            TS_REPORT_FATAL( MOD_SRPTP, "Unknown CM state transition: %d", event );
            break;
    }

    up( &target->sema );

	return( TS_SUCCESS );
}


/*
 * SRPTP functions called by the SRP driver (downcalls)
 */

/*
 * Called by a host SRP driver to get the CA GUID.
 * OUT: 8 byte array containing the guid
 */
int
srptp_refresh_hca_info ( void )
{

    int status, hca_index, port_index;
    tTS_IB_PORT_PROPERTIES_STRUCT port_properties;
    srp_host_port_params_t *port;
    srp_host_hca_params_t *hca;
    int port_active_count=0;

	for ( hca_index = 0; hca_index < MAX_HCAS; hca_index++ ) {

        hca = &hca_params[hca_index];

        if ( hca->valid == FALSE )
            break;

        for( port_index = 0; port_index < MAX_LOCAL_PORTS_PER_HCA; port_index++ ) {

            port = &hca->port[port_index];

		    set_current_state(TASK_INTERRUPTIBLE);
		    schedule_timeout(HZ/10);
		    status = tsIbPortPropertiesGet( hca->ca_hndl,
                                            port_index+1,
                                            &port_properties );

    	    if (status != 0) {
       		    TS_REPORT_WARN (MOD_SRPTP, "tsIbHcaPortQuery failed");
       		    TS_EXIT_FAIL (MOD_SRPTP, -1);
    	    }

		    port->slid = port_properties.lid;
		    port->port_state = port_properties.port_state;

            if ( port->port_state == TS_IB_PORT_STATE_ACTIVE )
                port_active_count++;

		    status = tsIbGidEntryGet( hca->ca_hndl,
                       		          port->local_port, // local port index
                       		          0,                // gid index in the gid table
                       		          port->local_gid );

            if ( status )  {
       	 	    TS_REPORT_WARN (MOD_SRPTP, "tsIbHcaPortQuery hca %d local port %d failed",
				                hca->hca_index+1, port->local_port);
       	 	    TS_EXIT_FAIL (MOD_SRPTP, -1);
		    }
        }
	}

	return (port_active_count);
}



/*
 * Called by a host SRP driver to open a connection with an SRP target.
 * IN: target DLID, SRP connection handle for the new connection,
 * SRP login request, length of SRP login request
 */
int
srptp_connect ( srp_host_conn_t *conn,
                tTS_IB_PATH_RECORD path_record,
                char *srp_login_req,
                int srp_login_req_len )
{
    tTS_IB_QP_CREATE_PARAM_STRUCT qp_param;
    tTS_IB_QP_ATTRIBUTE qp_attr;
    tTS_IB_CM_ACTIVE_PARAM_STRUCT active_param;
    srp_host_hca_params_t *hca;
    int status;

    hca = conn->port->hca;

    //
    // First create the QP we are going to use for this
    // connection
    //
    qp_param.limit.max_outstanding_send_request = MAX_SEND_WQES;
    qp_param.limit.max_outstanding_receive_request = MAX_RECV_WQES;
    qp_param.limit.max_send_gather_element = 1;
    qp_param.limit.max_receive_scatter_element = 1;

    qp_param.pd = hca->pd_hndl;
	qp_param.send_queue = conn->cqs_hndl;
	qp_param.receive_queue = conn->cqr_hndl;
    qp_param.send_policy = TS_IB_WQ_SIGNAL_ALL;
    qp_param.receive_policy = TS_IB_WQ_SIGNAL_ALL;
    qp_param.rd_domain = 0;
    qp_param.transport = TS_IB_TRANSPORT_RC;
    qp_param.device_specific = NULL;

    status = tsIbQpCreate( &qp_param, &conn->qp_hndl, &conn->qpn );
    if ( status ) {
        TS_REPORT_FATAL( MOD_SRPTP, "QP Create failed %d", status );
        return( status );
    }

    //
    // Modify QP to init state
    //
    qp_attr = kmalloc( sizeof( tTS_IB_QP_ATTRIBUTE_STRUCT ), GFP_ATOMIC );
    if ( qp_attr == NULL ) {
        return( ENOMEM );
    }

    memset( qp_attr, 0x00, sizeof( tTS_IB_QP_ATTRIBUTE_STRUCT ));

    qp_attr->valid_fields |= TS_IB_QP_ATTRIBUTE_STATE;
    qp_attr->state         = TS_IB_QP_STATE_INIT;

    qp_attr->valid_fields |= TS_IB_QP_ATTRIBUTE_RDMA_ATOMIC_ENABLE;
    qp_attr->enable_rdma_read  = 1;
    qp_attr->enable_rdma_write = 1;

    qp_attr->valid_fields |= TS_IB_QP_ATTRIBUTE_PORT;
    qp_attr->port          = conn->port->local_port;

    qp_attr->valid_fields |= TS_IB_QP_ATTRIBUTE_PKEY_INDEX;
    qp_attr->pkey_index    = 0;

    status = tsIbQpModify( conn->qp_hndl, qp_attr );
    kfree( qp_attr );

    if ( status ) {
        tsIbQpDestroy( conn->qp_hndl );
        return( status );
    }

    //
    // Make the connection
    //
    active_param.qp = conn->qp_hndl;
    active_param.req_private_data = srp_login_req;
    active_param.req_private_data_len = srp_login_req_len;
    active_param.responder_resources = 4;
    active_param.initiator_depth = 4;
    active_param.retry_count = 7;
    active_param.rnr_retry_count = 3;
    active_param.cm_response_timeout = 19;
    active_param.max_cm_retries = 3;
    active_param.flow_control = TRUE;

    path_record->packet_life = 14;
    path_record->mtu = TS_IB_MTU_1024;

    status = tsIbCmConnect ( &active_param,         // Parameters
                             path_record,           // Primary Path
                             NULL,                  // alternate path
						     SRP_SERVICE_ID,        // Service ID
                             FALSE,                 // peer-to-peer
						     conn_handler,          // Callback function
                             (void *)conn->target,  // Argument
                             &conn->comm_id );      // comm_id

    if ( status ) {
        TS_REPORT_FATAL (MOD_SRPTP, "tsIbConnect failed: %d", status);

        conn->comm_id = (tTS_IB_CM_COMM_ID) -1;
        tsIbQpDestroy( conn->qp_hndl );
        srp_host_close_conn( conn );

        return( status );
    }

    return( TS_SUCCESS );
}



/*
 * Close a connection.
 * IN: SRPTP connection handle, flag to indicate if callback should be
 * suppressed (force_no_callback is non-zero) or not (force_no_callback is
 * zero)
 */
int
srptp_disconnect ( srp_host_conn_t *conn )
{
    int status;
    int comm_id;

    comm_id = conn->comm_id;

    status = tsIbCmDisconnect (comm_id);

    if ( status ) {
        if ( status == -EINVAL ) {
            TS_REPORT_STAGE( MOD_SRPTP, "Invalid comm_id 0x%x", conn->comm_id );
            return 0;
        } else if ( status == -EPROTO ) {
            TS_REPORT_STAGE( MOD_SRPTP, "Already trying to disconnect comm_id 0x%x",
                             comm_id );
            return 0;
        } else {
            TS_REPORT_WARN( MOD_SRPTP, "tsIbisconnect failed: %d", status );
        }
    }else {
        TS_REPORT_STAGE( MOD_SRPTP, "Successful disconn comm_id 0x%x", conn->comm_id );
    }
	return 0;
}


int
srptp_register_memory ( srp_host_conn_t *conn,
                        srp_host_buf_t *buf,
                        tUINT32 offset,
                        uint64_t *buffer_list,
                        tUINT32 list_len )
{
    int status;
    tTS_IB_LKEY l_key;
    uint64_t start_address = (uintptr_t) buf->data;

    if (buf == NULL) {

        TS_REPORT_WARN (MOD_SRPTP, "buf is NULL");
        return(-1);

    }

    TS_REPORT_DATA(MOD_SRPTP, "iova %llx, iova_offset %x length 0x%x",
		start_address, offset,buf->size );

    status = tsIbFmrRegisterPhysical( conn->target->port->hca->fmr_pool,
                                      buffer_list, list_len,
                                      &start_address, offset,
                                      &buf->mr_hndl,
                                      &l_key,
                                      &buf->r_key );

    if ( status ) {
        TS_REPORT_DATA (MOD_SRPTP, "Memory registration failed: %d", status);
        return( status );
    }

    TS_REPORT_DATA (MOD_SRPTP,
                     "l_key %x, r_key %x, mr_hndl %x",
                      l_key, buf->r_key, buf->mr_hndl );

    return(0);
}

int
srptp_dereg_phys_host_buf( srp_host_buf_t *buf )
{
    int status;

    TS_REPORT_DATA (MOD_SRPTP, "releasing mr_hndl %x", buf->mr_hndl );

    status = tsIbFmrDeregister( buf->mr_hndl );

    if (status != 0) {
        TS_REPORT_WARN (MOD_SRPTP, "de-registration failed: %d", status);
        return(-1);
    }

    return(0);
}
