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

  $Id: sdp_dev.h 35 2004-04-09 05:34:32Z roland $
*/

#ifndef _TS_SDP_DEV_H
#define _TS_SDP_DEV_H
/*
 * linux types
 */
#include <linux/module.h>
#include <linux/errno.h>        /* error codes       */
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <net/sock.h>
/*
 * sdp types
 */
#include <ib_legacy_types.h>
#include <ts_ib_core.h>
#include "sdp_inet.h"
#include "sdp_types.h"
#include "sdp_msgs.h"

#define TS_SDP_DEV_NAME  "ts_sdp"      /* character device driver name */
#define TS_SDP_DEV_PROTO AF_INET_SDP   /* AF_INET number */
#define TS_SDP_MSG_HDR_REQ_SIZE 0x10     /* required header size (BSDH) */
#define TS_SDP_MSG_HDR_OPT_SIZE 0x14     /* optional header size (SNKAH) */
#define TS_SDP_MSG_HDR_SIZE     TS_SDP_MSG_HDR_REQ_SIZE
/*
 * set of performance parameters. Some are interdependent. If you
 * change, run full regression test suite, you may be surprised.
 */
#define TS_SDP_DEV_SEND_CQ_SIZE   0x003F
#define TS_SDP_DEV_RECV_CQ_SIZE   0x003F
#define TS_SDP_DEV_RECV_WIN_MAX   0x40000 /* number of bytes in flight */
#define TS_SDP_INET_RECV_WIN      0x40000
#define TS_SDP_INET_SEND_WIN      0x40000
#define TS_SDP_DEV_SEND_POST_MAX  0x003F
#define TS_SDP_DEV_RECV_POST_MAX  0x003F

#define TS_SDP_RECV_BUFFERS_MAX   0x0040 /* max number of recvs buffered  */
#define TS_SDP_SEND_BUFFERS_MAX   0x0040 /* max number of sends buffered  */
#define TS_SDP_DEV_SEND_BACKLOG   0x0002 /* tx backlog before PIPELINED mode */

#define TS_SDP_DEV_INET_THRSH     1024   /* send high water mark */
#define TS_SDP_PORT_RANGE_LOW     10000
#define TS_SDP_PORT_RANGE_HIGH    20000

#define TS_SDP_QP_LIMIT_WQE_SEND  0x003F /* max outstanding send requests */
#define TS_SDP_QP_LIMIT_WQE_RECV  0x003F /* max outstanding recv requests */
#define TS_SDP_QP_LIMIT_SG_SEND   0x0001 /* max send scather/gather entries */
#define TS_SDP_QP_LIMIT_SG_RECV   0x0001 /* max recv scather/gather entries */

#define TS_SDP_CM_PARAM_RETRY     0x07   /* connect retry count. */
#define TS_SDP_CM_PARAM_RNR_RETRY 0x07   /* RNR retry count. */
/*
 * maximum number of src/sink advertisments we can handle at a given time.
 */
#define TS_SDP_MSG_MAX_ADVS        0xFF
/*
 * Service ID is 64 bits, but a socket port is only the low 16 bits, a
 * mask is defined for the rest of the 48 bits, and is reserved in the
 * IBTA.
 */
#define TS_SDP_MSG_SERVICE_ID_RANGE (0x0000000000010000ULL)
#define TS_SDP_MSG_SERVICE_ID_VALUE (0x000000000001FFFFULL)
#define TS_SDP_MSG_SERVICE_ID_MASK  (0xFFFFFFFFFFFF0000ULL)

#define TS_SDP_MSG_SID_TO_PORT(sid)  ((tUINT16)((sid) & 0xFFFF))
#define TS_SDP_MSG_PORT_TO_SID(port) \
        ((tUINT64)(TS_SDP_MSG_SERVICE_ID_RANGE | ((port) & 0xFFFF)))
/*
 * invalid socket identifier, top entry in table.
 */
#define TS_SDP_DEV_SK_LIST_SIZE 4096  /* array of active sockets */
#define TS_SDP_DEV_SK_INVALID   (TS_SDP_DEV_SK_LIST_SIZE - 1)
/*
 * The protocol requires a SrcAvail message to contain at least one
 * byte of the data stream, when the connection is in combined mode.
 * Here's the amount of data to send.
 */
#define TS_SDP_SRC_AVAIL_GRATUITOUS 0x01
#define TS_SDP_SRC_AVAIL_THRESHOLD  0x40
/*
 * Slow start for src avail advertisments. (because they are slower then
 * sink advertisments.) Fractional increase. If we've received a sink
 * then use the fractional component for an even slower start. Once
 * a peer is known to use sinks, they probably will again.
 */
#define TS_SDP_SRC_AVAIL_FRACTION   0x06
#define TS_SDP_DEV_SEND_POST_SLOW   0x01
#define TS_SDP_DEV_SEND_POST_COUNT  0x0A
/* ---------------------------------------------------------------------- */
/*                                                                        */
/* SDP experimental parameters.                                           */
/*                                                                        */
/* ---------------------------------------------------------------------- */
/*
 * maximum consecutive unsignalled send events.
 * (crap, watch out for deactivated nodelay!)
 */
#if 0
#define TS_SDP_CONN_UNSIG_SEND_MAX 0x00
#else
#define TS_SDP_CONN_UNSIG_SEND_MAX 0x0F
#endif
/*
 * FMR pool creation parameters.
 */
#ifdef _TS_SDP_AIO_SUPPORT
#define TS_SDP_FMR_POOL_SIZE  1024
#else
#define TS_SDP_FMR_POOL_SIZE  4
#endif
#define TS_SDP_FMR_DIRTY_SIZE 32
#define TS_SDP_FMR_CACHE_SIZE 64
/*
 * connection flow control.
 */
#define TS_SDP_CONN_RECV_POST_FREQ 0x08 /* rate for posting new recv buffs */
#define TS_SDP_CONN_RECV_POST_ACK  0x08 /* rate for posting ack windows. */
/* ---------------------------------------------------------------------- */
/*                                                                        */
/* SDP root device structure.                                             */
/*                                                                        */
/* ---------------------------------------------------------------------- */
struct tSDP_DEV_PORT_STRUCT {
  tTS_IB_PORT   index; /* port ID */
  tTS_IB_GID    gid;   /* port GID */
  tSDP_DEV_PORT next;  /* next port in the list */
}; /* tSDP_DEV_PORT_STRUCT */

struct tSDP_DEV_HCA_STRUCT {
  tTS_IB_DEVICE_HANDLE    ca;        /* HCA */
  tTS_IB_PD_HANDLE        pd;        /* protection domain for this HCA */
  tTS_IB_MR_HANDLE        mem_h;     /* registered memory region */
  tTS_IB_LKEY             l_key;     /* local key */
  tTS_IB_RKEY             r_key;     /* remote key */
  uint64_t                iova;      /* address */
  tTS_IB_FMR_POOL_HANDLE  fmr_pool;  /* fast memory for Zcopy */
  tSDP_DEV_PORT           port_list; /* ports on this HCA */
  tSDP_DEV_HCA            next;      /* next HCA in the list */
}; /* tSDP_DEV_HCA_STRUCT */

struct tSDP_DEV_ROOT_STRUCT {
  tUINT32              src_addr;
  tINT32               proto;
  /*
   * devices. list of installed HCA's and some associated parameters
   */
  tSDP_DEV_HCA hca_list;
  /*
   * connections. The table is a simple linked list, since it does not
   * need to require fast lookup capabilities.
   */
  tUINT32       sk_size;     /* socket array size */
  tUINT32       sk_ordr;     /* order size of region. */
  tUINT32       sk_rover;    /* order size of region. */
  tUINT32       sk_entry;    /* number of socket table entries. */
  tSDP_CONN    *sk_array;    /* array of sockets. */
  /*
   * connection managment
   */
  tSDP_CONN     listen_list; /* list of listening connections */
  tSDP_CONN     bind_list;   /* connections bound to a port. */
  /*
   * list locks
   */
  spinlock_t            bind_lock;
  spinlock_t            sock_lock;
  spinlock_t            listen_lock;
  /*
   * SDP wide listen
   */
  tTS_IB_LISTEN_HANDLE listen_handle; /* listen handle */
  /*
   * cache's
   */
  kmem_cache_t        *conn_cache;
}; /* tSDP_DEV_ROOT_STRUCT */

#endif /* _TS_SDP_DEV_H */
