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

  $Id: sdp_link.h 35 2004-04-09 05:34:32Z roland $
*/

#ifndef _TS_SDP_LINK_H
#define _TS_SDP_LINK_H
/*
 * topspin specific headers.
 */
#include <ib_legacy_types.h>
#include "sdp_types.h"

/* ------------------------------------------------------------------------- */
/*                             IP over IB Section                            */
/* ------------------------------------------------------------------------- */

#define TS_SDP_IPOIB_MGID_MASK_HIGH (0xFF12401B00000000ULL)
#define TS_SDP_IPOIB_MGID_MASK_LOW  (0x00000000FFFFFFFFULL)
#define TS_SDP_IPOIB_QPN_MASK        0x00FFFFFF

#define TS_SDP_IPOIB_ADDR_LEN    20
#define TS_SDP_IPOIB_ENCAP_LEN    4
#define TS_SDP_IPOIB_HDR_LEN     44
#define TS_SDP_IPOIB_PSEUDO_LEN  40
#define TS_SDP_IPOIB_GRH_LEN     40
/*
 * IPoIB hardware address.
 */
struct tSDP_IPOIB_ADDR_STRUCT {
  tUINT32 qpn;        /* MSB = reserved, low 3 bytes=QPN */
  union {
    tUINT8  all[16];
    struct {
      tUINT64 high;
      tUINT64 low;
    } s;
  } gid;
} __attribute__ ((packed));
/*
 * The two src and dst addresses are the same size as the GRH.
 */
struct tSDP_IPOIB_HDR_STRUCT {
  tSDP_IPOIB_ADDR_STRUCT src;
  tSDP_IPOIB_ADDR_STRUCT dst;
  tUINT16 proto;
  tUINT16 reserved;
} __attribute__ ((packed));
/*
 * Ethernet/IPoIB pseudo ARP header, used by out IPoIB driver.
 */
struct tSDP_IPOIB_ARP_STRUCT {
  tUINT16 addr_type;    /* format of hardware address   */
  tUINT16 proto_type;   /* format of protocol address   */
  tUINT8  addr_len;     /* length of hardware address   */
  tUINT8  proto_len;    /* length of protocol address   */
  tUINT16 cmd;          /* ARP opcode (command)         */
  /*
   * begin ethernet
   */
  tUINT8   src_hw[ETH_ALEN];
  tUINT32  src_ip;
  tUINT8   dst_hw[ETH_ALEN];
  tUINT32  dst_ip;
} __attribute__ ((packed));

/*
 * macro for determining if a net device is out IPoIB device.
 */
#define TS_SDP_IPOIB_DEV_TOPSPIN(dev) \
        ((0 == strncmp((dev)->name, "ib", 2)) ? 0 : -1)

#define TS_SDP_IPOIB_RETRY_VALUE    3        /* number of retries. */
#define TS_SDP_IPOIB_RETRY_INTERVAL (HZ * 1) /* retry frequency */
#define TS_SDP_IPOIB_PROC_DUMP_SIZE 24
#define TS_SDP_PATH_PROC_DUMP_SIZE  87

#define TS_SDP_DEV_PATH_WAIT       (5 * (HZ))
#define TS_SDP_PATH_TIMER_INTERVAL (15 * (HZ))  /* cache sweep frequency */
#define TS_SDP_PATH_REAPING_AGE    (300 * (HZ)) /* idle time before reaping */
/*
 * usage flags.
 */
#define TS_SDP_IPOIB_FLAG_FUNC 0x01  /* consumer reference */
#define TS_SDP_IPOIB_FLAG_TIME 0x02  /* timer reference */
#define TS_SDP_IPOIB_FLAG_TASK 0x04  /* tasklet reference */

#define TS_SDP_IPOIB_FLAG_GET_FUNC(iw) ((iw)->flags & TS_SDP_IPOIB_FLAG_FUNC)
#define TS_SDP_IPOIB_FLAG_GET_TIME(iw) ((iw)->flags & TS_SDP_IPOIB_FLAG_TIME)
#define TS_SDP_IPOIB_FLAG_GET_TASK(iw) ((iw)->flags & TS_SDP_IPOIB_FLAG_TASK)
#define TS_SDP_IPOIB_FLAG_SET_FUNC(iw) ((iw)->flags |= TS_SDP_IPOIB_FLAG_FUNC)
#define TS_SDP_IPOIB_FLAG_SET_TIME(iw) ((iw)->flags |= TS_SDP_IPOIB_FLAG_TIME)
#define TS_SDP_IPOIB_FLAG_SET_TASK(iw) ((iw)->flags |= TS_SDP_IPOIB_FLAG_TASK)
#define TS_SDP_IPOIB_FLAG_CLR_FUNC(iw) ((iw)->flags &= ~TS_SDP_IPOIB_FLAG_FUNC)
#define TS_SDP_IPOIB_FLAG_CLR_TIME(iw) ((iw)->flags &= ~TS_SDP_IPOIB_FLAG_TIME)
#define TS_SDP_IPOIB_FLAG_CLR_TASK(iw) ((iw)->flags &= ~TS_SDP_IPOIB_FLAG_TASK)
#define TS_SDP_IPOIB_FLAGS_EMPTY(iw) ((iw)->flags == 0 ? 1 : 0)

/* ------------------------------------------------------------------------- */
/* Event driven address resoslution                                          */
/* ------------------------------------------------------------------------- */
/*
 * tables
 */
struct tSDP_LINK_ROOT_STRUCT {
  /*
   * waiting for resolution table
   */
  tSDP_IPOIB_WAIT    wait_list;
  kmem_cache_t      *wait_cache;
  spinlock_t         wait_lock;
  /*
   * path record cache list.
   */
  tSDP_PATH_ELEMENT  path_list;
  kmem_cache_t      *path_cache;
  spinlock_t         path_lock;
}; /* tSDP_LINK_ROOT_STRUCT */
/*
 * wait for an ARP event to complete.
 */
struct tSDP_IPOIB_WAIT_STRUCT {
  tSDP_PATH_LOOKUP_ID     plid;         /* request identifier */
  tSDP_PATH_LOOKUP_FUNC   func;         /* callback function for completion */
  tPTR                    arg;          /* user argument */
  struct net_device      *dev;          /* ipoib device */
  tTS_KERNEL_TIMER_STRUCT timer;        /* retry timer */
  tUINT8                  retry;        /* retry counter */
  tUINT8                  flags;        /* usage flags */
  tUINT8                  hw[ETH_ALEN]; /* hardware address */
  tUINT32                 src_addr;     /* requested address. */
  tUINT32                 dst_addr;     /* requested address. */
  tUINT32                 gw_addr;      /* next hop IP address */
  tUINT8                  local_rt;     /* local route only */
  tINT32                  bound_dev;    /* bound device interface */
  tTS_IB_GID              src_gid;      /* source GID */
  tTS_IB_GID              dst_gid;      /* destination GID */
  tTS_IB_PORT             hw_port;      /* hardware port */
  tTS_IB_DEVICE_HANDLE    ca;           /* hardware HCA */
  tTS_IB_CLIENT_QUERY_TID tid;          /* path record lookup transactionID */
  tSDP_IPOIB_WAIT         next;         /* next element in wait list. */
  tSDP_IPOIB_WAIT        *p_next;       /* previous next element in list */
}; /* tSDP_IPOIB_WAIT_STRUCT */

/*
 * wait for an ARP event to complete.
 */
struct tSDP_PATH_ELEMENT_STRUCT {
  tUINT32                   src_addr; /* requested address. */
  tUINT32                   dst_addr; /* requested address. */
  tUINT32                   usage;    /* last used time. */
  tTS_IB_PORT               hw_port;  /* source port */
  tTS_IB_DEVICE_HANDLE      ca;       /* hardware HCA */
  tTS_IB_PATH_RECORD_STRUCT path_s;   /* path structure */
  tSDP_PATH_ELEMENT         next;     /* next element in wait list. */
  tSDP_PATH_ELEMENT        *p_next;   /* previous next element in list */
}; /* tSDP_PATH_ELEMENT_STRUCT */

#endif /* _TS_SDP_LINK_H */
