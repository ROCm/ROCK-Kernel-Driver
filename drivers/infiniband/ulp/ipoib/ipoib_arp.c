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

  $Id: ipoib_arp.c 32 2004-04-09 03:57:42Z roland $
*/

#include "ipoib.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include "ts_ib_sa_client.h"

#include <linux/slab.h>
#include <linux/if_arp.h>

enum {
  TS_IPOIB_ADDRESS_HASH_BITS  = TS_IPOIB_ADDRESS_HASH_BYTES * 8,
};

struct tTS_IPOIB_ARP_CACHE_STRUCT {
  struct list_head table[256];
};

struct tTS_IPOIB_ARP_ENTRY_STRUCT {
  struct list_head        cache_list;

  atomic_t                refcnt;

  uint8_t                 hash[TS_IPOIB_ADDRESS_HASH_BYTES];

  tTS_IB_GID              gid;
  tTS_IB_QPN              qpn;
  tTS_IB_LID              lid;
  tTS_IB_SL               sl;
  tTS_IB_ADDRESS_HANDLE   address_handle;
  tTS_IB_CLIENT_QUERY_TID tid;

  unsigned long           created;
  unsigned long           last_verify;
  unsigned long           last_directed_query;
  unsigned long           first_directed_reply;

  unsigned char           require_verify : 1;
  unsigned char           directed_query : 1;
  unsigned char           directed_reply : 4;

  unsigned char           logcount;

  struct sk_buff_head     pkt_queue;

  struct net_device      *dev;
};

struct tTS_IPOIB_ARP_ITERATOR_STRUCT {
  struct net_device *dev;
  uint8_t            hash;
  struct list_head  *cur;
};

struct tTS_IPOIB_ARP_PAYLOAD_STRUCT {
  uint8_t src_hw_addr[TS_IPOIB_HW_ADDR_LEN];
  uint8_t src_ip_addr[4];
  uint8_t dst_hw_addr[TS_IPOIB_HW_ADDR_LEN];
  uint8_t dst_ip_addr[4];
};

/* =============================================================== */
/*.._tsIpoibArpHash -- hash GID/QPN to 6 bytes                     */
static void _tsIpoibArpHash(
                            tTS_IB_GID gid,
                            tTS_IB_QPN qpn,
                            uint8_t *hash
                            ) {
  /* We use the FNV hash (http://www.isthe.com/chongo/tech/comp/fnv/) */
#define TS_FNV_64_PRIME 0x100000001b3ULL
#define TS_FNV_64_INIT  0xcbf29ce484222325ULL

  int i;
  uint64_t h = TS_FNV_64_INIT;

  /* make qpn big-endian so we know where digits are */
  qpn = cpu_to_be32(qpn);

  for (i = 0; i < sizeof(tTS_IB_GID) + 3; ++i) {
    h *= TS_FNV_64_PRIME;
    h ^= (i < sizeof(tTS_IB_GID)
          ? gid[i]
          : ((uint8_t *) &qpn)[i - sizeof(tTS_IB_GID) + 1]);
  }

  /* xor fold down to 6 bytes and make big-endian */
  h = cpu_to_be64((h >> TS_IPOIB_ADDRESS_HASH_BITS)
                  ^ (h & ((1ULL << TS_IPOIB_ADDRESS_HASH_BITS) - 1)));

  memcpy(hash, ((uint8_t *) &h) + 2, TS_IPOIB_ADDRESS_HASH_BYTES);
}

/* =============================================================== */
/*..tsIpoibArpEntryGet -- increment reference count for ARP entry  */
void tsIpoibArpEntryGet(
                        tTS_IPOIB_ARP_ENTRY entry
                        )
{
  atomic_inc(&entry->refcnt);
}

/* =============================================================== */
/*..tsIpoibArpEntryPut -- decrement reference count for ARP entry  */
void tsIpoibArpEntryPut(
                        tTS_IPOIB_ARP_ENTRY entry
                        )
{
  struct net_device *dev = entry->dev;

  if (atomic_dec_and_test(&entry->refcnt)) {
    TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_ARP,
           "%s: deleting ARP shadow cache entry %02x:%02x:%02x:%02x:%02x:%02x",
           dev->name,
           entry->hash[0], entry->hash[1], entry->hash[2],
           entry->hash[3], entry->hash[4], entry->hash[5]);

    if (entry->address_handle != TS_IB_HANDLE_INVALID) {
      int ret = tsIbAddressDestroy(entry->address_handle);
      if (ret < 0) {
        TS_REPORT_WARN(MOD_IB_NET,
                       "tsIbAddressDestroy failed (ret = %d)", ret);
      }
    }

    while (!skb_queue_empty(&entry->pkt_queue)) {
      struct sk_buff *skb = skb_dequeue(&entry->pkt_queue);

      skb->dev = dev;
      dev_kfree_skb(skb);
    }

    kfree(entry);
  }
}

/* =============================================================== */
/*..__tsIpoibDeviceArpFind -- find ARP entry (unlocked)            */
static tTS_IPOIB_ARP_ENTRY __tsIpoibDeviceArpFind(
                                                  struct net_device *dev,
                                                  const uint8_t *hash
                                                  ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  struct list_head *cur;

  list_for_each(cur, &priv->arp_cache->table[hash[0]]) {
    tTS_IPOIB_ARP_ENTRY entry =
      list_entry(cur, tTS_IPOIB_ARP_ENTRY_STRUCT, cache_list);

    TS_REPORT_DATA(MOD_IB_NET,
                   "%s: matching %02x:%02x:%02x:%02x:%02x:%02x",
                   dev->name,
                   entry->hash[0], entry->hash[1], entry->hash[2],
                   entry->hash[3], entry->hash[4], entry->hash[5]);

    if (!memcmp(hash, entry->hash, TS_IPOIB_ADDRESS_HASH_BYTES)) {
      tsIpoibArpEntryGet(entry);
      return entry;
    }
  }

  return NULL;
}

/* =============================================================== */
/*.._tsIpoibDeviceArpFind -- find ARP entry                        */
static tTS_IPOIB_ARP_ENTRY _tsIpoibDeviceArpFind(
                                                 struct net_device *dev,
                                                 const uint8_t *hash
                                                 ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  tTS_IPOIB_ARP_ENTRY entry;
  unsigned long flags;

  spin_lock_irqsave(&priv->lock, flags);
  entry = __tsIpoibDeviceArpFind(dev, hash);
  spin_unlock_irqrestore(&priv->lock, flags);

  return entry;
}

/* =============================================================== */
/*.._tsIpoibArpPathRecordCompletion -- path record comp func       */
static int _tsIpoibArpPathRecordCompletion(
                                           tTS_IB_CLIENT_QUERY_TID tid,
                                           int status,
                                           tTS_IB_PATH_RECORD path,
                                           int remaining,
                                           void *entry_ptr
                                           ) {
  tTS_IPOIB_ARP_ENTRY entry = entry_ptr;
  struct net_device *dev = entry->dev;
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_ARP,
           "%s: path record lookup done, status %d",
           dev->name, status);

  entry->tid = TS_IB_CLIENT_QUERY_TID_INVALID;

  if (!status) {
    tTS_IB_ADDRESS_VECTOR_STRUCT av = {
      .dlid             = path->dlid,
      .service_level    = path->sl,
      .port             = priv->port,
      .source_path_bits = 0,
      .use_grh          = 0,
      .static_rate      = 0
    };

    if (tsIbAddressCreate(priv->pd,
                          &av,
                          &entry->address_handle)) {
      TS_REPORT_WARN(MOD_IB_NET,
                     "%s: tsIpoibAddressCreate failed",
                     dev->name);
    } else {
      TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_ARP,
               "%s: created address handle 0x%08x for LID 0x%04x, SL %d",
               dev->name, entry->address_handle, path->dlid, path->sl);

      entry->lid = path->dlid;
      entry->sl  = path->sl;

      /* actually send any queued packets */
      while (!skb_queue_empty(&entry->pkt_queue)) {
        struct sk_buff *skb = skb_dequeue(&entry->pkt_queue);

        skb->dev = dev;

        if (dev_queue_xmit(skb)) {
          TS_REPORT_WARN(MOD_IB_NET,
                         "%s: dev_queue_xmit failed to requeue packet",
                         dev->name);
        }
      }
    }
  } else {
    if (status != -ETIMEDOUT && entry->logcount < 20) {
      TS_REPORT_WARN(MOD_IB_NET,
                     "%s: tsIbPathRecordRequest completion failed for %02x:%02x:%02x:%02x:%02x:%02x, status = %d",
                     dev->name,
                     entry->hash[0], entry->hash[1], entry->hash[2],
                     entry->hash[3], entry->hash[4], entry->hash[5],
                     status);
      entry->logcount++;
    }

    /* Flush out any queued packets */
    while (!skb_queue_empty(&entry->pkt_queue)) {
      struct sk_buff *skb = skb_dequeue(&entry->pkt_queue);

      skb->dev = dev;
      dev_kfree_skb(skb);
    }
  }

  tsIpoibArpEntryPut(entry);

  /* nonzero return means no more callbacks (we have our path) */
  return 1;
}

/* =============================================================== */
/*.._tsIpoibArpAllocateEntry -- allocate shadow ARP entry          */
static tTS_IPOIB_ARP_ENTRY _tsIpoibArpAllocateEntry(
                                                    struct net_device *dev
                                                    ) {
  tTS_IPOIB_ARP_ENTRY entry;

  entry = kmalloc(sizeof *entry, GFP_ATOMIC);
  if (!entry) {
    return NULL;
  }

  atomic_set(&entry->refcnt, 2);	/* The calling function needs to put */

  entry->dev            = dev;

  entry->require_verify = 0;
  entry->directed_query = 1;
  entry->directed_reply = 0;

  entry->created              = jiffies;
  entry->last_verify          = jiffies;
  entry->last_directed_query  = jiffies;
  entry->first_directed_reply = jiffies;

  entry->logcount = 0;

  INIT_LIST_HEAD(&entry->cache_list);

  skb_queue_head_init(&entry->pkt_queue);

  entry->address_handle = TS_IB_HANDLE_INVALID;

  /* Will force a trigger on the first packet we need to send */
  entry->tid            = TS_IB_CLIENT_QUERY_TID_INVALID;

  return entry;
}

/* =============================================================== */
/*..tsIpoibDeviceArpAdd -- add ARP entry                           */
tTS_IPOIB_ARP_ENTRY tsIpoibDeviceArpAdd(
                                        struct net_device *dev,
                                        tTS_IB_GID gid,
                                        tTS_IB_QPN qpn
                                        ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  uint8_t hash[TS_IPOIB_ADDRESS_HASH_BYTES];
  tTS_IPOIB_ARP_ENTRY entry;
  unsigned long flags;

  _tsIpoibArpHash(gid, qpn, hash);

  entry = _tsIpoibDeviceArpFind(dev, hash);
  if (entry) {
    if (entry->qpn != qpn
        || memcmp(entry->gid, gid, sizeof(tTS_IB_GID))) {
      TS_REPORT_WARN(MOD_IB_NET,
                     "%s: hash collision",
                     dev->name);
      tsIpoibArpEntryPut(entry);
      return NULL;
    } else {
      return entry;
    }
  }

  entry = _tsIpoibArpAllocateEntry(dev);
  if (!entry) {
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: out of memory for ARP entry",
                   dev->name);
    return NULL;
  }

  memcpy(entry->hash, hash, sizeof entry->hash);
  memcpy(entry->gid, gid, sizeof(tTS_IB_GID));

  entry->qpn = qpn;

  entry->require_verify = 1;

  spin_lock_irqsave(&priv->lock, flags);
  list_add_tail(&entry->cache_list, &priv->arp_cache->table[hash[0]]);
  spin_unlock_irqrestore(&priv->lock, flags);

  return entry;
}

/* =============================================================== */
/*..tsIpoibDeviceArpLocalAdd -- add ARP hash for local node        */
tTS_IPOIB_ARP_ENTRY tsIpoibDeviceArpLocalAdd(
                                             struct net_device *dev,
                                             tTS_IB_GID gid,
                                             tTS_IB_QPN qpn
                                             ) {
  _tsIpoibArpHash(gid, qpn, dev->dev_addr);
  return tsIpoibDeviceArpAdd(dev, gid, qpn);
}

/* =============================================================== */
/*..tsIpoibArpIteratorInit -- create new ARP iterator              */
tTS_IPOIB_ARP_ITERATOR tsIpoibArpIteratorInit(
                                              struct net_device *dev
                                              ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  tTS_IPOIB_ARP_ITERATOR iter;

  iter = kmalloc(sizeof *iter, GFP_KERNEL);
  if (!iter) {
    return NULL;
  }

  iter->dev  = dev;
  iter->hash = 0;
  iter->cur  = priv->arp_cache->table[0].next;

  while (iter->cur == &priv->arp_cache->table[iter->hash]) {
    ++iter->hash;
    if (iter->hash == 0) {
      /* ARP table is empty */
      kfree(iter);
      return NULL;
    }
    iter->cur = priv->arp_cache->table[iter->hash].next;
  }

  return iter;
}

/* =============================================================== */
/*..tsIpoibArpIteratorFree -- free ARP iterator                    */
void tsIpoibArpIteratorFree(
                            tTS_IPOIB_ARP_ITERATOR iter
                            ) {
  kfree(iter);
}

/* =============================================================== */
/*..tsIpoibArpIterator -- incr. iter. -- return non-zero at end    */
int tsIpoibArpIteratorNext(
                           tTS_IPOIB_ARP_ITERATOR iter
                           ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = iter->dev->priv;

  while (1) {
    iter->cur = iter->cur->next;

    if (iter->cur == &priv->arp_cache->table[iter->hash]) {
      ++iter->hash;
      if (!iter->hash) {
        return 1;
      }
      iter->cur = &priv->arp_cache->table[iter->hash];
    } else {
      return 0;
    }
  }
}

/* =============================================================== */
/*..tsIpoibArpIteratorRead -- get data pointed to by ARP iterator  */
void tsIpoibArpIteratorRead(
                            tTS_IPOIB_ARP_ITERATOR iter,
                            uint8_t *hash,
                            tTS_IB_GID gid,
                            tTS_IB_QPN *qpn,
                            unsigned long *created,
                            unsigned long *last_verify,
                            unsigned int *queuelen,
                            unsigned int *complete
                            ) {
  tTS_IPOIB_ARP_ENTRY entry;

  entry = list_entry(iter->cur, tTS_IPOIB_ARP_ENTRY_STRUCT, cache_list);

  memcpy(hash, entry->hash, TS_IPOIB_ADDRESS_HASH_BYTES);
  memcpy(gid, entry->gid, sizeof(tTS_IB_GID));
  *qpn = entry->qpn;
  *created = entry->created;
  *last_verify = entry->last_verify;
  *queuelen = skb_queue_len(&entry->pkt_queue);
  *complete = entry->address_handle != TS_IB_HANDLE_INVALID;
}

/* =============================================================== */
/*.._tsIpoibArpPathLookup - start path lookup                      */
static int _tsIpoibArpPathLookup(
                                 tTS_IPOIB_ARP_ENTRY entry
                                 ) {
  struct net_device *dev = entry->dev;
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  tTS_IB_CLIENT_QUERY_TID tid;

  tsIpoibArpEntryGet(entry);
  if (tsIbPathRecordRequest(priv->ca,
                            priv->port,
                            priv->local_gid,
                            entry->gid,
                            priv->pkey,
                            0,
                            HZ,
                            3600 * HZ, /* XXX cache jiffies */
                            _tsIpoibArpPathRecordCompletion,
                            entry,
                            &tid)) {
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: tsIbPathRecordRequest failed",
                   dev->name);
    tsIpoibArpEntryPut(entry);
  } else {
    TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_ARP,
             "%s: no address vector, starting path record lookup",
             dev->name);
    entry->tid = tid;
  }

  return 0;
}

/* =============================================================== */
/*.._tsIpoibArpLookup -- return address and qpn for entry          */
static int _tsIpoibArpLookup(
                             tTS_IPOIB_ARP_ENTRY entry
                             ) {
  struct net_device *dev = entry->dev;

  if (entry->address_handle != TS_IB_HANDLE_INVALID) {
    return 0;
  }

  /*
   * Found an entry, but without an address handle.
   * Check to see if we have a path record lookup executing and if not,
   * start one up.
   */

  if (entry->tid == TS_IB_CLIENT_QUERY_TID_INVALID) {
    _tsIpoibArpPathLookup(entry);
  } else {
    TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_ARP,
             "%s: no address vector, but path record lookup already started",
             dev->name);
  }

  return -EAGAIN;
}

/* =============================================================== */
/*..tsIpoibDeviceArpLookup -- lookup a hash in shadow ARP cache    */
int tsIpoibDeviceArpLookup(
                           struct net_device *dev,
                           uint8_t *hash,
                           tTS_IPOIB_ARP_ENTRY *entry
                           ) {
  tTS_IPOIB_ARP_ENTRY tentry;

  tentry = _tsIpoibDeviceArpFind(dev, hash);
  if (!tentry) {
    return -ENOENT;
  }

  *entry = tentry;

  return _tsIpoibArpLookup(tentry);
}

/* =============================================================== */
/*.._tsIpoibArpTxCallback -- put reference to entry after TX       */
static void _tsIpoibArpTxCallback(
                                  void *ptr
                                  )
{
  tsIpoibArpEntryPut((tTS_IPOIB_ARP_ENTRY)ptr);
}

/* =============================================================== */
/*..tsIpoibDeviceArpSend -- send packet to dest                    */
int tsIpoibDeviceArpSend(
                         struct net_device *dev,
                         tTS_IPOIB_ARP_ENTRY entry,
                         struct sk_buff *skb
                         )
{
  return tsIpoibDeviceSend(dev, skb, _tsIpoibArpTxCallback,
                           entry, entry->address_handle, entry->qpn);
}


/* =============================================================== */
/*..tsIpoibArpEntryQueuePacket -- queue packet during path rec lookup */
int tsIpoibArpEntryQueuePacket(
                               tTS_IPOIB_ARP_ENTRY entry,
                               struct sk_buff *skb
                               ) {
  skb_queue_tail(&entry->pkt_queue, skb);

  return 0;
}

/* =============================================================== */
/*..tsIpoibDeviceArpRewriteReceive -- rewrite ARP packet for Linux */
int tsIpoibDeviceArpRewriteReceive(
                                   struct net_device *dev,
                                   struct sk_buff *skb
                                   ) {
  struct arphdr *arp;
  struct tTS_IPOIB_ARP_PAYLOAD_STRUCT *payload;
  struct arphdr *new_arp;
  struct ethhdr *header;
  uint8_t *new_payload;
  struct sk_buff *new_skb;
  tTS_IPOIB_ARP_ENTRY entry;
  uint8_t hash[TS_IPOIB_ADDRESS_HASH_BYTES];
  int ret = 0;

  arp     = (struct arphdr *) skb->data;
  payload = (struct tTS_IPOIB_ARP_PAYLOAD_STRUCT *) skb_pull(skb, sizeof *arp);

  TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_ARP,
           "%s: ARP receive: hwtype=0x%04x proto=0x%04x hwlen=%d prlen=%d op=0x%04x "
           "sip=%d.%d.%d.%d dip=%d.%d.%d.%d",
           dev->name,
           ntohs(arp->ar_hrd),
           ntohs(arp->ar_pro),
           arp->ar_hln,
           arp->ar_pln,
           ntohs(arp->ar_op),
           payload->src_ip_addr[0], payload->src_ip_addr[1],
           payload->src_ip_addr[2], payload->src_ip_addr[3],
           payload->dst_ip_addr[0], payload->dst_ip_addr[1],
           payload->dst_ip_addr[2], payload->dst_ip_addr[3]);

  new_skb = dev_alloc_skb(dev->hard_header_len
                          + sizeof *new_arp
                          + 2 * (TS_IPOIB_ADDRESS_HASH_BYTES + 4));
  if (!new_skb) {
    ret = -ENOMEM;
    goto out;
  }

  new_skb->mac.raw = new_skb->data;
  header = (struct ethhdr *)new_skb->mac.raw;
  skb_reserve(new_skb, dev->hard_header_len);

  new_arp     = (struct arphdr *) skb_put(new_skb, sizeof *new_arp);
  new_payload = (uint8_t *) skb_put(new_skb,
                                    2 * (TS_IPOIB_ADDRESS_HASH_BYTES + 4));

  header->h_proto = htons(ETH_P_ARP);

  new_skb->dev      = dev;
  new_skb->pkt_type = PACKET_HOST;
  new_skb->protocol = htons(ETH_P_ARP);

  /* copy ARP header */
  *new_arp = *arp;
  new_arp->ar_hrd = htons(ARPHRD_ETHER);
  new_arp->ar_hln = TS_IPOIB_ADDRESS_HASH_BYTES;

  /* copy IP addresses */
  memcpy(new_payload + TS_IPOIB_ADDRESS_HASH_BYTES,
         payload->src_ip_addr,
         4);
  memcpy(new_payload + 2 * TS_IPOIB_ADDRESS_HASH_BYTES + 4,
         payload->dst_ip_addr,
         4);

  /* rewrite IPoIB hw address to hashes */
  if (be32_to_cpu(*(uint32_t *) payload->src_hw_addr) & 0xffffff) {
    _tsIpoibArpHash(payload->src_hw_addr + 4,
                    be32_to_cpu(*(uint32_t *) payload->src_hw_addr) & 0xffffff,
                    hash);

    /* add shadow ARP entries if necessary */
    if (ARPOP_REPLY == ntohs(arp->ar_op)) {
      entry = _tsIpoibDeviceArpFind(dev, hash);
      if (entry) {
        if (entry->directed_query &&
            time_before(jiffies, entry->last_directed_query + HZ)) {
          /* Directed query, everything's good */
          entry->last_verify = jiffies;
          entry->directed_query = 0;
        } else {
          /*
           * If we receive another ARP packet in that's not directed and
           * we already have a path record outstanding, don't drop it yet
           */
          if (entry->tid == TS_IB_CLIENT_QUERY_TID_INVALID) {
            /* Delete old one and create a new one */
            TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_ARP,
                     "%s: LID change inferred on query for %02x:%02x:%02x:%02x:%02x:%02x",
                     dev->name,
                     hash[0], hash[1], hash[2], hash[3], hash[4], hash[5]);

            tsIpoibDeviceArpDelete(dev, hash);
            tsIpoibArpEntryPut(entry);
            entry = NULL;
          } else
            TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_ARP,
                     "%s: lookup in progress, skipping destroying entry %02x:%02x:%02x:%02x:%02x:%02x",
                     dev->name,
                     hash[0], hash[1], hash[2], hash[3], hash[4], hash[5]);
        }
      }
    } else
      entry = NULL;

    /* Small optimization, if we already found it once, don't search again */
    if (!entry)
      entry = tsIpoibDeviceArpAdd(dev,
                                  payload->src_hw_addr + 4,
                                  be32_to_cpu(*(uint32_t *) payload->src_hw_addr) & 0xffffff);

    if (ARPOP_REQUEST == ntohs(arp->ar_op)) {
      if (entry && !entry->directed_reply) {
        /* Record when this window started */
        entry->first_directed_reply = jiffies;
      }
    }

    if (entry) {
      tsIpoibArpEntryPut(entry);
    }
  } else {
    memset(hash, 0, sizeof hash);
  }

  memcpy(new_payload, hash, TS_IPOIB_ADDRESS_HASH_BYTES);
  memcpy(header->h_source, hash, sizeof header->h_source);

  if (be32_to_cpu(*(uint32_t *) payload->dst_hw_addr) & 0xffffff) {
    _tsIpoibArpHash(payload->dst_hw_addr + 4,
                    be32_to_cpu(*(uint32_t *) payload->dst_hw_addr) & 0xffffff,
                    hash);

    entry = tsIpoibDeviceArpAdd(dev,
                                payload->dst_hw_addr + 4,
                                be32_to_cpu(*(uint32_t *) payload->dst_hw_addr) & 0xffffff);
    if (entry) {
      tsIpoibArpEntryPut(entry);
    }

    memcpy(new_payload + TS_IPOIB_ADDRESS_HASH_BYTES + 4,
           hash,
           TS_IPOIB_ADDRESS_HASH_BYTES);
    memcpy(header->h_dest, hash, sizeof header->h_dest);
  } else {
    memset(new_payload + TS_IPOIB_ADDRESS_HASH_BYTES + 4,
           0,
           TS_IPOIB_ADDRESS_HASH_BYTES);
    memset(header->h_dest, 0xff, sizeof header->h_dest);
  }

  netif_rx_ni(new_skb);

 out:
  dev_kfree_skb(skb);
  return ret;
}

/* =============================================================== */
/*..tsIpoibDeviceArpRewriteSend -- rewrite and send ARP packet     */
int tsIpoibDeviceArpRewriteSend(
                                struct net_device *dev,
                                struct sk_buff *skb
                                )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  unsigned char broadcast_mac_addr[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
  struct sk_buff *new_skb;
  struct arphdr *arp = (struct arphdr *) (skb->data + ETH_HLEN);
  uint8_t *payload = ((uint8_t *) arp) + sizeof *arp;
  struct arphdr *new_arp;
  struct tTS_IPOIB_ARP_PAYLOAD_STRUCT *new_payload;
  tTS_IPOIB_ARP_ENTRY dentry = NULL, entry;
  tTS_IPOIB_MULTICAST_GROUP dmcast = NULL;
  int ret;

  TS_TRACE(MOD_IB_NET, T_VERY_VERBOSE, TRACE_IB_NET_ARP,
           "%s: ARP send: hwtype=0x%04x proto=0x%04x hwlen=%d prlen=%d op=0x%04x "
           "sip=%d.%d.%d.%d dip=%d.%d.%d.%d",
           dev->name,
           ntohs(arp->ar_hrd),
           ntohs(arp->ar_pro),
           arp->ar_hln,
           arp->ar_pln,
           ntohs(arp->ar_op),
           payload[    arp->ar_hln    ], payload[    arp->ar_hln + 1],
           payload[    arp->ar_hln + 2], payload[    arp->ar_hln + 3],
           payload[2 * arp->ar_hln + 4], payload[2 * arp->ar_hln + 5],
           payload[2 * arp->ar_hln + 6], payload[2 * arp->ar_hln + 7]);

  if (memcmp(broadcast_mac_addr, skb->data, ETH_ALEN) == 0) {
    /* Broadcast gets handled differently */
    ret = tsIpoibDeviceBroadcastLookup(dev, &dmcast);

    /* mcast is only valid if we get a return code of 0 or -EAGAIN */
    switch (ret) {
    case 0:
      break;
    case -EAGAIN:
      tsIpoibMulticastGroupQueuePacket(dmcast, skb);
      tsIpoibMulticastGroupPut(dmcast);
      return 0;
    default:
      TS_REPORT_WARN(MOD_IB_NET,
                     "%s: dropping ARP packet with unknown dest "
                     "%02x:%02x:%02x:%02x:%02x:%02x",
                     dev->name,
                     skb->data[0], skb->data[1],
                     skb->data[2], skb->data[3],
                     skb->data[4], skb->data[5]);
      return 1;
    }
  } else {
    dentry = _tsIpoibDeviceArpFind(dev, skb->data);
    if (!dentry) {
      TS_REPORT_WARN(MOD_IB_NET,
                     "%s: dropping ARP packet with unknown dest "
                     "%02x:%02x:%02x:%02x:%02x:%02x",
                     dev->name,
                     skb->data[0], skb->data[1],
                     skb->data[2], skb->data[3],
                     skb->data[4], skb->data[5]);
      return 1;
    }

    /* Make sure we catch any LID changes */

    /* Update the entry to mark that we last sent a directed ARP query */
    if (dentry->require_verify && dentry->address_handle != TS_IB_HANDLE_INVALID) {
      if (ARPOP_REQUEST == ntohs(arp->ar_op)) {
        dentry->directed_query = 1;
        dentry->last_directed_query = jiffies;
      }

      /*
       * Catch a LID change on the remote end. If we reply to 3 or more
       * ARP queries without a reply, then ditch the entry we have and
       * requery
       */
      if (ARPOP_REPLY == ntohs(arp->ar_op)) {
        dentry->directed_reply++;

        if (!time_before(jiffies, dentry->first_directed_reply + 4 * HZ)) {
          /* We're outside of the time window, so restart the counter */
          dentry->directed_reply = 0;
        } else if (dentry->directed_reply > 3) {
          if (dentry->tid == TS_IB_CLIENT_QUERY_TID_INVALID) {
            /* Delete old one and create a new one */
            TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_ARP,
                     "LID change inferred on reply for %02x:%02x:%02x:%02x:%02x:%02x",
                     dentry->hash[0], dentry->hash[1], dentry->hash[2],
                     dentry->hash[3], dentry->hash[4], dentry->hash[5]);

            tsIpoibDeviceArpDelete(dev, dentry->hash);
            entry = tsIpoibDeviceArpAdd(dev, dentry->gid, dentry->qpn);
            if (NULL == entry) {
              TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_ARP,
                       "could not allocate new entry for %02x:%02x:%02x:%02x:%02x:%02x",
                       dentry->hash[0], dentry->hash[1], dentry->hash[2],
                       dentry->hash[3], dentry->hash[4], dentry->hash[5]);
              tsIpoibArpEntryPut(dentry);
              return 1;
            }

            tsIpoibArpEntryPut(dentry);

            dentry = entry;
          } else
            TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_ARP,
                     "lookup in progress, skipping destroying entry %02x:%02x:%02x:%02x:%02x:%02x",
                     dentry->hash[0], dentry->hash[1], dentry->hash[2],
                     dentry->hash[3], dentry->hash[4], dentry->hash[5]);
        }
      }
    }

    ret = _tsIpoibArpLookup(dentry);
    if (ret == -EAGAIN) {
      tsIpoibArpEntryQueuePacket(dentry, skb);
      tsIpoibArpEntryPut(dentry);
      return 0;
    }
  }

  new_skb = dev_alloc_skb(dev->hard_header_len
                          + sizeof *new_arp + sizeof *new_payload);
  if (!new_skb) {
    if (dentry) {
      tsIpoibArpEntryPut(dentry);
    }
    if (dmcast) {
      tsIpoibMulticastGroupPut(dmcast);
    }

    return 1;
  }
  skb_reserve(new_skb, dev->hard_header_len);

  new_arp     = (struct arphdr *) skb_put(new_skb, sizeof *new_arp);
  new_payload = (struct tTS_IPOIB_ARP_PAYLOAD_STRUCT *) skb_put(new_skb, sizeof *new_payload);

  /* build what we need for the header */
  {
    uint16_t *t;

    /* ethertype */
    t = (uint16_t *) skb_push(new_skb, 2);
    *t = htons(ETH_P_ARP);

    /* leave space so send funct can skip ethernet addrs */
    skb_push(new_skb, TS_IPOIB_ADDRESS_HASH_BYTES * 2);
  }

  /* copy ARP header */
  *new_arp = *arp;
  new_arp->ar_hrd = htons(ARPHRD_INFINIBAND);
  new_arp->ar_hln = TS_IPOIB_HW_ADDR_LEN;

  /* copy IP addresses */
  memcpy(&new_payload->src_ip_addr,
         payload +     arp->ar_hln,
         4);
  memcpy(&new_payload->dst_ip_addr,
         payload + 2 * arp->ar_hln + 4,
         4);

  /* rewrite hash to IPoIB hw address */
  entry = _tsIpoibDeviceArpFind(dev, payload);
  if (!entry) {
    TS_REPORT_WARN(MOD_IB_NET,
                   "%s: can't find hw address for hash "
                   "%02x:%02x:%02x:%02x:%02x:%02x",
                   dev->name,
                   payload[0], payload[1], payload[2],
                   payload[3], payload[4], payload[5]);
    memset(new_payload->src_hw_addr, 0, TS_IPOIB_HW_ADDR_LEN);
  } else {
    *((uint32_t *) new_payload->src_hw_addr) = cpu_to_be32(entry->qpn);
    memcpy(&new_payload->src_hw_addr[4],
           entry->gid,
           sizeof(tTS_IB_GID));
    tsIpoibArpEntryPut(entry);
  }

  if (memcmp(broadcast_mac_addr, payload + TS_IPOIB_ADDRESS_HASH_BYTES + 4,
             ETH_ALEN) == 0) {
    *((uint32_t *) new_payload->dst_hw_addr) = cpu_to_be32(TS_IB_MULTICAST_QPN);
    memcpy(&new_payload->dst_hw_addr[4],
           priv->bcast_gid,
           sizeof(tTS_IB_GID));
  } else {
    entry = _tsIpoibDeviceArpFind(dev, payload + TS_IPOIB_ADDRESS_HASH_BYTES + 4);
    if (!entry) {
      memset(new_payload->dst_hw_addr, 0, TS_IPOIB_HW_ADDR_LEN);
    } else {
      *((uint32_t *) new_payload->dst_hw_addr) = cpu_to_be32(entry->qpn);
      memcpy(&new_payload->dst_hw_addr[4],
             entry->gid,
             sizeof(tTS_IB_GID));
      tsIpoibArpEntryPut(entry);
    }
  }

  dev_kfree_skb(skb);

  if (dmcast) {
    tsIpoibDeviceMulticastSend(dev, dmcast, new_skb);
  } else {
    tsIpoibDeviceArpSend(dev, dentry, new_skb);
  }

  return 0;
}

/* =============================================================== */
/*..tsIpoibDeviceArpInit -- initialize ARP cache                   */
int tsIpoibDeviceArpInit(
                         struct net_device *dev
                         ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  priv->arp_cache = kmalloc(sizeof *priv->arp_cache, GFP_KERNEL);
  if (!priv->arp_cache) {
    return -ENOMEM;
  }

  {
    int i;

    for (i = 0; i < 256; ++i) {
      INIT_LIST_HEAD(&priv->arp_cache->table[i]);
    }
  }

  return 0;
}

/* =============================================================== */
/*..tsIpoibDeviceArpFlush -- flush ARP cache                       */
void tsIpoibDeviceArpFlush(
                           struct net_device *dev
                           )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  struct list_head *cur, *tmp;
  unsigned long flags;
  int i;

  TS_TRACE(MOD_IB_NET, T_VERBOSE, TRACE_IB_NET_ARP,
           "%s: flushing shadow ARP cache", dev->name);

  /*
   * Instead of destroying the address vector, we destroy the entire entry,
   * but we create a new empty entry before that. This way we don't have any
   * races with freeing a used address vector.
   */
  spin_lock_irqsave(&priv->lock, flags);
  for (i = 0; i < 256; ++i) {
    list_for_each_safe(cur, tmp, &priv->arp_cache->table[i]) {
      tTS_IPOIB_ARP_ENTRY nentry, entry =
        list_entry(cur, tTS_IPOIB_ARP_ENTRY_STRUCT, cache_list);

      /*
       * Allocation failure isn't fatal, just drop the entry. If it's
       * important, a new one will be generated later automatically.
       */
      nentry = _tsIpoibArpAllocateEntry(entry->dev);
      if (nentry) {
        memcpy(nentry->hash, entry->hash, sizeof nentry->hash);
        memcpy(nentry->gid, entry->gid, sizeof(tTS_IB_GID));

        nentry->require_verify = entry->require_verify;
        nentry->qpn = entry->qpn;

        /* Add it before the current entry */
        list_add_tail(&nentry->cache_list, &entry->cache_list);

        tsIpoibArpEntryPut(nentry);
      }

      list_del_init(&entry->cache_list);

      tsIpoibArpEntryPut(entry);
    }
  }
  spin_unlock_irqrestore(&priv->lock, flags);
}

/* =============================================================== */
/*..tsIpoibDeviceArpDestroy -- destroy ARP cache                   */
static void tsIpoibDeviceArpDestroy(
                                    struct net_device *dev
                                    )
{
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  struct list_head *cur, *tmp;
  unsigned long flags;
  int i;

  spin_lock_irqsave(&priv->lock, flags);
  for (i = 0; i < 256; ++i) {
    list_for_each_safe(cur, tmp, &priv->arp_cache->table[i]) {
      tTS_IPOIB_ARP_ENTRY entry =
        list_entry(cur, tTS_IPOIB_ARP_ENTRY_STRUCT, cache_list);

      list_del_init(&entry->cache_list);

      tsIpoibArpEntryPut(entry);
    }
  }
  spin_unlock_irqrestore(&priv->lock, flags);
}

/* =============================================================== */
/*..tsIpoibDeviceArpCleanup -- clean up ARP cache                  */
void tsIpoibDeviceArpCleanup(
                             struct net_device *dev
                             ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;

  TS_REPORT_CLEANUP(MOD_IB_NET,
                    "%s: cleaning up ARP table",
                    dev->name);

  tsIpoibDeviceArpDestroy(dev);

  kfree(priv->arp_cache);
}

/* =============================================================== */
/*..tsIpoibDeviceArpGetGid -- find a hash in shadow ARP cache      */
int tsIpoibDeviceArpGetGid(
                           struct net_device *dev,
                           uint8_t           *hash,
                           tTS_IB_GID         gid
                           ) {
  tTS_IPOIB_ARP_ENTRY entry = _tsIpoibDeviceArpFind(dev, hash);

  if (!entry) {
    return -EINVAL;
  }

  memcpy(gid, entry->gid, sizeof(tTS_IB_GID));

  tsIpoibArpEntryPut(entry);

  return 0;
}

/* =============================================================== */
/*..tsIpoibDeviceArpDelete -- delete shadow ARP cache entry        */
int tsIpoibDeviceArpDelete(
                           struct net_device *dev,
                           const uint8_t     *hash
                           ) {
  struct tTS_IPOIB_DEVICE_PRIVATE_STRUCT *priv = dev->priv;
  tTS_IPOIB_ARP_ENTRY entry;
  unsigned long flags;

  spin_lock_irqsave(&priv->lock, flags);

  entry = __tsIpoibDeviceArpFind(dev, hash);
  if (!entry) {
    spin_unlock_irqrestore(&priv->lock, flags);

    return 0;
  }

  list_del_init(&entry->cache_list);

  tsIpoibArpEntryPut(entry);	/* Once for the find */
  tsIpoibArpEntryPut(entry);	/* Once for the original reference */

  spin_unlock_irqrestore(&priv->lock, flags);

  return 1;
}
