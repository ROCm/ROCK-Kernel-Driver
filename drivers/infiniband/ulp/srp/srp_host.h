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

  $Id: srp_host.h 35 2004-04-09 05:34:32Z roland $
*/

#ifndef _SRP_HOST_H
#define _SRP_HOST_H


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kdev_t.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>

#include <linux/byteorder/generic.h>

#if !defined(LINUX_VERSION_CODE)
#include <linux/version.h>
#endif

#include <asm/processor.h>
#include <asm/scatterlist.h>

#include <net/sock.h>
#include <linux/pci.h>
#include <scsi.h>
#include <hosts.h>
#include "ib_legacy_types.h"
#include "ts_kernel_trace.h"
#include "ts_kernel_thread.h"
#include "ts_kernel_uintptr.h"
#include "ts_ib_core_types.h"
#include "ts_ib_core.h"
#include "ts_ib_dm_client_host.h"
#include "ts_ib_sa_client.h"
#include "ts_ib_cm_types.h"

#include "srp_cmd.h"

#include "srptp.h"

extern int max_luns;
extern int max_srp_targets;
extern int max_cmds_per_lun;
extern int use_srp_indirect_addressing;
extern int max_xfer_sectors_per_io;
extern int fmr_cache;

#define MAX_IO_QUEUE	1024

#define MAX_IOS_PER_TARGET 64
#define MAX_PREPOST_RECVS 3
#define MAX_SEND_WQES (MAX_IOS_PER_TARGET*2)
#define MAX_RECV_WQES (MAX_SEND_WQES+MAX_PREPOST_RECVS)

#define MAX_HCAS 2
#define MAX_LOCAL_PORTS_PER_HCA 2
#define MAX_LOCAL_PORTS MAX_HCAS*MAX_LOCAL_PORTS_PER_HCA
#define MAX_IOCS 16
#define MAX_ASYNC_EVENT_HANDLES 5

#define IOQ_CMD_DELTA_TIMEOUT            2*HZ
#define IOQ_ABORT_TIMEOUT                10*HZ
#define TARGET_RESOURCE_LIST_TIMEOUT     10*HZ
#define HCA_RESOURCE_LIST_TIMEOUT        4*HZ
#define SRP_DM_THREAD_TIMEOUT            HZ
#define TARGET_POTENTIAL_STARTUP_TIMEOUT 7*HZ
#define TARGET_POTENTIAL_TIMEOUT         30*HZ
#define TARGET_NO_PATHS_TIMEOUT          10*HZ
#define SRP_SHUTDOWN_TIMEOUT             300*HZ
#define DM_FILTER_TIMEOUT                5*HZ
#define IB_DISCOVERY_TIMEOUT             60
#define PORT_QUERY_TIMEOUT               5*HZ
#define PATH_RECORD_TIMEOUT              5*HZ

#define MAX_CONNECTION_RETRIES 3
#define MAX_ACTIVE_LIST_RETRIES 1
#define MAX_QUERY_RETRIES 3
#define MAX_PATH_RECORD_RETRIES 3
#define MAX_DM_RETRIES 3
#define SRP_HOST_PATH_RECORD_RETRIES 5
#define MAX_HARD_REJECT_COUNT 3

typedef
struct _srp_host_port_params {

    struct _srp_host_hca_params *hca;

    int valid;

    int index;

    int num_connections;

    tTS_IB_PORT local_port;

    tTS_IB_GID  local_gid;

	tTS_IB_LID	slid;

	tTS_IB_PORT_STATE	port_state;

    int dm_query_in_progress;

    int dm_retry_count;

    int dm_need_retry;

    int dm_need_query;

    tTS_IB_DM_CLIENT_HOST_TID dm_xid;

    tTS_IB_CLIENT_QUERY_TID out_of_service_xid;

    tTS_IB_CLIENT_QUERY_TID in_service_xid;

} srp_host_port_params_t;

/*
 * Local HCA Parameters
 */
typedef
struct _srp_host_hca_params {

    int valid;

    int hca_index;

	/* channel adapter handle */
	tTS_IB_DEVICE_HANDLE ca_hndl;

	/* protection domain handle */
	tTS_IB_PD_HANDLE pd_hndl;

    tUINT8 I_PORT_ID[16];

    tTS_IB_FMR_POOL_HANDLE fmr_pool;

    /* send completion queue handle */
    tTS_IB_CQ_HANDLE cqs_hndl;

    /* receive completion queue handle */
    tTS_IB_CQ_HANDLE cqr_hndl;

    /* number of connections on this hca */
    int num_connections;

    spinlock_t 			spin_lock;

    /*
     * I/Os are queued here for resources that run out
     * on an HCA basis
     */
    int    resource_count;
	struct list_head resource_list;

    struct _srp_host_port_params port[MAX_LOCAL_PORTS_PER_HCA];

    tTS_IB_ASYNC_EVENT_HANDLER_HANDLE async_handles[MAX_ASYNC_EVENT_HANDLES];

} srp_host_hca_params_t;

/*
 * Driver parameters
 */
typedef
struct _srp_host_driver_params {

    struct Scsi_Host *host;

    tTS_KERNEL_THREAD thread;

	tUINT8 I_PORT_ID[16];

    int dm_shutdown;

    int need_refresh;

    int done_count;
    struct list_head done_list;
    spinlock_t spin_lock;

    int query_count;

    struct semaphore sema;

    int num_active_local_ports;

    int dm_active;

    int port_query;

    int num_connections;

    int num_active_connections;

    int num_pending_connections;

    struct list_head query_list;

    struct pci_dev *pdev;

} srp_host_driver_params_t;


typedef
struct _ioc_entry {

    int valid;

    tTS_IB_GUID guid;

    tTS_IB_LID lid;

    tTS_IB_PATH_RECORD_STRUCT iou_path_record[MAX_HCAS][MAX_LOCAL_PORTS_PER_HCA];

    int path_valid[MAX_HCAS][MAX_LOCAL_PORTS_PER_HCA];

    int num_connections;

} ioc_entry_t;


typedef struct _srp_host_buf_t {
    /*
     * mirrors Linux's scatterlist
     */
    char *data;                 /* buffer virtual address */
    char *reserved;             /* unused */
    unsigned int size;          /* size of buffer */
    /*
     * ---------------------------
     */
    uint64_t r_addr;            /* RDMA buffer address to be used by the
                                 * target */
    tTS_IB_RKEY r_key;
    tTS_IB_MR_HANDLE mr_hndl;       /* buffer's memory handle */
} srp_host_buf_t;


typedef struct _srp_pkt_t {
    struct _srp_pkt_t *next;
    struct srp_host_connection_s *conn;
    struct _srp_target_t *target;
    int pkt_index;
    int in_use;
    char *data;
    tTS_IB_GATHER_SCATTER_STRUCT scatter_gather_list;
    tTS_IB_RKEY r_key;
} srp_pkt_t;


typedef enum _ioq_type_t {
    IOQ_COMMAND = 0,
    IOQ_ABORT,
    IOQ_INTERNAL_ABORT
}ioq_type_t;

typedef enum _queue_type_t {
    QUEUE_ACTIVE = 0,
    QUEUE_HCA,
    QUEUE_TARGET,
    QUEUE_PENDING,
    QUEUE_TEMP
} queue_type_t;

typedef struct ioq_s {
    Scsi_Cmnd *req;
    Scsi_Cmnd *req_temp;

    srp_host_buf_t *sr_list;

	/*
	 * Indirect addressing :
	 * The sr_list becomes an array of srp_host_buf_t elements.
	 * There is one entry per region being registered.
	 */
	int				    sr_list_length; /* number of elements in the array */
    srp_pkt_t           *pkt;
    srp_pkt_t           *recv_pkt;
    ioq_type_t          type;
    queue_type_t        queue_type;

    struct list_head	active_list;
    struct list_head	pending_list;
    struct list_head    target_resource_list;
    struct list_head    hca_resource_list;
    struct list_head    done_list;
    struct list_head    temp_list;

    unsigned long       timeout;
    int                 active_retries;

	struct _srp_target_t        *target;
} ioq_t;


/*
 * SRP Connection State
 * The ordering of the integer values of the states is relevant.
 */
typedef enum _connection_state_t {
    SRP_HOST_LOGIN_INPROGRESS = 1,           /* Waiting for Login to finish */
    SRP_HOST_GET_PATH_RECORD,
    SRP_UP,                                  /* after successful login */
	SRP_HOST_RETRY_STALE_CONNECTION,
    SRP_HOST_LOGIN_TIMEOUT,                  /* Waiting for Login to finish */
    SRP_HOST_LOGOUT_INPROGRESS,              /* Waiting for Login to finish */
    SRP_DOWN,                                /* login fail  */
    SRP_HOST_DISCONNECT_NEEDED
} connection_state_t;

/*
 *	SRP Connection related information
 */
typedef struct srp_host_connection_s {

    struct list_head conn_list;

    connection_state_t state;

    int              redirected;

    uintptr_t        srptp_conn_hndl;

    atomic_t 		 request_limit;

    atomic_t 		 local_request_limit;

    atomic_t 		 recv_post;

    int 			 retry_count;

    uint8_t 		 login_buff[256];

    int 			 login_buff_len;

    uint8_t          login_resp_data[256];

    int              login_resp_len;

	uint8_t			 redirected_port_gid[16];

    tTS_IB_CM_COMM_ID comm_id;

    tTS_IB_QP_HANDLE  qp_hndl;

    tTS_IB_QPN        qpn;

    /* send completion queue handle */
    tTS_IB_CQ_HANDLE cqs_hndl;

    /* receive completion queue handle */
    tTS_IB_CQ_HANDLE cqr_hndl;

    /*
	 * Path Record of the port to which we have been redirected to.
	 */
	tTS_IB_PATH_RECORD_STRUCT	path_record;
    tTS_IB_CLIENT_QUERY_TID     path_record_tid;
	int                         path_record_retry_count;

	/*
	 * Data Buffers Mgmt
	 */
	struct list_head data_buffers_free_list;
	uint8_t 		*data_buffers_vaddr;
	tTS_IB_RKEY r_key;   /* R_Key to be used by the target */
	tTS_IB_LKEY l_key;
	tTS_IB_MR_HANDLE mr_hndl;       /* buffer's memory handle */

	struct _srp_target_t	*target;
    struct _srp_host_port_params *port;
} srp_host_conn_t;


typedef enum _srp_target_state_t {
    TARGET_NOT_INITIALIZED = 0,
    TARGET_INITIALIZED,
    TARGET_NO_CONNECTIONS,
    TARGET_NO_PATHS,
    TARGET_POTENTIAL_CONNECTION,
    TARGET_ACTIVE_CONNECTION
}srp_target_state_t;


typedef struct _srp_target_t {

	/*
	 * index within the array of targets we maintain.
	 * This is also the T part of the BTL as seen by the
	 * SCSI stack.
	 */
	int 				target_index;

    int                 need_disconnect;

    int                 need_device_reset;

    int                 hard_reject;

    int                 hard_reject_count;

    int                 valid;

    srp_target_state_t  state;

    unsigned long       timeout;

    spinlock_t 			spin_lock;

    struct semaphore    sema;

	srp_host_conn_t		*active_conn;
    int                 conn_count;
    struct list_head	conn_list;

    /* conn info */
    srp_pkt_t **rcv_pkt_array;

    /* free list for SRP packet structures */
    srp_pkt_t *srp_pkt_free_list;

    /* memory area for SRP packet structures */
    srp_pkt_t *srp_pkt_hdr_area;

    /* memory area for SRP packet payloads */
    uint8_t *srp_pkt_data_area;

    tTS_IB_RKEY r_key[MAX_HCAS];

    tTS_IB_LKEY l_key[MAX_HCAS];

    int max_num_pkts;

    atomic_t free_pkt_counter;

    /* memory handle obtained by registering the packet payload memory area */
    tTS_IB_MR_HANDLE srp_pkt_data_mhndl[MAX_HCAS];

    /* send completion queue handle */
    tTS_IB_CQ_HANDLE cqs_hndl[MAX_HCAS];

    /* receive completion queue handle */
    tTS_IB_CQ_HANDLE cqr_hndl[MAX_HCAS];

    /*
     * IB pathing information
     */
    tUINT64 			   service_name;
    srp_host_port_params_t *port;
    ioc_entry_t            *ioc;

    /*
     * ioc mask inidicates which IOCs, this target/service
     * is visible on.  ioc_needs_request indicates that the
     * ioc paths may have been recently refreshed by the
     * DM
     */
    int ioc_mask[MAX_IOCS];
    int ioc_needs_request[MAX_IOCS];

    /*
     * Active queue - any io given to hardware
     * pending queue - io not given to hardware
     * resource queue - io that could not be given to hardware
     *                  because there were not srp_pkts
     *
     * Each queue will have a corresponding counter.
     */
    int                 active_count;
    int                 pending_count;
    int                 resource_count;
    struct list_head	active_list;
	struct list_head	pending_list;
	struct list_head	resource_list;

	/*
	 * Counters
	 */
	int64_t 			ios_processed;

} srp_target_t ;



typedef enum _srp_query_state_t {
    QUERY_PATH_RECORD_LOOKUP = 1,
    QUERY_PORT_INFO
}srp_query_state_t;


typedef
struct _srp_query_entry {

    struct list_head	           list;

    tTS_IB_GID                     remote_gid;

    tTS_IB_LID                     remote_lid;

    srp_host_port_params_t         *port;

    int                            retry;

    int                            need_retry;

    srp_query_state_t              state;

    int                            id;

    struct timer_list              timer;

    tTS_IB_CLIENT_QUERY_TID        xid;

    tTS_IB_DM_CLIENT_HOST_TID      dm_xid;

} srp_query_entry_t;


/* global vars */
extern int srp_tracelevel;

extern srp_target_t *srp_targets;

extern srp_host_driver_params_t driver_params;

extern srp_host_hca_params_t hca_params[];

extern int srp_cmd_pkt_size;

extern int max_ios_per_conn;

/* srp_host func */
extern int srp_host_alloc_pkts( srp_target_t *target );

extern void srp_fmr_flush_function( tTS_IB_FMR_POOL_HANDLE fmr_pool, void *flush_arg );

extern tUINT32 parse_parameters( char *parameters );

extern tUINT32 parse_target_binding_parameters( char *parameters );

extern int StringToHex64(char *,uint64_t *);

extern int srp_host_disconnect_done( srp_host_conn_t *conn, int status );

extern int srp_host_close_conn ( srp_host_conn_t *conn );

extern void srp_recv( int index, srp_target_t *target );

extern void srp_send_done ( int index, srp_target_t *target );

extern int srp_host_connect_done ( srp_host_conn_t *conn,
                                   int );

extern srp_host_conn_t *srp_host_find_conn( srp_target_t *target, tTS_IB_CM_COMM_ID comm_id );

extern void srp_host_completion_error( int pkt_index, srp_target_t *target );

void initialize_connection( srp_target_t *target );

void remove_connection( srp_host_conn_t *s, srp_target_state_t target_state );

extern void srp_host_free_pkt_index( int index, srp_target_t *target );

/* srp_dm functions */

int srp_get_path_record( tTS_IB_GID find_gid,
                         srp_host_port_params_t *port,
                         int retry_count,
                         tTS_IB_CLIENT_QUERY_TID *tid,
                         tTS_IB_PATH_RECORD_COMPLETION_FUNC completion_function,
                         void *completion_arg );

void srp_update_cache( tTS_IB_PATH_RECORD path_record, srp_host_port_params_t *port );

void srp_port_query_cancel( srp_host_port_params_t *port );

void srp_register_out_of_service( srp_host_port_params_t *port, int flag );

void srp_register_in_service( srp_host_port_params_t *port, int flag );

void pick_connection_path( srp_target_t *target );

int srp_dm_query( srp_host_port_params_t *port );

void srp_dm_kill_ioc( srp_target_t *target, int flag );

int srp_dm_init(void);

void srp_dm_unload(void);

/* srptp */
int srptp_connect ( srp_host_conn_t *conn,
                    tTS_IB_PATH_RECORD path_record,
                    char *srp_login_req,
                    int srp_login_req_len );

int srptp_disconnect ( srp_host_conn_t *conn );

/*
 * Called by a host SRP driver to register a buffer on the host.
 * IN: host buffer
 */
int srptp_register_memory ( srp_host_conn_t *conn,
                            srp_host_buf_t *buf,
                            tUINT32 offset,
                            uint64_t *phys_buffer_list,
                            tUINT32 list_len );

/*
 * Called by a host SRP driver to deregister a buffer on the host.
 * IN: host buffer
 */
int srptp_dereg_phys_host_buf (srp_host_buf_t * buf);

int srptp_post_send ( srp_pkt_t * srp_pkt );

int srptp_post_recv( srp_pkt_t *srp_pkt );

void cq_send_handler( tTS_IB_CQ_HANDLE cq, tTS_IB_CQ_ENTRY cq_entry, void *arg );
void cq_recv_handler( tTS_IB_CQ_HANDLE cq, tTS_IB_CQ_ENTRY cq_entry, void *arg );
#endif
