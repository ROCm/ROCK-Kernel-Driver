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

  $Id: ip2pr_priv.h 35 2004-04-09 05:34:32Z roland $
*/

#ifndef _TS_IP2PR_PRIV_H
#define _TS_IP2PR_PRIV_H
/*
 * topspin generic includes
 */
#include <ib_legacy_types.h>
#include <trace_codes.h>
#include <trace_masks.h>
#include <ts_kernel_trace.h>
#include <ts_kernel_services.h>
#include <ts_kernel_timer.h>
/*
 * kernel includes
 */
#include <asm/atomic.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/errno.h>
/* TODO: fix correctly (Scott). */
#ifndef ECANCELED
#define ECANCELED 125
#endif
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/net.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/socket.h>
#include <linux/ctype.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
/*
 * RH AS 2.1 2.4.9-e.3 kernel has min and max in sock.h.
 * See ts_kernel_services.h for our definition.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,10)
#undef min
#undef max
#endif
#include <net/sock.h>
#include <net/route.h>
#include <net/dst.h>
#include <net/ip.h>

/*
 * topspin IB includes
 */
#include <ib_legacy_types.h>
#include <ts_ib_cm.h>
#include <ts_ib_sa_client.h>
#include <ts_kernel_trace.h>
#include <ts_ib_core.h>

#include "ipoib_proto.h"
#include "ip2pr_export.h"
#include "ip2pr_proc.h"

/* ------------------------------------------------------------------------- */
/*                             IP over IB Section                            */
/* ------------------------------------------------------------------------- */

#define TS_IP2PR_IPOIB_MGID_MASK_HIGH (0xFF12401B00000000ULL)
#define TS_IP2PR_IPOIB_MGID_MASK_LOW  (0x00000000FFFFFFFFULL)
#define TS_IP2PR_IPOIB_QPN_MASK        0x00FFFFFF

#define TS_IP2PR_IPOIB_ADDR_LEN    20
#define TS_IP2PR_IPOIB_ENCAP_LEN    4
#define TS_IP2PR_IPOIB_HDR_LEN     44
#define TS_IP2PR_IPOIB_PSEUDO_LEN  40
#define TS_IP2PR_IPOIB_GRH_LEN     40
/*
 * IPoIB hardware address.
 */
struct tIP2PR_IPOIB_ADDR_STRUCT {
  tUINT32 qpn;        /* MSB = reserved, low 3 bytes=QPN */
  union {
    tUINT8  all[16];
    struct {
      tUINT64 high;
      tUINT64 low;
    } s;
  } gid;
} __attribute__ ((packed));
typedef struct tIP2PR_IPOIB_ADDR_STRUCT tIP2PR_IPOIB_ADDR_STRUCT, *tIP2PR_IPOIB_ADDR;
/*
 * The two src and dst addresses are the same size as the GRH.
 */
struct tIP2PR_IPOIB_HDR_STRUCT {
  tIP2PR_IPOIB_ADDR_STRUCT src;
  tIP2PR_IPOIB_ADDR_STRUCT dst;
  tUINT16 proto;
  tUINT16 reserved;
} __attribute__ ((packed));
/*
 * Ethernet/IPoIB pseudo ARP header, used by out IPoIB driver.
 */
struct tIP2PR_IPOIB_ARP_STRUCT {
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
typedef struct tIP2PR_IPOIB_ARP_STRUCT tIP2PR_IPOIB_ARP_STRUCT, *tIP2PR_IPOIB_ARP;

typedef enum {
  IP2PR_LOCK_HELD = 0,
  IP2PR_LOCK_NOT_HELD = 1
} IP2PR_USE_LOCK;

/*
 * macro for determining if a net device is out IPoIB device.
 */
#define TS_IP2PR_IPOIB_DEV_TOPSPIN(dev) \
        ((0 == strncmp((dev)->name, "ib", 2)) ? 0 : -1)

#define TS_IP2PR_IPOIB_RETRY_VALUE    3        /* number of retries. */
#define TS_IP2PR_IPOIB_RETRY_INTERVAL (1)      /* retry frequency */
#define TS_IP2PR_IPOIB_PROC_DUMP_SIZE 24
#define TS_IP2PR_PATH_PROC_DUMP_SIZE  87

#define TS_IP2PR_DEV_PATH_WAIT       (2)
#define TS_IP2PR_PATH_RETRIES        (5)
#define TS_IP2PR_PATH_BACKOFF        (20)

#define TS_IP2PR_PATH_TIMER_INTERVAL (15 * (HZ))  /* cache sweep frequency */
#define TS_IP2PR_PATH_REAPING_AGE    (300)        /* idle time before reaping */

#define TS_IP2PR_MAX_DEV_PATH_WAIT       (20)
#define TS_IP2PR_PATH_MAX_RETRIES        (25)     /* number of retries. */
#define TS_IP2PR_PATH_MAX_BACKOFF        (100)
#define TS_IP2PR_PATH_MAX_CACHE_TIMEOUT  (1500)

/*
 * State flags
 */
#define IP2PR_STATE_ARP_WAIT		0x01
#define IP2PR_STATE_PATH_WAIT		0x02

/*
 * usage flags.
 */
#define TS_IP2PR_IPOIB_FLAG_FUNC 0x01  /* consumer reference */
#define TS_IP2PR_IPOIB_FLAG_TIME 0x02  /* timer reference */
#define TS_IP2PR_IPOIB_FLAG_TASK 0x04  /* tasklet reference */

#define TS_IP2PR_IPOIB_FLAG_GET_FUNC(iw) ((iw)->flags & TS_IP2PR_IPOIB_FLAG_FUNC)
#define TS_IP2PR_IPOIB_FLAG_GET_TIME(iw) ((iw)->flags & TS_IP2PR_IPOIB_FLAG_TIME)
#define TS_IP2PR_IPOIB_FLAG_GET_TASK(iw) ((iw)->flags & TS_IP2PR_IPOIB_FLAG_TASK)
#define TS_IP2PR_IPOIB_FLAG_SET_FUNC(iw) ((iw)->flags |= TS_IP2PR_IPOIB_FLAG_FUNC)
#define TS_IP2PR_IPOIB_FLAG_SET_TIME(iw) ((iw)->flags |= TS_IP2PR_IPOIB_FLAG_TIME)
#define TS_IP2PR_IPOIB_FLAG_SET_TASK(iw) ((iw)->flags |= TS_IP2PR_IPOIB_FLAG_TASK)
#define TS_IP2PR_IPOIB_FLAG_CLR_FUNC(iw) ((iw)->flags &= ~TS_IP2PR_IPOIB_FLAG_FUNC)
#define TS_IP2PR_IPOIB_FLAG_CLR_TIME(iw) ((iw)->flags &= ~TS_IP2PR_IPOIB_FLAG_TIME)
#define TS_IP2PR_IPOIB_FLAG_CLR_TASK(iw) ((iw)->flags &= ~TS_IP2PR_IPOIB_FLAG_TASK)
#define TS_IP2PR_IPOIB_FLAGS_EMPTY(iw) ((iw)->flags == 0 ? 1 : 0)

#define LOOKUP_GID2PR  1
#define LOOKUP_IP2PR   2
/* ------------------------------------------------------------------------- */
/* Event driven address resoslution                                          */
/* ------------------------------------------------------------------------- */
/*
 * tables
 */
typedef struct tIP2PR_IPOIB_WAIT_STRUCT tIP2PR_IPOIB_WAIT_STRUCT, *tIP2PR_IPOIB_WAIT;
/*
 * wait for an ARP event to complete.
 */
struct tIP2PR_IPOIB_WAIT_STRUCT {
  tINT8                   type;         /* ip2pr or gid2pr */
  tIP2PR_PATH_LOOKUP_ID   plid;         /* request identifier */
  tPTR                    func;         /* callback function for completion */
  tPTR                    arg;          /* user argument */
  struct net_device      *dev;          /* ipoib device */
  tTS_KERNEL_TIMER_STRUCT timer;        /* retry timer */
  tUINT8                  retry;        /* retry counter */
  tUINT8                  flags;        /* usage flags */
  tUINT8                  state;        /* current state */
  tUINT8                  hw[ETH_ALEN]; /* hardware address */
  tUINT32                 src_addr;     /* requested address. */
  tUINT32                 dst_addr;     /* requested address. */
  tUINT32                 gw_addr;      /* next hop IP address */
  tUINT8                  local_rt;     /* local route only */
  tINT32                  bound_dev;    /* bound device interface */
  tTS_IB_GID              src_gid;      /* source GID */
  tTS_IB_GID              dst_gid;      /* destination GID */
  tTS_IB_PKEY             pkey;         /* pkey to use */
  tTS_IB_PORT             hw_port;      /* hardware port */
  tTS_IB_DEVICE_HANDLE    ca;           /* hardware HCA */
  tUINT32                 prev_timeout; /* timeout value for pending request */
  tTS_IB_CLIENT_QUERY_TID tid;          /* path record lookup transactionID */
  spinlock_t              lock;
  tIP2PR_IPOIB_WAIT       next;         /* next element in wait list. */
  tIP2PR_IPOIB_WAIT      *p_next;       /* previous next element in list */
}; /* tIP2PR_IPOIB_WAIT_STRUCT */

typedef struct tIP2PR_PATH_ELEMENT_STRUCT tIP2PR_PATH_ELEMENT_STRUCT, \
              *tIP2PR_PATH_ELEMENT;
/*
 * wait for an ARP event to complete.
 */
struct tIP2PR_PATH_ELEMENT_STRUCT {
  tUINT32                   src_addr; /* requested address. */
  tUINT32                   dst_addr; /* requested address. */
  tUINT32                   usage;    /* last used time. */
  tTS_IB_PORT               hw_port;  /* source port */
  tTS_IB_DEVICE_HANDLE      ca;       /* hardware HCA */
  tTS_IB_PATH_RECORD_STRUCT path_s;   /* path structure */
  tIP2PR_PATH_ELEMENT       next;   /* next element in wait list. */
  tIP2PR_PATH_ELEMENT      *p_next; /* previous next element in list */
}; /* tIP2PR_PATH_ELEMENT_STRUCT */

struct tIP2PR_USER_REQ_STRUCT {
  tTS_IB_PATH_RECORD_STRUCT    path_record;
  tINT32                status;
  tTS_IB_DEVICE_HANDLE  device;
  tTS_IB_PORT           port;
  struct semaphore      sem;
};
typedef struct tIP2PR_USER_REQ_STRUCT tIP2PR_USER_REQ_STRUCT, *tIP2PR_USER_REQ;

/*
 * List of Path records cached on a port on a hca
 */
typedef struct tIP2PR_GID_PR_ELEMENT_STRUCT tIP2PR_GID_PR_ELEMENT_STRUCT, \
               *tIP2PR_GID_PR_ELEMENT;
struct tIP2PR_GID_PR_ELEMENT_STRUCT {
  tTS_IB_PATH_RECORD_STRUCT  path_record;
  tUINT32                    usage;    /* last used time. */
  tIP2PR_GID_PR_ELEMENT      next;
  tIP2PR_GID_PR_ELEMENT     *p_next;
};

/*
 * List of Source GID's
 */
typedef struct tIP2PR_SGID_ELEMENT_STRUCT tIP2PR_SGID_ELEMENT_STRUCT, \
               *tIP2PR_SGID_ELEMENT;
struct tIP2PR_SGID_ELEMENT_STRUCT {
  tTS_IB_GID                gid;
  tTS_IB_DEVICE_HANDLE      ca;
  tTS_IB_PORT               port;
  tTS_IB_PORT_STATE         port_state;
  int                       gid_index;
  tIP2PR_GID_PR_ELEMENT     pr_list;
  tIP2PR_SGID_ELEMENT       next;       /* next element in the GID list */
  tIP2PR_SGID_ELEMENT      *p_next;     /* previous next element in the list */
};

struct tIP2PR_LINK_ROOT_STRUCT {
  /*
   * waiting for resolution table
   */
  tIP2PR_IPOIB_WAIT  wait_list;
  kmem_cache_t      *wait_cache;
  spinlock_t         wait_lock;
  int                max_retries;
  int                retry_timeout;
  int                backoff;

  int		     cache_timeout;

  /*
   * path record cache list.
   */
  tIP2PR_PATH_ELEMENT path_list;
  kmem_cache_t      *path_cache;
  spinlock_t         path_lock;
  /*
   * user request cache
   */
  kmem_cache_t      *user_req;

  /*
   * source gid list
   */
  tIP2PR_SGID_ELEMENT    src_gid_list;
  kmem_cache_t      *src_gid_cache;
  kmem_cache_t      *gid_pr_cache;
  spinlock_t        gid_lock;

}; /* tIP2PR_LINK_ROOT_STRUCT */
typedef struct tIP2PR_LINK_ROOT_STRUCT tIP2PR_LINK_ROOT_STRUCT, *tIP2PR_LINK_ROOT;

#define TS_EXPECT(mod, expr)
#define TS_CHECK_NULL(value, result)
#define TS_CHECK_LT(value, bound, result)
#define TS_CHECK_GT(value, bound, result)
#define TS_CHECK_EQ(value, test, result)
#define TS_CHECK_EXPR(expr, result)

#define IP2PR_MAX_HCAS      10
#define TS_IP2PR_INVALID_ASYNC_HANDLE (NULL)

#define IP2PR_DEVNAME   "ts_ip2pr"

#endif  /* _TS_IP2PR_PRIV_H */
