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

  $Id: ipoib_ib.c 67 2004-04-18 15:50:54Z roland $
*/

#include "ipoib.h"

#include "ts_ib_sa_client.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_cache.h"

#include <asm/io.h>

tTS_IB_GID broadcast_mgid = {
  0xff, 0x12, 0x40, 0x1b, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff
};

static int _tsIpoibReceive(
                           struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv,
                           tTS_IB_WORK_REQUEST_ID                  work_request_id,
                           struct sk_buff                         *skb
                           )
{
  tTS_IB_GATHER_SCATTER_STRUCT list = {
    .address = (unsigned long) skb->data,
    .length  = TS_IPOIB_BUF_SIZE,
    .key     = priv->lkey,
  };
  tTS_IB_RECEIVE_PARAM_STRUCT param = {
    .work_request_id     = work_request_id,
    .scatter_list        = &list,
    .num_scatter_entries = 1,
    .device_specific     = NULL,
    .signaled            = 1,
  };

  tsKernelCacheSync(skb->data, TS_IPOIB_BUF_SIZE, TS_DMA_FROM_DEVICE);

  return tsIbReceive(priv->qp, &param, 1);
}

/* =============================================================== */
/*.._tsIpoibDeviceIbPostReceive -- post a receive buffer           */
static int _tsIpoibDevicePostReceive(
                                     struct net_device *dev,
                                     int id
                                     ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  struct sk_buff *skb;
  int ret;

  skb = dev_alloc_skb(TS_IPOIB_BUF_SIZE + 4);
  if (!skb) {
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: failed to allocate receive buffer",
                   dev->name);
    return -ENOMEM;
  }
  skb_reserve(skb, 4);		/* 16 byte align IP header */
  priv->rx_ring[id] = skb;

  ret = _tsIpoibReceive(priv, id, skb);
  if (ret) {
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: tsIpoibReceive failed for buf %d",
                   dev->name, id);
  }

  return ret;
}

/* =============================================================== */
/*.._tsIpoibDeviceIbPostReceives -- post all receive buffers       */
static int _tsIpoibDevicePostReceives(
                                      struct net_device *dev
                                      ) {
  int i;

  for (i = 0; i < TS_IPOIB_RX_RING_SIZE; ++i) {
    if (_tsIpoibDevicePostReceive(dev, i)) {
      TS_REPORT_FATAL(MOD_IB_NET,
                      "%s: tsIpoibDevicePostReceive failed for buf %d",
                      dev->name, i);
      return -EIO;
    }
  }

  return 0;
}

/* =============================================================== */
/*..ipoib_completion -- analogous to interrupt handler             */
void ipoib_completion(tTS_IB_CQ_HANDLE    cq,
                      struct ib_cq_entry *entry,
                      void *dev_ptr)
{
  struct net_device *dev = (struct net_device *) dev_ptr;
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  unsigned int work_request_id = (unsigned int)entry->work_request_id;

  TS_REPORT_DATA(MOD_IB_NET,
                 "%s: called: id %d, op %d, status: %d",
                 dev->name, work_request_id, entry->op, entry->status);

  if (0 == entry->status) {
    switch (entry->op) {
    case TS_IB_OP_RECEIVE:
      if (work_request_id < TS_IPOIB_RX_RING_SIZE) {
        struct sk_buff *skb = priv->rx_ring[work_request_id];

        TS_REPORT_DATA(MOD_IB_NET,
                       "%s: received %d bytes, SLID 0x%04x",
                       dev->name, entry->bytes_transferred, entry->slid);

        skb_put(skb, entry->bytes_transferred);
        skb_pull(skb, TS_IB_GRH_BYTES);

        if (entry->slid != priv->local_lid || entry->sqpn != priv->local_qpn) {
          struct ethhdr *header;

          skb->protocol = *(uint16_t *) skb->data;

          /* pull the IPoIB header and add an ethernet header */
          skb_pull(skb, TS_IPOIB_ENCAP_LEN);

          header = (struct ethhdr *)skb_push(skb, ETH_HLEN);

          /*
           * We could figure out the MAC address from the IPoIB header and
           * matching, but it's probably too much effort for what's worth
           */
          memset(header->h_dest, 0, sizeof(header->h_dest));
          memset(header->h_source, 0, sizeof(header->h_source));
          header->h_proto = skb->protocol;

          skb->mac.raw  = skb->data;
          skb_pull(skb, ETH_HLEN);

          dev->last_rx = jiffies;
          ++priv->stats.rx_packets;
          priv->stats.rx_bytes += skb->len;

          if (skb->protocol == htons(ETH_P_ARP)) {
            if (tsIpoibDeviceArpRewriteReceive(dev, skb)) {
              TS_REPORT_WARN(MOD_IB_NET,
                             "%s: tsIpoibArpRewriteReceive failed",
                             dev->name);
            }
          } else {
            skb->dev      = dev;
            skb->pkt_type = PACKET_HOST;
            netif_rx_ni(skb);
          }
        } else {
          TS_REPORT_DATA(MOD_IB_NET,
                         "%s: dropping loopback packet",
                         dev->name);
          dev_kfree_skb(skb);
        }

        /* repost receive */
        if (_tsIpoibDevicePostReceive(dev, work_request_id)) {
          TS_REPORT_FATAL(MOD_IB_NET,
                          "%s: tsIpoibDevicePostReceive failed for buf %d",
                          dev->name, work_request_id);
        }
      } else {
        TS_REPORT_WARN(MOD_IB_NET,
                       "%s: completion event with wrid %d",
                       dev->name,
                       work_request_id);
      }
      break;

    case TS_IB_OP_SEND:
    {
      struct tTS_IPOIB_TX_BUF *tx_req;

      if (work_request_id >= TS_IPOIB_TX_RING_SIZE) {
        TS_REPORT_WARN(MOD_IB_NET,
                       "%s: completion event with wrid %d",
                       dev->name,
                       work_request_id);
        break;
      }

      TS_REPORT_DATA(MOD_IB_NET,
                     "%s: send complete, wrid %d",
                     dev->name, work_request_id);

      tx_req = &priv->tx_ring[work_request_id];

      clear_bit(TS_IPOIB_FLAG_TIMEOUT, &priv->flags);

      ++priv->stats.tx_packets;
      priv->stats.tx_bytes += tx_req->skb->len;

      dev_kfree_skb(tx_req->skb);
      tx_req->skb = NULL;

      tx_req->callback(tx_req->ptr);
      tx_req->callback = NULL;
      tx_req->ptr = NULL;

      atomic_inc(&priv->tx_free);
      if (atomic_read(&priv->tx_free) > TS_IPOIB_TX_RING_SIZE / 2) {
        netif_wake_queue(dev);
      }

      break;
    }

    default:
      TS_REPORT_WARN(MOD_IB_NET, "%s: got unexpected completion event (status=%d, op=%d)",
                     dev->name, entry->status, entry->op);
      break;
    }
  } else {
    TS_REPORT_WARN(MOD_IB_NET, "%s: got failed completion event (status=%d, wrid=%d, op=%d)",
                   dev->name, entry->status, work_request_id, entry->op);

    if (entry->op == TS_IB_OP_SEND) {
      if (work_request_id < TS_IPOIB_TX_RING_SIZE) {
        struct tTS_IPOIB_TX_BUF *tx_req;

        tx_req = &priv->tx_ring[work_request_id];

        dev_kfree_skb(tx_req->skb);
        tx_req->skb = NULL;
      }
    }
  }
}

static int _tsIpoibSend(
                        struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv,
                        tTS_IB_WORK_REQUEST_ID                  work_request_id,
                        tTS_IB_ADDRESS_HANDLE                   address,
                        tTS_IB_QPN                              qpn,
                        struct sk_buff                         *skb
                        )
{
  tTS_IB_GATHER_SCATTER_STRUCT list = {
    .address = (unsigned long)skb->data,
    .length  = skb->len,
    .key     = priv->lkey,
  };
  tTS_IB_SEND_PARAM_STRUCT param = {
    .work_request_id    = work_request_id,
    .op                 = TS_IB_OP_SEND,
    .gather_list        = &list,
    .num_gather_entries = 1,
    .dest_qpn           = qpn,
    .dest_qkey          = priv->qkey,
    .dest_address       = address,
    .signaled           = 1,
  };

  tsKernelCacheSync(skb->data, skb->len, TS_DMA_TO_DEVICE);

  return tsIbSend(priv->qp, &param, 1);
}

/* =============================================================== */
/*..tsIpoibDeviceSend -- schedule an IB send work request          */
int tsIpoibDeviceSend(
                      struct net_device *dev,
                      struct sk_buff *skb,
                      tTS_IPOIB_TX_CALLBACK callback,
                      void *ptr,
                      tTS_IB_ADDRESS_HANDLE address,
                      tTS_IB_QPN qpn
                      ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  struct tTS_IPOIB_TX_BUF *tx_req;

  if (skb->len > dev->mtu + TS_IPOIB_HW_ADDR_LEN) {
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: packet len %d too long to send, dropping",
                   dev->name, skb->len);
    ++priv->stats.tx_dropped;
    ++priv->stats.tx_errors;

    goto err;
  }

  if (!(skb = skb_unshare(skb, GFP_ATOMIC))) {
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: failed to unshare sk_buff, dropping",
                   dev->name, skb->len);
    ++priv->stats.tx_dropped;
    ++priv->stats.tx_errors;

    goto err;
  }

  TS_REPORT_DATA(MOD_IB_NET,
                 "%s: sending packet, length=%d address=0x%08x qpn=0x%06x",
                 dev->name, skb->len, address, qpn);

  /* make the skb look like an IPoIB packet again */
  {
    struct ethhdr *header = (struct ethhdr *)skb->data;
    uint16_t *reserved, *ether_type;

    skb_pull(skb, ETH_HLEN);
    reserved = (uint16_t *)skb_push(skb, 2);
    ether_type = (uint16_t *)skb_push(skb, 2);

    *ether_type = header->h_proto;
    *reserved = 0;
  }

  /* We put the skb into the tx_ring _before_ we call tsIpoibSend()
     because it's entirely possible that the completion handler will
     run before we execute anything after the tsIpoibSend().  That
     means we have to make sure everything is properly recorded and
     our state is consistent before we call tsIpoibSend(). */
  tx_req = &priv->tx_ring[priv->tx_head];
  tx_req->skb = skb;
  tx_req->callback = callback;
  tx_req->ptr = ptr;

  if (_tsIpoibSend(priv, priv->tx_head, address, qpn, skb)) {
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: tsIpoibSend failed",
                   dev->name);
    ++priv->stats.tx_errors;
    tx_req->skb = NULL;
    tx_req->callback = NULL;
    tx_req->ptr = NULL;
  } else {
    dev->trans_start = jiffies;

    priv->tx_head = (priv->tx_head + 1) % TS_IPOIB_TX_RING_SIZE;

    if (atomic_dec_and_test(&priv->tx_free)) {
      TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_GEN,
               "%s: TX ring full, stopping Linux queue",
               dev->name);
      netif_stop_queue(dev);
    }

    return 0;
  }

err:
  dev_kfree_skb(skb);

  callback(ptr);

  return 0;
}

int tsIpoibDeviceIbUp(
                      struct net_device *dev
                      )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  set_bit(TS_IPOIB_FLAG_OPER_UP, &priv->flags);

  return tsIpoibDeviceStartMulticastThread(dev);
}

int tsIpoibDeviceIbOpen(
                        struct net_device *dev
                        )
{
  int ret;

  ret = tsIpoibDeviceTransportQpCreate(dev);
  if (ret) {
    TS_REPORT_FATAL(MOD_IB_NET,
                    "%s: tsIpoibDeviceTransportQpCreate returned %d",
                    dev->name, ret);
    return -1;
  }

  ret = _tsIpoibDevicePostReceives(dev);
  if (ret) {
    TS_REPORT_FATAL(MOD_IB_NET,
                    "%s: _tsIpoibDevicePostReceives returned %d",
                    dev->name, ret);
    return -1;
  }

  return 0;
}

/* =============================================================== */
/*..tsIpoibDeviceIbDown -- remove from multicast, etc              */
int tsIpoibDeviceIbDown(
                        struct net_device *dev
                        )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  int count = 0;

  clear_bit(TS_IPOIB_FLAG_OPER_UP, &priv->flags);
  netif_carrier_off(dev);

#if defined(TS_HOST_DRIVER)   || \
    defined(TS_ppc440_en_sun) || \
    defined(TS_ppc440_fc_sun) || \
    defined(TS_ppc440_fg_sun)
  /* Shutdown the P_Key thread if still active */
  if (!test_bit(TS_IPOIB_PKEY_ASSIGNED, &priv->flags))
    tsIpoibDeviceStopPKeyThread(dev);
#endif

  tsIpoibDeviceStopMulticastThread(dev);

  /*
   * Flush the multicast groups first so we stop any multicast joins. The
   * completion thread may have already died and we may deadlock waiting for
   * the completion thread to finish some multicast joins.
   */
  tsIpoibDeviceMulticastFlush(dev);

  /* Wait for all joins to finish first */
  while (atomic_read(&priv->mcast_joins)) {
    if (count == 5) {
      TS_REPORT_WARN(MOD_IB_NET,
                     "%s: still waiting on %d multicast joins still to complete",
                     dev->name, atomic_read(&priv->mcast_joins));
    }

    set_current_state(TASK_INTERRUPTIBLE);
    schedule_timeout(HZ);
    set_current_state(TASK_RUNNING);
    count++;
  }

  /* Delete broadcast and local addresses since they will be recreated */
  tsIpoibDeviceMulticastDown(dev);
  tsIpoibDeviceArpDelete(dev, dev->dev_addr);

  /* Invalidate all address vectors */
  tsIpoibDeviceArpFlush(dev);

  return 0;
}

/* =============================================================== */
/*..tsIpoibDeviceIbStop -- cleanup QP and RX ring                  */
int tsIpoibDeviceIbStop(
                        struct net_device *dev
                        )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  int i;

  /* Kill the existing QP and allocate a new one */
  if (priv->qp != TS_IB_HANDLE_INVALID) {
    tsIpoibDeviceTransportQpDestroy(dev);
  }

  for (i = 0; i < TS_IPOIB_RX_RING_SIZE; ++i) {
    if (priv->rx_ring[i]) {
      dev_kfree_skb(priv->rx_ring[i]);
      priv->rx_ring[i] = NULL;
    }
  }

  return 0;
}

/* =============================================================== */
/*..tsIpoibDeviceIbInit -- set up IB resources for iface           */
int tsIpoibDeviceIbInit(
                        struct net_device *dev,
                        tTS_IB_DEVICE_HANDLE ca,
                        int port
                        ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  priv->ca        = ca;
  priv->port      = port;
  priv->qp        = TS_IB_HANDLE_INVALID;
  priv->mcast_tid = TS_IB_CLIENT_QUERY_TID_INVALID;

  if (tsIpoibDeviceTransportInit(dev, ca)) {
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: tsIpoibDeviceTransportInit failed", dev->name);
    return -ENODEV;
  }

  if (dev->flags & IFF_UP) {
    if (tsIpoibDeviceIbOpen(dev)) {
      tsIpoibDeviceTransportCleanup(dev);
      return -ENODEV;
    }
  }

  return 0;
}

/* =============================================================== */
/*..tsIpoibDeviceIbFlush -- flush and rejoin multicast             */
void tsIpoibDeviceIbFlush(
                          void *_dev
                          )
{
  struct net_device *dev = (struct net_device *)_dev;
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  struct list_head *ptr;

  if (!test_bit(TS_IPOIB_FLAG_ADMIN_UP, &priv->flags)) {
    return;
  }

  TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_GEN, "%s: flushing", dev->name);

  tsIpoibDeviceIbDown(dev);

  /*
   * The device could have been brought down between the start and when we
   * get here, don't bring it back up if it's not configured up
   */
  if (test_bit(TS_IPOIB_FLAG_ADMIN_UP, &priv->flags)) {
    tsIpoibDeviceIbUp(dev);
  }

  /* Flush any child interfaces too */
  list_for_each(ptr, &priv->child_intfs) {
    struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *cpriv;

    cpriv = list_entry(ptr, struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT, list);

    tsIpoibDeviceIbFlush(&cpriv->dev);
  }
}

/* =============================================================== */
/*..tsIpoibDeviceIbCleanup -- clean up IB resources for iface      */
void tsIpoibDeviceIbCleanup(struct net_device *dev)
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  TS_REPORT_CLEANUP(MOD_IB_NET,
                    "%s: cleaning up IB resources",
                    dev->name);

  tsIpoibDeviceStopMulticastThread(dev);

  /* Wait until all multicast joins are done */
  while (atomic_read(&priv->mcast_joins)) {
    set_current_state(TASK_RUNNING);
    schedule();
  }

  /* Delete the broadcast address */
  tsIpoibDeviceMulticastDown(dev);

  tsIpoibDeviceTransportCleanup(dev);
}

#if defined(TS_HOST_DRIVER)   || \
    defined(TS_ppc440_en_sun) || \
    defined(TS_ppc440_fc_sun) || \
    defined(TS_ppc440_fg_sun)
/*
 * Delayed P_Key Assigment Interim Support
 *
 * The following is initial implementation of delayed P_Key assigment
 * mechanism. It is using the same approach implemented for the multicast
 * group join. The single goal of this implementation is to quickly address
 * Bug #2507. This implementation will probably be removed when the P_Key
 * change async notification is available.
 */
static DECLARE_MUTEX(pkey_sem);
static void _tsIpoibDevicePkeyThread(void *dev_ptr);
int tsIpoibDeviceOpen(struct net_device *dev);

/* =============================================================== */
/*..tsIpoibDeviceStartPKeyThread -- Start the P_Key thread         */
int tsIpoibDeviceStartPKeyThread(
                                      struct net_device *dev
                                      )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  char thread_name[sizeof "ibX.YYYY_pkey"];
  int ret = 0;

  TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_GEN,
           "%s: starting P_Key thread", dev->name);

  down(&pkey_sem);

  if (priv->pkey_thread) {
    goto out;
  }

  snprintf(thread_name, sizeof thread_name, "%s_pkey", dev->name);

  if (tsKernelThreadStart(thread_name,
                          _tsIpoibDevicePkeyThread,
                          dev,
                          &priv->pkey_thread)) {
    TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to start the P_Key thread",
                    dev->name);
    ret = -1;
  }

out:
  up(&pkey_sem);

  return ret;
}

/* =============================================================== */
/*..tsIpoibDeviceStopPKeyThread -- Stop the P_Key thread           */
int tsIpoibDeviceStopPKeyThread(
                                     struct net_device *dev
                                     )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  int ret = 0;

  TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_GEN,
           "%s: stopping P_Key thread", dev->name);

  down(&pkey_sem);

  if (priv->pkey_thread) {
    tsKernelThreadStop(priv->pkey_thread);
    priv->pkey_thread = NULL;

    ret = 1;
  }

  up(&pkey_sem);

  return ret;
}

/* =============================================================== */
/*.._tsIpoibDevicePkeyThread -- poll multicast join           */
static void _tsIpoibDevicePkeyThread(
                                          void *dev_ptr
                                          ) {
  struct net_device                      *dev = dev_ptr;
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  /* P_Key already assigned */
  if (test_bit(TS_IPOIB_PKEY_ASSIGNED, &priv->flags))
    return;

  /* Wait for the IB SM to program the interface P_Key */
  for (;;)
  {
    tsIpoibDeviceCheckPkeyPresence(dev);

    if (test_bit(TS_IPOIB_PKEY_ASSIGNED, &priv->flags)) {
      tsIpoibDeviceOpen(dev);
      return;
    }

    /* Pause */
    set_current_state(TASK_INTERRUPTIBLE);
    schedule_timeout(3 * HZ);
    set_current_state(TASK_RUNNING);

    if (signal_pending(current)) {
      return;
    }
  }

} /* _tsIpoibDevicePkeyThread */

/* ======================================================================== */
/*..tsIpoibDeviceDelayOpen -- Check to see whether to delay the device open */
int tsIpoibDeviceDelayOpen(
                        struct net_device *dev
                        )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  /* Look for the interface pkey value in the IB Port P_Key table and */
  /* set the interface pkey assigment flag                            */
  tsIpoibDeviceCheckPkeyPresence(dev);

  /* P_Key value not assigned yet - start the P_Key thread */
  if (!test_bit(TS_IPOIB_PKEY_ASSIGNED, &priv->flags)) {
    tsIpoibDeviceStartPKeyThread(dev);
    return 1;
  }

  return 0;
}
#endif /* TS_HOST_DRIVER    || TS_ppc440_en_sun) || */

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
