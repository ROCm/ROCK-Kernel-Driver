/*
 * Device Mapper Netlink Support (dm-netlink)
 *
 * Copyright (C) 2005 IBM Corporation
 * 	Author: Mike Anderson <andmike@us.ibm.com>
 * 	skb mempool derived from drivers/scsi/scsi_transport_iscsi.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */
#include <linux/module.h>
#include <linux/mempool.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/security.h>
#include <net/sock.h>
#include <net/netlink.h>
#include <linux/dm-netlink.h>
#include "dm-netlink.h"

#define MIN_EVT_SKBS	16
#define HIWAT_EVT_SKBS	32
#define EVT_SKB_SIZE	NLMSG_SPACE(128)

struct mp_zone {
	mempool_t *pool;
	kmem_cache_t *cache;
	int allocated;
	int size;
	int hiwat;
	struct list_head freequeue;
	spinlock_t freelock;
};

static struct mp_zone z_dm_evt;

static void* mp_zone_alloc_dm_evt(unsigned int gfp_mask,
				    void *pool_data)
{
	struct dm_evt *evt = NULL;
	struct mp_zone *zone = pool_data;

	evt = kmem_cache_alloc(zone->cache, gfp_mask);
	if (!evt)
		goto out;


	evt->skb = alloc_skb(zone->size, gfp_mask);
	if (!evt->skb)
		goto cache_out;

cache_out:
	kmem_cache_free(zone->cache, evt);
out:
	return evt;
}

static void mp_zone_free_dm_evt(void *element, void *pool_data)
{
	struct dm_evt *evt = element;
	struct mp_zone *zone = pool_data;

	kfree_skb(evt->skb);
	kmem_cache_free(zone->cache, evt);
}

static void
mp_zone_complete(struct mp_zone *zone, int release_all)
{
	unsigned long flags;
	struct dm_evt *evt, *n;

	spin_lock_irqsave(&zone->freelock, flags);
	if (zone->allocated) {
		list_for_each_entry_safe(evt, n, &zone->freequeue, zlist) {
			if (skb_shared(evt->skb)) {
				if (release_all)
					kfree_skb(evt->skb);
				else
					continue;
			}

			list_del(&evt->zlist);
			mempool_free(evt, zone->pool);
			--zone->allocated;
		}
	}
	spin_unlock_irqrestore(&zone->freelock, flags);
}

static int mp_zone_init(struct mp_zone *zp, unsigned size,
			     int min_nr, unsigned hiwat)
{
	zp->size = size;
	zp->hiwat = hiwat;
	zp->allocated = 0;
	INIT_LIST_HEAD(&zp->freequeue);
	spin_lock_init(&zp->freelock);

	zp->pool = mempool_create(min_nr, mp_zone_alloc_dm_evt,
				  mp_zone_free_dm_evt, zp);
	if (!zp->pool)
		return -ENOMEM;

	return 0;
}

static struct dm_evt* mp_zone_get_dm_evt(struct mp_zone *zone)
{
	struct dm_evt *evt;
	unsigned long flags;

	/* Check for ones we can complete before we alloc */
	mp_zone_complete(zone, 0);

	evt = mempool_alloc(zone->pool, GFP_ATOMIC);
	if (evt) {
		skb_get(evt->skb);
		INIT_LIST_HEAD(&evt->zlist);
		spin_lock_irqsave(&z_dm_evt.freelock, flags);
		list_add(&evt->zlist, &z_dm_evt.freequeue);
		++zone->allocated;
		spin_unlock_irqrestore(&z_dm_evt.freelock, flags);
	}
	return evt;
}

static struct sock *dm_nl_sock;
static int dm_nl_daemon_pid;

static u64 dm_evt_seqnum;
static DEFINE_SPINLOCK(sequence_lock);

static struct dm_evt *dm_nl_build_path_msg(char* dm_name, int type,
					     int blk_err)
{
	struct dm_evt *evt;
	struct nlmsghdr	*nlh;
	struct dm_nl_msghdr *dm_nlh;
	u64 seq;
	struct timeval tv;
	int err = -ENOBUFS;

	evt = mp_zone_get_dm_evt(&z_dm_evt);
	if (!evt) {
		printk(KERN_ERR "%s: mp_zone_get_dm_evt %d\n",
		       __FUNCTION__, err);
		err = -ENOMEM;
		goto out;
	}

	nlh = nlmsg_put(evt->skb, dm_nl_daemon_pid, 0, DM_EVT,
			NLMSG_ALIGN(sizeof(*dm_nlh)), 0);
	if (!nlh)
		goto nla_put_failure;

	dm_nlh = nlmsg_data(nlh);
	dm_nlh->type = type;
	dm_nlh->version = DM_E_ATTR_MAX;

	spin_lock(&sequence_lock);
	seq = ++dm_evt_seqnum;
	spin_unlock(&sequence_lock);
	do_gettimeofday(&tv);

	NLA_PUT_U64(evt->skb, DM_E_ATTR_SEQNUM, seq);
	NLA_PUT_U64(evt->skb, DM_E_ATTR_TSSEC, tv.tv_sec);
	NLA_PUT_U64(evt->skb, DM_E_ATTR_TSUSEC, tv.tv_usec);
	NLA_PUT_STRING(evt->skb, DM_E_ATTR_DMNAME, dm_name);

	if (blk_err)
		NLA_PUT_U32(evt->skb, DM_E_ATTR_BLKERR, blk_err);

	nlmsg_end(evt->skb, nlh);

	return evt;

nla_put_failure:
	printk(KERN_ERR "%s: nla_put_failure\n",
	       __FUNCTION__);
	/* reduce skb users so zone_complete can free */
	kfree_skb(evt->skb);
	mp_zone_complete(&z_dm_evt, 0);
out:
	return ERR_PTR(err);

}

void dm_send_evt(struct dm_evt *evt)
{
	int err;

	if (!dm_nl_sock || !dm_nl_daemon_pid)
		return;

	err = nlmsg_unicast(dm_nl_sock, evt->skb, dm_nl_daemon_pid);
	if (err < 0)
		printk(KERN_ERR "%s: nlmsg_unicast %d\n", __FUNCTION__,
		       err);
}
EXPORT_SYMBOL(dm_send_evt);

struct dm_evt *dm_path_fail_evt(char* dm_name, int blk_err)
{
	struct dm_evt *evt;
	evt = dm_nl_build_path_msg(dm_name, DM_EVT_FAIL_PATH, blk_err);

	return evt;
}

EXPORT_SYMBOL(dm_path_fail_evt);

struct dm_evt *dm_path_reinstate_evt(char* dm_name)
{
	struct dm_evt *evt;
	evt = dm_nl_build_path_msg(dm_name, DM_EVT_REINSTATE_PATH, 0);

	return evt;
}

EXPORT_SYMBOL(dm_path_reinstate_evt);

#define RCV_SKB_FAIL(err) do { netlink_ack(skb, nlh, (err)); return; } while (0)

static void dm_nl_rcv_msg(struct sk_buff *skb)
{
	int pid, flags;
	struct nlmsghdr *nlh = (struct nlmsghdr *) skb->data;

	if (skb->len >= NLMSG_SPACE(0)) {

		if (nlh->nlmsg_len < sizeof(*nlh) ||
			skb->len < nlh->nlmsg_len) {
			return;
		}
		pid = nlh->nlmsg_pid;
		flags = nlh->nlmsg_flags;

		if (security_netlink_recv(skb))
			RCV_SKB_FAIL(-EPERM);

		if (dm_nl_daemon_pid) {
			if (dm_nl_daemon_pid != pid) {
				RCV_SKB_FAIL(-EBUSY);
			}
		} else {
			dm_nl_daemon_pid = pid;
		}

		if (flags & NLM_F_ACK)
			netlink_ack(skb, nlh, 0);
	}
}

static void dm_nl_rcv(struct sock *sk, int len)
{
	struct sk_buff *skb;
	unsigned int qlen;

	for (qlen = skb_queue_len(&sk->sk_receive_queue); qlen; qlen--) {
		skb = skb_dequeue(&sk->sk_receive_queue);
		dm_nl_rcv_msg(skb);
		kfree_skb(skb);
	}
}

static int dm_nl_rcv_nl_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct netlink_notify *n = ptr;

	if (event == NETLINK_URELEASE &&
	    n->protocol == NETLINK_DM && n->pid) {
		if ( n->pid == dm_nl_daemon_pid  ) {
			dm_nl_daemon_pid = 0;
		}
		mp_zone_complete(&z_dm_evt, 1);
	}

	return NOTIFY_DONE;
}

static struct notifier_block dm_nl_nl_notifier = {
	.notifier_call  = dm_nl_rcv_nl_event,
};

static struct sock *dm_nl_sock;
static int dm_nl_daemon_pid;

int __init dm_nl_init(void)
{
	int err;

	err = netlink_register_notifier(&dm_nl_nl_notifier);
	if (err)
		return err;

	dm_nl_sock = netlink_kernel_create(NETLINK_DM, 0,
					    dm_nl_rcv, THIS_MODULE);
	if (!dm_nl_sock) {
		err = -ENOBUFS;
		goto notifier_out;
	}

	z_dm_evt.cache = kmem_cache_create("dm_events",
			sizeof(struct dm_evt), 0, 0, NULL, NULL);
	if (!z_dm_evt.cache)
		goto socket_out;

	err = mp_zone_init(&z_dm_evt, EVT_SKB_SIZE,
				MIN_EVT_SKBS, HIWAT_EVT_SKBS);
	if (err)
		goto cache_out;

	printk(KERN_DEBUG "dm-netlink version 0.0.2 loaded\n");

	return err;

cache_out:
	kmem_cache_destroy(z_dm_evt.cache);
socket_out:
	sock_release(dm_nl_sock->sk_socket);
notifier_out:
	netlink_unregister_notifier(&dm_nl_nl_notifier);
	printk(KERN_ERR "%s: failed %d\n", __FUNCTION__, err);
	return err;
}

void dm_nl_exit(void)
{
	flush_scheduled_work();
	mempool_destroy(z_dm_evt.pool);
	kmem_cache_destroy(z_dm_evt.cache);
	sock_release(dm_nl_sock->sk_socket);
	netlink_unregister_notifier(&dm_nl_nl_notifier);
}
