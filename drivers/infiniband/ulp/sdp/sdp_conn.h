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

  $Id: sdp_conn.h 35 2004-04-09 05:34:32Z roland $
*/

#ifndef _TS_SDP_CONN_H
#define _TS_SDP_CONN_H
/*
 * topspin specific includes.
 */
#include <ib_legacy_types.h>
#include "sdp_types.h"
#include "sdp_advt.h"
#include "sdp_iocb.h"
#include "sdp_dev.h"
#include "sdp_os.h"
/* ---------------------------------------------------------------------- */
/* SDP connection specific definitions                                    */
/* ---------------------------------------------------------------------- */
/*
 * definitions
 */
#define TS_SDP_CONN_SEND_POST_NAME "tx_sdp_post" /* name for pool */
#define TS_SDP_CONN_RECV_POST_NAME "rx_sdp_post" /* name for pool */
#define TS_SDP_CONN_SEND_DATA_NAME "tx_sdp_data" /* name for pool */
#define TS_SDP_CONN_SEND_CTRL_NAME "tx_sdp_ctrl" /* name for pool */
#define TS_SDP_CONN_RDMA_READ_NAME "rx_sdp_rdma" /* name for pool */
#define TS_SDP_CONN_RDMA_SEND_NAME "tx_sdp_rdma" /* name for pool */
#define TS_SDP_SOCK_RECV_DATA_NAME "rx_lnx_data"
#define TS_SDP_INET_PEEK_DATA_NAME "rx_lnx_peek"
#define TS_SDP_CONN_COMM_ID_NULL   0xFFFFFFFF     /* when no id is available */

#define TS_SDP_CONN_F_SRC_CANCEL_L 0x0001 /* source cancel was issued */
#define TS_SDP_CONN_F_SRC_CANCEL_R 0x0002 /* source cancel was received */
#define TS_SDP_CONN_F_SRC_CANCEL_C 0x0004 /* source data cancelled */
#define TS_SDP_CONN_F_SNK_CANCEL   0x0008 /* sink cancel issued */
#define TS_SDP_CONN_F_DIS_HOLD     0x0010 /* Hold pending disconnects. */
#define TS_SDP_CONN_F_OOB_SEND     0x0020 /* OOB notification pending. */
#define TS_SDP_CONN_F_RECV_CQ_PEND 0x0040 /* recv CQ event is pending */
#define TS_SDP_CONN_F_SEND_CQ_PEND 0x0080 /* send CQ event is pending */
#define TS_SDP_CONN_F_DEAD         0xFFFF /* connection has been deleted */

#define TS_SDP_CONN_F_MASK_EVENT   (TS_SDP_CONN_F_RECV_CQ_PEND | \
				    TS_SDP_CONN_F_SEND_CQ_PEND)
/*
 * sdp connection table proc entries
 */
#define TS_SDP_SOPT_PROC_DUMP_SIZE   55 /* output line size. */
#define TS_SDP_CONN_PROC_MAIN_SIZE  183 /* output line size. */
#define TS_SDP_CONN_PROC_DATA_SIZE  176 /* output line size. */
#define TS_SDP_CONN_PROC_RDMA_SIZE  101 /* output line size. */
/*
 * Work Request ID mask to identify IOCB READs and BUFF RECVs.
 */
#define TS_SDP_WRID_READ_FLAG 0x80000000
#define TS_SDP_WRID_RECV_MASK 0x7FFFFFFF
/*
 * SDP states.
 */
typedef enum {
  TS_SDP_MODE_BUFF  = 0x00,
  TS_SDP_MODE_COMB  = 0x01,
  TS_SDP_MODE_PIPE  = 0x02,
  TS_SDP_MODE_ERROR = 0x03
} tSDP_MODE;
/*
 * First two bytes are the primary state values. Third byte is a bit
 * field used for different mask operations, defined below. Fourth
 * byte are the mib values for the different states.
 */
#define TS_SDP_CONN_ST_LISTEN      0x0100 /* listening */

#define TS_SDP_CONN_ST_ESTABLISHED 0x11B1 /* connected */

#define TS_SDP_CONN_ST_REQ_PATH    0x2100 /* active open, path record lookup */
#define TS_SDP_CONN_ST_REQ_SENT    0x2200 /* active open, Hello msg sent */
#define TS_SDP_CONN_ST_REQ_RECV    0x2300 /* passive open, Hello msg recv'd */
#define TS_SDP_CONN_ST_REP_SENT    0x2480 /* passive open, Hello ack sent */
#define TS_SDP_CONN_ST_REP_RECV    0x2500 /* active open, Hello ack recv'd */
#define TS_SDP_CONN_ST_RTU_SENT    0x2680 /* active open, Hello ack, acked */

#define TS_SDP_CONN_ST_DIS_RECV_1  0x31B1 /* recv disconnect, passive close */
#define TS_SDP_CONN_ST_DIS_PEND_1  0x32F1 /* pending disconn, active close */
#define TS_SDP_CONN_ST_DIS_SEND_1  0x33B1 /* send disconnect, active close */
#define TS_SDP_CONN_ST_DIS_SENT_1  0x34A1 /* disconnect sent, active close */
#define TS_SDP_CONN_ST_DIS_PEND_R  0x35F1 /* disconnect recv, active close */
#define TS_SDP_CONN_ST_DIS_RECV_R  0x36B1 /* disconnect recv, active close */
#define TS_SDP_CONN_ST_DIS_PEND_2  0x37F1 /* pending disconn, passive close */
#define TS_SDP_CONN_ST_DIS_SEND_2  0x38B1 /* send disconnect, passive close */
#define TS_SDP_CONN_ST_DIS_SENT_2  0x3901 /* disconnect sent, passive close */
#define TS_SDP_CONN_ST_TIME_WAIT_1 0x3A01 /* IB/gateway disconnect */
#define TS_SDP_CONN_ST_TIME_WAIT_2 0x3B01 /* waiting for idle close */

#define TS_SDP_CONN_ST_ERROR_CM    0xFB03 /* CM error, waiting on gateway */
#define TS_SDP_CONN_ST_ERROR_CQ    0xFC03 /* gateway error, waiting on CM */
#define TS_SDP_CONN_ST_ERROR_STRM  0xFD01 /* gateway error, waiting on CM */
#define TS_SDP_CONN_ST_CLOSED      0xFE03 /* not connected */
#define TS_SDP_CONN_ST_INVALID     0xFF03 /* not connected */
/*
 * states masks for SDP
 */
#define TS_SDP_ST_MASK_EVENTS    0x0001 /* event processing is allowed. */
#define TS_SDP_ST_MASK_ERROR     0x0002 /* protocol in error state. */
#define TS_SDP_ST_MASK_SEND_OK   0x0010 /* posting data for send */
#define TS_SDP_ST_MASK_CTRL_OK   0x0020 /* posting control for send */
#define TS_SDP_ST_MASK_DIS_PEND  0x0040 /* disconnect transmission pending. */
#define TS_SDP_ST_MASK_RCV_POST  0x0080 /* posting IB recv's is allowed. */
/*
 * transition one of the disconnect pending states to disconnect send
 */
#define TS_SDP_ST_PEND_2_SEND(conn) \
        (conn)->state = ((conn)->state + 0x0100) & ~TS_SDP_ST_MASK_DIS_PEND;
/*
 * internal connection structure
 */
#define TS_SDP_SOCK_ST_CLOSED      0x0101
#define TS_SDP_SOCK_ST_CONNECT     0x0204
#define TS_SDP_SOCK_ST_ACCEPTING   0x0300
#define TS_SDP_SOCK_ST_ACCEPTED    0x0404 /* writable ? */
#define TS_SDP_SOCK_ST_ESTABLISHED 0x050E
#define TS_SDP_SOCK_ST_DISCONNECT  0x0602 /* active close request */
#define TS_SDP_SOCK_ST_CLOSE       0x070C /* passive close request */
#define TS_SDP_SOCK_ST_CLOSING     0x0800
#define TS_SDP_SOCK_ST_LISTEN      0x0901
#define TS_SDP_SOCK_ST_ERROR       0xFF01
/*
 * state masks.
 */
#define TS_SDP_ST_MASK_CLOSED 0x0001 /* socket is not referenced by the GW. */
#define TS_SDP_ST_MASK_READY  0x0002 /* ready to recv data from GW */
#define TS_SDP_ST_MASK_SEND   0x0004 /* valid state for API send */
#define TS_SDP_ST_MASK_OPEN   0x0008 /* send window is valid (writeable) */
/*
 * shutdown
 */
#define TS_SDP_SHUTDOWN_RECV   0x01 /* recv connection half close */
#define TS_SDP_SHUTDOWN_SEND   0x02 /* send connection half close */
#define TS_SDP_SHUTDOWN_MASK   0x03
#define TS_SDP_SHUTDOWN_NONE   0x00
/*
 * event dispatch table
 */
#define TS_SDP_MSG_EVENT_TABLE_SIZE 0x20
/* --------------------------------------------------------------------- */
/* state transition information recording                                */
/* --------------------------------------------------------------------- */
#ifdef _TS_SDP_CONN_STATE_REC

#define TS_SDP_CONN_STATE_MAX 16 /* maximum state transitions recorded. */

typedef struct tSDP_CONN_STATE_STRUCT tSDP_CONN_STATE_STRUCT, \
              *tSDP_CONN_STATE;

struct tSDP_CONN_STATE_STRUCT {
  tUINT8  value;
  tUINT16 state[TS_SDP_CONN_STATE_MAX];
  tPTR    file[TS_SDP_CONN_STATE_MAX];
  tINT32  line[TS_SDP_CONN_STATE_MAX];
}; /* tSDP_CONN_STATE_STRUCT */

#define TS_SDP_CONN_ST_SET(conn, val) \
{ \
  (conn)->state = (val); \
  if (TS_SDP_CONN_STATE_MAX > (conn)->state_rec.value) { \
    (conn)->state_rec.state[(conn)->state_rec.value] = (val); \
    (conn)->state_rec.file[(conn)->state_rec.value] = __FILE__; \
    (conn)->state_rec.line[(conn)->state_rec.value] = __LINE__; \
    (conn)->state_rec.value++; \
  } \
}

#define TS_SDP_CONN_ST_INIT(conn) \
{ \
  (conn)->state = TS_SDP_CONN_ST_INVALID; \
  for ((conn)->state_rec.value = 0; \
       TS_SDP_CONN_STATE_MAX > (conn)->state_rec.value; \
       (conn)->state_rec.value++) { \
    (conn)->state_rec.state[(conn)->state_rec.value] = TS_SDP_CONN_ST_INVALID;\
    (conn)->state_rec.file[(conn)->state_rec.value] = NULL; \
    (conn)->state_rec.line[(conn)->state_rec.value] = 0; \
  } \
  (conn)->state_rec.value = 0; \
}
#else
#define TS_SDP_CONN_ST_SET(conn, val) (conn)->state = (val)
#define TS_SDP_CONN_ST_INIT(conn)     (conn)->state = TS_SDP_CONN_ST_INVALID
#endif
/*
 * connection lock
 */
struct tSDP_CONN_LOCK_STRUCT {
  tUINT32           users;
  spinlock_t        slock;
  wait_queue_head_t waitq;
}; /* tSDP_CONN_LOCK_STRUCT */
/*
 * SDP Connection structure.
 */
struct tSDP_CONN_STRUCT {
  tINT32            hashent; /* connection ID/hash entry */
  tSDP_ATOMIC       refcnt;  /* connection reference count. */

  struct sock      *sk;
  /*
   * SDP specific data
   */
  tUINT32       send_buf;
  tUINT32       send_qud;
  tUINT32       send_pipe;   /* buffered bytes in the local send queue */
  tINT32        oob_offset;  /* bytes till OOB byte is sent. */

  tINT16        send_usig;  /* number of unsignalled sends in the pipe. */
  tINT16        send_cons;  /* number of consecutive unsignalled sends. */

  tSDP_GENERIC_TABLE_STRUCT send_queue; /* queue of send objects. */
  tSDP_POOL_STRUCT          send_post;  /* posted sends */

  tUINT32       send_seq;   /* sequence number of last message sent */
  tUINT32       recv_seq;   /* sequence number of last message received */
  tUINT32       advt_seq;   /* sequence number of last message acknowledged */

  tSDP_POOL_STRUCT recv_pool;   /* pool of received buffer */
  tSDP_POOL_STRUCT recv_post;   /* posted receives */

  tINT32        byte_strm;  /* buffered bytes in the local recv queue */
  tINT32        rwin_max;   /* maximum recveive window size */

  tUINT16       state;      /* connection state */
  tUINT16       istate;     /* inet connection state */

  tUINT8        flags;     /* single bit flags. */
  tUINT8        shutdown;  /* shutdown flag */
  tUINT8        recv_mode;  /* current flow control mode */
  tUINT8        send_mode;  /* current flow control mode */

  tUINT16       recv_max;   /* max posted/used receive buffers */
  tUINT16       send_max;   /* max posted/used send buffers */

  tUINT16       recv_size;  /* local recv buffer size */
  tUINT16       send_size;  /* remote recv buffer size */

  tUINT8        l_max_adv; /* local maximum zcopy advertisments */
  tUINT8        r_max_adv; /* remote maximum zcopy advertisments */
  tUINT8        s_cur_adv; /* current source advertisments (slow start) */
  tUINT8        s_par_adv; /* current source advertisments (slow start) */

  tUINT16       r_recv_bf; /* number of recv buffers remote currently has */
  tUINT16       l_recv_bf; /* number of recv buffers local currently has */
  tUINT16       l_advt_bf; /* number of recv buffers local has advertised */

  tUINT8        s_wq_size; /* current number of posted sends. */

  tUINT8        s_wq_cur;  /* buffered transmission limit current */
  tUINT8        s_wq_par;  /* buffered transmission limit increment */

  tUINT8        src_recv;  /* outstanding remote source advertisments */
  tUINT8        snk_recv;  /* outstanding remote sink advertisments */
  tUINT8        src_sent;  /* outstanding local source advertisments */
  tUINT8        snk_sent;  /* outstanding local sink advertisments */

  tUINT8        sink_actv;

  tUINT8        src_cncl;  /* local source advertisments cancelled by user */
  tUINT32       src_cseq; /* sequence number of source cancel message */
  /*
   * work request ID's used to double-check queue consistency
   */
  tUINT32       send_wrid;
  tUINT32       recv_wrid;

  tUINT32       send_cq_size;
  tUINT32       recv_cq_size;
  /*
   * stale SnkAvail detection
   */
  tUINT32       nond_recv;  /* non discarded buffers received. */
  tUINT32       nond_send;  /* non discarded buffers sent */

  tINT32        error; /* error value on connection. */
  /*
   * OOB/URG data transfer.
   */
  tINT16                  rcv_urg_cnt; /* queued urgent data */
  /*
   * listen backlog
   */
  tUINT16 backlog_cnt;  /* depth of the listen backlog queue */
  tUINT16 backlog_max;  /* max length of the listen backlog queue */
  /*
   * memory specific data
   */
  tSDP_GENERIC_TABLE_STRUCT send_ctrl;  /* control messages waiting to
					   be transmitted, which do not
					   depend on data ordering */
  /*
   * advertisment managment
   */
  tSDP_ADVT_TABLE_STRUCT  src_pend; /* pending remote source advertisments */
  tSDP_ADVT_TABLE_STRUCT  src_actv; /* active remote source advertisments */
  tSDP_ADVT_TABLE_STRUCT  snk_pend; /* pending remote sink advertisments */
  /*
   * outstanding IOCBs/BUFFs
   */
  tSDP_IOCB_TABLE_STRUCT    r_pend; /* pending user read IOCBs */
  tSDP_IOCB_TABLE_STRUCT    r_snk;  /* active user read sink IOCBs */
  tSDP_IOCB_TABLE_STRUCT    w_src;  /* active user write source IOCBs */

  tSDP_GENERIC_TABLE_STRUCT r_src;  /* active user read source IOCBs */
  tSDP_GENERIC_TABLE_STRUCT w_snk;  /* active user write sink IOCBs */
  /*
   * addresses
   */
  tUINT32            src_addr;  /* ipv4 address on the stream interface */
  tUINT32            dst_addr;  /* ipv4 address of the remote SDP client */
  tUINT16            dst_port;  /* tcp port of the remote SDP client */
  tUINT16            src_port;  /* tcp port on the stream interface */
  /*
   * IB specific data
   */
  tTS_IB_GID        d_gid;
  tTS_IB_GID        s_gid;
  tUINT16           d_lid;
  tUINT16           s_lid;
  tTS_IB_QPN        d_qpn;
  tTS_IB_QPN        s_qpn;

  tIP2PR_PATH_LOOKUP_ID   plid;     /* path record lookup */

  tTS_IB_DEVICE_HANDLE    ca;       /* hca that we'll be using for sdp */
  tTS_IB_QP_HANDLE        qp;       /* queue pair for the SDP connection */
  tTS_IB_PD_HANDLE        pd;       /* protection domain used by the kernel */
  tTS_IB_CQ_HANDLE        send_cq;  /* send completion queue */
  tTS_IB_CQ_HANDLE        recv_cq;  /* recv completion queue */
  tTS_IB_PORT             hw_port;  /* hca port */
  tTS_IB_LKEY             l_key;    /* local key for buffered memory */
  tTS_IB_FMR_POOL_HANDLE  fmr_pool;  /* fast memory for Zcopy */

  tTS_IB_CM_COMM_ID    comm_id;  /* CM handle durring async connection setup */
  /*
   * timer for defered execution. Used to call CM functions from a
   * non-interupt context.
   */
  tTS_KERNEL_TIMER_STRUCT cm_exec;
  /*
   * SDP connection lock
   */
  tSDP_CONN_LOCK_STRUCT     lock;
  /*
   * table managment
   */
  tSDP_CONN  lstn_next;   /* next connection in the chain */
  tSDP_CONN *lstn_p_next; /* previous next connection in the chain */

  tSDP_CONN  bind_next;   /* next connection in the chain */
  tSDP_CONN *bind_p_next; /* previous next connection in the chain */
  /*
   * listen/accept managment
   */
  tSDP_CONN parent;       /* listening socket queuing. */
  tSDP_CONN accept_next;  /* sockets waiting for acceptance. */
  tSDP_CONN accept_prev;  /* sockets waiting for acceptance. */
  /*
   * OS info
   */
  tUINT16       pid;        /* process ID of creator */
  /*
   * TCP specific socket options
   */
  tUINT8   nodelay; /* socket nodelay is set */
  tUINT32  src_zthresh; /* source zero copy threshold */
  tUINT32  snk_zthresh; /* sink zero copy threshold */
  /*
   * stats
   */
  tUINT32 send_mid[TS_SDP_MSG_EVENT_TABLE_SIZE]; /* send event stats */
  tUINT32 recv_mid[TS_SDP_MSG_EVENT_TABLE_SIZE]; /* recv event stats */

  tUINT64 send_bytes;  /* socket bytes sent */
  tUINT64 recv_bytes;  /* socket bytes received */
  tUINT64 write_bytes; /* AIO bytes sent */
  tUINT64 read_bytes;  /* AIO bytes received */

  tUINT32 read_queued;  /* reads queued for reception */
  tUINT32 write_queued; /* writes queued for transmission */

  tUINT32 src_serv;   /* source advertisments completed. */
  tUINT32 snk_serv;   /* sink advertisments completed. */

#ifdef _TS_SDP_CONN_STATE_REC
  tSDP_CONN_STATE_STRUCT state_rec;
#endif
}; /* tSDP_CONN_STRUCT */

#define TS_SDP_WRID_GT(x, y) ((tINT32)((x) - (y)) > 0)
#define TS_SDP_WRID_LT(x, y) ((tINT32)((x) - (y)) < 0)
#define TS_SDP_WRID_GTE(x, y) ((tINT32)((x) - (y)) >= 0)
#define TS_SDP_WRID_LTE(x, y) ((tINT32)((x) - (y)) <= 0)

#define TS_SDP_SEQ_GT(x, y) ((tINT32)((x) - (y)) > 0)
#define TS_SDP_SEQ_LT(x, y) ((tINT32)((x) - (y)) < 0)
#define TS_SDP_SEQ_GTE(x, y) ((tINT32)((x) - (y)) >= 0)
#define TS_SDP_SEQ_LTE(x, y) ((tINT32)((x) - (y)) <= 0)
/*
 * statistics.
 */
#ifdef _TS_SDP_CONN_STATS_REC
#define TS_SDP_CONN_STAT_SEND_INC(conn, size)  ((conn)->send_bytes += (size))
#define TS_SDP_CONN_STAT_RECV_INC(conn, size)  ((conn)->recv_bytes += (size))
#define TS_SDP_CONN_STAT_READ_INC(conn, size)  ((conn)->read_bytes += (size))
#define TS_SDP_CONN_STAT_WRITE_INC(conn, size) ((conn)->write_bytes += (size))

#define TS_SDP_CONN_STAT_RQ_INC(conn, size) ((conn)->read_queued  += (size))
#define TS_SDP_CONN_STAT_WQ_INC(conn, size) ((conn)->write_queued += (size))
#define TS_SDP_CONN_STAT_RQ_DEC(conn, size) ((conn)->read_queued  -= (size))
#define TS_SDP_CONN_STAT_WQ_DEC(conn, size) ((conn)->write_queued -= (size))

#define TS_SDP_CONN_STAT_SRC_INC(conn) ((conn)->src_serv++)
#define TS_SDP_CONN_STAT_SNK_INC(conn) ((conn)->snk_serv++)

#define TS_SDP_CONN_STAT_SEND_MID_INC(conn, mid) \
        ((conn)->send_mid[(mid)]++)
#define TS_SDP_CONN_STAT_RECV_MID_INC(conn, mid) \
        ((conn)->recv_mid[(mid)]++)
#else
#define TS_SDP_CONN_STAT_SEND_INC(conn, size)
#define TS_SDP_CONN_STAT_RECV_INC(conn, size)
#define TS_SDP_CONN_STAT_READ_INC(conn, size)
#define TS_SDP_CONN_STAT_WRITE_INC(conn, size)

#define TS_SDP_CONN_STAT_RQ_INC(conn, size)
#define TS_SDP_CONN_STAT_WQ_INC(conn, size)
#define TS_SDP_CONN_STAT_RQ_DEC(conn, size)
#define TS_SDP_CONN_STAT_WQ_DEC(conn, size)

#define TS_SDP_CONN_STAT_SRC_INC(conn)
#define TS_SDP_CONN_STAT_SNK_INC(conn)

#define TS_SDP_CONN_STAT_SEND_MID_INC(conn, mid)
#define TS_SDP_CONN_STAT_RECV_MID_INC(conn, mid)

#endif
/* ---------------------------------------------------------------------- */
/*                                                                        */
/* SDP connection lock                                                    */
/*                                                                        */
/* ---------------------------------------------------------------------- */
#if !defined(TS_KERNEL_2_6)

#define TS_SDP_CONN_LOCK_BH(conn)   spin_lock_bh(&((conn)->lock.slock))
#define TS_SDP_CONN_UNLOCK_BH(conn) spin_unlock_bh(&((conn)->lock.slock))

#define TS_SDP_CONN_LOCK(conn)           \
do {                                     \
  spin_lock_bh(&((conn)->lock.slock));   \
  if ((conn)->lock.users != 0) {         \
     tsSdpConnLockInternalLock(conn);    \
  } /* if */                             \
  (conn)->lock.users = 1;                \
  spin_unlock_bh(&((conn)->lock.slock)); \
} while(0)

#define TS_SDP_CONN_UNLOCK(conn)         \
do {                                     \
  spin_lock_bh(&((conn)->lock.slock));   \
  if (0 < (TS_SDP_CONN_F_MASK_EVENT & conn->flags) && \
      0 < (TS_SDP_ST_MASK_EVENTS & conn->state)) { \
     tsSdpConnLockInternalUnlock(conn);  \
  } /* if */                             \
  (conn)->lock.users = 0;                \
  wake_up(&((conn)->lock.waitq));        \
  spin_unlock_bh(&((conn)->lock.slock)); \
} while(0)

#define TS_SDP_CONN_RELOCK(conn)         \
do {                                     \
  spin_lock_bh(&((conn)->lock.slock));   \
     tsSdpConnLockInternalRelock(conn);  \
  spin_unlock_bh(&((conn)->lock.slock)); \
} while(0)

#else /* kernel 2.6 */

/* Work around the temporary workaround for THCA locking bugs :) */

#define TS_SDP_CONN_LOCK_BH(conn)   spin_lock_irqsave(&((conn)->lock.slock), flags)
#define TS_SDP_CONN_UNLOCK_BH(conn) spin_unlock_irqrestore(&((conn)->lock.slock), flags)

#define TS_SDP_CONN_LOCK(conn)                          \
do {                                                    \
  unsigned long flags;                                  \
  spin_lock_irqsave(&((conn)->lock.slock), flags);      \
  if ((conn)->lock.users != 0) {                        \
     tsSdpConnLockInternalLock(conn, &flags);           \
  } /* if */                                            \
  (conn)->lock.users = 1;                               \
  spin_unlock_irqrestore(&((conn)->lock.slock), flags); \
} while(0)

#define TS_SDP_CONN_UNLOCK(conn)                        \
do {                                                    \
  unsigned long flags;                                  \
  spin_lock_irqsave(&((conn)->lock.slock), flags);      \
  if (0 < (TS_SDP_CONN_F_MASK_EVENT & conn->flags) &&   \
      0 < (TS_SDP_ST_MASK_EVENTS & conn->state)) {      \
     tsSdpConnLockInternalUnlock(conn);                 \
  } /* if */                                            \
  (conn)->lock.users = 0;                               \
  wake_up(&((conn)->lock.waitq));                       \
  spin_unlock_irqrestore(&((conn)->lock.slock), flags); \
} while(0)

#define TS_SDP_CONN_RELOCK(conn)                        \
do {                                                    \
  unsigned long flags;                                  \
  spin_lock_irqsave(&((conn)->lock.slock), flags);      \
     tsSdpConnLockInternalRelock(conn);                 \
  spin_unlock_irqrestore(&((conn)->lock.slock), flags); \
} while(0)

#endif /* kernel 2.6 */

/* ---------------------------------------------------------------------- */
/*                                                                        */
/* SDP connection inline functions                                        */
/*                                                                        */
/* ---------------------------------------------------------------------- */
/* ====================================================================== */
/*..__tsSdpInetWriteSpace -- writable space on send side. */
static __inline__ tINT32 __tsSdpInetWriteSpace
(
 tSDP_CONN conn,
 tINT32    urg
 )
{
  tINT32 size;

  TS_CHECK_NULL(conn, -EINVAL);
  /*
   * Allow for more space if Urgent data is being considered
   */
  size = (conn->send_buf - conn->send_qud);
  /*
   * write space is determined by amount of outstanding bytes of data
   * and number of buffers used for transmission by this connection
   */
  if (0 < (TS_SDP_ST_MASK_OPEN & conn->istate) &&
      (TS_SDP_SEND_BUFFERS_MAX >
       tsSdpGenericTableTypeSize(&conn->send_queue,
				 TS_SDP_GENERIC_TYPE_BUFF))) {

    return ((TS_SDP_DEV_INET_THRSH < size || 1 < urg) ? size : 0);
  } /* if */
  else {

    return 0;
  } /* else */
} /* __tsSdpInetWriteSpace */

/* ====================================================================== */
/*..__tsSdpInetWritable -- return non-zero if socket is writable. */
static __inline__ tINT32 __tsSdpInetWritable
(
 tSDP_CONN conn
 )
{
  TS_CHECK_NULL(conn, -EINVAL);

  if (0 < (TS_SDP_ST_MASK_OPEN & conn->istate)) {

    return (__tsSdpInetWriteSpace(conn, 0) < (conn->send_qud/2)) ? 0 : 1;
  } /* if */
  else {

    return 0;
  } /* else */
} /* __tsSdpInetWritable */

/* ======================================================================== */
/*..__tsSdpConnStatDump -- dump stats to the log */
static __inline__ tINT32 __tsSdpConnStatDump
(
 tSDP_CONN conn
)
{
#ifdef _TS_SDP_CONN_STATS_REC
  tUINT32 counter;

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	   "STAT: src <%u> snk <%u>", conn->src_serv, conn->snk_serv);

  for (counter = 0; counter < 0x20; counter++) {

    if (0 < conn->send_mid[counter] ||
	0 < conn->recv_mid[counter]) {

      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	       "STAT: MID send <%02x> <%u>", counter, conn->send_mid[counter]);
      TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_INOUT,
	       "STAT: MID recv <%02x> <%u>", counter, conn->recv_mid[counter]);
    } /* if*/
  } /* for */
#endif
  return 0;
} /* __tsSdpConnStatDump */

/* ======================================================================== */
/*..__tsSdpConnStateDump -- dump state information to the log */
static __inline__ tINT32 __tsSdpConnStateDump
(
 tSDP_CONN conn
)
{
#ifdef _TS_SDP_CONN_STATE_REC
  tUINT32 counter;

  TS_CHECK_NULL(conn, -EINVAL);

  TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_INOUT,
	   "STATE: Connection <%04x> state:", conn->hashent);

  if (TS_SDP_CONN_ST_INVALID == conn->state_rec.state[0]) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_INOUT,
	     "STATE:   No state history. <%d>", conn->state_rec.value);
  }
  else {

    for (counter = 0;
	 TS_SDP_CONN_ST_INVALID != conn->state_rec.state[counter];
	 counter++) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_INOUT,
	       "STATE:   counter <%02x> state <%04x> <%s:%d>",
	       counter,
	       conn->state_rec.state[counter],
	       conn->state_rec.file[counter],
	       conn->state_rec.line[counter]);
    } /* for */
  } /* else */
#endif
  return 0;
} /* __tsSdpConnStateDump */

/* ======================================================================== */
/*..__tsSdpConnHold -- increment reference count */
static inline void __tsSdpConnHold
(
 tSDP_CONN conn
)
{
  TS_SDP_ATOMIC_INC(conn->refcnt);
} /* __tsSdpConnHold */

/* ======================================================================== */
/*..__tsSdpConnPut -- decrement reference count */
static inline void __tsSdpConnPut
(
 tSDP_CONN conn
)
{
  if (TS_SDP_ATOMIC_DEC_TEST(conn->refcnt)) {

    (void)tsSdpConnDestruct(conn);
  } /* if */
} /* __tsSdpConnHold */

/* ======================================================================== */
/*..__tsSdpConnError -- get the connections error value destructively. */
static inline tINT32 __tsSdpConnError
(
 tSDP_CONN conn
)
{
  /*
   * The connection error parameter is set and read under the connection
   * lock, however the linux socket error, needs to be xchg'd since the
   * SO_ERROR getsockopt happens outside of the connection lock.
   */
  tINT32 error = xchg(&TS_SDP_OS_SK_ERR(conn->sk), 0);
  TS_SDP_OS_CONN_SET_ERR(conn, 0);

  return -error;
} /* __tsSdpConnError */

#define TS_SDP_CONN_HOLD(conn) __tsSdpConnHold((conn))
#define TS_SDP_CONN_PUT(conn)  __tsSdpConnPut((conn))
#define TS_SDP_CONN_ERROR(conn)  __tsSdpConnError((conn))

#endif /* _TS_SDP_CONN_H */
