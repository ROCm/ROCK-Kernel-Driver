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

  $Id: ipoib_main.c 53 2004-04-14 20:10:38Z roland $
*/

#include "ipoib.h"
#include "ipoib_ioctl.h"

#include "ts_kernel_services.h"
#include "ts_kernel_trace.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <linux/if_arp.h>	/* For ARPHRD_xxx */

#include <linux/ip.h>
#include <linux/in.h>

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("IP-over-InfiniBand net driver");
MODULE_LICENSE("Dual BSD/GPL");

DECLARE_MUTEX(ipoib_device_mutex);
LIST_HEAD(ipoib_device_list);

extern tTS_IB_GID broadcast_mgid;

static const uint8_t broadcast_mac_addr[] = {
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

#if defined(TS_HOST_DRIVER)   || \
    defined(TS_ppc440_en_sun) || \
    defined(TS_ppc440_fc_sun) || \
    defined(TS_ppc440_fg_sun)
/* This is based on the dev_change_flags from later kernels */
int tsIpoibDevChangeFlags(
                          struct net_device *dev,
                          unsigned flags
                          )
{
  int ret;
  int old_flags = dev->flags;

  /* Set the flags on our device */
  dev->flags = (flags & (IFF_DEBUG|IFF_NOTRAILERS|IFF_NOARP|IFF_DYNAMIC|
                         IFF_MULTICAST|IFF_PORTSEL|IFF_AUTOMEDIA)) |
                (dev->flags & (IFF_UP|IFF_VOLATILE|IFF_PROMISC|IFF_ALLMULTI));

  /* Load in the correct multicast list now the flags have changed */
  dev_mc_upload(dev);

  /*
   * Have we downed the interface. We handle IFF_UP ourselves according to
   * user attempts to set it, rather than blindly setting it.
   */
  ret = 0;
  if ((old_flags ^ flags) & IFF_UP) {	/* Bit is different  ? */
    ret = ((old_flags & IFF_UP) ? dev_close : dev_open)(dev);

    if (ret == 0)
      dev_mc_upload(dev);
  }

#if 0
  /* We don't have access to netdev_chain from this module */
  if (dev->flags&IFF_UP &&
      ((old_flags^dev->flags)&~(IFF_UP|IFF_PROMISC|IFF_ALLMULTI|IFF_VOLATILE)))
    notifier_call_chain(&netdev_chain, NETDEV_CHANGE, dev);
#endif

  if ((flags ^ dev->gflags) & IFF_PROMISC) {
    int inc = (flags&IFF_PROMISC) ? +1 : -1;

    dev->gflags ^= IFF_PROMISC;
    dev_set_promiscuity(dev, inc);
  }

  /*
   * NOTE: order of synchronization of IFF_PROMISC and IFF_ALLMULTI
   * is important. Some (broken) drivers set IFF_PROMISC, when
   * IFF_ALLMULTI is requested not asking us and not reporting.
   */
  if ((flags ^ dev->gflags) & IFF_ALLMULTI) {
    int inc = (flags&IFF_ALLMULTI) ? +1 : -1;

    dev->gflags ^= IFF_ALLMULTI;
    dev_set_allmulti(dev, inc);
  }

#if 0
  /* rtmsg_ifinfo isn't exported on all kernels we need to support */
  if (old_flags ^ dev->flags)
    rtmsg_ifinfo(RTM_NEWLINK, dev, old_flags ^ dev->flags);
#endif

  return ret;
}
#endif

int tsIpoibDeviceHandle(
                        struct net_device *dev,
                        tTS_IB_DEVICE_HANDLE *ca,
                        tTS_IB_PORT *port,
                        tTS_IB_GID gid,
                        tTS_IB_PKEY *pkey
                        ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  *ca = priv->ca;
  *port = priv->port;
  memcpy(gid, priv->local_gid, sizeof(tTS_IB_GID));
  *pkey = priv->pkey;

  return 0;
}

int tsIpoibDeviceOpen(
                      struct net_device *dev
                      ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_FLOW_CONFIG,
           "%s: bringing up interface", dev->name);

  set_bit(TS_IPOIB_FLAG_ADMIN_UP, &priv->flags);

#if defined(TS_HOST_DRIVER)   || \
    defined(TS_ppc440_en_sun) || \
    defined(TS_ppc440_fc_sun) || \
    defined(TS_ppc440_fg_sun)
  if (tsIpoibDeviceDelayOpen(dev)) {
    return 0;
  }
#endif

  if (tsIpoibDeviceIbOpen(dev)) {
    return -EINVAL;
  }
  if (tsIpoibDeviceIbUp(dev)) {
    return -EINVAL;
  }

#if defined(TS_HOST_DRIVER)   || \
    defined(TS_ppc440_en_sun) || \
    defined(TS_ppc440_fc_sun) || \
    defined(TS_ppc440_fg_sun)
  if (!test_bit(TS_IPOIB_FLAG_SUBINTERFACE, &priv->flags)) {
    struct list_head *ptr;

    /* Bring up any child interfaces too */
    down(&ipoib_device_mutex);
    list_for_each(ptr, &priv->child_intfs) {
      struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *cpriv;
      int flags;

      cpriv = list_entry(ptr, struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT, list);

      flags = cpriv->dev.flags;
      if (flags & IFF_UP) {
        continue;
      }

      tsIpoibDevChangeFlags(&cpriv->dev, flags | IFF_UP);
    }
    up(&ipoib_device_mutex);
  }
#endif

  netif_start_queue(dev);

  return 0;
}

static int _tsIpoibDeviceStop(
                              struct net_device *dev
                              ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_FLOW_CONFIG,
           "%s: stopping interface", dev->name);

  clear_bit(TS_IPOIB_FLAG_ADMIN_UP, &priv->flags);

  netif_stop_queue(dev);

  tsIpoibDeviceIbDown(dev);
  tsIpoibDeviceIbStop(dev);

#if defined(TS_HOST_DRIVER)   || \
    defined(TS_ppc440_en_sun) || \
    defined(TS_ppc440_fc_sun) || \
    defined(TS_ppc440_fg_sun)
  if (!test_bit(TS_IPOIB_FLAG_SUBINTERFACE, &priv->flags)) {
    struct list_head *ptr;

    /* Bring down any child interfaces too */
    down(&ipoib_device_mutex);
    list_for_each(ptr, &priv->child_intfs) {
      struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *cpriv;
      int flags;

      cpriv = list_entry(ptr, struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT, list);

      flags = cpriv->dev.flags;
      if (!(flags & IFF_UP)) {
        continue;
      }

      tsIpoibDevChangeFlags(&cpriv->dev, flags & ~IFF_UP);
    }
    up(&ipoib_device_mutex);
  }
#endif

  return 0;
}

static int _tsIpoibDeviceIoctl(
                               struct net_device *dev,
                               struct ifreq *ifr,
                               int cmd
                               ) {
  TS_REPORT_INOUT(MOD_IB_NET, "%s: ioctl 0x%04x", dev->name, cmd);

  /* Start with SIOCDEVPRIVATE + 3 so we don't conflict with any of
     the standard MII ioctls */

  switch (cmd) {
  default:
    return -ENOTSUPP;
  }
}

static int _tsIpoibDeviceChangeMtu(
                                   struct net_device *dev,
                                   int new_mtu
                                   ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  if (new_mtu > TS_IPOIB_PACKET_SIZE - TS_IPOIB_ENCAP_LEN) {
    return -EINVAL;
  }

  priv->admin_mtu = new_mtu;

  dev->mtu = min(priv->mcast_mtu, priv->admin_mtu);

  return 0;
}

static int _tsIpoibDeviceConfig(
                                struct net_device *dev,
                                struct ifmap *map
                                ) {
  return -EOPNOTSUPP;
}

static int _tsIpoibDeviceXmit(
                              struct sk_buff *skb,
                              struct net_device *dev
                              ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  uint16_t ethertype;

  ethertype = ntohs(((struct ethhdr *) skb->data)->h_proto);

  TS_REPORT_DATA(MOD_IB_NET,
                 "%s: packet to transmit, length=%d ethertype=0x%04x",
                 dev->name, skb->len, ethertype);

  if (!netif_carrier_ok(dev)) {
    TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_GEN,
             "%s: dropping packet since fabric is not up",
             dev->name);

    dev->trans_start = jiffies;
    ++priv->stats.tx_packets;
    priv->stats.tx_bytes += skb->len;
    dev_kfree_skb(skb);
    return 0;
  }

#if defined(TS_IPOIB_TRANSPORT_SYSPORT) && defined(TS_TEMPORARY_ANAFA_MULTICAST_BUG_WORKAROUND)
  {
    if (memcmp(broadcast_mac_addr, skb->data, ETH_ALEN) == 0) {
      TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_GEN,
               "dropping multicast packet (Anafa F/W bug workaround)");
      dev->trans_start = jiffies;
      ++priv->stats.tx_packets;
      priv->stats.tx_bytes += skb->len;
      dev_kfree_skb(skb);
      return 0;
    }
  }
#endif

  switch (ethertype) {
  case ETH_P_ARP:
    if (tsIpoibDeviceArpRewriteSend(dev, skb)) {
      ++priv->stats.tx_dropped;
      dev_kfree_skb(skb);
    }
    return 0;

  case ETH_P_IP:
  {
    int ret;

    if (skb->data[0] == 0x01 && skb->data[1] == 0x00
        && skb->data[2] == 0x5e && (skb->data[3] & 0x80) == 0x00) {
      /* Multicast MAC addr */
      tTS_IPOIB_MULTICAST_GROUP mcast = NULL;
      tTS_IB_GID mgid;
      struct iphdr *iph = (struct iphdr *)(skb->data + ETH_HLEN);
      u32 multiaddr = ntohl(iph->daddr);

      memcpy(mgid, ipoib_broadcast_mgid, sizeof(tTS_IB_GID));

      /* Add in the P_Key */
      mgid[4] = (priv->pkey >> 8) & 0xff;
      mgid[5] = priv->pkey & 0xff;

      /* Fixup the group mapping */
      mgid[12] = (multiaddr >> 24) & 0x0f;
      mgid[13] = (multiaddr >> 16) & 0xff;
      mgid[14] = (multiaddr >> 8) & 0xff;
      mgid[15] = multiaddr & 0xff;

      ret = tsIpoibDeviceMulticastLookup(dev, mgid, &mcast);
      switch (ret) {
      case 0:
        return tsIpoibDeviceMulticastSend(dev, mcast, skb);
      case -EAGAIN:
        tsIpoibMulticastGroupQueuePacket(mcast, skb);
        tsIpoibMulticastGroupPut(mcast);
        return 0;
      }
    } else if (memcmp(broadcast_mac_addr, skb->data, ETH_ALEN) == 0) {
      tTS_IPOIB_MULTICAST_GROUP mcast = NULL;

      ret = tsIpoibDeviceMulticastLookup(dev, priv->bcast_gid, &mcast);
      switch (ret) {
      case 0:
        return tsIpoibDeviceMulticastSend(dev, mcast, skb);
      case -EAGAIN:
        tsIpoibMulticastGroupQueuePacket(mcast, skb);
        tsIpoibMulticastGroupPut(mcast);
        return 0;
      }
    } else {
      tTS_IPOIB_ARP_ENTRY entry = NULL;

      ret = tsIpoibDeviceArpLookup(dev, skb->data, &entry);
      switch (ret) {
      case 0:
        return tsIpoibDeviceArpSend(dev, entry, skb);
      case -EAGAIN:
        tsIpoibArpEntryQueuePacket(entry, skb);
        tsIpoibArpEntryPut(entry);
        return 0;
      }
    }

    switch (ret) {
    case 0:
    case -EAGAIN:
      /* Shouldn't get here anyway */
      break;
    case -ENOENT:
      TS_REPORT_WARN(MOD_IB_NET,
                     "%s: dropping packet with unknown dest "
                     "%02x:%02x:%02x:%02x:%02x:%02x",
                     dev->name,
                     skb->data[0], skb->data[1], skb->data[2],
                     skb->data[3], skb->data[4], skb->data[5]);
      ++priv->stats.tx_dropped;
      dev_kfree_skb(skb);
      return 0;
    default:
      TS_REPORT_WARN(MOD_IB_NET,
                     "%s: sending to %02x:%02x:%02x:%02x:%02x:%02x "
                     "failed (ret = %d)",
                     dev->name,
                     skb->data[0], skb->data[1], skb->data[2],
                     skb->data[3], skb->data[4], skb->data[5], ret);
      ++priv->stats.tx_dropped;
      dev_kfree_skb(skb);
      return 0;
    }
    return 0;
  }

  case ETH_P_IPV6:
    TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_GEN,
             "%s: dropping IPv6 packet", dev->name);
    ++priv->stats.tx_dropped;
    dev_kfree_skb(skb);
    return 0;

  default:
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: dropping packet with unknown ethertype 0x%04x",
                   dev->name, ethertype);
    ++priv->stats.tx_dropped;
    dev_kfree_skb(skb);
    return 0;
  }

  return 0;
}

struct net_device_stats *_tsIpoibDeviceGetStats(
                                                struct net_device *dev
                                                ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  return &priv->stats;
}

static void _tsIpoibDeviceTimeout(
                                  struct net_device *dev
                                  ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  if (atomic_read(&priv->tx_free)
      && !test_bit(TS_IPOIB_FLAG_TIMEOUT, &priv->flags)) {
    char ring[TS_IPOIB_TX_RING_SIZE + 1];
    int i;

    for (i = 0; i < TS_IPOIB_TX_RING_SIZE; ++i) {
      ring[i] = priv->tx_ring[i].skb ? 'X' : '.';
    };
    ring[i] = 0;

    TS_REPORT_WARN(MOD_IB_NET, "%s: transmit timeout: latency %ld, tx_free %d, tx_ring [%s]",
                   dev->name,
                   jiffies - dev->trans_start,
                   atomic_read(&priv->tx_free),
                   ring);

    set_bit(TS_IPOIB_FLAG_TIMEOUT, &priv->flags);
  } else {
    TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_GEN,
             "%s: transmit timeout: latency %ld",
             dev->name, jiffies - dev->trans_start);
  }
}

/*
 * Setup the packet to look like ethernet here, we'll fix it later when
 * we actually send it to look like an IPoIB packet
 */
static int _tsIpoibDeviceHardHeader(
                                    struct sk_buff *skb,
                                    struct net_device *dev,
                                    unsigned short type,
                                    void *daddr,
                                    void *saddr,
                                    unsigned len
                                    ) {
  struct ethhdr *header   = (struct ethhdr *) skb_push(skb, ETH_HLEN);

  TS_REPORT_DATA(MOD_IB_NET,
                 "%s: building header, ethertype=0x%04x",
                 dev->name, type);

  if (daddr) {
    memcpy(header->h_dest,   daddr, TS_IPOIB_ADDRESS_HASH_BYTES);
  }
  if (saddr) {
    memcpy(header->h_source, saddr, TS_IPOIB_ADDRESS_HASH_BYTES);
  } else {
    memcpy(header->h_source, dev->dev_addr, TS_IPOIB_ADDRESS_HASH_BYTES);
  }

  header->h_proto = htons(type);

  return 0;
}

static void _tsIpoibDeviceSetMulticastList(
                                           struct net_device *dev
                                           ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  /* Skip any multicast groups for the chassis interface */
  if (test_bit(TS_IPOIB_FLAG_CHASSISINTF, &priv->flags)) {
    return;
  }

  schedule_task(&priv->restart_task);
}

int tsIpoibDeviceInit(
                      struct net_device *dev,
                      tTS_IB_DEVICE_HANDLE ca,
                      int port_num
                      ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  TS_REPORT_INOUT(MOD_IB_NET, "%s: initializing device",
                  dev->name);

  dev->open               = tsIpoibDeviceOpen;
  dev->stop               = _tsIpoibDeviceStop;
  dev->do_ioctl           = _tsIpoibDeviceIoctl;
  dev->change_mtu         = _tsIpoibDeviceChangeMtu;
  dev->set_config         = _tsIpoibDeviceConfig;
  dev->hard_start_xmit    = _tsIpoibDeviceXmit;
  dev->get_stats          = _tsIpoibDeviceGetStats;
  dev->tx_timeout         = _tsIpoibDeviceTimeout;
  dev->hard_header        = _tsIpoibDeviceHardHeader;
  dev->set_multicast_list = _tsIpoibDeviceSetMulticastList;
  dev->watchdog_timeo  = HZ;

  dev->rebuild_header      = 0;
  dev->set_mac_address     = 0;
  dev->header_cache_update = 0;

  dev->flags |=  IFF_BROADCAST | IFF_MULTICAST;

  dev->hard_header_len	= ETH_HLEN;
  dev->addr_len		= TS_IPOIB_ADDRESS_HASH_BYTES;
  dev->type		= ARPHRD_ETHER;
  dev->tx_queue_len     = TS_IPOIB_TX_RING_SIZE * 2;
  /* MTU will be reset when mcast join happens */
  dev->mtu              = TS_IPOIB_PACKET_SIZE - TS_IPOIB_ENCAP_LEN;
  priv->mcast_mtu = priv->admin_mtu = dev->mtu;

  memset(dev->broadcast, 0xff, dev->addr_len);

  netif_carrier_off(dev);

  SET_MODULE_OWNER(dev);

  spin_lock_init(&priv->lock);

  if (tsIpoibDeviceArpInit(dev)) {
    goto out;
  }

  /* Allocate RX/TX "rings" to hold queued skbs */

  priv->rx_ring = kmalloc(TS_IPOIB_RX_RING_SIZE * sizeof(struct sk_buff *),
                          GFP_KERNEL);
  if (!priv->rx_ring) {
    TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to allocate RX ring (%d entries)",
                    dev->name, TS_IPOIB_RX_RING_SIZE);
    goto out_arp_cleanup;
  }
  memset(priv->rx_ring,
         0,
         TS_IPOIB_RX_RING_SIZE * sizeof(struct sk_buff *));

  priv->tx_ring = kmalloc(TS_IPOIB_TX_RING_SIZE * sizeof(struct tTS_IPOIB_TX_BUF),
                          GFP_KERNEL);
  if (!priv->tx_ring) {
    TS_REPORT_FATAL(MOD_IB_NET, "%s: failed to allocate TX ring (%d entries)",
                    dev->name, TS_IPOIB_TX_RING_SIZE);
    goto out_rx_ring_cleanup;
  }
  memset(priv->tx_ring,
         0,
         TS_IPOIB_TX_RING_SIZE * sizeof(struct tTS_IPOIB_TX_BUF));

  /* set up the rest of our private data */

  /* priv->tx_head is already 0 */
  atomic_set(&priv->tx_free, TS_IPOIB_TX_RING_SIZE);

  if (tsIpoibDeviceIbInit(dev, ca, port_num)) {
    goto out_tx_ring_cleanup;
  }

  if (tsIpoibDeviceProcInit(dev)) {
    goto out_ib_cleanup;
  }

  return 0;

 out_ib_cleanup:
  tsIpoibDeviceIbCleanup(dev);

 out_tx_ring_cleanup:
  kfree(priv->tx_ring);

 out_rx_ring_cleanup:
  kfree(priv->rx_ring);

 out_arp_cleanup:
  tsIpoibDeviceArpCleanup(dev);

 out:
  return -ENOMEM;
}

void tsIpoibDeviceCleanup(
                          struct net_device *dev
                          ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  struct list_head *ptr, *tmp;
  int i;

  /* Delete any child interfaces first */
  /* Safe since it's either protected by ipoib_device_mutex or empty */
  list_for_each_safe(ptr, tmp, &priv->child_intfs) {
    struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *cpriv;

    cpriv = list_entry(ptr, struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT, list);

    tsIpoibDeviceCleanup(&cpriv->dev);
    unregister_netdev(&cpriv->dev);

    list_del(&cpriv->list);

    kfree(cpriv);
  }

  tsIpoibDeviceProcCleanup(dev);
  tsIpoibDeviceArpCleanup(dev);
  tsIpoibDeviceIbCleanup(dev);

  if (priv->rx_ring) {
    TS_REPORT_CLEANUP(MOD_IB_NET,
                      "%s: cleaning up RX ring",
                      dev->name);

    for (i = 0; i < TS_IPOIB_RX_RING_SIZE; ++i) {
      if (priv->rx_ring[i]) {
        dev_kfree_skb(priv->rx_ring[i]);
      }
    }

    kfree(priv->rx_ring);
    priv->rx_ring = NULL;
  }

  if (priv->tx_ring) {
    TS_REPORT_CLEANUP(MOD_IB_NET,
                      "%s: cleaning up TX ring",
                      dev->name);

    for (i = 0; i < TS_IPOIB_TX_RING_SIZE; ++i) {
      if (priv->tx_ring[i].skb) {
        dev_kfree_skb(priv->tx_ring[i].skb);
      }
    }

    kfree(priv->tx_ring);
    priv->tx_ring = NULL;
  }
}

struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *tsIpoibAllocateInterface(
                                                                 void
                                                                 ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv;

  priv = kmalloc(sizeof(*priv), GFP_KERNEL);
  if (!priv) {
    TS_REPORT_FATAL(MOD_IB_NET, "failed to allocate private struct");
    return NULL;
  }

  memset(priv, 0, sizeof(*priv));

  sema_init(&priv->mcast_mutex, 1);

  atomic_set(&priv->mcast_joins, 0);

  INIT_LIST_HEAD(&priv->child_intfs);
  INIT_LIST_HEAD(&priv->multicast_list);

  INIT_TQUEUE(&priv->restart_task, tsIpoibDeviceMulticastRestartTask, &priv->dev);

  priv->dev.priv = priv;

  return priv;
}

int ipoib_add_port(const char *format,
                   tTS_IB_DEVICE_HANDLE hca,
                   tTS_IB_PORT port,
                   int chassis_intf)
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv;
  int result = -ENOMEM;

  priv = tsIpoibAllocateInterface();
  if (!priv) {
    goto alloc_mem_failed;
  }

  priv->pkey = 0xffff;
  if (chassis_intf) {
    set_bit(TS_IPOIB_FLAG_CHASSISINTF, &priv->flags);
  }

#if 0
  /* We'll probably use something like this in the future */
  result = tsIbPkeyEntryGet(priv->ca,
                            priv->port,
                            0,
                            &priv->pkey);
  if (result) {
    TS_REPORT_FATAL(MOD_IB_NET,
                    "%s: tsIbPkeyEntryGet failed (ret = %d)",
                    priv->dev.name, result);
    goto dev_pkey_get_failed;
  }
#endif

  result = dev_alloc_name(&priv->dev, format);
  if (result < 0) {
    TS_REPORT_FATAL(MOD_IB_NET,
                    "failed to get device name (ret = %d)",
                    result);
    goto dev_alloc_failed;
  }

  result = tsIpoibDeviceInit(&priv->dev, hca, port);
  if (result < 0) {
    TS_REPORT_FATAL(MOD_IB_NET,
                    "failed to initialize net device %d, port %d (ret = %d)",
                    hca, port, result);
    goto device_init_failed;
  }

  result = tsIpoibTransportPortMonitorStart(&priv->dev);
  if (result < 0) {
    TS_REPORT_FATAL(MOD_IB_NET,
                    "failed to setup port monitor for device %d, port %d (ret = %d)",
                    hca, port, result);
    goto port_monitor_failed;
  }

  result = register_netdev(&priv->dev);
  if (result) {
    TS_REPORT_FATAL(MOD_IB_NET,
                    "%s: failed to initialize; error %i",
                    priv->dev.name, result);
    goto register_failed;
  }

  down(&ipoib_device_mutex);
  list_add_tail(&priv->list, &ipoib_device_list);
  up(&ipoib_device_mutex);

  return 0;

register_failed:
  tsIpoibTransportPortMonitorStop(&priv->dev);

port_monitor_failed:
  tsIpoibDeviceCleanup(&priv->dev);

device_init_failed:
  /*
   * Nothing to do since the device name only gets finally added
   * to the linked list in register_netdev
   */

dev_alloc_failed:
  kfree(priv);

alloc_mem_failed:

  return result;
}

static int __init ipoib_init(void)
{
	int ret;

	ret = ipoib_transport_create_devices();
	if (ret)
		return ret;

	down(&ipoib_device_mutex);
	if (list_empty(&ipoib_device_list)) {
		up(&ipoib_device_mutex);
		ipoib_transport_cleanup();
		return -ENODEV;
	}
	up(&ipoib_device_mutex);

	tsIpoibProcInit();

#if defined(TS_HOST_DRIVER)   || \
    defined(TS_ppc440_en_sun) || \
    defined(TS_ppc440_fc_sun) || \
    defined(TS_ppc440_fg_sun)
	tsIpoibVlanInit();
#endif

	return 0;
}

static void __exit ipoib_cleanup(void)
{
	struct list_head *ptr, *tmp;

#if defined(TS_HOST_DRIVER)   || \
    defined(TS_ppc440_en_sun) || \
    defined(TS_ppc440_fc_sun) || \
    defined(TS_ppc440_fg_sun)
	tsIpoibVlanCleanup();
#endif
	tsIpoibProcCleanup();
	ipoib_transport_cleanup();

	down(&ipoib_device_mutex);
	list_for_each_safe(ptr, tmp, &ipoib_device_list) {
		struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = list_entry(ptr, struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT, list);

		tsIpoibTransportPortMonitorStop(&priv->dev);
		tsIpoibDeviceCleanup(&priv->dev);
		unregister_netdev(&priv->dev);

		list_del(&priv->list);

		kfree(priv);
	}
	up(&ipoib_device_mutex);
}

module_init(ipoib_init);
module_exit(ipoib_cleanup);

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
