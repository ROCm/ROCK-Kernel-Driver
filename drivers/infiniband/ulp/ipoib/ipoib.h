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

  $Id: ipoib.h 53 2004-04-14 20:10:38Z roland $
*/

#ifndef _TS_IPOIB_H
#define _TS_IPOIB_H

#include <linux/config.h>
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include <linux/modversions.h>
#endif

#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>   /* struct device, and other headers */
#include <linux/proc_fs.h>
#include <linux/completion.h>

#include <asm/atomic.h>
#include <asm/semaphore.h>

#include "ipoib_proto.h"

#include <ts_ib_core.h>
#include <ts_ib_header_types.h>

#include <ts_ib_sa_client.h>

#include <ts_kernel_services.h>
#include <ts_kernel_thread.h>

#ifdef TS_KERNEL_2_6
#define tq_struct work_struct
#define INIT_TQUEUE(x,y,z) INIT_WORK(x,y,z)
#define schedule_task schedule_work
#endif

/* constants */

#define ARPHRD_INFINIBAND 32

#define TS_IPOIB_PACKET_SIZE	2048

enum {
  TS_IPOIB_RX_RING_SIZE       = 128,
  TS_IPOIB_TX_RING_SIZE       = 64,

  TS_IPOIB_BUF_SIZE           = TS_IPOIB_PACKET_SIZE + sizeof (tTS_IB_GRH_STRUCT),

  TS_IPOIB_ADDRESS_HASH_BYTES = ETH_ALEN,
  TS_IPOIB_ENCAP_LEN          = 4,
  TS_IPOIB_HW_ADDR_LEN        = 20,

  TS_IPOIB_FLAG_TX_FULL       = 0,
  TS_IPOIB_FLAG_TIMEOUT       = 1,
  TS_IPOIB_FLAG_OPER_UP       = 2,
  TS_IPOIB_FLAG_ADMIN_UP      = 3,
  TS_IPOIB_PKEY_ASSIGNED      = 4,
  TS_IPOIB_FLAG_SUBINTERFACE  = 5,
  TS_IPOIB_FLAG_CHASSISINTF   = 6,

  TS_IPOIB_MAX_BACKOFF_SECONDS = 16,

  TS_IPOIB_MCAST_FLAG_FOUND    = 0,	/* used in set_multicast_list */
  TS_IPOIB_MCAST_FLAG_SENDONLY = 1,
  TS_IPOIB_MCAST_FLAG_BUSY     = 2,	/* joining or already joined */
  TS_IPOIB_MCAST_FLAG_ATTACHED = 3,
};

/* structs */

typedef struct tTS_IPOIB_ARP_CACHE_STRUCT tTS_IPOIB_ARP_CACHE_STRUCT,
  *tTS_IPOIB_ARP_CACHE;
typedef struct tTS_IPOIB_ARP_ENTRY_STRUCT tTS_IPOIB_ARP_ENTRY_STRUCT,
  *tTS_IPOIB_ARP_ENTRY;
typedef struct tTS_IPOIB_ARP_ITERATOR_STRUCT tTS_IPOIB_ARP_ITERATOR_STRUCT,
  *tTS_IPOIB_ARP_ITERATOR;

typedef struct tTS_IPOIB_MULTICAST_ITERATOR_STRUCT tTS_IPOIB_MULTICAST_ITERATOR_STRUCT,
  *tTS_IPOIB_MULTICAST_ITERATOR;
typedef struct tTS_IPOIB_MULTICAST_GROUP_STRUCT tTS_IPOIB_MULTICAST_GROUP_STRUCT,
  *tTS_IPOIB_MULTICAST_GROUP;

typedef void (*tTS_IPOIB_TX_CALLBACK)(void *);

struct tTS_IPOIB_TX_BUF {
  struct sk_buff            *skb;
  tTS_IPOIB_TX_CALLBACK      callback;
  void                      *ptr;
};

struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT {
  spinlock_t        lock;
  struct net_device dev;
  struct list_head  list;

  struct list_head  child_intfs;

  unsigned long     flags;

  struct semaphore               mcast_mutex;

  tTS_KERNEL_THREAD              mcast_thread;
  tTS_IB_CLIENT_QUERY_TID        mcast_tid;
  int                            mcast_status;
  struct completion              mcast_complete;

  tTS_IPOIB_MULTICAST_GROUP      broadcast;
  struct list_head               multicast_list;
  rb_root_t                      multicast_tree;

  atomic_t                       mcast_joins;

  struct tq_struct               restart_task;

  tTS_IB_DEVICE_HANDLE ca;
  tTS_IB_PORT          port;
  tTS_IB_PD_HANDLE     pd;
  tTS_IB_MR_HANDLE     mr;
  tTS_IB_LKEY          lkey;
  tTS_IB_CQ_HANDLE     cq;
  tTS_IB_QP_HANDLE     qp;
  tTS_IB_QKEY          qkey;
  tTS_IB_PKEY          pkey;
  tTS_KERNEL_THREAD    pkey_thread;

  tTS_IB_GID           local_gid;
  tTS_IB_LID           local_lid;
  tTS_IB_QPN           local_qpn;

  tTS_IB_GID           bcast_gid;

  unsigned int         admin_mtu;
  unsigned int         mcast_mtu;

  struct sk_buff         **rx_ring;

  struct tTS_IPOIB_TX_BUF *tx_ring;
  int                      tx_head;
  atomic_t                 tx_free;

  tTS_IPOIB_ARP_CACHE      arp_cache;

  struct proc_dir_entry   *arp_proc_entry;
  struct proc_dir_entry   *mcast_proc_entry;

  tTS_IB_ASYNC_EVENT_HANDLER_HANDLE active_handler;

  struct net_device_stats  stats;
};

/* list of IPoIB network devices */
extern struct semaphore ipoib_device_mutex;
extern struct list_head ipoib_device_list;

extern tTS_IB_GID ipoib_broadcast_mgid;

/* functions */

void ipoib_completion(tTS_IB_CQ_HANDLE    cq,
                      struct ib_cq_entry *entry,
                      void *dev_ptr);

int tsIpoibDeviceSend(
                      struct net_device *dev,
                      struct sk_buff *skb,
                      tTS_IPOIB_TX_CALLBACK callback,
                      void *ptr,
                      tTS_IB_ADDRESS_HANDLE address,
                      tTS_IB_QPN qpn
                      );

int tsIpoibDeviceSendPacket(
                            struct net_device *dev,
                            struct sk_buff *skb
                            );

struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *tsIpoibAllocateInterface(
                                                                 void
                                                                 );

int tsIpoibDeviceIbOpen(
                        struct net_device *dev
                        );

int tsIpoibDeviceIbUp(
                      struct net_device *dev
                      );

int tsIpoibDeviceIbDown(
                        struct net_device *dev
                        );

int tsIpoibDeviceIbStop(
                        struct net_device *dev
                        );

int tsIpoibDeviceInit(
                      struct net_device *dev,
                      tTS_IB_DEVICE_HANDLE ca,
                      int port
                      );

void tsIpoibDeviceCleanup(
                          struct net_device *dev
                          );

int tsIpoibDeviceIbInit(
                        struct net_device *dev,
                        tTS_IB_DEVICE_HANDLE ca,
                        int port
                        );

void tsIpoibDeviceIbFlush(
                          void *dev
                          );

void tsIpoibDeviceIbCleanup(
                            struct net_device *dev
                            );

void tsIpoibArpEntryGet(
                        tTS_IPOIB_ARP_ENTRY entry
                        );

void tsIpoibArpEntryPut(
                        tTS_IPOIB_ARP_ENTRY entry
                        );

tTS_IPOIB_ARP_ENTRY tsIpoibDeviceArpAdd(
                                        struct net_device *dev,
                                        tTS_IB_GID gid,
                                        tTS_IB_QPN qpn
                                        );

tTS_IPOIB_ARP_ENTRY tsIpoibDeviceArpLocalAdd(
                                             struct net_device *dev,
                                             tTS_IB_GID gid,
                                             tTS_IB_QPN qpn
                                             );

tTS_IPOIB_ARP_ITERATOR tsIpoibArpIteratorInit(
                                              struct net_device *dev
                                              );

void tsIpoibArpIteratorFree(
                            tTS_IPOIB_ARP_ITERATOR iter
                            );

int tsIpoibArpIteratorNext(
                           tTS_IPOIB_ARP_ITERATOR iter
                           );

void tsIpoibArpIteratorRead(
                            tTS_IPOIB_ARP_ITERATOR iter,
                            uint8_t *hash,
                            tTS_IB_GID gid,
                            tTS_IB_QPN *qpn,
                            unsigned long *created,
                            unsigned long *last_verify,
                            unsigned int *queuelen,
                            unsigned int *complete
                            );

int tsIpoibDeviceArpLookup(
                           struct net_device *dev,
                           uint8_t *hash,
                           tTS_IPOIB_ARP_ENTRY *entry
                           );

int tsIpoibArpEntryQueuePacket(
                               tTS_IPOIB_ARP_ENTRY entry,
                               struct sk_buff *skb
                               );

int tsIpoibDeviceArpSend(
                         struct net_device *dev,
                         tTS_IPOIB_ARP_ENTRY entry,
                         struct sk_buff *skb
                         );

int tsIpoibDeviceArpRewriteReceive(
                                   struct net_device *dev,
                                   struct sk_buff *skb
                                   );

int tsIpoibDeviceArpRewriteSend(
                                struct net_device *dev,
                                struct sk_buff *skb
                                );

int tsIpoibDeviceArpInit(
                         struct net_device *dev
                         );

void tsIpoibDeviceArpFlush(
                           struct net_device *dev
                           );

void tsIpoibDeviceArpCleanup(
                             struct net_device *dev
                             );

int tsIpoibDeviceArpDelete(
                           struct net_device *dev,
                           const uint8_t     *hash
                           );

int tsIpoibDeviceProcInit(
                          struct net_device *dev
                          );

void tsIpoibDeviceProcCleanup(
                              struct net_device *dev
                              );

int tsIpoibProcInit(
                    void
                    );

void tsIpoibProcCleanup(
                        void
                        );

void tsIpoibMulticastGroupGet(
                              tTS_IPOIB_MULTICAST_GROUP mcast
                              );

void tsIpoibMulticastGroupPut(
                              tTS_IPOIB_MULTICAST_GROUP mcast
                              );

int tsIpoibDeviceBroadcastLookup(
                                 struct net_device *dev,
                                 tTS_IPOIB_MULTICAST_GROUP *mcast
                                 );

int tsIpoibDeviceMulticastLookup(
                                 struct net_device *dev,
                                 tTS_IB_GID mgid,
                                 tTS_IPOIB_MULTICAST_GROUP *mcast
                                 );

int tsIpoibMulticastGroupQueuePacket(
                                     tTS_IPOIB_MULTICAST_GROUP mcast,
                                     struct sk_buff *skb
                                     );

int tsIpoibDeviceMulticastSend(
                               struct net_device *dev,
                               tTS_IPOIB_MULTICAST_GROUP mcast,
                               struct sk_buff *skb
                               );

void tsIpoibDeviceMulticastRestartTask(
                                       void *dev_ptr
                                       );

int tsIpoibDeviceStartMulticastThread(
                                      struct net_device *dev
                                      );

int tsIpoibDeviceStopMulticastThread(
                                     struct net_device *dev
                                     );

void tsIpoibDeviceMulticastDown(
                                struct net_device *dev
                                );

void tsIpoibDeviceMulticastFlush(
                                 struct net_device *dev
                                 );

tTS_IPOIB_MULTICAST_ITERATOR tsIpoibMulticastIteratorInit(
                                                          struct net_device *dev
                                                          );

void tsIpoibMulticastIteratorFree(
                                  tTS_IPOIB_MULTICAST_ITERATOR iter
                                  );

int tsIpoibMulticastIteratorNext(
                                 tTS_IPOIB_MULTICAST_ITERATOR iter
                                 );

void tsIpoibMulticastIteratorRead(
                                  tTS_IPOIB_MULTICAST_ITERATOR iter,
                                  tTS_IB_GID gid,
                                  unsigned long *created,
                                  unsigned int *queuelen,
                                  unsigned int *complete,
                                  unsigned int *send_only
                                  );

int tsIpoibDeviceMulticastAttach(
                                 struct net_device *dev,
                                 tTS_IB_LID mlid,
                                 tTS_IB_GID mgid
                                 );

int tsIpoibDeviceMulticastDetach(
                                 struct net_device *dev,
                                 tTS_IB_LID mlid,
                                 tTS_IB_GID mgid
                                 );

int tsIpoibDeviceTransportQpCreate(
                                   struct net_device *dev
                                   );

void tsIpoibDeviceTransportQpDestroy(
                                     struct net_device *dev
                                     );

int tsIpoibDeviceTransportInit(
                               struct net_device *dev,
                               tTS_IB_DEVICE_HANDLE ca
                               );

void tsIpoibDeviceTransportCleanup(
                                   struct net_device *dev
                                   );

int ipoib_add_port(const char *format,
                   tTS_IB_DEVICE_HANDLE device,
                   tTS_IB_PORT port,
                   int chassis_intf);

int ipoib_transport_create_devices(void);
void ipoib_transport_cleanup(void);

int tsIpoibTransportPortMonitorStart(
                                     struct net_device *dev
                                     );

void tsIpoibTransportPortMonitorStop(
                                     struct net_device *dev
                                     );

int tsIpoibVlanInit(
                    void
                    );

void tsIpoibVlanCleanup(
                        void
                        );

int tsIpoibDeviceStartPKeyThread(
                                  struct net_device *dev
                                 );

int tsIpoibDeviceStopPKeyThread(
                                  struct net_device *dev
                                );

void tsIpoibDeviceCheckPkeyPresence(
                                      struct net_device *dev
                                    );

int tsIpoibDeviceDelayOpen(
                        struct net_device *dev
                        );

/* debugging helper */

static inline void tsIpoibDumpSkb(
                                  struct sk_buff *skb
                                  ) {
  int i;

  for (i = 0; i < skb->len; ++i) {
    if (i % 8 == 0) {
      printk(KERN_INFO "   ");
    }
    printk(" %02x", skb->data[i]);
    if ((i + 1) % 8 == 0 || i == skb->len - 1) {
      printk("\n");
    }
  }
}

#define IPOIB_GID_FMT		"%02x%02x%02x%02x%02x%02x%02x%02x" \
				"%02x%02x%02x%02x%02x%02x%02x%02x"

#define IPOIB_GID_ARG(gid)	gid[ 0], gid[ 1], gid[ 2], gid[ 3], \
				gid[ 4], gid[ 5], gid[ 6], gid[ 7], \
				gid[ 8], gid[ 9], gid[10], gid[11], \
				gid[12], gid[13], gid[14], gid[15]

#endif /* _TS_IPOIB_H */
