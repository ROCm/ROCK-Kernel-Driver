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

  $Id: ipoib_multicast.c 32 2004-04-09 03:57:42Z roland $
*/

#include "ipoib.h"

#include "ts_ib_sa_client.h"

#include "ts_kernel_services.h"
#include "ts_kernel_trace.h"
#include "ts_kernel_cache.h"

#include <linux/rtnetlink.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/igmp.h>
#include <linux/inetdevice.h>

/* Used for all multicast joins (broadcast, IPv4 mcast and IPv6 mcast) */
struct tTS_IPOIB_MULTICAST_GROUP_STRUCT {
  rb_node_t                       rb_node;
  struct list_head                list;

  atomic_t                        refcnt;
  unsigned long                   created;

  tTS_IB_MULTICAST_MEMBER_STRUCT  mcast_member;
  tTS_IB_ADDRESS_HANDLE           address_handle;
  tTS_IB_CLIENT_QUERY_TID         tid;

  tTS_IB_GID                      mgid;

  unsigned long                   flags;
  unsigned char                   logcount;

  struct sk_buff_head             pkt_queue;

  struct net_device              *dev;
};

struct tTS_IPOIB_MULTICAST_ITERATOR_STRUCT {
  struct net_device *dev;
  rb_node_t         *rb_node;
};

#ifndef TS_HOST_DRIVER
unsigned long long mcast_chassisid;

tTS_IB_GID ts_ipoib_broadcast_mgid = {
  0xff, 0x18, 0xa0, 0x1b, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x05, 0xad, 0xff, 0xff, 0xff, 0xff
};
#endif

tTS_IB_GID ipoib_broadcast_mgid = {
  0xff, 0x12, 0x40, 0x1b, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff
};

/* =============================================================== */
/*..tsIpoibMulticastGroupGet - get reference to multicast group    */
void tsIpoibMulticastGroupGet(
                              tTS_IPOIB_MULTICAST_GROUP mcast
                              )
{
  atomic_inc(&mcast->refcnt);
}

/* =============================================================== */
/*..tsIpoibMulticastGroupPut - put reference to multicast group    */
void tsIpoibMulticastGroupPut(
                              tTS_IPOIB_MULTICAST_GROUP mcast
                              )
{
  struct net_device *dev = mcast->dev;

  if (atomic_dec_and_test(&mcast->refcnt)) {
    TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_MULTICAST,
           "%s: deleting multicast group " IPOIB_GID_FMT,
           dev->name, IPOIB_GID_ARG(mcast->mgid));

    if (mcast->address_handle != TS_IB_HANDLE_INVALID) {
      int ret = tsIbAddressDestroy(mcast->address_handle);
      if (ret < 0) {
        TS_REPORT_WARN(MOD_IB_NET,
                       "%s: tsIbAddressDestroy failed (ret = %d)",
                       dev->name, ret);
      }
    }

    while (!skb_queue_empty(&mcast->pkt_queue)) {
      struct sk_buff *skb = skb_dequeue(&mcast->pkt_queue);

      skb->dev = dev;

      dev_kfree_skb(skb);
    }

    kfree(mcast);
  }
}

/* =============================================================== */
/*..tsIpoibMulticastAllocateGroup - allocate mcast group entry     */
tTS_IPOIB_MULTICAST_GROUP tsIpoibMulticastAllocateGroup(
                                                        struct net_device *dev
                                                        ) {
  tTS_IPOIB_MULTICAST_GROUP mcast;

  mcast = kmalloc(sizeof *mcast, GFP_ATOMIC);
  if (!mcast) {
    return NULL;
  }

  memset(mcast, 0, sizeof(*mcast));

  atomic_set(&mcast->refcnt, 2);	/* Calling function needs to put */

  mcast->dev = dev;
  mcast->created = jiffies;
  mcast->logcount = 0;

  INIT_LIST_HEAD(&mcast->list);
  skb_queue_head_init(&mcast->pkt_queue);

  mcast->address_handle = TS_IB_HANDLE_INVALID;

  /* Will force a trigger on the first packet we need to send */
  mcast->tid            = TS_IB_CLIENT_QUERY_TID_INVALID;

  return mcast;
}

/* =============================================================== */
/*..__tsIpoibDeviceMulticastFind - find multicast group            */
tTS_IPOIB_MULTICAST_GROUP __tsIpoibDeviceMulticastFind(
                                                       struct net_device *dev,
                                                       tTS_IB_GID mgid
                                                       )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  rb_node_t *n = priv->multicast_tree.rb_node;

  while (n) {
    tTS_IPOIB_MULTICAST_GROUP mcast;
    int ret;

    mcast = rb_entry(n, tTS_IPOIB_MULTICAST_GROUP_STRUCT, rb_node);

    ret = memcmp(mgid, mcast->mgid, sizeof(tTS_IB_GID));
    if (ret < 0)
      n = n->rb_left;
    else if (ret > 0)
      n = n->rb_right;
    else {
      tsIpoibMulticastGroupGet(mcast);
      return mcast;
    }
  }

  return NULL;
}

/* =============================================================== */
/*.._tsIpoibDeviceMulticastFind - find multicast group             */
tTS_IPOIB_MULTICAST_GROUP _tsIpoibDeviceMulticastFind(
                                                      struct net_device *dev,
                                                      tTS_IB_GID mgid
                                                      )
{
  tTS_IPOIB_MULTICAST_GROUP mcast;
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  unsigned long flags;

  spin_lock_irqsave(&priv->lock, flags);
  mcast = __tsIpoibDeviceMulticastFind(dev, mgid);
  spin_unlock_irqrestore(&priv->lock, flags);

  return mcast;
}

/* =============================================================== */
/*..__tsIpoibDeviceMulticastAdd -- add multicast group to rbtree   */
static int __tsIpoibDeviceMulticastAdd(
                                       struct net_device *dev,
                                       tTS_IPOIB_MULTICAST_GROUP mcast
                                       )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  rb_node_t **n = &priv->multicast_tree.rb_node, *pn = NULL;

  while (*n) {
    tTS_IPOIB_MULTICAST_GROUP tmcast;
    int ret;

    pn = *n;
    tmcast = rb_entry(pn, tTS_IPOIB_MULTICAST_GROUP_STRUCT, rb_node);

    ret = memcmp(mcast->mgid, tmcast->mgid, sizeof(tTS_IB_GID));
    if (ret < 0)
      n = &pn->rb_left;
    else if (ret > 0)
      n = &pn->rb_right;
    else
      return -EEXIST;
  }

  rb_link_node(&mcast->rb_node, pn, n);
  rb_insert_color(&mcast->rb_node, &priv->multicast_tree);

  return 0;
}

/* =============================================================== */
/*.._tsIpoibMulticastJoinFinish - finish joining mcast group entry  */
static int _tsIpoibMulticastJoinFinish(
                                       tTS_IPOIB_MULTICAST_GROUP mcast,
                                       tTS_IB_MULTICAST_MEMBER member_ptr
                                       ) {
  struct net_device *dev = mcast->dev;
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  int ret;

  mcast->mcast_member = *member_ptr;
  priv->qkey = priv->broadcast->mcast_member.qkey;

  if (test_and_set_bit(TS_IPOIB_MCAST_FLAG_ATTACHED, &mcast->flags)) {
#ifdef TS_ANAFA_PORT_STATE_WORKAROUND
    if (mcast != priv->broadcast)
#endif
      TS_REPORT_WARN(MOD_IB_NET,
                     "%s: multicast group " IPOIB_GID_FMT " already attached?",
                     dev->name, IPOIB_GID_ARG(mcast->mgid));

    return 0;
  }

  /* Set the cached Q_Key before we attach if it's the broadcast group */
  if (memcmp(mcast->mgid, priv->bcast_gid, sizeof(tTS_IB_GID)) == 0) {
    priv->qkey = priv->broadcast->mcast_member.qkey;
  }

  ret = tsIpoibDeviceMulticastAttach(dev, mcast->mcast_member.mlid, mcast->mgid);
  if (ret < 0) {
    TS_REPORT_FATAL(MOD_IB_NET,
                    "%s: couldn't attach QP to multicast group " IPOIB_GID_FMT,
                    dev->name, IPOIB_GID_ARG(mcast->mgid));

    clear_bit(TS_IPOIB_MCAST_FLAG_ATTACHED, &mcast->flags);
    return ret;
  }

  {
    tTS_IB_ADDRESS_VECTOR_STRUCT av = {
      .dlid             = mcast->mcast_member.mlid,
      .port             = priv->port,
      .service_level    = mcast->mcast_member.sl,
      .source_path_bits = 0,
      .use_grh          = 1,
      .flow_label       = mcast->mcast_member.flowlabel,
      .hop_limit        = mcast->mcast_member.hoplmt,
      .source_gid_index = 0,
      .static_rate      = 0,
      .traffic_class    = mcast->mcast_member.tclass,
    };

    memcpy(av.dgid, mcast->mcast_member.mgid, sizeof av.dgid);

    if (tsIbAddressCreate(priv->pd,
                         &av,
                         &mcast->address_handle)) {
      TS_REPORT_WARN(MOD_IB_NET,
                     "%s: tsIpoibAddressCreate failed",
                     dev->name);
    } else {
      TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_MULTICAST,
               "%s: MGID " IPOIB_GID_FMT " AV 0x%08x, LID 0x%04x, SL %d",
               dev->name, IPOIB_GID_ARG(mcast->mgid),
               mcast->address_handle,
               mcast->mcast_member.mlid, mcast->mcast_member.sl);
    }
  }

  /* actually send any queued packets */
  while (!skb_queue_empty(&mcast->pkt_queue)) {
    struct sk_buff *skb = skb_dequeue(&mcast->pkt_queue);

    skb->dev = dev;

    if (dev_queue_xmit(skb)) {
      TS_REPORT_WARN(MOD_IB_NET,
                     "%s: dev_queue_xmit failed to requeue packet",
                     dev->name);
    }
  }

  return 0;
}

/* =============================================================== */
/*.._tsIpoibMulticastSendOnlyJoinComplete -- handler for multicast join */
static void _tsIpoibMulticastSendOnlyJoinComplete(
                                                  tTS_IB_CLIENT_QUERY_TID tid,
                                                  int status,
                                                  tTS_IB_MULTICAST_MEMBER member_ptr,
                                                  void *mcast_ptr
                                                  )
{
  tTS_IPOIB_MULTICAST_GROUP mcast = mcast_ptr;
  struct net_device *dev = mcast->dev;
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  mcast->tid = TS_IB_CLIENT_QUERY_TID_INVALID;

  if (!status) {
    _tsIpoibMulticastJoinFinish(mcast, member_ptr);
  } else {
    if (mcast->logcount++ < 20) {
      TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_MULTICAST,
               "%s: multicast join failed for " IPOIB_GID_FMT ", status %d",
               dev->name, IPOIB_GID_ARG(mcast->mgid), status);
    }

    /* Flush out any queued packets */
    while (!skb_queue_empty(&mcast->pkt_queue)) {
      struct sk_buff *skb = skb_dequeue(&mcast->pkt_queue);

      skb->dev = dev;

      dev_kfree_skb(skb);
    }

    /* Clear the busy flag so we try again */
    clear_bit(TS_IPOIB_MCAST_FLAG_BUSY, &mcast->flags);
  }

  tsIpoibMulticastGroupPut(mcast);

  atomic_dec(&priv->mcast_joins);
}

/* =============================================================== */
/*.._tsIpoibMulticastSendOnlyJoin -- start send only join          */
static int _tsIpoibMulticastSendOnlyJoin(
                                         tTS_IPOIB_MULTICAST_GROUP mcast
                                         ) {
  struct net_device *dev = mcast->dev;
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  tTS_IB_CLIENT_QUERY_TID tid;
  int ret = 0;

  atomic_inc(&priv->mcast_joins);

  if (!test_bit(TS_IPOIB_FLAG_OPER_UP, &priv->flags)) {
    TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_MULTICAST,
             "%s: device shutting down, no multicast joins",
             dev->name);
    ret = -ENODEV;
    goto out;
  }

  if (test_and_set_bit(TS_IPOIB_MCAST_FLAG_BUSY, &mcast->flags)) {
    TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_MULTICAST,
             "%s: multicast entry busy, skipping",
             dev->name);
    ret = -EBUSY;
    goto out;
  }

  tsIpoibMulticastGroupGet(mcast);
  ret = tsIbMulticastGroupJoin(priv->ca,
                               priv->port,
                               mcast->mgid,
                               priv->pkey,
/* ib_sm doesn't support send only yet
                               TS_IB_MULTICAST_JOIN_SEND_ONLY_NON_MEMBER,
 */
                               TS_IB_MULTICAST_JOIN_FULL_MEMBER,
                               HZ,
                               _tsIpoibMulticastSendOnlyJoinComplete,
                               mcast,
                               &tid);
  if (ret) {
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: tsIbMulticastGroupJoin failed",
                   dev->name);
    tsIpoibMulticastGroupPut(mcast);
  } else {
    TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_MULTICAST,
             "%s: no multicast record for " IPOIB_GID_FMT ", starting join",
             dev->name, IPOIB_GID_ARG(mcast->mgid));

    mcast->tid = tid;
  }

out:
  if (ret) {
    atomic_dec(&priv->mcast_joins);
  }

  return ret;
}

/* =============================================================== */
/*.._tsIpoibMulticastJoinComplete - handle comp of mcast join      */
static void _tsIpoibMulticastJoinComplete(
                                          tTS_IB_CLIENT_QUERY_TID tid,
                                          int status,
                                          tTS_IB_MULTICAST_MEMBER member_ptr,
                                          void *mcast_ptr
                                          ) {
  tTS_IPOIB_MULTICAST_GROUP mcast = mcast_ptr;
  struct net_device *dev = mcast->dev;
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  priv->mcast_tid = TS_IB_CLIENT_QUERY_TID_INVALID;

  if (!status) {
    priv->mcast_status = _tsIpoibMulticastJoinFinish(mcast, member_ptr);
  } else {
    priv->mcast_status = status;
  }

  complete(&priv->mcast_complete);
}

/* =============================================================== */
/*.._tsIpoibDeviceMulticastJoin - join multicast group for iface   */
static int _tsIpoibDeviceMulticastJoin(
                                       struct net_device *dev,
                                       tTS_IPOIB_MULTICAST_GROUP mcast
                                       ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  int status;

  TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_MULTICAST,
           "%s: joining MGID " IPOIB_GID_FMT,
           dev->name, IPOIB_GID_ARG(mcast->mgid));

  priv->mcast_status = -ERESTARTSYS;

  init_completion(&priv->mcast_complete);

  status = tsIbMulticastGroupJoin(priv->ca,
                                  priv->port,
                                  mcast->mgid,
                                  priv->pkey,
                                  TS_IB_MULTICAST_JOIN_FULL_MEMBER,
                                  HZ,
                                  _tsIpoibMulticastJoinComplete,
                                  mcast,
                                  &priv->mcast_tid);
  if (status) {
    TS_REPORT_FATAL(MOD_IB_NET,
                    "%s: tsIbMulticastGroupJoin failed, status %d",
                    dev->name, status);
    priv->mcast_status = -EINVAL;
    return status;
  }

  wait_for_completion(&priv->mcast_complete);

  if (priv->mcast_status) {
    if (priv->mcast_status == -ETIMEDOUT || priv->mcast_status == -EINTR ||
        priv->mcast_status == -EINVAL) {
      TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_MULTICAST,
               "%s: multicast join failed for " IPOIB_GID_FMT ", status %d",
               dev->name, IPOIB_GID_ARG(mcast->mgid), priv->mcast_status);
    } else {
      TS_REPORT_WARN(MOD_IB_NET,
                     "%s: multicast join failed for " IPOIB_GID_FMT ", status %d",
                     dev->name, IPOIB_GID_ARG(mcast->mgid), priv->mcast_status);
    }
    return priv->mcast_status;
  }

  return status;
}

/* =============================================================== */
/*.._tsIpoibDeviceMulticastJoinGroup - join multicast group        */
static int _tsIpoibDeviceMulticastJoinGroup(
                                            struct net_device *dev,
                                            tTS_IPOIB_MULTICAST_GROUP mcast
                                            ) {
  unsigned int        backoff = 1;
  int                 ret;

  if (test_bit(TS_IPOIB_MCAST_FLAG_ATTACHED, &mcast->flags)) {
    return 0;
  }

  do {
    ret = _tsIpoibDeviceMulticastJoin(dev, mcast);
    if (ret && ret != -ETIMEDOUT && ret != -EINVAL) {
      return ret;
    }

    if (ret) {
      TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_MULTICAST,
               "%s: couldn't join multicast group, backing off. retrying in %d seconds",
               dev->name, backoff);

      set_current_state(TASK_INTERRUPTIBLE);
      schedule_timeout(backoff * HZ);
      set_current_state(TASK_RUNNING);

      if (signal_pending(current)) {
        return -EINTR;
      }

      backoff *= 2;
      if (backoff > TS_IPOIB_MAX_BACKOFF_SECONDS) {
        backoff = TS_IPOIB_MAX_BACKOFF_SECONDS;
      }
    }
  } while (ret);

  return 0;
}

/* =============================================================== */
/*.._tsIpoibDeviceMulticastThread -- poll multicast join           */
static void _tsIpoibDeviceMulticastThread(
                                          void *dev_ptr
                                          ) {
  struct net_device                      *dev = dev_ptr;
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
#if !defined(TS_ppc440_fc_sun) && \
    !defined(TS_ppc440_en_sun) && \
    !defined(TS_ppc440_fg_sun)
  struct list_head          *ptr;
#endif
  tTS_IPOIB_ARP_ENTRY        entry;
  unsigned long              flags;
  int                        ret = 0;

  atomic_inc(&priv->mcast_joins);

  if (!priv->broadcast) {
    priv->broadcast = tsIpoibMulticastAllocateGroup(dev);
    if (!priv->broadcast) {
      TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to allocate broadcast group",
                      dev->name);
      ret = -ENOMEM;
      goto out;
    }

#ifndef TS_HOST_DRIVER
    if (test_bit(TS_IPOIB_FLAG_CHASSISINTF, &priv->flags)) {
      uint32_t chassisid = cpu_to_be32((uint32_t)mcast_chassisid);

      memcpy(priv->bcast_gid, ts_ipoib_broadcast_mgid, sizeof(tTS_IB_GID));
      memcpy(&priv->bcast_gid[12], &chassisid, sizeof(chassisid));
    } else
#endif
    {
      memcpy(priv->bcast_gid, ipoib_broadcast_mgid, sizeof(tTS_IB_GID));
      priv->bcast_gid[4] = (priv->pkey >> 8) & 0xff;
      priv->bcast_gid[5] = priv->pkey & 0xff;
    }

    memcpy(priv->broadcast->mgid, priv->bcast_gid, sizeof(tTS_IB_GID));

    spin_lock_irqsave(&priv->lock, flags);
    __tsIpoibDeviceMulticastAdd(dev, priv->broadcast);
    spin_unlock_irqrestore(&priv->lock, flags);

    tsIpoibMulticastGroupPut(priv->broadcast);	/* for Allocate() */
  }

  ret = _tsIpoibDeviceMulticastJoinGroup(dev, priv->broadcast);
  if (ret < 0) {
    goto out;
  }

  /* Workaround bug #2392. Sun's SM gives us errors when trying to join */
#if !defined(TS_ppc440_fc_sun) && \
    !defined(TS_ppc440_en_sun) && \
    !defined(TS_ppc440_fg_sun)
  while (1) {
    tTS_IPOIB_MULTICAST_GROUP mcast = NULL;

    if (signal_pending(current)) {
      ret = -EINTR;
      goto out;
    }

    spin_lock_irqsave(&priv->lock, flags);
    list_for_each(ptr, &priv->multicast_list) {
      mcast = list_entry(ptr, tTS_IPOIB_MULTICAST_GROUP_STRUCT, list);

      if (!test_bit(TS_IPOIB_MCAST_FLAG_SENDONLY, &mcast->flags) &&
          !test_bit(TS_IPOIB_MCAST_FLAG_BUSY, &mcast->flags) &&
          !test_bit(TS_IPOIB_MCAST_FLAG_ATTACHED, &mcast->flags)) {
        /* Found the next unjoined group */
        break;
      }
    }
    spin_unlock_irqrestore(&priv->lock, flags);

    if (ptr == &priv->multicast_list) {
      /* All done */
      break;
    }

    ret = _tsIpoibDeviceMulticastJoinGroup(dev, mcast);
    if (ret < 0) {
      goto out;
    }
  }
#endif

  {
    tTS_IB_PORT_LID_STRUCT port_lid;

    tsIbCachedLidGet(priv->ca,
                     priv->port,
                     &port_lid);

    priv->local_lid = port_lid.lid;
  }

  if (tsIbGidEntryGet(priv->ca,
                      priv->port,
                      0,
                      priv->local_gid)) {
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: tsIbGidEntryGet failed", dev->name);
  }

  priv->mcast_mtu = tsIbMtuEnumToInt(priv->broadcast->mcast_member.mtu) -
                    TS_IPOIB_ENCAP_LEN;

  dev->mtu = min(priv->mcast_mtu, priv->admin_mtu);

  entry = tsIpoibDeviceArpLocalAdd(dev, priv->local_gid, priv->local_qpn);
  if (entry) {
    tsIpoibArpEntryPut(entry);
  }

  TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_MULTICAST,
           "%s: successfully joined all multicast groups", dev->name);

  netif_carrier_on(dev);

#ifdef TS_ANAFA_PORT_STATE_WORKAROUND
  /*
   * We don't get port state change events with the Anafa. This causes
   * problems in that we don't rejoin the multicast group if the SM
   * restarted. The workaround is to rejoin the multicast group every
   * x seconds.
   */
  while (1) {
    tTS_IB_MULTICAST_MEMBER_STRUCT mcast_member;

    /* Make a copy of mcast_member */
    memcpy(&mcast_member, &priv->broadcast->mcast_member, sizeof(mcast_member));

    ret = _tsIpoibDeviceMulticastJoin(dev, priv->broadcast);
    if (!ret) {
      /* Join succeeded. Let's compare some of the bits to see if it's diff */
      if (priv->broadcast->mcast_member.mlid != mcast_member.mlid ||
          priv->broadcast->mcast_member.sl != mcast_member.sl ||
          priv->broadcast->mcast_member.flowlabel != mcast_member.flowlabel ||
          priv->broadcast->mcast_member.hoplmt != mcast_member.hoplmt ||
          priv->broadcast->mcast_member.tclass != mcast_member.tclass) {
        static struct tq_struct flush_task;

        flush_task.routine = tsIpoibDeviceIbFlush;
        flush_task.data = dev;

        /*
         * We need to do a flush now. We can't do it from this thread since
         * we end up killing and restarting this thread. So we need to use
         * a kernel task.
         */
        schedule_task(&flush_task);
        goto out;
      }
    }

    /* Pause */
    set_current_state(TASK_INTERRUPTIBLE);
    schedule_timeout(20 * HZ);
    set_current_state(TASK_RUNNING);

    if (signal_pending(current)) {
      ret = -EINTR;
      goto out;
    }
  }
#endif

 out:
  if (ret && ret != -EINTR) {
    TS_REPORT_FATAL(MOD_IB_NET,
                    "%s: multicast join failed, status %d",
                    dev->name, ret);
  }

  atomic_dec(&priv->mcast_joins);
}

DECLARE_MUTEX(thread_mutex);

/* =============================================================== */
/*..tsIpoibDeviceStartMulticastThread -- start multicast thread    */
int tsIpoibDeviceStartMulticastThread(
                                      struct net_device *dev
                                      )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  char thread_name[64];
  int ret = 0;

  TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_MULTICAST,
           "%s: starting multicast thread", dev->name);

  down(&thread_mutex);

  if (priv->mcast_thread) {
    goto out;
  }

  snprintf(thread_name, sizeof thread_name - 1, "%s_join_mcast", dev->name);

  if (tsKernelThreadStart(thread_name,
                          _tsIpoibDeviceMulticastThread,
                          dev,
                          &priv->mcast_thread)) {
    TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to start multicast join thread",
                    dev->name);
    ret = -1;
  }

out:
  up(&thread_mutex);

  return ret;
}

/* =============================================================== */
/*..tsIpoibDeviceStopMulticastThread -- stop multicast join        */
int tsIpoibDeviceStopMulticastThread(
                                     struct net_device *dev
                                     )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  int ret = 0;

  TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_MULTICAST,
           "%s: stopping multicast thread", dev->name);

  down(&thread_mutex);

  if (priv->mcast_thread) {
    tsKernelThreadStop(priv->mcast_thread);
    priv->mcast_thread = NULL;

    ret = 1;
  }

  if (priv->mcast_tid != TS_IB_CLIENT_QUERY_TID_INVALID) {
    /* Kill the query and thread */
    if (!tsIbClientQueryCancel(priv->mcast_tid)) {
      priv->mcast_status = -EINTR;
      complete(&priv->mcast_complete);
    } else {
      TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_MULTICAST,
               "%s: multicast Join TID 0x%016llx no longer exists",
               dev->name, priv->mcast_tid);
    }

    priv->mcast_tid = TS_IB_CLIENT_QUERY_TID_INVALID;
  }

  up(&thread_mutex);

  return ret;
}

/* =============================================================== */
/*..tsIpoibDeviceMulticastLeave -- leave multicast group           */
int tsIpoibDeviceMulticastLeave(
                                struct net_device *dev,
                                tTS_IPOIB_MULTICAST_GROUP mcast
                                )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  int result;

  if (!test_and_clear_bit(TS_IPOIB_MCAST_FLAG_ATTACHED, &mcast->flags)) {
    return 0;
  }

  /* XXX - make sure the QP is valid before try to leave the multicast group */
  if (priv->qp == TS_IB_HANDLE_INVALID) {
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: tsIbMulticastGroup Leave is trying to use invalid QP",
                    dev->name);
    return 0;
  }

  /* Remove ourselves from the multicast group */
  result = tsIpoibDeviceMulticastDetach(dev, mcast->mcast_member.mlid, mcast->mgid);
  if (result) {
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: tsIpoibDeviceMulticastDetach failed (result = %d)",
                   dev->name, result);
  }

  result = tsIbMulticastGroupLeave(priv->ca, priv->port, mcast->mcast_member.mgid);
  if (result) {
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: tsIbMulticastGroup Leave failed (result = %d)",
                    dev->name, result);
  }

  return 0;
}

/* =============================================================== */
/*.._tsIpoibMulticastCancel -- cancel multicast group join          */
static int _tsIpoibMulticastCancel(
                                   tTS_IPOIB_MULTICAST_GROUP mcast
                                   ) {
  if (mcast->tid != TS_IB_CLIENT_QUERY_TID_INVALID) {
    /* Kill the lookup */
    if (!tsIbClientQueryCancel(mcast->tid)) {
      if (test_bit(TS_IPOIB_MCAST_FLAG_SENDONLY, &mcast->flags)) {
        _tsIpoibMulticastSendOnlyJoinComplete(mcast->tid, -EINTR, NULL, mcast);
      } else {
        _tsIpoibMulticastJoinComplete(mcast->tid, -EINTR, NULL, mcast);
      }
    } else {
      TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_MULTICAST,
               "%s: multicast join TID 0x%016llx no longer exists",
               mcast->dev->name, mcast->tid);
    }
  }

  return 0;
}

/* =============================================================== */
/*.._tsIpoibMulticastDelete -- delete multicast group join          */
static int _tsIpoibDeviceMulticastDelete(
                                         struct net_device *dev,
                                         tTS_IB_GID mgid
                                         )
{
  tTS_IPOIB_MULTICAST_GROUP mcast;
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  unsigned long flags;

  spin_lock_irqsave(&priv->lock, flags);
  mcast = __tsIpoibDeviceMulticastFind(dev, mgid);
  if (!mcast) {
    spin_unlock_irqrestore(&priv->lock, flags);
    return -ENOENT;
  }

  rb_erase(&mcast->rb_node, &priv->multicast_tree);
  spin_unlock_irqrestore(&priv->lock, flags);

  tsIpoibDeviceMulticastLeave(mcast->dev, mcast);
  tsIpoibMulticastGroupPut(mcast);	/* for Find() */
  tsIpoibMulticastGroupPut(mcast);	/* for call */

  return 0;
}

/* =============================================================== */
/*..tsIpoibDeviceBroadcastLookup -- return reference to broadcast  */
int tsIpoibDeviceBroadcastLookup(
                                 struct net_device *dev,
                                 tTS_IPOIB_MULTICAST_GROUP *mcast
                                 )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  if (!priv->broadcast) {
    return -ENOENT;
  }

  tsIpoibMulticastGroupGet(priv->broadcast);

  *mcast = priv->broadcast;

  if (priv->broadcast->address_handle == TS_IB_HANDLE_INVALID) {
    return -EAGAIN;
  }

  return 0;
}

/* =============================================================== */
/*..tsIpoibDeviceMulticastLookup -- return reference to multicast  */
int tsIpoibDeviceMulticastLookup(
                                 struct net_device *dev,
                                 tTS_IB_GID mgid,
                                 tTS_IPOIB_MULTICAST_GROUP *mmcast
                                 )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  tTS_IPOIB_MULTICAST_GROUP mcast;
  unsigned long flags;
  int ret = 0;

  spin_lock_irqsave(&priv->lock, flags);
  mcast = __tsIpoibDeviceMulticastFind(dev, mgid);
  if (!mcast) {
    /* Let's create a new send only group now */
    TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_MULTICAST,
             "%s: setting up send only multicast group for " IPOIB_GID_FMT,
             dev->name, IPOIB_GID_ARG(mgid));

    mcast = tsIpoibMulticastAllocateGroup(dev);
    if (!mcast) {
      TS_REPORT_WARN(MOD_IB_NET,
                     "%s: unable to allocate memory for multicast structure",
                     dev->name);
      ret = -ENOMEM;
      goto out;
    }

    set_bit(TS_IPOIB_MCAST_FLAG_SENDONLY, &mcast->flags);

    memcpy(mcast->mgid, mgid, sizeof(tTS_IB_GID));

    __tsIpoibDeviceMulticastAdd(dev, mcast);

    list_add_tail(&mcast->list, &priv->multicast_list);

    /* Leave references for the calling application */
  }

  if (mcast->address_handle == TS_IB_HANDLE_INVALID) {
    if (mcast->tid != TS_IB_CLIENT_QUERY_TID_INVALID) {
      TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_MULTICAST,
               "%s: no address vector, but multicast join already started",
               dev->name);
    } else if (test_bit(TS_IPOIB_MCAST_FLAG_SENDONLY, &mcast->flags)) {
      _tsIpoibMulticastSendOnlyJoin(mcast);
    }

    ret = -EAGAIN;
  }

  *mmcast = mcast;

out:
  spin_unlock_irqrestore(&priv->lock, flags);

  return ret;
}

/* =============================================================== */
/*.._tsIpoibMulticastTxCallback -- put reference to group after TX */
static void _tsIpoibMulticastTxCallback(
                                        void *ptr
                                        )
{
  tsIpoibMulticastGroupPut((tTS_IPOIB_MULTICAST_GROUP)ptr);
}

/* =============================================================== */
/*..tsIpoibDeviceMulticastSend -- send skb to multicast group      */
int tsIpoibDeviceMulticastSend(
                               struct net_device *dev,
                               tTS_IPOIB_MULTICAST_GROUP mcast,
                               struct sk_buff *skb
                               )
{
  return tsIpoibDeviceSend(dev, skb, _tsIpoibMulticastTxCallback,
                           mcast, mcast->address_handle, TS_IB_MULTICAST_QPN);
}

/* =============================================================== */
/*..tsIpoibMulticastGroupQueuePacket -- queue skb pending join          */
int tsIpoibMulticastGroupQueuePacket(
                                     tTS_IPOIB_MULTICAST_GROUP mcast,
                                     struct sk_buff *skb
                                     )
{
  skb_queue_tail(&mcast->pkt_queue, skb);

  return 0;
}

/* =============================================================== */
/*..tsIpoibDeviceMulticastFlush -- flush joins and address vectors */
void tsIpoibDeviceMulticastFlush(
                                 struct net_device *dev
                                 )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  LIST_HEAD(remove_list);
  tTS_IPOIB_MULTICAST_GROUP mcast, nmcast;
  struct list_head *ptr, *tmp;
  unsigned long flags;

  TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_MULTICAST,
           "%s: flushing multicast list", dev->name);

  spin_lock_irqsave(&priv->lock, flags);
  list_for_each_safe(ptr, tmp, &priv->multicast_list) {
    mcast = list_entry(ptr, tTS_IPOIB_MULTICAST_GROUP_STRUCT, list);

    nmcast = tsIpoibMulticastAllocateGroup(dev);
    if (nmcast) {
      nmcast->flags = mcast->flags & (1 << TS_IPOIB_MCAST_FLAG_SENDONLY);

      memcpy(nmcast->mgid, mcast->mgid, sizeof(tTS_IB_GID));

      /* Add the new group in before the to-be-destroyed group */
      list_add_tail(&nmcast->list, &mcast->list);
      list_del_init(&mcast->list);

      rb_replace_node(&mcast->rb_node, &nmcast->rb_node, &priv->multicast_tree);

      tsIpoibMulticastGroupPut(nmcast);		/* for Allocate() */

      list_add_tail(&mcast->list, &remove_list);
    } else {
      TS_REPORT_WARN(MOD_IB_NET,
                     "%s: could not reallocate multicast group " IPOIB_GID_FMT,
                     dev->name, IPOIB_GID_ARG(mcast->mgid));
    }
  }

  if (priv->broadcast) {
    nmcast = tsIpoibMulticastAllocateGroup(dev);
    if (nmcast) {
      memcpy(nmcast->mgid, priv->broadcast->mgid, sizeof(tTS_IB_GID));

      rb_replace_node(&priv->broadcast->rb_node, &nmcast->rb_node,
                      &priv->multicast_tree);

      tsIpoibMulticastGroupPut(nmcast);		/* for Allocate() */

      list_add_tail(&priv->broadcast->list, &remove_list);
    }

    priv->broadcast = nmcast;
  }

  spin_unlock_irqrestore(&priv->lock, flags);

  list_for_each_safe(ptr, tmp, &remove_list) {
    mcast = list_entry(ptr, tTS_IPOIB_MULTICAST_GROUP_STRUCT, list);

    list_del_init(&mcast->list);

    _tsIpoibMulticastCancel(mcast);
    tsIpoibDeviceMulticastLeave(dev, mcast);
    tsIpoibMulticastGroupPut(mcast);
  }
}

/* =============================================================== */
/*..tsIpoibDeviceMulticastDown -- delete broadcast group           */
void tsIpoibDeviceMulticastDown(
                                struct net_device *dev
                                )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  /* Delete broadcast since it will be recreated */
  if (priv->broadcast) {
    _tsIpoibDeviceMulticastDelete(dev, priv->broadcast->mgid);
    priv->broadcast = NULL;
  }
}

/* =============================================================== */
/*..tsIpoibDeviceMulticastRestartTask -- rescan multicast lists    */
void tsIpoibDeviceMulticastRestartTask(
                                       void *dev_ptr
                                       ) {
  struct net_device *dev = dev_ptr;
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  struct in_device *in_dev = in_dev_get(dev);
  struct list_head *ptr, *tmp;
  struct ip_mc_list *im;
  tTS_IPOIB_MULTICAST_GROUP mcast;
  LIST_HEAD(remove_list);
  unsigned long flags;

  if (!in_dev) {
    return;
  }

  /*
   * This function is a bit "evil" in some respects. We expose ourself as
   * an ethernet device to the networking core. Unfortunately, ethernet
   * drops the upper 5 bits of the multicast group ID when it builds the
   * hardware address. The IB multicast spec doesn't. As a result, we
   * need to dig into the IP layers to pull out the IP address so we can
   * build the IB multicast GID correctly.
   */

  tsIpoibDeviceStopMulticastThread(dev);

  spin_lock_irqsave(&priv->lock, flags);

  /*
   * Unfortunately, the networking core only gives us a list of all of
   * the multicast hardware addresses. We need to figure out which ones
   * are new and which ones have been removed
   */

  /* Clear out the found flag */
  list_for_each(ptr, &priv->multicast_list) {
    mcast = list_entry(ptr, tTS_IPOIB_MULTICAST_GROUP_STRUCT, list);
    clear_bit(TS_IPOIB_MCAST_FLAG_FOUND, &mcast->flags);
  }

  read_lock(&in_dev->lock);

  /* Mark all of the entries that are found or don't exist */
  for (im = in_dev->mc_list; im; im = im->next) {
    u32 multiaddr = ntohl(im->multiaddr);
    tTS_IB_GID mgid;

    memcpy(mgid, ipoib_broadcast_mgid, sizeof(tTS_IB_GID));

    /* Add in the P_Key */
    mgid[4] = (priv->pkey >> 8) & 0xff;
    mgid[5] = priv->pkey & 0xff;

    /* Fixup the group mapping */
    mgid[12] = (multiaddr >> 24) & 0x0f;
    mgid[13] = (multiaddr >> 16) & 0xff;
    mgid[14] = (multiaddr >> 8) & 0xff;
    mgid[15] = multiaddr & 0xff;

    mcast = __tsIpoibDeviceMulticastFind(dev, mgid);
    if (!mcast || test_bit(TS_IPOIB_MCAST_FLAG_SENDONLY, &mcast->flags)) {
      tTS_IPOIB_MULTICAST_GROUP nmcast;

      /* Not found or send-only group, let's add a new entry */
      TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_MULTICAST,
                     "%s: adding multicast entry for mgid " IPOIB_GID_FMT,
                     dev->name, IPOIB_GID_ARG(mgid));

      nmcast = tsIpoibMulticastAllocateGroup(dev);
      if (!nmcast) {
        TS_REPORT_WARN(MOD_IB_NET,
                       "%s: unable to allocate memory for multicast structure",
                       dev->name);
        continue;
      }

      set_bit(TS_IPOIB_MCAST_FLAG_FOUND, &nmcast->flags);

      memcpy(nmcast->mgid, mgid, sizeof(tTS_IB_GID));

      if (mcast) {
        /* Destroy the send only entry */
        list_del(&mcast->list);
        list_add_tail(&mcast->list, &remove_list);

        rb_replace_node(&mcast->rb_node, &nmcast->rb_node, &priv->multicast_tree);
      } else {
        __tsIpoibDeviceMulticastAdd(dev, nmcast);
      }

      list_add_tail(&nmcast->list, &priv->multicast_list);

      tsIpoibMulticastGroupPut(nmcast);		/* for Allocate() */
    }

    if (mcast) {
      set_bit(TS_IPOIB_MCAST_FLAG_FOUND, &mcast->flags);

      /* Drop the reference for the group we found */
      tsIpoibMulticastGroupPut(mcast);		/* for Find() */
    }
  }

  read_unlock(&in_dev->lock);

  /* Remove all of the entries don't exist anymore */
  list_for_each_safe(ptr, tmp, &priv->multicast_list) {
    mcast = list_entry(ptr, tTS_IPOIB_MULTICAST_GROUP_STRUCT, list);

    if (!test_bit(TS_IPOIB_MCAST_FLAG_FOUND, &mcast->flags) &&
        !test_bit(TS_IPOIB_MCAST_FLAG_SENDONLY, &mcast->flags)) {
      TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_MULTICAST,
                     "%s: deleting multicast group " IPOIB_GID_FMT,
                     dev->name, IPOIB_GID_ARG(mcast->mgid));

      rb_erase(&mcast->rb_node, &priv->multicast_tree);

      /* Move to the remove list */
      list_del(&mcast->list);
      list_add_tail(&mcast->list, &remove_list);
    }
  }
  spin_unlock_irqrestore(&priv->lock, flags);

  /* We have to cancel outside of the spinlock */
  list_for_each_safe(ptr, tmp, &remove_list) {
    tTS_IPOIB_MULTICAST_GROUP mcast =
      list_entry(ptr, tTS_IPOIB_MULTICAST_GROUP_STRUCT, list);

    list_del_init(&mcast->list);

    _tsIpoibMulticastCancel(mcast);
    tsIpoibDeviceMulticastLeave(mcast->dev, mcast);
    tsIpoibMulticastGroupPut(mcast);
  }

  if (test_bit(TS_IPOIB_FLAG_ADMIN_UP, &priv->flags)) {
    tsIpoibDeviceStartMulticastThread(dev);
  }

  in_dev_put(in_dev);
}

/* =============================================================== */
/*..tsIpoibMulticastIteratorInit -- create new multicast iterator  */
tTS_IPOIB_MULTICAST_ITERATOR tsIpoibMulticastIteratorInit(
                                                          struct net_device *dev
                                                          ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  tTS_IPOIB_MULTICAST_ITERATOR iter;
  rb_node_t *node, *parent = NULL;

  if (!priv->multicast_tree.rb_node) {
    return NULL;
  }

  iter = kmalloc(sizeof *iter, GFP_KERNEL);
  if (!iter) {
    return NULL;
  }

  iter->dev  = dev;

  /* Start at the minimum node for the tree */
  node = priv->multicast_tree.rb_node;
  while (node) {
    parent = node;
    node = node->rb_left;
  }

  iter->rb_node = parent;

  return iter;
}

/* =============================================================== */
/*..tsIpoibMulticastIteratorFree -- free multicast iterator        */
void tsIpoibMulticastIteratorFree(
                                  tTS_IPOIB_MULTICAST_ITERATOR iter
                                  ) {
  kfree(iter);
}

/* =============================================================== */
/*..tsIpoibMulticastIterator -- incr. iter. -- return non-zero at end */
int tsIpoibMulticastIteratorNext(
                                 tTS_IPOIB_MULTICAST_ITERATOR iter
                                 ) {
  if (iter->rb_node->rb_right) {
    iter->rb_node = iter->rb_node->rb_right;
    while (iter->rb_node->rb_left) {
      iter->rb_node = iter->rb_node->rb_left;
    }
  } else {
    if (iter->rb_node->rb_parent &&
        iter->rb_node == iter->rb_node->rb_parent->rb_left) {
      iter->rb_node = iter->rb_node->rb_parent;
    } else {
      while (iter->rb_node->rb_parent &&
             iter->rb_node == iter->rb_node->rb_parent->rb_right) {
        iter->rb_node = iter->rb_node->rb_parent;
      }
      iter->rb_node = iter->rb_node->rb_parent;
    }
  }

  return iter->rb_node == NULL;
}

/* =============================================================== */
/*..tsIpoibMulticastIteratorRead -- get data pointed to by multicast iterator */
void tsIpoibMulticastIteratorRead(
                                  tTS_IPOIB_MULTICAST_ITERATOR iter,
                                  tTS_IB_GID mgid,
                                  unsigned long *created,
                                  unsigned int *queuelen,
                                  unsigned int *complete,
                                  unsigned int *send_only
                                  ) {
  tTS_IPOIB_MULTICAST_GROUP mcast;

  mcast = rb_entry(iter->rb_node, tTS_IPOIB_MULTICAST_GROUP_STRUCT, rb_node);

  memcpy(mgid, mcast->mgid, sizeof(tTS_IB_GID));
  *created = mcast->created;
  *queuelen = skb_queue_len(&mcast->pkt_queue);
  *complete = mcast->address_handle != TS_IB_HANDLE_INVALID;
  *send_only = (mcast->flags & (1 << TS_IPOIB_MCAST_FLAG_SENDONLY)) ? 1 : 0;
}
