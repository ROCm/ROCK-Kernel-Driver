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

  $Id: srp_host.c 93 2004-04-28 18:41:35Z roland $
*/

#include "srp_host.h"

MODULE_PARM ( dlid_conf, "i");
MODULE_PARM_DESC (dlid_conf, "dlid_conf (nonzero value indicates that dlid is being specified in the conf file)");
tUINT32 dlid_conf = 0;

MODULE_PARM (service_str, "s");
MODULE_PARM_DESC( service_str,"used with dlid_conf, in stand alone systems");
char *service_str = NULL;

MODULE_PARM ( ib_ports_mask,"i");
MODULE_PARM_DESC (ib_ports_mask,"bit mask to enable or disable SRP usage of local IB ports 1 - use port1 only, 2 - use port2 only, 3 - use both ports");
tUINT32	ib_ports_mask = 0xffffffff;

MODULE_PARM ( max_luns, "i");
MODULE_PARM_DESC (max_luns, "max luns - number of luns per target that will be supported");
int max_luns = 256;

MODULE_PARM ( max_srp_targets, "i");
MODULE_PARM_DESC (max_srp_targets, "max_srp_targets - number of targets that will be supported");
int max_srp_targets = 64;

MODULE_PARM ( max_cmds_per_lun, "i");
MODULE_PARM_DESC (max_cmds_per_lun, "max_cmds_per_lun - number of ios per lun supported");
int max_cmds_per_lun = 16;

MODULE_PARM ( use_srp_indirect_addressing, "i");
MODULE_PARM_DESC (use_srp_indirect_addressing,
	"use_srp_indirect_addressing - boolean indicating if the driver"
	"(default is 1)");
int use_srp_indirect_addressing = 1;

MODULE_PARM ( max_xfer_sectors_per_io, "i");
MODULE_PARM_DESC (max_xfer_sectors_per_io,
	"max_xfer_sectors_per_io - maximum number of sectors (512 bytes each)"
	"which can be transferred per IO (default is 128 (64KB)");
int max_xfer_sectors_per_io = 128;

MODULE_PARM( srp_discovery_timeout,"i");
MODULE_PARM_DESC(srp_discovery_timeout,"timeout (in seconds) for SRP discovery (of SRP targets) to complete");
tUINT32 srp_discovery_timeout = IB_DISCOVERY_TIMEOUT; /* 60 seconds */

MODULE_PARM( fmr_cache,"i");
MODULE_PARM_DESC( fmr_cache,"size of cached fmr entries");
int fmr_cache = 0;

MODULE_PARM (target_bindings, "s");
MODULE_PARM_DESC( target_bindings,"used to bind a wwpn to a target binding");
char *target_bindings = NULL;

MODULE_PARM ( srp_tracelevel, "i");
MODULE_PARM_DESC (srp_tracelevel, "Debug tracelevel");
int srp_tracelevel = 1;

#define INVALID_CONN_HANDLE (srp_host_conn_t *)0xdeadbeef

extern void print_target_bindings(void) ;
/*
 * Local prototypes
 *
 */
const char *srp_host_info (struct Scsi_Host *);

int srp_host_login (srp_host_conn_t * );

static int srp_host_login_resp (srp_host_conn_t *,
                                char *);

static int srp_host_login_rej (char *);

static int srp_process_resp ( srp_target_t *, srp_pkt_t *);

void srp_host_iodone (Scsi_Cmnd *,
                      srp_resp_t *,
                      ioq_t * );

int srp_build_command (srp_pkt_t *,
                       Scsi_Cmnd *,
                       srp_host_conn_t *,
                       ioq_t *ioq);

void srp_select_queue_depths(struct Scsi_Host *, Scsi_Device *);

int srp_host_detect (Scsi_Host_Template * template);

int srp_host_qcommand (Scsi_Cmnd * SCpnt,
                       void (*done) (Scsi_Cmnd *));

int srp_host_abort_eh (Scsi_Cmnd * SCpnt);

int srp_host_device_reset_eh( Scsi_Cmnd *SCpnt );

int srp_host_bus_reset_eh( Scsi_Cmnd *SCpnt );

int srp_host_reset_eh (Scsi_Cmnd * SCpnt );

int srp_build_rdma (srp_pkt_t *,
                    Scsi_Cmnd *,
                    srp_host_conn_t *,
                    ioq_t *ioq);

int srp_proc_info( struct Scsi_Host *host, char *buffer, char **start, off_t offset,
	          int length, int inout);


static void srp_host_module_cleanup (void);

void srp_host_resource_timer( void	*context );

int srp_host_send_command( srp_target_t *target, ioq_t *ioq, Scsi_Cmnd *SCpnt );

void srp_host_internal_qcommand( ioq_t *ioq );

void srp_host_scsi_init(void);

srp_target_t *srp_targets;
srp_host_driver_params_t driver_params;
srp_host_hca_params_t hca_params[MAX_HCAS];

struct scsi_host_template driver_template = {
    .module = THIS_MODULE,
    .queuecommand = srp_host_qcommand,
    .eh_abort_handler = srp_host_abort_eh,
    .eh_device_reset_handler = srp_host_device_reset_eh,
    .eh_host_reset_handler = srp_host_reset_eh,
    .proc_info = srp_proc_info,
    .proc_name = "srp",
    .can_queue = MAX_IO_QUEUE,
    .this_id = -1,
    .sg_tablesize = 16,
    .max_sectors = 128,
    .cmd_per_lun = 1,
    .use_clustering = ENABLE_CLUSTERING,
};


static int scsi_unload_in_progress = FALSE;
static unsigned long connection_timeout = 0;

DECLARE_WAIT_QUEUE_HEAD(dm_poll_thread_wait_queue);


__inline void srp_host_scsi_complete( Scsi_Cmnd *scsi_cmnd )
{
    unsigned long cpu_flags;

    /* complete back to scsi with DID_ERROR */
    spin_lock_irqsave( &driver_params.host->default_lock, cpu_flags );

    scsi_cmnd->SCp.buffer = NULL;
    (*scsi_cmnd->scsi_done)(scsi_cmnd);

    spin_unlock_irqrestore( &driver_params.host->default_lock, cpu_flags );
}


srp_host_conn_t *
srp_host_find_conn( srp_target_t *target, tTS_IB_CM_COMM_ID comm_id )
{
	struct list_head *conn_entry, *temp_entry;
    srp_host_conn_t *conn;

    list_for_each_safe( conn_entry, temp_entry, &target->conn_list ) {

	    conn = list_entry( conn_entry, srp_host_conn_t, conn_list );

        if ( conn->comm_id == comm_id )
            return( conn );

    }

    return( NULL );
}


void srp_fmr_flush_function( tTS_IB_FMR_POOL_HANDLE fmr_pool, void *flush_arg )
{
    srp_host_hca_params_t *hca = ( srp_host_hca_params_t * )flush_arg;
    ioq_t                 *ioq = NULL;
	struct list_head      *ioq_entry, *temp_entry;
    unsigned long         cpu_flags;
    struct list_head      temp_list;

    INIT_LIST_HEAD( &temp_list );

    spin_lock_irqsave(&hca->spin_lock, cpu_flags);

    list_for_each_safe( ioq_entry, temp_entry, &hca->resource_list ) {

        ioq = list_entry( ioq_entry, ioq_t, hca_resource_list );

		list_del(&ioq->hca_resource_list);
        hca->resource_count--;

        ioq->queue_type = QUEUE_TEMP;
        list_add_tail( &ioq->temp_list, &temp_list );
    }

    spin_unlock_irqrestore(&hca->spin_lock, cpu_flags);

    TS_REPORT_DATA( MOD_SRPTP, "FMR flush completed for hca %d",
                    hca->hca_index+1 );

    /*
     * De-registered FMR entries have now been flushed out of the hardware
     * Retry any I/Os that got queued as a result of no FMR entries being
     * available
     */
    list_for_each_safe( ioq_entry, temp_entry, &temp_list ) {

	    ioq = list_entry( ioq_entry, ioq_t, temp_list );

		list_del(&ioq->temp_list);

        TS_REPORT_DATA( MOD_SRPTP, " Retrying ioq %p, from hca resource queue", ioq );

        srp_host_internal_qcommand( ioq );
    }
}


/*
 * Path must be specified in the target
 */
void
initialize_connection( srp_target_t	*target )
{
    ioc_entry_t *ioc_entry;
    srp_host_port_params_t *port;
    srp_host_conn_t	*s;
    unsigned long cpu_flags;

    spin_lock_irqsave( &target->spin_lock, cpu_flags );

    port = target->port;
    ioc_entry = target->ioc;
    s = target->active_conn;

    if ( s == NULL ) {

		s = kmalloc (sizeof (srp_host_conn_t), GFP_KERNEL);
		if (s == NULL) {
			TS_REPORT_FATAL( MOD_SRPTP,
				             "Couldnt allocate memory for connection to 0x%llx\n",
				             be64_to_cpu(target->service_name));
			return;
		}

        TS_REPORT_WARN( MOD_SRPTP, "Initializing connection for target %d",
                        target->target_index);

        memset (s, 0x00, sizeof (srp_host_conn_t));

   		/* attach the target to the connection */
   		s->target = target;
        s->cqs_hndl = target->cqs_hndl[port->hca->hca_index];
        s->cqr_hndl = target->cqr_hndl[port->hca->hca_index];

        driver_params.num_connections++;
        port->num_connections++;
        target->ioc->num_connections++;
        target->hard_reject = FALSE;
        target->hard_reject_count = 0;

        target->active_conn = s;
        target->conn_count++;
		list_add_tail( &s->conn_list, &target->conn_list );
	}

    target->timeout = jiffies + connection_timeout;
    target->state = TARGET_POTENTIAL_CONNECTION;

    s->port = port;

    s->state = SRP_HOST_LOGIN_INPROGRESS;
    s->redirected = FALSE;
    s->path_record_tid = 0;

    srp_host_login ( s );

    spin_unlock_irqrestore( &target->spin_lock, cpu_flags );

	driver_params.num_pending_connections++;

    memcpy( &s->path_record,
            &ioc_entry->iou_path_record[port->hca->hca_index][port->local_port-1],
            sizeof( tTS_IB_PATH_RECORD_STRUCT) );

    srptp_connect ( s,
                    &s->path_record,
                    (__u8 *) s->login_buff,
                    s->login_buff_len );
}


void connection_reset( srp_target_t *target )
{
    unsigned long cpu_flags;

    spin_lock_irqsave( &target->spin_lock, cpu_flags );

    target->active_conn->state = SRP_HOST_DISCONNECT_NEEDED;
    target->active_conn = NULL;
    target->timeout = jiffies + connection_timeout;
    target->state = TARGET_POTENTIAL_CONNECTION;
    target->need_device_reset = TRUE;

    spin_unlock_irqrestore( &target->spin_lock, cpu_flags );
}


void
remove_connection( srp_host_conn_t *s, srp_target_state_t target_state )
{
    srp_target_t *target = s->target;
    unsigned long cpu_flags;
    int force_close = FALSE;

    spin_lock_irqsave( &target->spin_lock, cpu_flags );

    if ( s->state == SRP_HOST_LOGIN_INPROGRESS ) {
        driver_params.num_pending_connections--;
    } else if ( s->state == SRP_HOST_GET_PATH_RECORD ) {
        force_close = TRUE;
        driver_params.num_pending_connections--;
    } else if ( s->state == SRP_UP ) {
        driver_params.num_active_connections--;
    }

    target->port->num_connections--;
    target->ioc->num_connections--;

    target->state = target_state;
    if ( target->state == TARGET_POTENTIAL_CONNECTION )
        target->timeout = jiffies + connection_timeout;
    target->active_conn = NULL;
    target->need_disconnect = FALSE;
    target->hard_reject = FALSE;

    s->state = SRP_HOST_LOGOUT_INPROGRESS;

    spin_unlock_irqrestore( &target->spin_lock, cpu_flags );

    if (( s->path_record_tid ) &&
        ( s->path_record_tid != TS_IB_CLIENT_QUERY_TID_INVALID )) {
        tsIbClientQueryCancel( s->path_record_tid );
    }

    if ( force_close ) {
        srp_host_close_conn( s );
    } else {
        srptp_disconnect ( s );
    }
}


static srp_pkt_t* srp_host_pkt_index_to_ptr( int index, srp_target_t *target )
{
    if ( !( index > target->max_num_pkts ) ) {
        return( &target->srp_pkt_hdr_area[index] );
    } else {
        TS_REPORT_FATAL( MOD_SRPTP,"Out of range index %d, target %d",
                         index, target->target_index );
        return( NULL );
    }
}


/*
 * Free an SRP packet.
 * IN: SRP packet
 */
static void srp_host_free_pkt( srp_pkt_t *srp_pkt )
{
    srp_target_t *target;
    unsigned long cpu_flags;

    if (srp_pkt == NULL) {
        TS_REPORT_WARN (MOD_SRPTP, "srp_pkt is NULL");
    } else {
        target = (srp_target_t *)srp_pkt->target;

        spin_lock_irqsave( &target->spin_lock, cpu_flags );

        if ( srp_pkt->in_use == FALSE ) {

            TS_REPORT_STAGE( MOD_SRPTP, "srp_pkt already free %d",
                             srp_pkt->pkt_index );

        } else if ( srp_pkt->in_use == TRUE ) {

            srp_pkt->scatter_gather_list.address = (uint64_t)(unsigned long)srp_pkt->data;
            srp_pkt->scatter_gather_list.length = srp_cmd_pkt_size;
            srp_pkt->in_use = FALSE;
            srp_pkt->conn = INVALID_CONN_HANDLE;

            srp_pkt->next = target->srp_pkt_free_list;
            target->srp_pkt_free_list = srp_pkt;


            atomic_inc (&target->free_pkt_counter);
        }
        spin_unlock_irqrestore( &target->spin_lock, cpu_flags );
    }
}


static void srp_host_free_pkt_no_lock( srp_pkt_t *srp_pkt )
{
    srp_target_t *target;

    if (srp_pkt == NULL) {
        TS_REPORT_WARN (MOD_SRPTP, "srp_pkt is NULL");
    } else {
        target = (srp_target_t *)srp_pkt->target;

        if ( srp_pkt->in_use == FALSE ) {

            TS_REPORT_STAGE( MOD_SRPTP, "srp_pkt already free %d",
                             srp_pkt->pkt_index );

        } else if ( srp_pkt->in_use == TRUE ) {

            srp_pkt->scatter_gather_list.address = (uint64_t)(unsigned long)srp_pkt->data;
            srp_pkt->scatter_gather_list.length = srp_cmd_pkt_size;
            srp_pkt->in_use = FALSE;
            srp_pkt->conn = INVALID_CONN_HANDLE;

            srp_pkt->next = target->srp_pkt_free_list;
            target->srp_pkt_free_list = srp_pkt;


            atomic_inc (&target->free_pkt_counter);
        }
    }
}


void srp_host_free_pkt_index( int index, srp_target_t *target )
{
    srp_pkt_t *pkt;

    pkt = srp_host_pkt_index_to_ptr( index, target );

    srp_host_free_pkt( pkt );
}


/*
 * Get a free SRP packet structure from the free list
 */
static srp_pkt_t *
srp_host_get_pkt ( srp_target_t *target )
{
    unsigned long cpu_flags;
    srp_pkt_t *srp_pkt = NULL;

    spin_lock_irqsave( &target->spin_lock, cpu_flags );
    if ( target->srp_pkt_free_list == NULL ) {
        TS_REPORT_DATA( MOD_SRPTP, "Packet pool exhausted on target %d",
                        target->target_index );
    } else {

        srp_pkt = target->srp_pkt_free_list;
        target->srp_pkt_free_list = target->srp_pkt_free_list->next;

        atomic_dec (&target->free_pkt_counter);

        srp_pkt->next = NULL;
        srp_pkt->in_use = TRUE;
    }

    spin_unlock_irqrestore( &target->spin_lock, cpu_flags );

	return( srp_pkt );
}


static int srp_host_pre_post( srp_target_t *target, srp_host_conn_t *s )
{
    int i;
    int count;
    srp_pkt_t *recv_pkt = NULL;

    count = atomic_read( &target->free_pkt_counter );

    for( i = 0; i < MAX_PREPOST_RECVS; i++ ) {

        recv_pkt = srp_host_get_pkt( target );

        if ( recv_pkt ) {
            recv_pkt->conn = s;
            recv_pkt->scatter_gather_list.key = target->l_key[s->port->hca->hca_index];
            recv_pkt->scatter_gather_list.length = srp_cmd_pkt_size;

            srptp_post_recv( recv_pkt );

        } else {
            TS_REPORT_WARN( MOD_SRPTP, "No buffers to pre-post %d", target->target_index );
            return ( -ENOMEM );
        }
    }

    return( TS_SUCCESS );
}


/*
 * Allocate SRP packet structures and packet data
 */
int srp_host_alloc_pkts( srp_target_t *target )
{
    int pkt_num;
    srp_pkt_t *srp_pkt;
    void *srp_pkt_data;
    int status;
    int max_num_pkts;
    srp_host_hca_params_t *hca;
    int cq_entries;
    int requested_cq_entries;
    int hca_index;
    tTS_IB_CQ_CALLBACK_STRUCT cq_callback;

    /* allocate twice as many packets, send and receive */
    max_num_pkts = MAX_SEND_WQES + MAX_RECV_WQES;
    target->max_num_pkts = max_num_pkts;

    target->srp_pkt_hdr_area = (srp_pkt_t *)
                               kmalloc( (max_num_pkts * sizeof (srp_pkt_t)), GFP_KERNEL );

    if ( target->srp_pkt_hdr_area == NULL ) {
        TS_REPORT_FATAL (MOD_SRPTP, "kmalloc for packet data structures failed");
        return(-ENOMEM);
    }

    target->srp_pkt_free_list = target->srp_pkt_hdr_area;
    target->srp_pkt_data_area = kmalloc( max_num_pkts * srp_cmd_pkt_size, GFP_KERNEL );

    if (target->srp_pkt_data_area == NULL) {
        TS_REPORT_FATAL (MOD_SRPTP, "kmalloc for packet data area failed");
        goto ALLOC_FAIL;
        return(-ENOMEM);
    }


    for( hca_index = 0; hca_index < MAX_HCAS; hca_index++ ) {

        hca = &hca_params[hca_index];

        if( hca->valid == FALSE )
            break;

        cq_callback.context = TS_IB_CQ_CALLBACK_INTERRUPT;
        cq_callback.policy = TS_IB_CQ_PROVIDER_REARM;
        cq_callback.function.entry = cq_send_handler;
        cq_callback.arg = (void *)target;

        cq_entries = MAX_SEND_WQES;
        requested_cq_entries = cq_entries;

        status = tsIbCqCreate ( hca->ca_hndl,
                                &cq_entries,
                                &cq_callback,
                                NULL,
                                &target->cqs_hndl[hca_index] );

        if (status != 0) {
            if ( cq_entries < requested_cq_entries ) {
                TS_REPORT_FATAL ( MOD_SRPTP,
		                          "Send completion queue creation failed: %d asked for %d entries",
			                      cq_entries, requested_cq_entries);
                goto CQ_MR_FAIL;
            }
        }

        cq_callback.function.entry = cq_recv_handler;
	    cq_entries = MAX_RECV_WQES;
        requested_cq_entries = cq_entries;
        status = tsIbCqCreate ( hca->ca_hndl,
                                &cq_entries,
                                &cq_callback,
                                NULL,
                                &target->cqr_hndl[hca_index] );

        if (status != 0) {
            if ( cq_entries < requested_cq_entries ) {
                TS_REPORT_FATAL ( MOD_SRPTP,
                                  "Recv completeion queue creation failed" );
                tsIbCqDestroy (target->cqs_hndl[hca_index]);
                goto CQ_MR_FAIL;
            }
        }

        status = tsIbMemoryRegister ( hca->pd_hndl,
                                      target->srp_pkt_data_area,
                                      max_num_pkts * srp_cmd_pkt_size,
                                      TS_IB_ACCESS_LOCAL_WRITE | TS_IB_ACCESS_REMOTE_READ,
                                      &target->srp_pkt_data_mhndl[hca_index],
                                      &target->l_key[hca_index],
                                      &target->r_key[hca_index] );

        if (status != 0) {
            TS_REPORT_FATAL (MOD_SRPTP, "Memory registration failed: %d", status);
            goto CQ_MR_FAIL;
        }
    }

    srp_pkt = target->srp_pkt_hdr_area;
    srp_pkt_data = target->srp_pkt_data_area;

    for (pkt_num = 0; pkt_num < max_num_pkts; pkt_num++) {

        memset( srp_pkt, 0x00, sizeof( srp_pkt_t ));
        srp_pkt->conn = INVALID_CONN_HANDLE;
        srp_pkt->target = target;
        srp_pkt->data = srp_pkt_data;
        srp_pkt->scatter_gather_list.address = (uint64_t)(uintptr_t)srp_pkt_data;
        srp_pkt->scatter_gather_list.length = srp_cmd_pkt_size;

        if (pkt_num == max_num_pkts - 1)
            srp_pkt->next = NULL;
        else
            srp_pkt->next = srp_pkt + 1;

        srp_pkt->pkt_index = pkt_num;

        srp_pkt++;
        srp_pkt_data += srp_cmd_pkt_size;

    }

    atomic_set (&target->free_pkt_counter, max_num_pkts);

    return(0);

CQ_MR_FAIL:
    for( hca_index = 0; hca_index < MAX_HCAS; hca_index++ ) {

        hca = &hca_params[hca_index];

        if( hca->valid == FALSE )
            break;

        tsIbMemoryDeregister( target->srp_pkt_data_mhndl[hca_index] );

        tsIbCqDestroy( target->cqr_hndl[hca_index] );
        tsIbCqDestroy( target->cqs_hndl[hca_index] );
    }

    kfree (target->srp_pkt_data_area);

ALLOC_FAIL:
    kfree (target->srp_pkt_hdr_area);
    return( -ENOMEM );
}


void srp_host_cleanup_targets( void )
{
    int target_index;
    int hca_index;
    srp_host_hca_params_t *hca;
    srp_target_t *target;

    for( target_index = 0; target_index < max_srp_targets; target_index++ ) {

        target = &srp_targets[target_index];

        if ( target->valid == TRUE ) {

            for ( hca_index = 0; hca_index < MAX_HCAS; hca_index++ ) {

                hca = &hca_params[hca_index];

                if( hca->valid == FALSE )
                    break;

                tsIbMemoryDeregister( target->srp_pkt_data_mhndl[hca_index] );

                tsIbCqDestroy (target->cqr_hndl[hca_index]);
                tsIbCqDestroy (target->cqs_hndl[hca_index]);

            }

            kfree (target->srp_pkt_data_area);
            kfree (target->srp_pkt_hdr_area);
        }
    }

    kfree( srp_targets );
}


void
initialize_target(srp_target_t *target) {

    spin_lock_init(&target->spin_lock);
    sema_init( &target->sema, 1 );

	target->pending_count =
	target->active_count =
    target->resource_count = 0;
	target->target_index = target - &srp_targets[0];
    target->state = TARGET_POTENTIAL_CONNECTION;
    target->timeout = jiffies + TARGET_POTENTIAL_STARTUP_TIMEOUT;
    target->valid = FALSE;
    target->need_disconnect = FALSE;
    target->need_device_reset = FALSE;
    target->hard_reject = FALSE;
    target->hard_reject_count = 0;

	INIT_LIST_HEAD(&target->conn_list);
	INIT_LIST_HEAD(&target->active_list);
	INIT_LIST_HEAD(&target->pending_list);
	INIT_LIST_HEAD(&target->resource_list);
}


void srp_pending_to_scsi( srp_target_t *target, unsigned long status )
{
	struct list_head *ioq_entry, *temp_entry;
    unsigned long    cpu_flags;
    ioq_t            *ioq;
    struct list_head temp_list;

    TS_REPORT_STAGE( MOD_SRPTP,
                     "Flushing the pending queue back to scsi for target %d",
                     target->target_index );

    INIT_LIST_HEAD( &temp_list );

    spin_lock_irqsave( &target->spin_lock, cpu_flags );

    list_for_each_safe( ioq_entry, temp_entry, &target->pending_list ) {

        ioq = list_entry( ioq_entry, ioq_t, pending_list );

        target->pending_count--;
        list_del(&ioq->pending_list );

        ioq->req_temp = ioq->req;

        ioq->req = NULL;

        if ( ioq->type == IOQ_COMMAND ) {
            ioq->req_temp->result = status << 16;

            TS_REPORT_STAGE( MOD_SRPTP,
                             " Sending IO back to scsi from pending list ioq %p",
                             ioq );

            ioq->queue_type = QUEUE_TEMP;
            list_add_tail( &ioq->temp_list, &temp_list );
        }
    }

    spin_unlock_irqrestore( &target->spin_lock, cpu_flags );

    list_for_each_safe( ioq_entry, temp_entry, &temp_list ) {
        ioq = list_entry( ioq_entry, ioq_t, temp_list );

        list_del(&ioq->temp_list );

        srp_host_scsi_complete( ioq->req_temp );

        kfree(ioq);
    }
}


void srp_move_to_pending( srp_target_t *target )
{
    srp_host_hca_params_t *hca = target->port->hca;
    ioq_t *ioq;
	struct list_head *ioq_entry, *temp_entry;
    unsigned long cpu_flags;

    TS_REPORT_STAGE( MOD_SRPTP, "Moving all I/Os to pending list for target %d",
                     target->target_index );

    spin_lock_irqsave( &target->spin_lock, cpu_flags );

    /*
     * First, walk the active list and move them to the pending list
     */
    list_for_each_safe( ioq_entry, temp_entry, &target->active_list ) {

        ioq = list_entry( ioq_entry, ioq_t, active_list );
		target->active_count--;
        list_del(&ioq->active_list);

        TS_REPORT_STAGE(MOD_SRPTP, " Moving ioq %p, SCpnt %p from active to pending",
                         ioq, ioq->req );

        /*
         * Reclaim memory registrations
         */
		if (ioq->sr_list) {
			uint32_t	sr_list_index=0;
			for (; sr_list_index < ioq->sr_list_length; sr_list_index++) {
	   			srptp_dereg_phys_host_buf( ioq->sr_list + sr_list_index );
			}
       		kfree(ioq->sr_list);
		}

        /*
         * We are going to be given a new conn, clear the packet pointer
         */
        ioq->pkt = NULL;
        ioq->recv_pkt = NULL;

        if ( ioq->type == IOQ_ABORT ) {
            TS_REPORT_STAGE( MOD_SRPTP, " Dumping pending flush ioq %p from pending", ioq );
            kfree( ioq );
        } else {
            ioq->queue_type = QUEUE_PENDING;
            target->pending_count++;
		    list_add_tail( &ioq->pending_list, &target->pending_list );
        }
    }

    /*
     * Walk the resource list for the target and move it to the
     * pending list
     */
    list_for_each_safe( ioq_entry, temp_entry, &target->resource_list ) {

        ioq = list_entry( ioq_entry, ioq_t, target_resource_list );
		target->resource_count--;
		list_del(&ioq->target_resource_list );

        TS_REPORT_STAGE( MOD_SRPTP, " Moving ioq %p from target resource to pending", ioq );

        ioq->queue_type = QUEUE_PENDING;
        target->pending_count++;
		list_add_tail( &ioq->pending_list, &target->pending_list );
    }

    spin_unlock_irqrestore( &target->spin_lock, cpu_flags );

    spin_lock_irqsave( &hca->spin_lock, cpu_flags );

    /*
     * Walk the resource list for the HCA this target is using
     */
    list_for_each_safe( ioq_entry, temp_entry, &hca->resource_list ) {

        ioq = list_entry( ioq_entry, ioq_t, hca_resource_list );

        if ( ioq->target == target ) {
            hca->resource_count--;
            list_del(&ioq->hca_resource_list );

            TS_REPORT_STAGE( MOD_SRPTP, " Moving ioq %p from hca resource to pending", ioq );

            spin_lock( &target->spin_lock );
            /*
             * reclaim send and receive frames
             */
           	srp_host_free_pkt_no_lock(ioq->pkt);
            ioq->pkt = NULL;
           	srp_host_free_pkt_no_lock(ioq->recv_pkt);
            ioq->recv_pkt = NULL;

            ioq->queue_type = QUEUE_PENDING;
            target->pending_count++;
		    list_add_tail( &ioq->pending_list, &target->pending_list );

            spin_unlock( &target->spin_lock );
        }
    }

    spin_unlock_irqrestore( &hca->spin_lock, cpu_flags );
}


void srp_flush_pending_to_active( srp_target_t *target )
{
    ioq_t *ioq;
	struct list_head *ioq_entry, *temp_entry;
    unsigned long cpu_flags;
	Scsi_Cmnd	*SCpnt;
    struct list_head temp_list;

    INIT_LIST_HEAD( &temp_list );

    spin_lock_irqsave( &target->spin_lock, cpu_flags );

    list_for_each_safe( ioq_entry, temp_entry, &target->pending_list ) {

        ioq = list_entry( ioq_entry, ioq_t, pending_list );

        target->pending_count--;
		list_del( &ioq->pending_list );

        ioq->queue_type = QUEUE_TEMP;

        list_add_tail( &ioq->temp_list, &temp_list );
    }

    spin_unlock_irqrestore( &target->spin_lock, cpu_flags );

    TS_REPORT_STAGE( MOD_SRPTP, "Flush pending to active for target %d",
                     target->target_index );

    list_for_each_safe( ioq_entry, temp_entry, &temp_list ) {

        ioq = list_entry( ioq_entry, ioq_t, temp_list );
		list_del( &ioq->temp_list );

        SCpnt = ioq->req;

        TS_REPORT_STAGE(MOD_SRPTP, " Retrying SCpnt %p ioq %p", SCpnt, ioq);

        if ( ioq->type == IOQ_COMMAND ) {

            srp_host_internal_qcommand( ioq );

        } else {
            /* we should never have any other I/O type on the pending queue */
            TS_REPORT_FATAL( MOD_SRPTP, "Pending queue error ioq %p type 0x%d found",
                             ioq, ioq->type );
        }
    }
}


void sweep_targets(void)
{
    int i;
    srp_target_t *target;

    TS_REPORT_STAGE( MOD_SRPTP, "Sweeping all targets, that are in need of a connection" );

    /*
     * Sweep all targets for connections that are down, and try to
     * find a connection
     */
    for (i = 0; i < max_srp_targets; i++ ) {

        target = &srp_targets[i];

        down( &target->sema );

        if ((( target->state > TARGET_INITIALIZED ) &&
             ( target->state < TARGET_ACTIVE_CONNECTION )) &&
            ( target->valid == TRUE ) &&
            ( target->active_conn == NULL )) {

            TS_REPORT_STAGE( MOD_SRPTP, "target %d, no active connection", target->target_index );

            pick_connection_path( target );
        }

        up( &target->sema );
    }
}


void srp_host_rediscover(void)
{
    int hca_index, port_index;
    srp_host_port_params_t *port;

    for ( hca_index = 0; hca_index < MAX_HCAS; hca_index++ ) {

        for ( port_index = 0; port_index < MAX_LOCAL_PORTS_PER_HCA; port_index++) {

            port = &hca_params[hca_index].port[port_index];

            port->dm_need_query = TRUE;
        }
    }
}

void srp_host_redistribute_connections(void)
{
    int i;
    srp_target_t *target;

    scsi_block_requests( driver_params.host );

    for( i = 0; i < max_srp_targets; i++ ) {

        target = &srp_targets[i];

        down( &target->sema );

        if ( target->state == TARGET_ACTIVE_CONNECTION ) {

            remove_connection( target->active_conn, TARGET_POTENTIAL_CONNECTION );

            srp_move_to_pending( target );
        }

        up( &target->sema );

    }

    /* goes through and makes connections to all targets */
    sweep_targets();

    scsi_unblock_requests( driver_params.host );
}


/*
 * The thread periodically polls the DM client for a list of SRP
 * services.
 * If it finds new services, it initiates connections to them.
 * It does not check for services which have disappeared.
 */
void
srp_dm_poll_thread(void *arg) {

	int 						port_index, hca_index;
    int                         dm_query_sum = 0;
    unsigned long               dm_query_filter_timeout = 0;
    int                         sweep_targets_for_connections = FALSE;
    srp_target_t                *target;
    srp_host_hca_params_t       *hca;
    srp_host_port_params_t      *port;
	struct list_head            *ioq_entry, *temp_entry;
    unsigned long               cpu_flags;
    ioq_t                       *ioq;
    int                         i;
    int                         retry_count;
    struct list_head            temp_list;

	strncpy(current->comm, "ts_srp_dm", sizeof(current->comm));
	current->comm[sizeof(current->comm)-1] = 0;

	tsIbHostIoQueryInit();

	while (1) {

        TS_REPORT_DATA( MOD_SRPTP, "DM poll thread wakeup" );

		/*
         * Driver is exiting, need to kill this thread
         */
        if ( driver_params.dm_shutdown ) {

            for ( hca_index = 0; hca_index < MAX_HCAS; hca_index++ ) {
                for ( port_index = 0; port_index < MAX_LOCAL_PORTS_PER_HCA; port_index++) {

                    port = &hca_params[hca_index].port[port_index];
                    if ( port->valid == FALSE )
                        break;

                    srp_port_query_cancel( port );

                    if ( port->out_of_service_xid != 0 )
                        srp_register_out_of_service( port, FALSE );

                    if ( port->in_service_xid != 0 )
                        srp_register_in_service( port, FALSE );

                    if ( port->dm_query_in_progress ) {
                        TS_REPORT_STAGE( MOD_SRP,
                                         "Canceling DM Query on hca %d port %d",
                                         hca_index+1, port->local_port );
                        tsIbHostIoQueryCancel( port->dm_xid );
                    }
                }
            }
            break;
        }


        /*
         * One of the ports, needs a dm_query,
         * refresh the hca, and then do a dm_query
         */
		if ( driver_params.need_refresh ) {

            driver_params.need_refresh = FALSE;

            /*
	 	     * Refresh our local port information
	 	     */
		    TS_REPORT_STAGE(MOD_SRPTP,"Refreshing HCA/port info");
            driver_params.num_active_local_ports = srptp_refresh_hca_info();
        }


		/*
	  	 * Fill up the srp_target entries using the DM Client.
		 * Start the connection state machine for new services.
	 	 */
        down( &driver_params.sema );
        dm_query_sum = 0;
        for ( hca_index = 0; hca_index < MAX_HCAS; hca_index++ ) {

            for ( port_index = 0; port_index < MAX_LOCAL_PORTS_PER_HCA; port_index++) {

                port = &hca_params[hca_index].port[port_index];

                if ( port->valid == FALSE )
                    break;

			    if ( port->dm_query_in_progress ) {
                    dm_query_sum++;
			        continue;
                }

                if ( port->dm_need_query == FALSE )
                    continue;

                if ( port->out_of_service_xid == 0 )
                    srp_register_out_of_service( port, TRUE );

                if ( port->in_service_xid == 0 )
                    srp_register_in_service( port, TRUE );

                port->dm_retry_count = 0;

                if ( srp_dm_query( port ) == 0 ) {
                    dm_query_sum++;
                }
            }
        }
        up( &driver_params.sema );


        /*
         * Try and allow all the DM Queries to complete
         * before making connections for the targets.
         */
	    if ( ( dm_query_sum ) ||
             ( !list_empty( &driver_params.query_list ) ) ||
             ( driver_params.port_query ) ) {

            TS_REPORT_STAGE( MOD_SRPTP, "Number of active dm_queries %d", dm_query_sum );

            sweep_targets_for_connections = TRUE;
            driver_params.port_query = FALSE;

            if ( dm_query_filter_timeout == 0 )
                dm_query_filter_timeout = jiffies + DM_FILTER_TIMEOUT;

            /*
             * This is to filter out, if one of the ports is constantly going
             * up/down, In that case there may always be a DM query in progress.
             * This filter will allow us to sweep the targets with the current
             * best paths.
             */
            if ( time_after( jiffies, dm_query_filter_timeout )) {
                dm_query_filter_timeout = 0;
                sweep_targets();
            }

            driver_params.dm_active = TRUE;

        } else if ( sweep_targets_for_connections == TRUE ) {
            sweep_targets_for_connections = FALSE;
            dm_query_filter_timeout = 0;
            driver_params.dm_active = FALSE;
            sweep_targets();
        }


        INIT_LIST_HEAD( &temp_list );

        /*
         * Check timeouts
         */
        for ( i = 0; i < max_srp_targets; i++ ) {

            target = &srp_targets[i];

            down ( &target->sema );

            /*
             * TODO:
             * Cleanup various disconnect/reconnect methods into
             * one method
             */
            if ( target->need_disconnect == TRUE ) {

                remove_connection( target->active_conn, TARGET_POTENTIAL_CONNECTION );

                srp_move_to_pending( target );

                initialize_connection( target );

                target->need_device_reset = FALSE;
                target->hard_reject = FALSE;
            }

            if ( target->need_device_reset == TRUE ) {
	            struct list_head *conn_entry;
                srp_host_conn_t *conn;

                target->need_device_reset = FALSE;
                target->need_disconnect = FALSE;
                target->hard_reject = FALSE;

                list_for_each( conn_entry, &target->conn_list ) {

	                conn = list_entry( conn_entry, srp_host_conn_t, conn_list );

                    if ( conn->state == SRP_HOST_DISCONNECT_NEEDED ) {

                        remove_connection( conn, TARGET_POTENTIAL_CONNECTION );

                        initialize_connection( target );
                    }
                }
            }

            if (( target->hard_reject == TRUE ) && ( target->active_conn )) {

                target->need_device_reset = FALSE;
                target->need_disconnect = FALSE;
                target->hard_reject = FALSE;

                if ( target->hard_reject_count++ < MAX_HARD_REJECT_COUNT ) {

                    TS_REPORT_WARN( MOD_SRPTP, "Retrying reject on Target %d retry %d",
                                    target->target_index, target->hard_reject_count );

                    driver_params.num_pending_connections--;

                    initialize_connection( target );

                } else {
                    srp_host_conn_t *conn = target->active_conn;

                    /*
                     * Exceeded the retries to make this connection to
                     * the redirected path.  Try a new path/IOC, HARD rejects are always
                     * considered a target specific failure, so set the flag to indicate
                     * target failure only.
                     */
                    remove_connection( conn, TARGET_POTENTIAL_CONNECTION );

                    srp_host_close_conn(conn);

                    srp_dm_kill_ioc( target, FALSE );

                    pick_connection_path( target );
                }
            }

            if ( ( ( target->state == TARGET_POTENTIAL_CONNECTION ) &&
                   ( time_after( jiffies, target->timeout ) ) ) ||
                 ( ( target->state == TARGET_NO_PATHS ) &&
                   ( time_after( jiffies, target->timeout ) ) ) ) {

                TS_REPORT_STAGE( MOD_SRPTP, "Target %d, no connection timeout",
                                 target->target_index );

                target->state = TARGET_NO_CONNECTIONS;

                srp_pending_to_scsi( target, DID_NO_CONNECT );
            }

            up( &target->sema );

            spin_lock_irqsave( &target->spin_lock, cpu_flags );

            retry_count = target->resource_count;

            list_for_each_safe( ioq_entry, temp_entry, &target->resource_list ) {

		        ioq = list_entry( ioq_entry, ioq_t, target_resource_list );

                if ( time_after(jiffies, ioq->timeout) ) {

                    target->resource_count--;
		            list_del(&ioq->target_resource_list);

                    ioq->req->result = DID_ERROR << 16;
                    ioq->req_temp = ioq->req;
                    ioq->req = NULL;

                    TS_REPORT_FATAL(MOD_SRPTP, "Timeout on taret resource list for %d", target->target_index );

                    ioq->queue_type = QUEUE_TEMP;
                    list_add_tail( &ioq->temp_list, &temp_list );
                }

                if ( --retry_count == 0 )
                    break;
            }

            spin_unlock_irqrestore( &target->spin_lock, cpu_flags );

        } // end loop on targets


        for ( hca = &hca_params[0];
              hca < &hca_params[MAX_HCAS];
              hca++ ) {

            if ( hca->valid == FALSE )
                break;

            /*
             * Walk the resource list for the HCA, for timeouts...
             * If we could not get an FMR, then return the I/O back
             * to SCSI
             */
            spin_lock_irqsave( &hca->spin_lock, cpu_flags );
            retry_count = hca->resource_count;

            list_for_each_safe( ioq_entry, temp_entry, &hca->resource_list ) {

                ioq = list_entry( ioq_entry, ioq_t, hca_resource_list );

                if (time_after(jiffies, ioq->timeout)) {

                    hca->resource_count--;
                    list_del(&ioq->hca_resource_list );

                    ioq->req->result = DID_ERROR << 16;
                    ioq->req_temp = ioq->req;
                    ioq->req = NULL;

                    TS_REPORT_FATAL( MOD_SRPTP, "HCA resource timeout on target %d",
                                     ioq->target->target_index );

                    /*
                     * Return the send and receive buffers
                     */
                    spin_lock( &ioq->target->spin_lock );
	                srp_host_free_pkt_no_lock( ioq->pkt );
	                srp_host_free_pkt_no_lock( ioq->recv_pkt );
                    atomic_inc( &ioq->target->active_conn->request_limit );
                    atomic_inc( &ioq->target->active_conn->local_request_limit );
                    ioq->pkt = NULL;
                    ioq->recv_pkt = NULL;
                    spin_unlock( &ioq->target->spin_lock );

                    ioq->queue_type = QUEUE_TEMP;
                    list_add_tail( &ioq->temp_list, &temp_list );
                }

                if ( --retry_count == 0 )
                    break;
            }
            spin_unlock_irqrestore( &hca->spin_lock, cpu_flags );

        }


        /*
         * Sweep the done list...Sending back to SCSI
         */
        spin_lock_irqsave( &driver_params.spin_lock, cpu_flags );
        list_for_each_safe( ioq_entry, temp_entry, &driver_params.done_list ) {

	        ioq = list_entry( ioq_entry, ioq_t, done_list );

            driver_params.done_count--;
            list_del(&ioq->done_list);

            ioq->req_temp = ioq->req;
            ioq->req = NULL;

            ioq->queue_type = QUEUE_TEMP;
            list_add_tail( &ioq->temp_list, &temp_list );
        }
        spin_unlock_irqrestore( &driver_params.spin_lock, cpu_flags );

        /*
         * Now actually, return the I/Os as the result of timeout and
         * removal from the done list
         */
        list_for_each_safe( ioq_entry, temp_entry, &temp_list ) {

            ioq = list_entry( ioq_entry, ioq_t, temp_list );
            list_del(&ioq->temp_list);
            srp_host_scsi_complete( ioq->req_temp );
            kfree(ioq);
        }

        /*
		 * Wait for a signal to wake up, or the timeout
         */
		interruptible_sleep_on_timeout( &dm_poll_thread_wait_queue, SRP_DM_THREAD_TIMEOUT );
	}

    tsIbHostIoQueryCleanup();

	return;

}

int srp_host_init(void)
{
    int hca_index, port_index;
    int ret;
    srp_target_t *target;
    unsigned long connections_timeout = 0;

    connection_timeout = TARGET_POTENTIAL_TIMEOUT*HZ;

	if (use_srp_indirect_addressing) {
		TS_REPORT_WARN(MOD_SRPTP,"SRP Host using indirect addressing");
	}

    if (srptp_init_module())
		return EINVAL;

    spin_lock_init(&driver_params.spin_lock);
	INIT_LIST_HEAD(&driver_params.done_list);
	INIT_LIST_HEAD(&driver_params.query_list);
    memcpy( driver_params.I_PORT_ID, hca_params[0].I_PORT_ID, 16 );

    /*
     * Initialize target structures
     */
    srp_targets = (srp_target_t *)kmalloc((sizeof(srp_target_t)*max_srp_targets), GFP_KERNEL );
    if( srp_targets == NULL ) {
        TS_REPORT_FATAL( MOD_SRPTP, "kmalloc failed srp_targets" );
        return ENOMEM;
    }

    memset( &srp_targets[0], 0, sizeof( srp_target_t ) * max_srp_targets );

    for ( target = &srp_targets[0];
          target < &srp_targets[max_srp_targets];
          target++ ) {
        initialize_target( target );
    }
    
   if ( srp_dm_init() )
        return( EINVAL );


    ret = tsKernelThreadStart("ts_srp_dm",
                              srp_dm_poll_thread,
                              NULL,
                              &driver_params.thread );

    if ( ret ) {
        TS_REPORT_FATAL( MOD_SRPTP, "Could not start thread" );
        return ( -1 );
    }

    /*
     * Initialize target structure with wwpn from target_bindings string
     */
	if ( target_bindings != NULL ) {
        if ( parse_target_binding_parameters( target_bindings ) == TS_FAIL ) {
            TS_REPORT_STAGE( MOD_SRPTP, "Problems parsing target_bindings string" );
            return( -EINVAL );
        }
    }

    /*
     * Initialize srp DM & hca spinlocks
     * Request a DM query for each local port and create srp_dm_thread
     */
    for( hca_index = 0; hca_index < MAX_HCAS; hca_index++ ) {

        spin_lock_init(&hca_params[hca_index].spin_lock);
	    INIT_LIST_HEAD(&hca_params[hca_index].resource_list);

        for( port_index = 0; port_index < MAX_LOCAL_PORTS_PER_HCA; port_index++ ) {
            if( hca_params[hca_index].port[port_index].valid ) {
                hca_params[hca_index].port[port_index].dm_need_query = TRUE;
            }
        }
    }

    driver_params.need_refresh = TRUE;


    /* let thread kick in */
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(1);

    wake_up_interruptible(&dm_poll_thread_wait_queue);

    /*
     * Following loop, is to allow time for the IB port to come up,
     * complete DM queries and complete their connections.
     */
    do {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(3*HZ);
		connections_timeout += 3*HZ;

		/*
		 * Wait for :
		 *	(1) Atleast one local port to become active &&
		 *	(2) dm_query to complete &&
		 *	(3) all (if any) the connections initiated to complete
		 *	OR
		 *	(1) timeout to expire
		 */
	} while ( ( ( driver_params.num_active_local_ports == 0 ) ||
			    ( driver_params.dm_active == TRUE )	||
			    ( driver_params.num_pending_connections != 0)) &&
			  ( connections_timeout < (srp_discovery_timeout * HZ)) );

	/*
     * Figure out why we exited the loop
     */
    if (connections_timeout > (srp_discovery_timeout * HZ)) {
		TS_REPORT_WARN( MOD_SRPTP,
			            "Timed out waiting for connections to complete"
			            "Number of pending connections %d\n",
			            driver_params.num_pending_connections);
	}

	if ( driver_params.num_active_local_ports ) {
		TS_REPORT_WARN( MOD_SRPTP,
				        "%d active connections %d pending connections\n",
				        driver_params.num_active_connections,
                        driver_params.num_pending_connections );
	} else {
		TS_REPORT_WARN( MOD_SRPTP,
				        "No active local port\n");
	}

    srp_host_scsi_init();

	print_target_bindings();

    return 0;
}


int srp_proc_info( struct Scsi_Host *host, char *buffer, char **start, off_t offset,
	          int length, int inout)
{
    int len = 0;
    int i;
    srp_target_t *target;
    int not_first_entry = FALSE;
    uint8_t *gid;
    uint8_t *ioc_guid;
    char *buf;

    if ( inout == TRUE ) {
        /* write to proc interface, redistribute connections */
        if ( !buffer || length >= PAGE_SIZE ) {
            return( -EINVAL );
        }

        buf = (char *)__get_free_page(GFP_KERNEL);
        if( !buf ) {
            return( -ENOMEM );
        }

        memcpy( buf, buffer, length );
        buf[length] = '\0';

        if ( strncmp( "force_rebalance", buf, length-1 ) == 0 ) {
            srp_host_redistribute_connections();
        } else if ( strncmp( "force_rediscover", buf, length-1 ) == 0 ) {
            srp_host_rediscover();
        }

        free_page((unsigned long)buf);

        return( length );
    }

    len = sprintf( &buffer[len], "\nTopspin SRP Driver\n\n" );


    len += sprintf( &buffer[len], "Index" );
    len += sprintf( &buffer[len], "          Service" );
    len += sprintf( &buffer[len], "                            Active Port GID" );
    len += sprintf( &buffer[len], "                              IOC GUID\n" );
    for ( i = 0; i < max_srp_targets; i++ ) {

        target = &srp_targets[i];

        if (( target->valid == FALSE ) || ( target->state != TARGET_ACTIVE_CONNECTION ))
            continue;

        gid = target->port->local_gid;
        ioc_guid = target->ioc->guid;

        len += sprintf( &buffer[len], "%3d", target->target_index );
        len += sprintf( &buffer[len], "     T10.SRP%llX",
                        ( unsigned long long)cpu_to_be64(target->service_name));
        len += sprintf( &buffer[len], "     ");
        len += sprintf( &buffer[len],
                        "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
                        gid[0], gid[1], gid[2], gid[3], gid[4], gid[5], gid[6], gid[7],
                        gid[8], gid[9], gid[10], gid[11], gid[12], gid[13], gid[14], gid[15] );
        len += sprintf( &buffer[len], "    ");
        len += sprintf( &buffer[len],
                        "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
                        ioc_guid[0], ioc_guid[1], ioc_guid[2], ioc_guid[3],
                        ioc_guid[4], ioc_guid[5], ioc_guid[6], ioc_guid[7] );
        len += sprintf( &buffer[len], "    %d %d",
                        atomic_read(&target->active_conn->local_request_limit )-1,
                        atomic_read(&target->active_conn->request_limit)-1 );
        len += sprintf( &buffer[len], " %d\n",
                        atomic_read(&target->free_pkt_counter)-1 );


    }
    len += sprintf( &buffer[len], "\n" );

    /*
     * Connection counters
     */
    len += sprintf( &buffer[len], "Number of Pending Connections %d\n", driver_params.num_pending_connections );
    len += sprintf( &buffer[len], "Number of Active Connections %d\n", driver_params.num_active_connections );
    len += sprintf( &buffer[len], "Number of Connections %d\n\n", driver_params.num_connections );

    /*
     * Print out target bindings string
     */
	len += sprintf( &buffer[len], "srp_host: target_bindings=");
    for ( target = &srp_targets[0];
		  target < &srp_targets[max_srp_targets];
		  target++) {

        if ( target->valid == TRUE ) {

            // don't print colon on first guy
            if ( not_first_entry == TRUE ) {
			    len += sprintf( &buffer[len], ":");
            } else {
                not_first_entry = TRUE;
            }

            len += sprintf( &buffer[len], "%llx.%x",
                            (unsigned long long) cpu_to_be64(target->service_name),
                            target->target_index );
        }
	}
	len += sprintf( &buffer[len], "\n");

    return ( len );
}


void srp_slave_configure( struct scsi_device *device)
{
    int default_depth = max_cmds_per_lun;

    scsi_adjust_queue_depth( device, 0, default_depth );
}


void srp_host_scsi_init( void )
{
	struct pci_dev *hca_pdev;
    struct Scsi_Host *host;
    int status;

	/*
	 * Find a HCA on the PCI bus
	 */
	hca_pdev = pci_find_device(0x15b3,0x5a46,NULL);
    if (hca_pdev == NULL) {
	    hca_pdev = pci_find_device(0x1867,0x5a46,NULL);
    }
    if (hca_pdev == NULL) {
		TS_REPORT_FATAL(MOD_SRPTP,
			"Couldnt find PCI device : vendor id 0x15B3 device id 0x5a46");
		return;
	}

#if defined(__x86_64__)
    {
      int err = pci_set_dma_mask(hca_pdev, 0xffffffffffffffffULL);
      if (err)
        TS_REPORT_WARN(MOD_SRPTP, "Couldn't set 64-bit PCI DMA mask.");
    }
#endif

    host = scsi_host_alloc( &driver_template, 0x00 );
    if ( host == NULL ) {
        TS_REPORT_FATAL( MOD_SRPTP, "Could not allocate a scsi host struct" );
        return;
    }

    driver_params.host = host;
    driver_params.pdev = hca_pdev;

    host->this_id = 255;
    host->unique_id = 0;
    host->max_channel = 0;
    host->max_lun = max_luns;
    host->max_id = max_srp_targets;

    status = scsi_add_host( host, &hca_pdev->dev );
    if ( status ) {
        TS_REPORT_FATAL( MOD_SRPTP, "Probe failed" );

        scsi_host_put(host);
    }

    scsi_scan_host(host);

    return;
}


void srp_recv( int pkt_index, srp_target_t *target )
{
    srp_pkt_t *recv_pkt;
    unsigned char opcode;
    unsigned long cpu_flags;

    recv_pkt = srp_host_pkt_index_to_ptr( pkt_index, target );

    if ( recv_pkt == NULL ) {
        TS_REPORT_FATAL( MOD_SRPTP, "Bad pkt_index %d", pkt_index );
        return;
    }

    opcode = recv_pkt->data[0];

    switch (opcode) {
        case SRP_RSP:
            srp_process_resp(target, recv_pkt);
            break;

		case SRP_T_LOGOUT :
           	TS_REPORT_WARN( MOD_SRPTP,
			                "Received Target Logout for target %d, try reconnecting...",
			                target->target_index );

            /*
             * Force new I/Os to be queued...
             * The actual disconnect will occur when we get the CM:disconnect
             * message from the target.
             */
            spin_lock_irqsave( &target->spin_lock, cpu_flags );
            if ( target->active_conn == recv_pkt->conn ) {
                target->timeout = jiffies + connection_timeout;
                target->state = TARGET_POTENTIAL_CONNECTION;
            }
            spin_unlock_irqrestore( &target->spin_lock, cpu_flags );
            break;

        case SRP_TPAR_REQ:
        case SRP_AER_REQ:
        default:
            TS_REPORT_WARN( MOD_SRPTP, "SRP IU with unknown %x", opcode);
            break;
    }

	srp_host_free_pkt(recv_pkt);
}


static int
srp_host_login_resp (srp_host_conn_t * s,
                     char *data)
{
    srp_login_resp_t *resphdr = (srp_login_resp_t *) data;
    int remote_request_limit;

    remote_request_limit = be32_to_cpu(resphdr->request_limit_delta);
    atomic_set (&s->request_limit, remote_request_limit );
    atomic_set( &s->local_request_limit, MAX_IOS_PER_TARGET );

    if (atomic_read(&s->request_limit) != 0) {

        atomic_inc (&s->request_limit);

        atomic_inc (&s->local_request_limit);

        atomic_set( &s->recv_post, 0 );

    } else {
	    TS_REPORT_WARN( MOD_SRPTP,
			            "request limit 0 for 0x%llx",
			            be64_to_cpu(s->target->service_name));
    }

    TS_REPORT_WARN( MOD_SRPTP,
                    "Login successful for target %d hca %d local port %d req_limit %d %d",
		            s->target->target_index,
                    s->port->hca->hca_index+1, s->port->local_port,
                    atomic_read( &s->request_limit ),
                    atomic_read( &s->local_request_limit ) );

    return 0;
}


static int
srp_host_login_rej (char *data)
{
    srp_login_rej_t *resphdr = (srp_login_rej_t *) data;

    TS_REPORT_WARN(MOD_SRPTP, "Login rejected reason code %d", be32_to_cpu (resphdr->reason_code));
    return 0;
}


/*
 * Process a SRP_RSP...
 * Responeses from TASK Management and Commands will come through here
 */
static int
srp_process_resp (srp_target_t *target, srp_pkt_t * pkt)
{
    srp_host_conn_t *s;
    srp_resp_t *resphdr;
    tUINT32 resp_code;
    ioq_t *ioq = NULL;
    ioq_t *next_ioq = NULL;
    ioq_t *cmd_ioq;
    Scsi_Cmnd *scsi_cmnd;
    struct list_head *ioq_entry, *temp_entry;
    unsigned long cpu_flags;

#if DBG_IGNORE_WRITE
    resphdr = (srp_resp_t *) pkt->data;
    ioq = (ioq_t *)(tUINT32)(be64_to_cpu(resphdr->tag));
    if ( ioq->req ) {
        scsi_cmnd = ( Scsi_Cmnd * )ioq->req;
        if ( scsi_cmnd->cmnd[0] == 0x2A ) {
            TS_REPORT_FATAL( MOD_SRPTP, "Ignore Write %p", ioq );
            return 0;
        }
    }
#endif

    /* get some variables to get our context */
    resphdr = (srp_resp_t *) pkt->data;
    resphdr->tag = be64_to_cpu (resphdr->tag);

    spin_lock_irqsave(&target->spin_lock, cpu_flags);

    if ( target->active_conn != pkt->conn ) {
        /* Do not process anything, if the connection is down */
        spin_unlock_irqrestore(&target->spin_lock, cpu_flags);
        return 0;
    }

    s = pkt->conn;

    /* set our req_limit_data from the response */
    atomic_add(be32_to_cpu( resphdr->request_limit_delta ), &s->request_limit);
    atomic_dec( &s->recv_post );
    atomic_inc( &s->local_request_limit );

    ioq = (ioq_t *)(uintptr_t)resphdr->tag;
    TS_REPORT_DATA( MOD_SRPTP, "Host Processing an SRP Response for ioq %p", ioq );

    if (ioq->req == NULL) {
        /*
         * This could happen if the I/O completes much later after an abort or lun
         * reset.  SRP Target should ensure that this case does not occur.
         */
        TS_REPORT_WARN( MOD_SRPTP, "NULL request queue, for tag %x", (tUINT32)resphdr->tag );

        spin_unlock_irqrestore(&target->spin_lock, cpu_flags);
        return 0;
    } else {

		if ( ioq->queue_type == QUEUE_ACTIVE ) {
            target->active_count--;
		    list_del(&ioq->active_list);
        } else if ( ioq->queue_type == QUEUE_PENDING ) {
            TS_REPORT_FATAL(MOD_SRPTP,"taking completion on pending queue %p",ioq );
            spin_unlock_irqrestore(&target->spin_lock, cpu_flags);
            return 0;
        } else {
            TS_REPORT_STAGE( MOD_SRPTP, "Processing a response on wrong queue %p ioq" );
            spin_unlock_irqrestore(&target->spin_lock, cpu_flags);
            return 0;
        }

        /* while we have spinlocks lets get the next I/O, if there is one */
        list_for_each_safe( ioq_entry, temp_entry, &target->resource_list ) {

            next_ioq = list_entry( ioq_entry, ioq_t, target_resource_list );

            target->resource_count--;
            list_del(&next_ioq->target_resource_list);
            break;
        }

        scsi_cmnd = ( Scsi_Cmnd * )ioq->req;

        ioq->req = NULL;

        spin_unlock_irqrestore(&s->target->spin_lock, cpu_flags);

        /* Response codes only make sense if task management flags had been set. */
        if (( ioq->type == IOQ_ABORT ) ||
            ( ioq->type == IOQ_INTERNAL_ABORT )) {

            if ( resphdr->status.bit.rspvalid ) {

                /* response data */
                resp_code = ((tUINT8 *)(resphdr + 1))[3];

                switch( resp_code ) {
                    case NO_FAILURE:
                        /*
                         * Abort succeeded...we are going to kill I/O context
                         * for the original I/O.  Then kill the I/O context for
                         * the abort command itself
                         */
                        cmd_ioq = (ioq_t *)scsi_cmnd->SCp.buffer;

                        scsi_cmnd->result = DID_ABORT << 16;

                        /* we are done with this I/O, complete it back to scsi */
                        srp_host_scsi_complete( scsi_cmnd );

                        cmd_ioq->req = NULL;
                        /*
                         * Do not free, the original cmd_ioq here...
                         * The initiator of the abort will do it.  The
                         * initiator uses ->req field as an indicator of the
                         * Success of the abort.
                         */
                        kfree(ioq);
                        break;

                    case TSK_MGT_FAILED:
                        /*
                         * The abort request, failed, may mean the abort and completion
                         * crossed on the wire.  The target could not find the original
                         * I/O in his pending list.
                         */
                        /* if the commands' I/O context has not been killed, it means the
                         * I/O was flushed before the abort was sent, and may have been lost
                         * In that case complete the I/O
                         */
                        kfree(ioq);
                        break;

                    default:
                        TS_REPORT_WARN( MOD_SRPTP, "Illegal Response Code %x", resp_code );
                        break;
                }

            }

        } else {
            /* process the response for a command */
            srp_host_iodone ( scsi_cmnd, resphdr, ioq );

            srp_host_scsi_complete( scsi_cmnd );

            kfree(ioq);
        }

        if ( next_ioq ) {
            TS_REPORT_DATA( MOD_SRPTP, "Retrying ioq %p for target %d from target resource q",
                            next_ioq, target->target_index );

            srp_host_internal_qcommand( next_ioq );
        }

    }

    return 0;
}


void srp_host_internal_qcommand( ioq_t *ioq )
{
    int status;

    status = srp_host_send_command( ioq->target, ioq, ioq->req );

    if ( status ) {
        TS_REPORT_STAGE( MOD_SRPTP, "Internal qcommand failed for ioq %p on target %d",
                         ioq, ioq->target->target_index );

        srp_host_scsi_complete( ioq->req );

        ioq->req = NULL;

        kfree( ioq );
    }
}


/* NOTE:  currently a scsi target AND a scsi host map to an srp_session
 * a scsi channal maps to an srp_connection.  We need to check
 * this and figure out what we should do if we want multiple
 * hosts and multiple targets
 *
 * We probably need to have a hosts data structure encapsulating
 * the session data structure.  Who cares for now.
 */
/* We use the Scsi_Pointer structure that's included with each command
 * SCSI_Cmnd as a scratchpad for our I/O.
 *
 * SCp will always point to the SRB structure (defined in qla1280.h).
 * It is define as follows:
 *  - SCp.ptr  -- > NULL
 *  - SCp.buffer -->  pointer to ioq
 *  - SCp.buffers_residual --> NULL
 *  - SCp.have_data_in --> not used
 *  - SCp.sent_command --> not used
 *  - SCp.phase --> not used
 */
int
srp_host_qcommand (Scsi_Cmnd * SCpnt,
                   void (*done) (Scsi_Cmnd *))
{
    ioq_t *ioq = NULL;
	srp_target_t *target;
    int status;

    spin_unlock(&driver_params.host->default_lock);

    /* pre-format the SCSI response */
   	SCpnt->scsi_done = done;
    SCpnt->sense_buffer[0] = 0;

    /* point to the host structure for the given target */
	target = &srp_targets[SCpnt->device->id];

    /*
	 * If the command is internally queued, reuse the previously
	 * allocated ioq
	 */
   	ioq = kmalloc (sizeof (ioq_t), GFP_ATOMIC);

    if (ioq == NULL ) {
        TS_REPORT_FATAL( MOD_SRPTP,
                         "kmalloc failed for ioq alloc on tgt %d",
                         SCpnt->device->id );

        spin_lock(&driver_params.host->default_lock);

        return (-1);
    }

	memset(ioq,0,sizeof(ioq_t));
    ioq->req = SCpnt;
    ioq->target = target;

    /* fill out some back pointers for SCSI, so we have this info for aborting if needed */
    SCpnt->SCp.buffers_residual = 0;
    SCpnt->SCp.ptr = NULL;
    SCpnt->SCp.buffer = (void *) ioq;

    status = srp_host_send_command( target, ioq, SCpnt );

    if ( status ) {
        unsigned long cpu_flags;

        TS_REPORT_DATA( MOD_SRPTP, "Send Command failed ioq %p on target %d, queueing to done list",
                        ioq, target->target_index );

        spin_lock_irqsave(&driver_params.spin_lock, cpu_flags);
	    list_add_tail(&ioq->done_list,&driver_params.done_list);
        spin_unlock_irqrestore(&driver_params.spin_lock, cpu_flags);

        wake_up_interruptible(&dm_poll_thread_wait_queue);
    }

    spin_lock(&driver_params.host->default_lock);
    return 0;
}

int srp_host_send_command( srp_target_t *target, ioq_t *ioq, Scsi_Cmnd *SCpnt )
{
    int status;
    unsigned long cpu_flags;
    srp_pkt_t *send_pkt=NULL;
    srp_pkt_t *recv_pkt=NULL;
    srp_host_conn_t *s;

	spin_lock_irqsave(&target->spin_lock, cpu_flags);

    if (( target->state == TARGET_NOT_INITIALIZED ) ||
        ( target->state == TARGET_INITIALIZED ) ||
        ( target->state == TARGET_NO_CONNECTIONS  )) {

        SCpnt->result = DID_NO_CONNECT << 16;
        spin_unlock_irqrestore(&target->spin_lock, cpu_flags);
        goto SEND_FAIL;
    }

    TS_REPORT_DATA( MOD_SRPTP,
                    "Command on target %d SCpnt %p ioq %p",
	                SCpnt->device->id, SCpnt, ioq );
    TS_REPORT_DATA( MOD_SRPTP,
                    "SCpnt %p Request Buffer %p Request Buflen %x ioq %p",
                    SCpnt,
                    SCpnt->request_buffer,
                    SCpnt->request_bufflen, ioq );


    if ( ( target->state == TARGET_POTENTIAL_CONNECTION ) ||
         ( target->state == TARGET_NO_PATHS ) ) {

        TS_REPORT_DATA( MOD_SRPTP,
                         "Target %d, connection in progress queueing ioq %p",
                         target->target_index, ioq );

       	ioq->type = IOQ_COMMAND;

        ioq->queue_type = QUEUE_PENDING;

        target->pending_count++;
		list_add_tail(&ioq->pending_list,&target->pending_list);
        spin_unlock_irqrestore(&target->spin_lock, cpu_flags);
        goto SEND_SUCCESS;

    } else {
        s = target->active_conn;
        if (( s == NULL ) || ( s->state != SRP_UP )) {

            TS_REPORT_STAGE( MOD_SRPTP, "No active conn, target %d, target state %d",
                             target->target_index, target->state );

            SCpnt->result = DID_NO_CONNECT << 16;
            spin_unlock_irqrestore(&target->spin_lock, cpu_flags);
            goto SEND_FAIL;
        }
    }

    spin_unlock_irqrestore(&target->spin_lock, cpu_flags);

    /*
     * Check if we have already done allocation
     */
    if (( ioq->pkt == NULL ) || ( ioq->recv_pkt == NULL )) {

        spin_lock_irqsave(&target->spin_lock, cpu_flags);
        /* first check if we have remote credits */
        if ( atomic_dec_and_test(&s->request_limit) ) {

            TS_REPORT_DATA( MOD_SRPTP,
                            "Queuing exceeding request limit for target %d",
                            target->target_index );

            atomic_inc(&s->request_limit);

            /* queue to the target */
            ioq->timeout = jiffies + TARGET_RESOURCE_LIST_TIMEOUT;
            ioq->queue_type = QUEUE_TARGET;

            target->resource_count++;
	        list_add_tail(&ioq->target_resource_list,&target->resource_list);
            spin_unlock_irqrestore(&target->spin_lock, cpu_flags);

            goto SEND_SUCCESS;
        }


        /* next check if we have local credits */
        if ( atomic_dec_and_test(&s->local_request_limit) ) {

            TS_REPORT_DATA( MOD_SRPTP,
                            "Queuing exceeding local request limit for target %d",
                            target->target_index );

            atomic_inc(&s->request_limit);
            atomic_inc(&s->local_request_limit);

            if ( atomic_read( &s->local_request_limit ) < 0 )
                TS_REPORT_FATAL(MOD_SRPTP, "less than zero " );

            /* queue to the target */
            ioq->timeout = jiffies + TARGET_RESOURCE_LIST_TIMEOUT;
            ioq->queue_type = QUEUE_TARGET;

            target->resource_count++;
	        list_add_tail(&ioq->target_resource_list,&target->resource_list);
            spin_unlock_irqrestore(&target->spin_lock, cpu_flags);

            goto SEND_SUCCESS;
        }
        spin_unlock_irqrestore(&target->spin_lock, cpu_flags);

        ioq->type = IOQ_COMMAND;
        ioq->sr_list = NULL;

        /* get an srp packet and format it for srptp */
        send_pkt = srp_host_get_pkt( target );
        recv_pkt = srp_host_get_pkt( target );
        if (( send_pkt == NULL ) || ( recv_pkt == NULL )){
            if ( send_pkt ) {
                srp_host_free_pkt( send_pkt );
            }
            if ( recv_pkt ) {
                srp_host_free_pkt( recv_pkt );
            }

            ioq->pkt = NULL;
            ioq->recv_pkt = NULL;

            atomic_inc(&s->request_limit);
            atomic_inc(&s->local_request_limit);

            /* queue to the target */
            ioq->timeout = jiffies + TARGET_RESOURCE_LIST_TIMEOUT;
            ioq->queue_type = QUEUE_TARGET;

            TS_REPORT_STAGE( MOD_SRPTP,
                            "Queuing no packets target %d",
                            target->target_index );

            spin_lock_irqsave(&target->spin_lock, cpu_flags);
            target->resource_count++;
	        list_add_tail(&ioq->target_resource_list,&target->resource_list);
            spin_unlock_irqrestore(&target->spin_lock, cpu_flags);

            goto SEND_SUCCESS;
        }

        ioq->pkt = send_pkt;
        ioq->recv_pkt = recv_pkt;

        send_pkt->conn = s;
        send_pkt->scatter_gather_list.key = target->l_key[s->port->hca->hca_index];

        recv_pkt->conn = s;
        recv_pkt->scatter_gather_list.key = target->l_key[s->port->hca->hca_index];

    } else {
        send_pkt = ioq->pkt;
        recv_pkt = ioq->recv_pkt;
    }

    status = srp_build_command ( send_pkt, SCpnt, s, ioq);
    if (( status == -EAGAIN ) || ( status == -ENOMEM )) {
        srp_host_hca_params_t *hca = target->port->hca;

        if (ioq->sr_list) {
			uint32_t	sr_list_index=0;
			for (; sr_list_index < ioq->sr_list_length; sr_list_index++) {
	   			srptp_dereg_phys_host_buf( ioq->sr_list + sr_list_index );
			}
       		kfree(ioq->sr_list);
            ioq->sr_list = NULL;
		}

        TS_REPORT_DATA ( MOD_SRPTP, "Build command failed ioq %p, target %d, queueing to hca %d",
                          ioq, target->target_index, hca->hca_index+1 );

        if ( SCpnt->device->type == TYPE_TAPE ) {
            /* this is a TAPE device, do not queue otherwise we could get
             * out of order, instead, fail the command */
            if ( send_pkt ) {
                srp_host_free_pkt( send_pkt );
            }
            if ( recv_pkt ) {
                srp_host_free_pkt( recv_pkt );
            }

            ioq->pkt = NULL;
            ioq->recv_pkt = NULL;

            atomic_inc(&s->request_limit);
            atomic_inc(&s->local_request_limit);

            goto SEND_FAIL;
        } else {
            /* queue to the hca */
            ioq->timeout = jiffies + HCA_RESOURCE_LIST_TIMEOUT;
            ioq->queue_type = QUEUE_HCA;

            spin_lock_irqsave(&hca->spin_lock, cpu_flags);
            hca->resource_count++;
	        list_add_tail(&ioq->hca_resource_list,&hca->resource_list);
            spin_unlock_irqrestore(&hca->spin_lock, cpu_flags);

            goto SEND_SUCCESS;
        }

    } else if ( status == -EINVAL ) {
        srp_host_free_pkt(send_pkt);
        srp_host_free_pkt(recv_pkt);

		ioq->pkt = NULL;
        ioq->recv_pkt = NULL;

        atomic_inc(&s->request_limit);
        atomic_inc(&s->local_request_limit);

        TS_REPORT_STAGE ( MOD_SRPTP, "ioq %p, target %p, build command failed",
                          ioq, target );

        SCpnt->result = DID_ERROR << 16;

        goto SEND_FAIL;
    }

    ioq->queue_type = QUEUE_ACTIVE;

    spin_lock_irqsave(&target->spin_lock, cpu_flags);
    target->active_count++;
	list_add_tail(&ioq->active_list,&target->active_list);
	spin_unlock_irqrestore(&target->spin_lock, cpu_flags);

    /* send the srp packet */
    status = srptp_post_recv( recv_pkt );
    if ( status == TS_SUCCESS ) {
        status = srptp_post_send ( send_pkt );
    }

    if ( status ) {
        /* we have a problem posting, disconnect, never should happen */
        if (ioq->sr_list) {
			uint32_t	sr_list_index=0;
			for (; sr_list_index < ioq->sr_list_length; sr_list_index++) {
	   			srptp_dereg_phys_host_buf( ioq->sr_list + sr_list_index );
			}
       		kfree(ioq->sr_list);
            ioq->sr_list = NULL;
		}

        if ( send_pkt ) {
            srp_host_free_pkt( send_pkt );
        }
        if ( recv_pkt ) {
            srp_host_free_pkt( recv_pkt );
        }

        ioq->pkt = NULL;
        ioq->recv_pkt = NULL;

        atomic_inc(&s->request_limit);
        atomic_inc(&s->local_request_limit);

        spin_lock_irqsave( &target->spin_lock, cpu_flags );

        TS_REPORT_FATAL( MOD_SRPTP, "Send post error %d status %d",
                         target->target_index, status );

        target->state = TARGET_POTENTIAL_CONNECTION;
        target->need_disconnect = TRUE;

        spin_unlock_irqrestore( &target->spin_lock, cpu_flags );

        goto SEND_FAIL;
    }

SEND_SUCCESS:
    return (TS_SUCCESS);

SEND_FAIL:
    return( TS_FAIL );
}

#if 1
/*
 * The abort_eh entry point is called with irqsaved, so we
 * will not be able to take the completion for the abort.  We
 * could either poll the cq for the abort or...
 * 1) Ignore the abort
 * 2) SCSI will do a device reset
 * 3) we will complete all Active I/Os back to scsi
 * 4) reset the connection
 * 5) SCSI will retry all its I/Os
 */
int
srp_host_abort_eh (Scsi_Cmnd * SCpnt)
{
    TS_REPORT_WARN( MOD_SRPTP,
		            "Abort SCpnt %p on target %d",
		            SCpnt, SCpnt->device->id );

    return( FAILED );
}
#else
/*
 * Beging error handling entrypoints
 * new_eh entrypoints
 */
int
srp_host_abort_eh (Scsi_Cmnd * SCpnt)
{
    unsigned long cpu_flags;
    srp_host_conn_t *conn = NULL;
    ioq_t *ioq = NULL;
    ioq_t *cmd_ioq = NULL;
    int status;
    srp_target_t *target = NULL;

    spin_unlock( &driver_params.host->default_lock );

    cmd_ioq = (ioq_t *)SCpnt->SCp.buffer;

    TS_REPORT_STAGE( MOD_SRPTP,
		             "Aborting TAG %p SCpnt %p on target %d",
		             cmd_ioq, SCpnt, SCpnt->target );

    if ( cmd_ioq == NULL ) {
        /* I/O already completed */
        spin_lock( &driver_params.host->default_lock );
        return (SUCCESS);
    }

    target = cmd_ioq->target;

    spin_lock_irqsave( &target->spin_lock, cpu_flags );

    if (( target->state == TARGET_NOT_INITIALIZED ) ||
        ( target->state == TARGET_NO_CONNECTIONS  )) {
        /*
         * This should never happen...
         */
        TS_REPORT_STAGE( MOD_SRPTP,
                         "Abort to Target with no connections, target %d",
                         SCpnt->target );

        SCpnt->result = DID_NO_CONNECT << 16;

        spin_unlock_irqrestore(&target->spin_lock, cpu_flags);

        cmd_ioq->req = NULL;

        spin_lock( &driver_params.host->default_lock );
        (*SCpnt->scsi_done) (SCpnt);
        return (SUCCESS);

    } else if ( target->state == TARGET_POTENTIAL_CONNECTION ) {
        /* check if the abort is coming in at the same time we are looking
         * for a new connection for this service */

        TS_REPORT_STAGE( MOD_SRPTP,
                         "Abort while waiting for a connection on target %d",
                         SCpnt->target );

        if ( cmd_ioq->queue_type == QUEUE_PENDING ) {
            TS_REPORT_STAGE( MOD_SRPTP,
		                    "Abort ioq %p removing from potential queue", cmd_ioq );
            target->pending_count--;
            list_del( &cmd_ioq->pending_list );
        } else {
            TS_REPORT_FATAL( MOD_SRPTP,
                             "ioq %p, on wrong queue", ioq );
        }

        SCpnt->result = DID_ABORT << 16;

        cmd_ioq->req = NULL;
        kfree ( cmd_ioq );

        spin_unlock_irqrestore(&target->spin_lock, cpu_flags);

        spin_lock( &driver_params.host->default_lock );

        (*SCpnt->scsi_done) (SCpnt);
        return (SUCCESS);

    } else if ( target->state == TARGET_ACTIVE_CONNECTION ) {

	    if ( cmd_ioq->queue_type == QUEUE_TARGET ) {
            TS_REPORT_STAGE( MOD_SRPTP,
		                     "Abort ioq %p removing from target resource queue", cmd_ioq );

            target->resource_count--;
            list_del( &cmd_ioq->target_resource_list );

            SCpnt->result = DID_ABORT << 16;

            cmd_ioq->req = NULL;
            kfree ( cmd_ioq );

            spin_unlock_irqrestore(&target->spin_lock, cpu_flags);

            spin_lock( &driver_params.host->default_lock );
       	    (*SCpnt->scsi_done) (SCpnt);
            return (SUCCESS);

        } else if ( cmd_ioq->queue_type == QUEUE_HCA ) {
            srp_host_hca_params_t *hca = target->port->hca;

            TS_REPORT_STAGE( MOD_SRPTP,
		                     "Abort ioq %p removing from hca resource queue", cmd_ioq );

            spin_lock( &hca->spin_lock );
            hca->resource_count--;
            list_del( &cmd_ioq->hca_resource_list );
            spin_unlock( &hca->spin_lock );

            spin_unlock_irqrestore(&target->spin_lock, cpu_flags);

            if ( ioq->pkt ) {
                srp_host_free_pkt( ioq->pkt );
            }
            if ( ioq->recv_pkt ) {
                srp_host_free_pkt( ioq->recv_pkt );
            }

            ioq->pkt = NULL;
            ioq->recv_pkt = NULL;

            atomic_inc(&conn->request_limit);
            atomic_inc(&conn->local_request_limit);

            SCpnt->result = DID_ABORT << 16;

            cmd_ioq->req = NULL;
            kfree ( cmd_ioq );

            spin_lock( &driver_params.host->default_lock );
       	    (*SCpnt->scsi_done) (SCpnt);
            return (SUCCESS);
        }

        conn = target->active_conn;
        spin_unlock_irqrestore(&target->spin_lock, cpu_flags);
    } else {
        BUG();
    }

    connection_reset( target );

    srp_move_to_pending( target );

    srp_pending_to_scsi( target, DID_SOFT_ERROR );

    spin_lock( &driver_params.host->default_lock );
	return ( 0 );
}
#endif

int
srp_host_device_reset_eh( Scsi_Cmnd *SCpnt )
{
    srp_target_t *target = &srp_targets[SCpnt->device->id];
    unsigned long cpu_flags;

    spin_unlock( &driver_params.host->default_lock );

    TS_REPORT_WARN( MOD_SRPTP,
                    "Device reset...target %d",
    	            SCpnt->device->id );

    spin_lock_irqsave( &target->spin_lock, cpu_flags );

    if (( target->state == TARGET_NOT_INITIALIZED ) ||
        ( target->state == TARGET_NO_CONNECTIONS  )) {

        spin_unlock_irqrestore(&target->spin_lock, cpu_flags);

        spin_lock( &driver_params.host->default_lock );
        return( -EINVAL );

    } else if (( target->state == TARGET_POTENTIAL_CONNECTION ) ||
               ( target->state == TARGET_NO_PATHS ) ){

        spin_unlock_irqrestore(&target->spin_lock, cpu_flags);

        spin_unlock( &driver_params.host->default_lock );

        srp_pending_to_scsi( target, DID_SOFT_ERROR );

        spin_lock( &driver_params.host->default_lock );
        return( SUCCESS );

    } else {

        spin_unlock_irqrestore(&target->spin_lock, cpu_flags);

        connection_reset( target );

        srp_move_to_pending( target );

        srp_pending_to_scsi( target, DID_SOFT_ERROR );
    }

    spin_lock( &driver_params.host->default_lock );
	return (SUCCESS);
}

int
srp_host_bus_reset_eh( Scsi_Cmnd *SCpnt )
{
    TS_REPORT_WARN( MOD_SRPTP,
                    "Bus reset target %d",
    	            SCpnt->device->id );

    return (SUCCESS);
}


int
srp_host_reset_eh (Scsi_Cmnd * SCpnt )
{
    /* always return success but do not do anything, cannot reset the IB card */
    TS_REPORT_WARN( MOD_SRPTP, "Host reset");
    return (SUCCESS);
}


/*
 * end of Error Handling entry points
 */


int
srp_host_login (srp_host_conn_t * s )
{
    srp_login_req_t *header;

    TS_REPORT_STAGE( MOD_SRPTP, "Sending Login request for conn %p", s );
    TS_REPORT_STAGE( MOD_SRPTP, "SRP Initiator GUID:      %llx",
                     be64_to_cpu(*(tUINT64 *)&s->port->hca->I_PORT_ID[8]) );

    s->login_buff_len = sizeof (srp_login_req_t);
    header = (srp_login_req_t *) s->login_buff;

    /*
     * zero out header
     */
    memset (header, 0, sizeof (srp_login_req_t));
    header->opcode = SRP_LOGIN_REQ;
    header->tag = 0;
    header->request_I_T_IU = cpu_to_be32 (srp_cmd_pkt_size);
    memcpy (header->initiator_port_id, s->port->hca->I_PORT_ID, 16);
    memcpy (&header->target_port_id[0], &s->target->service_name, 8);
    memcpy (&header->target_port_id[8], s->target->ioc->guid, sizeof(tTS_IB_GUID));
   	header->req_buf_format.ddbd = 0;
	if (use_srp_indirect_addressing != 0)
    	header->req_buf_format.idbd = 1;

    return 0;
}


int
srp_build_command (srp_pkt_t * pkt,
                   Scsi_Cmnd * Cmnd,
                   srp_host_conn_t * s,
                   ioq_t *ioq)
{
    srp_cmd_t *cmd = (srp_cmd_t *) pkt->data;
    __u8 *p = (__u8 *) (cmd->lun);
    int status;

    /* build an SRP command */
    memset (cmd, 0, sizeof (srp_cmd_t));

    /* set indicate in the SRP packet if there is any data movement */
    if (Cmnd->request_bufflen) {
        if (Cmnd->sc_data_direction == SCSI_DATA_READ) {
			if (use_srp_indirect_addressing != 0) {
				cmd->difmt = 2; /* indirect table */
				/* cmd->dicount will be updated in the build_routine */
			} else {
            	cmd->difmt = 1;
            	cmd->dicount = 1;
			}
        }
        else if (Cmnd->sc_data_direction == SCSI_DATA_WRITE) {
			if (use_srp_indirect_addressing != 0) {
				cmd->dofmt = 2; /* indirect table */
				/* cmd->docount will be updated in the build_routine */
			} else {
            	cmd->dofmt = 1;
            	cmd->docount = 1;
			}
        }
        status = srp_build_rdma(pkt, Cmnd, s, ioq );
        if ( status ) {
            return (status);
        }
    }
    else {
        /* NO DATA MOVEMENT */
        cmd->dofmt = cmd->difmt = 0;
        pkt->scatter_gather_list.length = sizeof (srp_cmd_t);
    }

    /* copy the SCSI CDB into the SRP packet */
    memcpy (cmd->cdb, Cmnd->cmnd, Cmnd->cmd_len);

    /* clear SRP_CMD LUN field and set it */
    memset (p, 0, 8);
    p[1] = Cmnd->device->lun;

    /* set the SRP opcode */
    cmd->opcode = SRP_CMD;

    /* set the tag and increment it, make it modulo of max io queue size-2 ?? */
    cmd->tag = cpu_to_be64 ((uint64_t) (uintptr_t) ioq);
    return 0;
}


void
srp_host_iodone (Scsi_Cmnd *cmnd,
                 srp_resp_t *hdr,
                 ioq_t *ioq )
{
    srp_host_buf_t *sr_list;

    TS_REPORT_DATA( MOD_SRPTP, "io %p complete for target %d", ioq, cmnd->target);

    cmnd->result = (DID_OK << 16) | (hdr->status.bit.status & 0xff);

    if (( hdr->status.bit.diunder ) || ( hdr->status.bit.diover )){
        cmnd->resid = be32_to_cpu( hdr->data_in_residual_count );
    } else if (( hdr->status.bit.dounder ) || ( hdr->status.bit.doover )) {
        cmnd->resid = be32_to_cpu( hdr->data_out_residual_count );
    }

    if ( hdr->status.bit.status ) {
        TS_REPORT_STAGE( MOD_SRPTP, "status word %x", hdr->status.word );

        if ( status_byte( hdr->status.bit.status ) == CHECK_CONDITION ) {
            TS_REPORT_STAGE( MOD_SRPTP, "io %p check condition", ioq );

            memset( cmnd->sense_buffer, 0, sizeof( cmnd->sense_buffer ));

            if ( hdr->status.bit.snsvalid ) {
                int  srp_sense_size = be32_to_cpu( hdr->sense_len );
                char *sense_buffer;
                int  sense_size;
                int  i;

                if ( srp_sense_size < sizeof( cmnd->sense_buffer ) )
                    sense_size = srp_sense_size;
                else
                    sense_size = sizeof( cmnd->sense_buffer ) - 1;

                sense_buffer = (char *)hdr + sizeof( srp_resp_t ) + be32_to_cpu( hdr->response_len );

                for ( i = 0; i < sense_size; i+=4 ) {
                    TS_REPORT_STAGE( MOD_SRPTP, "sense_buffer[%d] %x", i, *((tUINT32 *)&sense_buffer[i]) );
                }
                TS_REPORT_STAGE( MOD_SRPTP, "io %p sense size %d", ioq, sense_size );

                memcpy( cmnd->sense_buffer, sense_buffer, sense_size );
                cmnd->SCp.Message = sense_size;
            }

        } else if ( hdr->status.word == 0x08000000 ) {
            /*
             * STATUS_BUSY is used by the SRP Target to indicate it has a resource problem
             */
            TS_REPORT_STAGE( MOD_SRPTP, "io %p status busy", ioq );

            cmnd->result = (DID_OK << 16) | (hdr->status.bit.status & 0xff);
        }
    }

    /* if a request_bufflen is present, then we know we had data movement,
     * we are going to need to free the response packet, and structures
     * to point to the host buffers */
    if (cmnd->request_bufflen) {
		uint32_t	sr_list_index=0;

        sr_list = ioq->sr_list;

		for (; sr_list_index < ioq->sr_list_length; sr_list_index++) {
	   		srptp_dereg_phys_host_buf( ioq->sr_list + sr_list_index );
		}

        kfree(sr_list);
    }

}

int
srp_build_indirect_data_buffer_descriptor(
	srp_pkt_t	*srp_pkt,
	struct scatterlist *sg_elements,
	int			sg_cnt,

	/*
	 * output parameters
	 */
	srp_host_buf_t	**sr_list_ptr,
	int			*sr_list_length)
{

	srp_cmd_t	*srp_cmd_frame = (srp_cmd_t *)srp_pkt->data;

	struct scatterlist	*sg_element;

	srp_cmd_indirect_data_buffer_descriptor_t
						*header =
		(srp_cmd_indirect_data_buffer_descriptor_t	*)
		(srp_cmd_frame + 1);

    srp_remote_buf_t	*curr_buff_descriptor =
		&header->partial_memory_descriptor_list[0];

	uint32_t				total_length = 0;

	dma_addr_t			curr_dma_addr, base_dma_addr;
	tUINT32				curr_registration_length = 0, curr_dma_length = 0;

	uint64_t			*dma_addr_list;
	tUINT32				dma_addr_index = 0;
    int                 status;

	srp_host_buf_t		*sr_list;

	TS_REPORT_DATA(MOD_SRPTP,"cmd 0x%p header 0x%p curr buff 0x%p",
		srp_cmd_frame, header, curr_buff_descriptor);

	/*
	 * allocate, assuming worst case, 1 registration per sg element
	 */
	sr_list = *sr_list_ptr = (srp_host_buf_t *)
		kmalloc (sizeof (srp_host_buf_t)*sg_cnt, GFP_ATOMIC);
    if (!sr_list) {
        return ( -ENOMEM );
    }

	sr_list->data = NULL;

    dma_addr_list = ( uint64_t *)
		kmalloc( sizeof(uint64_t)*((max_xfer_sectors_per_io*512/PAGE_SIZE) + 2),
			 GFP_ATOMIC );
    if( dma_addr_list == NULL ) {
		kfree(sr_list);
		*sr_list_ptr = NULL;
		return -ENOMEM;
	}

	dma_addr_index = 0;

	*sr_list_length = 0;

	/*
	 * register the sg elements.
	 * Try to minimize the number of registrations
	 */

	curr_dma_addr = sg_dma_address(&sg_elements[0]);

	for (sg_element = &sg_elements[0];
		 sg_element < &sg_elements[sg_cnt];
		 sg_element++) {

		TS_REPORT_DATA(MOD_SRPTP," addr 0x%lx length 0x%x",
                               (unsigned long)sg_dma_address(sg_element),sg_dma_len(sg_element));
		total_length += sg_dma_len(sg_element);
		curr_registration_length += sg_dma_len(sg_element);
		curr_dma_length += sg_dma_len(sg_element);

		/*
		 * This field is used as the suggested IOVA to the phys memory
		 * registration verb
		 */
		if (sr_list->data == NULL)
			sr_list->data = (char *)(uintptr_t)sg_dma_address(sg_element);

        /*
		 * Fill in the DMA addresses of this (DMA) contiguous region
		 * for physical memory registration later.
		 */
		base_dma_addr = curr_dma_addr;
		for (; ((curr_dma_addr & PAGE_MASK) <= ((base_dma_addr+curr_dma_length-1)&PAGE_MASK));
			   curr_dma_addr += PAGE_SIZE ) {

			dma_addr_list[dma_addr_index++] =
				curr_dma_addr & PAGE_MASK;

		}

		/*
		 * Register the current region
		 */
		sr_list->size = curr_registration_length;
		sr_list->r_addr = (uint64_t)(unsigned long)sr_list->data;

    	status = srptp_register_memory(
                   srp_pkt->conn,
				   sr_list, ((unsigned long)sr_list->data & (PAGE_SIZE-1)),
                   dma_addr_list,dma_addr_index );

        if ( status == -EAGAIN ) {

			for (sr_list = &((*sr_list_ptr)[0]);
				 sr_list < &((*sr_list_ptr)[*sr_list_length]);
				 sr_list++) {
				srptp_dereg_phys_host_buf(sr_list);
			}

			kfree(*sr_list_ptr);
			kfree(dma_addr_list);
			*sr_list_ptr = NULL;
			*sr_list_length = 0;
			return -ENOMEM;

		} else if ( status ) {
            TS_REPORT_FATAL( MOD_SRPTP, "FMR failure %d", status );
			kfree(*sr_list_ptr);
			kfree(dma_addr_list);
			*sr_list_ptr = NULL;
			*sr_list_length = 0;
			return -ENOMEM;
        }

		memset(curr_buff_descriptor,0,sizeof(srp_remote_buf_t));
		curr_buff_descriptor->r_data = cpu_to_be64 (sr_list->r_addr);
    	curr_buff_descriptor->r_size = cpu_to_be32 (sr_list->size);
		curr_buff_descriptor->r_key = cpu_to_be32 ((int) sr_list->r_key);

		/*
		 * Reset the dma_addr_list
		 */
		dma_addr_index = 0;
		curr_registration_length = 0;

		/*
		 * Update the sr_list
		 */
		sr_list++; curr_buff_descriptor++;
		sr_list->data = NULL;
		(*sr_list_length)++;

		/*
	 	 * Reset our current DMA region to the next (DMA) address region
	 	 * (if there is a next region)
	 	 */
		if ((sg_element+1) != &sg_elements[sg_cnt]) {
			curr_dma_addr = sg_dma_address(sg_element+1) ;
			curr_dma_length = 0;
		}
	}

	kfree(dma_addr_list);

	/*
	 * Fill up information about the indirect table.
	 * This could be used by the target to RDMA read in
	 * the indirect table.
	 */
	memset(header,0,sizeof(srp_cmd_indirect_data_buffer_descriptor_t));
	header->indirect_table_descriptor.r_data =
		cpu_to_be64((uint64_t)(unsigned long)
			&header->partial_memory_descriptor_list[0]);
	header->indirect_table_descriptor.r_key = cpu_to_be32(srp_pkt->r_key);
	header->indirect_table_descriptor.r_size =
		cpu_to_be32((*sr_list_length)*sizeof(srp_remote_buf_t));

	header->total_length = cpu_to_be32(total_length);

	if (srp_cmd_frame->dofmt)
		srp_cmd_frame->docount = (tUINT8)(*sr_list_length);
	else if (srp_cmd_frame->difmt)
		srp_cmd_frame->dicount = (tUINT8)(*sr_list_length);
	else
		srp_cmd_frame->dicount = srp_cmd_frame->docount = 0;

    srp_pkt->scatter_gather_list.length = sizeof (srp_cmd_t) +
			sizeof(srp_cmd_indirect_data_buffer_descriptor_t) +
			(*sr_list_length)*sizeof(srp_remote_buf_t);

	TS_REPORT_DATA(MOD_SRPTP,
		"tot length 0x%x dcount 0x%x", total_length,srp_cmd_frame->dicount);

	return 0;
}

int
srp_build_rdma (srp_pkt_t *srp_pkt,
                Scsi_Cmnd *cmnd,
                srp_host_conn_t *s,
                ioq_t *ioq)
{
    srp_remote_buf_t *srp_remote_buf;
    srp_host_buf_t *sr_list;
    struct scatterlist scatter_list;
    struct scatterlist *st_buffer;
    struct pci_dev *pdev = driver_params.pdev;
    int sg_cnt;
    int num_sg_elements;
    int offset, max_phys_pages, page_offset;
    uint64_t *phys_buffer_list;
    uint64_t new_phys_page, old_phys_page;
    int status, buf_len, num_phys_pages, old_buf_len;

    TS_REPORT_DATA ( MOD_SRPTP, "sg cnt = %d buffer %p phys %lx",
    	cmnd->use_sg, cmnd->request_buffer, virt_to_bus(cmnd->request_buffer));

    if (cmnd->use_sg) {
        st_buffer = (struct scatterlist *) cmnd->request_buffer;
		num_sg_elements = pci_map_sg(pdev,st_buffer,cmnd->use_sg,
			scsi_to_pci_dma_dir(cmnd->sc_data_direction));
    } else {
		memset(&scatter_list,0,sizeof(struct scatterlist));
        scatter_list.page = virt_to_page( cmnd->request_buffer );
        scatter_list.offset = ((unsigned long)cmnd->request_buffer & ~PAGE_MASK);
        scatter_list.length = cmnd->request_bufflen;
        st_buffer = &scatter_list;
		num_sg_elements =  /* 1 */
		pci_map_sg(pdev,st_buffer,1,scsi_to_pci_dma_dir(cmnd->sc_data_direction));
    }

	if (use_srp_indirect_addressing != 0) {

		return srp_build_indirect_data_buffer_descriptor(
			srp_pkt,st_buffer,num_sg_elements,
			&ioq->sr_list,&ioq->sr_list_length);

	}

    sr_list = (srp_host_buf_t *) kmalloc (sizeof (srp_host_buf_t), GFP_ATOMIC);
    if (!sr_list) {
        return ( -ENOMEM );
    }

//    sr_list->data = st_buffer[0].address;
	if (cmnd->use_sg == 0)
		sr_list->data = cmnd->request_buffer;
	else
    	sr_list->data = (void *)(uintptr_t)sg_dma_address(&st_buffer[0]);
    sr_list->r_addr = (uintptr_t)sr_list->data;
    sr_list->size = (uint32_t) cmnd->request_bufflen;

    /*
     * compute the number of physical pages
     * 1) compute the number of whole pages
     * 2) if there is an offset in the first page, add page, then
     *    check if we need to add a page for the remainder for the last page.
     * 3) even if there isn't an offset. the buffer may still have a remainder,
     *    in that case add a page for the remainder in the last page.
     */
    max_phys_pages = cmnd->request_bufflen / PAGE_SIZE;
    page_offset = (uint32_t)sg_dma_address(&st_buffer[0]) & ( PAGE_SIZE - 1 );
    if ( page_offset ) {
        max_phys_pages++;
        if ( ( PAGE_SIZE - page_offset ) < ( cmnd->request_bufflen % PAGE_SIZE ) )
            max_phys_pages++;
    } else if ( cmnd->request_bufflen % PAGE_SIZE ) {
        max_phys_pages++;
    }

    phys_buffer_list = ( uint64_t *)kmalloc( sizeof(uint64_t)*max_phys_pages, GFP_ATOMIC );
    if( phys_buffer_list == NULL ) {
        TS_REPORT_WARN( MOD_SRPTP, "phys buffer list allocation failed" );
        return ( -ENOMEM );
    }

    old_phys_page = 0;
    old_buf_len = 0;
    num_phys_pages = 0;

    /*
     * Next part of the code involves converting the scatter list given
     * by the scsi stack into a list of physical pages to give to ts_api
     * for fast memory registration.  There are 4 cases we are concerned
     * with:
     *  1. The scsi stack will give sequential buffers that are contigous
     *     and are less than a single page.  These type of buffers need to
     *     be coalesced into a single page to be given to ts_api.  This
     *     is typical on doing reads and scsi gives a scatter list with
     *     buffers that are less than a PAGE.
     *  2. The scsi statck will give sequential buffers that are the same.
     *     They have the same va and pa.  In this case we do not want to
     *     coalesce the buffers.  This is the case when you are doing
     *     writes from /dev/zero to your drive.
     *  3. The scsi stack gives you sequential buffers that are less than
     *     a page and are not contigous.  THIS WOULD BE A PROBLEM.  We
     *     need to trap for it.
     *  4. If the buffer is greater than a page then we want to assume
     *     the buffer is physically contigous and add those pages as
     *     well.
     */
    for ( sg_cnt = 0; sg_cnt < num_sg_elements; sg_cnt++ ) {

        new_phys_page = (uint32_t)sg_dma_address(&st_buffer[sg_cnt]);
        buf_len = sg_dma_len(&st_buffer[sg_cnt]);

        TS_REPORT_DATA( MOD_SRPTP, "virtual[%x] %llx len %x", sg_cnt,new_phys_page,buf_len);
	}

    for ( sg_cnt = 0; sg_cnt < num_sg_elements; sg_cnt++ ) {

        new_phys_page = (uint32_t)sg_dma_address(&st_buffer[sg_cnt]);
        buf_len = sg_dma_len(&st_buffer[sg_cnt]);

        TS_REPORT_DATA( MOD_SRPTP, "virtual[%x] %llx len %x", sg_cnt,new_phys_page,buf_len);

        if ( buf_len > PAGE_SIZE ) {
            int pages_in_buf;
            int pg_cnt;

            /*
             * The following are checks to see if the buffer is aligned at the start and the
             * end.  We only allow exceptions if the buffer is the first or the last.
             */
            if (( new_phys_page & ( PAGE_SIZE-1 ) ) && ( sg_cnt != 0 )) {
                TS_REPORT_FATAL( MOD_SRPTP, "buffer larger than 4k and has offset and not first");
                return( -EINVAL );
            }
            if ((((new_phys_page + buf_len) & (PAGE_SIZE-1) ) != 0 ) && ( sg_cnt != (num_sg_elements-1) )) {
				TS_REPORT_FATAL(MOD_SRPTP,"buf len 0x%x new_phys_page 0x%x", buf_len,new_phys_page);
				TS_REPORT_FATAL(MOD_SRPTP,"sg cnt %d num_sg_elements %d", sg_cnt, num_sg_elements);
				TS_REPORT_FATAL(MOD_SRPTP,"total length 0x%x", cmnd->request_bufflen);
                TS_REPORT_FATAL( MOD_SRPTP, "buffer larger than 4k and not aligned at end and not last");
				TS_REPORT_FATAL(MOD_SRPTP,"next addr 0x%x len 0x%x",
					(uint32_t)(sg_dma_address(&st_buffer[sg_cnt+1])),sg_dma_len(&st_buffer[sg_cnt+1]));
                return( -EINVAL );
            }

            pages_in_buf = buf_len / PAGE_SIZE;
            if ( buf_len % PAGE_SIZE )
                pages_in_buf++;

            for( pg_cnt = 0; pg_cnt < pages_in_buf; pg_cnt++ ) {
                phys_buffer_list[num_phys_pages] = new_phys_page & PAGE_MASK;
                TS_REPORT_DATA( MOD_SRPTP, "physical[%x], phys page %llx",
                                num_phys_pages, phys_buffer_list[num_phys_pages]);
                new_phys_page += PAGE_SIZE;
                num_phys_pages++;
            }

            /* back up the new_phys_page from the loop for the last entry */
            new_phys_page -= PAGE_SIZE;

        } else {

            /* check the page is aligned */
            if( new_phys_page & ( PAGE_SIZE-1 ) ) {

                /* there is an offset, we may need to coalesce the page
                   check if we are contigous with the previous page */
                if (( old_phys_page + old_buf_len ) == new_phys_page ) {
                    /* we are contigous, let's colaesce */
                } else {
                    /* Not coalescing the pages, this may occur on the first page,
                     * if it doesn't then we may have a hole in the map and we check for
                     * it below by seeing if the buffer with the offset is within
                     * same page as the previous one. */
                    if( (old_phys_page & PAGE_MASK ) == ( new_phys_page & PAGE_MASK )) {
                        TS_REPORT_FATAL( MOD_SRPTP, "illegal pages, we have a hole in the map" );
                        return( -EINVAL );
                    }

                    /* add the page */
                    phys_buffer_list[num_phys_pages] = new_phys_page & PAGE_MASK;
                    TS_REPORT_DATA( MOD_SRPTP, "physical[%x], phys page %llx",
                                    num_phys_pages, phys_buffer_list[num_phys_pages]);
                    num_phys_pages++;
                }
            } else {
                /* we have no offset, add the page */
                phys_buffer_list[num_phys_pages] = new_phys_page & PAGE_MASK;
                TS_REPORT_DATA( MOD_SRPTP, "physical[%x], phys page %llx",
                                num_phys_pages, phys_buffer_list[num_phys_pages]);
                num_phys_pages++;
            }

        }

        /* the old phys page needs to be the page with the offset for
         * doing the contingous calculation above */
        old_phys_page = new_phys_page;
        old_buf_len = buf_len;
    }

    /* get the offset of the virtual address */
    offset = (uintptr_t)(sr_list->data) & (PAGE_SIZE-1);

    if ( num_phys_pages > max_phys_pages ) {
        TS_REPORT_FATAL( MOD_SRPTP, "physical pages (%d) > number allocated (%d)",
			max_phys_pages,num_phys_pages );
        return( -EINVAL );
    }

    /* do fast memory registration */
    status = srptp_register_memory ( s,
                                       sr_list,
                                       offset,
                                       phys_buffer_list,
                                       num_phys_pages );

    kfree( phys_buffer_list );

    if ( status ) {
        kfree( sr_list );
        return( status );
    }

    ioq->sr_list = sr_list;
	ioq->sr_list_length = 1;

    srp_pkt->scatter_gather_list.length = sizeof (srp_cmd_t) + sizeof(srp_remote_buf_t);
    srp_remote_buf = (void *) srp_pkt->data + sizeof (srp_cmd_t);
    memset (srp_remote_buf, 0x00, sizeof (srp_remote_buf_t));

    srp_remote_buf->r_data = cpu_to_be64 (sr_list->r_addr);
    srp_remote_buf->r_size = cpu_to_be32 (sr_list->size);
    srp_remote_buf->r_key = cpu_to_be32 ((int) sr_list->r_key);

    return ( 0 );
}


const char *
srp_host_info (struct Scsi_Host *SChost)
{
    return ("SRP HOST  Adapter: $Id: srp_host.c 93 2004-04-28 18:41:35Z roland $");
}


void srp_send_done ( int pkt_index, srp_target_t *target )
{
    srp_pkt_t *send_pkt;

    send_pkt = srp_host_pkt_index_to_ptr( pkt_index, target );

    if ( send_pkt == NULL ) {
        TS_REPORT_FATAL( MOD_SRPTP, "Bad pkt_index %d", pkt_index );
        return;
    }

    srp_host_free_pkt( send_pkt );
}


int
srp_path_record_completion(
    tTS_IB_CLIENT_QUERY_TID tid,
    int 		            status,
    tTS_IB_PATH_RECORD      path_record,
    int 			        remaining,
    void 			        *context)
{
	tTS_IB_PATH_RECORD	redirected_path_record;
    srp_host_conn_t 	*s = (srp_host_conn_t *) context;
    srp_host_port_params_t *port = s->port;

    s->path_record_tid = 0;

	if ( (status == 0) && (path_record != NULL) ){

        TS_REPORT_WARN(MOD_SRPTP,"Redirecting for target %d", s->target->target_index );

        redirected_path_record = &s->path_record;

        memcpy( redirected_path_record,
                path_record,
				sizeof(tTS_IB_PATH_RECORD_STRUCT));

		redirected_path_record->slid = port->slid;

        if ( tid != TS_IB_CLIENT_QUERY_TID_INVALID) {
	        /* inidicates this did not come out of the cache,
             * so go ahead and update the cache */
            srp_update_cache( path_record, port );
        }

   		s->state = SRP_HOST_LOGIN_INPROGRESS;
        s->redirected = TRUE;
       	srptp_connect ( s,
                        redirected_path_record,
                        (__u8 *) s->login_buff,
                        s->login_buff_len );
	} else  {

		TS_REPORT_WARN(MOD_SRPTP,
			"Path record err code %d for target %d hca %d port %d",
			status, s->target->target_index,
            port->hca->hca_index+1, port->local_port );

        s->state = SRP_HOST_GET_PATH_RECORD;

		s->path_record_retry_count++;

    	if ( ( s->path_record_retry_count < SRP_HOST_PATH_RECORD_RETRIES )  &&
			 ( status = srp_get_path_record( s->redirected_port_gid,
                                             port,
                                             s->path_record_retry_count,
                                             &s->path_record_tid,
                                             srp_path_record_completion,
                                             s ) ) ) {

            TS_REPORT_WARN( MOD_SRPTP, "Target %d, Path Record retries exceeded %d",
                            s->target->target_index, s->path_record_retry_count );

            remove_connection( s, TARGET_POTENTIAL_CONNECTION );

            pick_connection_path( s->target );
        }
	}

	return 0;
}


int
srp_host_connect_done ( srp_host_conn_t *s,
                        int status )
{
    srp_host_port_params_t *port = s->port;
    srp_target_t *target = s->target;
    int err_code;

	switch (status) {

   	case SRPTP_SUCCESS:

        if (s->state == SRP_HOST_LOGIN_INPROGRESS) {
            unsigned char opcode;

            opcode = s->login_resp_data[0];

            if (opcode == SRP_LOGIN_RESP) {

    			s->state = SRP_UP;
			    driver_params.num_active_connections++;
			    driver_params.num_pending_connections--;

                srp_host_login_resp (s, s->login_resp_data );

				target->state = TARGET_ACTIVE_CONNECTION;

                srp_host_pre_post( target, s );

                srp_flush_pending_to_active( target );

            } else if ( opcode == SRP_LOGIN_RJT ) {

                srp_host_login_rej( s->login_resp_data );

                remove_connection( s, TARGET_POTENTIAL_CONNECTION );

                srp_host_close_conn( s );

                srp_dm_kill_ioc( target, s->redirected );

                pick_connection_path( target );

            } else {
                TS_REPORT_WARN( MOD_SRPTP, "Illegal SRP opcode 0x%x for login response", opcode );
            }

        } else {
            TS_REPORT_WARN(MOD_SRPTP, "connection %p is in wrong state %x", s, s->state );

            remove_connection( s, TARGET_NO_CONNECTIONS );
        }

        break;


	case SRPTP_RETRY_REDIRECT_CONNECTION :

        memcpy(s->redirected_port_gid, s->login_resp_data ,16);
		s->path_record_retry_count = 0;
		s->state = SRP_HOST_GET_PATH_RECORD;
        s->path_record_tid = 0;

        err_code = srp_get_path_record( s->redirected_port_gid,
                                        s->port,
                                        s->path_record_retry_count,
                                        &s->path_record_tid,
                                        srp_path_record_completion,
                                        s );

        if ( err_code ) {

            remove_connection( s, TARGET_POTENTIAL_CONNECTION );

            srp_dm_kill_ioc( s->target, s->redirected );

            pick_connection_path( s->target );
        }
	    break;



    case SRPTP_HARD_REJECT:
        TS_REPORT_WARN( MOD_SRPTP, "SRP Target rejected for target %d, redirect %d",
                        target->target_index, s->redirected );

        target->hard_reject = TRUE;

        break;



	case SRPTP_RETRY_STALE_CONNECTION:
	default:
        if ( ++(s->retry_count) >= MAX_CONNECTION_RETRIES ) {

     	    TS_REPORT_WARN(MOD_SRPTP,
        	    "Conn retry count exceeded for target %d on hca %d, port %d",
			    s->target->target_index, port->hca->hca_index+1,port->local_port);

            remove_connection( s, TARGET_POTENTIAL_CONNECTION );

            srp_host_close_conn(s);

            /*
             * Exceeded the retries to make this connection to
             * the redirected path.  Try a new path/IOC.
             */
            srp_dm_kill_ioc( target, s->redirected );

            pick_connection_path( target );

        } else {
     	    TS_REPORT_WARN(MOD_SRPTP,
        	    "Conn problem target %d hca %d port %d, retrying",
			    s->target->target_index, port->hca->hca_index+1,port->local_port);

            /*
             * Re-try connection to the same path record
             */
            s->state = SRP_HOST_LOGIN_INPROGRESS;

            srptp_connect ( s,
                            &s->path_record,
                            (__u8 *) s->login_buff,
                            s->login_buff_len );
		}
        break;
	}

    return (0);
}


int
srp_host_close_conn( srp_host_conn_t *s )
{
    srp_target_t *target = s->target;
    unsigned long cpu_flags;

    if ( s->state == SRP_HOST_LOGOUT_INPROGRESS ) {

        TS_REPORT_STAGE ( MOD_SRPTP, "Connection %p on target %d closed",
                          s, target->target_index );

        driver_params.num_connections--;

        spin_lock_irqsave( &target->spin_lock, cpu_flags );

        target->conn_count--;
        list_del( &s->conn_list );

        spin_unlock_irqrestore( &target->spin_lock, cpu_flags );

        kfree( s );

    } else if ( s->state == SRP_HOST_LOGIN_INPROGRESS ) {
        srp_host_port_params_t *port = s->port;

   		TS_REPORT_WARN(MOD_SRPTP,
            "Login failed for target %d through hca %d port %d",
			target->target_index,port->hca->hca_index+1,port->local_port);

        spin_lock_irqsave( &target->spin_lock, cpu_flags );

        target->port->num_connections--;
        target->ioc->num_connections--;

        target->state = TARGET_POTENTIAL_CONNECTION;
        target->timeout = jiffies + connection_timeout;
        target->active_conn = NULL;

        target->conn_count--;
        list_del( &s->conn_list );

        spin_unlock_irqrestore( &target->spin_lock, cpu_flags );

        kfree( s );

        driver_params.num_pending_connections--;
        driver_params.num_connections--;

        srp_move_to_pending( target );

        initialize_connection( target );

    } else {
    	TS_REPORT_WARN( MOD_SRPTP,
			"Close conn for target %d in state %x",
			target->target_index, s->state );
    }

    return( 0 );
}

/*
 * Callback from the completion handle to indicate we need
 * to kill a connection.  We maybe in interrupt context when
 * we get this so, set a flag and wakeup the thread
 */
void srp_host_completion_error( int pkt_index, srp_target_t *target )
{
    unsigned long cpu_flags;
    srp_pkt_t *pkt;
    srp_host_conn_t *s;

    pkt = srp_host_pkt_index_to_ptr( pkt_index, target );
    s = pkt->conn;

    spin_lock_irqsave( &target->spin_lock, cpu_flags );
    if (( target->state == TARGET_ACTIVE_CONNECTION ) &&
        ( s == target->active_conn )){

        TS_REPORT_STAGE( MOD_SRPTP, "Completeion error...start disconnect on target %d",
                         target->target_index );

        target->state = TARGET_POTENTIAL_CONNECTION;
        target->need_disconnect = TRUE;
    }
    spin_unlock_irqrestore( &target->spin_lock, cpu_flags );

    srp_host_free_pkt( pkt );

    wake_up_interruptible(&dm_poll_thread_wait_queue);
    return;
}

/*
 * Callback in "TIMEWAIT" state. This is the first stage of the disconnect
 * done callbacks. If we did not initiate the disconnect, simply retry
 * the connection. If we initiated the disconnect, wait for the second
 * callback i.e srp_host_close_conn to continue processing.
 */
int
srp_host_disconnect_done( srp_host_conn_t *s, int status )
{
    if (s->state != SRP_HOST_LOGOUT_INPROGRESS) {
    	TS_REPORT_STAGE( MOD_SRPTP, "Target %d disconnected us %p in state %x",
                         s->target->target_index, s, s->state );

        remove_connection( s, TARGET_POTENTIAL_CONNECTION );

        srp_move_to_pending( s->target );

        initialize_connection( s->target );

    } else {
		TS_REPORT_STAGE(MOD_SRPTP,
			"Ignoring disconnect done for target %d in state %x",
			s->target->target_index, s->state );
	}
    return(0);
}


static void
srp_host_module_cleanup (void)
{
    srp_host_conn_t *s;
    srp_target_t *target;
    unsigned long shutdown_timeout;

    srp_dm_unload();

	driver_params.dm_shutdown = 1;
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(0);

    /* First unregister, the scsi driver, set a flag to indicate
     * to the abort code to complete aborts immediately */
    scsi_unload_in_progress = TRUE;

	tsKernelThreadStop(driver_params.thread);

    scsi_remove_host( driver_params.host );

    for ( target = &srp_targets[0];
          target < &srp_targets[max_srp_targets];
          target++ ) {

		if (target->active_conn) {

            s = target->active_conn;

            TS_REPORT_STAGE(MOD_SRPTP,
				"Target %d active conn thru hca %d port %d pending count %d queued count %d",
				target->target_index,s->port->hca->hca_index+1,s->port->local_port,
				target->pending_count,
				target->active_count
			);

            remove_connection( s, TARGET_NO_CONNECTIONS );
        }
    }

    /*
     * Poll until all the connections are zero
     * or the timeout has occurred
     */
    shutdown_timeout = jiffies + SRP_SHUTDOWN_TIMEOUT;
    while  (( driver_params.num_connections != 0 ) &&
            ( time_after(shutdown_timeout, jiffies) ) ) {
	    set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout(1*HZ);
    }

    if ( driver_params.num_connections ) {
        TS_REPORT_WARN( MOD_SRPTP, "Hanging connections %d",
                        driver_params.num_connections );
    }

    srp_host_cleanup_targets();

    srptp_cleanup_module();

    scsi_host_put( driver_params.host );

    TS_REPORT_WARN( MOD_SRPTP, "SRP Driver unloaded" );
}


/*
 *  Function defination
 */
module_init (srp_host_init);
module_exit (srp_host_module_cleanup);
