/*
 * net/core/gen_stats.c
 *
 *             This program is free software; you can redistribute it and/or
 *             modify it under the terms of the GNU General Public License
 *             as published by the Free Software Foundation; either version
 *             2 of the License, or (at your option) any later version.
 *
 * Authors:  Thomas Graf <tgraf@suug.ch>
 *           Jamal Hadi Salim
 *           Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * See Documentation/networking/gen_stats.txt
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/socket.h>
#include <linux/rtnetlink.h>
#include <linux/gen_stats.h>
#include <net/gen_stats.h>


static inline int
gnet_stats_copy(struct gnet_dump *d, int type, void *buf, int size)
{
	RTA_PUT(d->skb, type, size, buf);
	return 0;

rtattr_failure:
	spin_unlock_bh(d->lock);
	return -1;
}

int
gnet_stats_start_copy_compat(struct sk_buff *skb, int type, int tc_stats_type,
	int xstats_type, spinlock_t *lock, struct gnet_dump *d)
{
	spin_lock_bh(lock);
	d->lock = lock;
	d->tail = (struct rtattr *) skb->tail;
	d->skb = skb;
	d->compat_tc_stats = tc_stats_type;
	d->compat_xstats = xstats_type;
	d->xstats = NULL;

	if (d->compat_tc_stats)
		memset(&d->tc_stats, 0, sizeof(d->tc_stats));

	return gnet_stats_copy(d, type, NULL, 0);
}

int
gnet_stats_start_copy(struct sk_buff *skb, int type, spinlock_t *lock,
	struct gnet_dump *d)
{
	return gnet_stats_start_copy_compat(skb, type, 0, 0, lock, d);
}


int
gnet_stats_copy_basic(struct gnet_dump *d, struct gnet_stats_basic *b)
{
	if (d->compat_tc_stats) {
		d->tc_stats.bytes = b->bytes;
		d->tc_stats.packets = b->packets;
	}
	
	return gnet_stats_copy(d, TCA_STATS_BASIC, b, sizeof(*b));
}

int
gnet_stats_copy_rate_est(struct gnet_dump *d, struct gnet_stats_rate_est *r)
{
	if (d->compat_tc_stats) {
		d->tc_stats.bps = r->bps;
		d->tc_stats.pps = r->pps;
	}

	return gnet_stats_copy(d, TCA_STATS_RATE_EST, r, sizeof(*r));
}

int
gnet_stats_copy_queue(struct gnet_dump *d, struct gnet_stats_queue *q)
{
	if (d->compat_tc_stats) {
		d->tc_stats.drops = q->drops;
		d->tc_stats.qlen = q->qlen;
		d->tc_stats.backlog = q->backlog;
		d->tc_stats.overlimits = q->overlimits;
	}
		
	return gnet_stats_copy(d, TCA_STATS_QUEUE, q, sizeof(*q));
}

int
gnet_stats_copy_app(struct gnet_dump *d, void *st, int len)
{
	if (d->compat_xstats)
		d->xstats = (struct rtattr *) d->skb->tail;
	return gnet_stats_copy(d, TCA_STATS_APP, st, len);
}

int
gnet_stats_finish_copy(struct gnet_dump *d)
{
	d->tail->rta_len = d->skb->tail - (u8 *) d->tail;

	if (d->compat_tc_stats)
		if (gnet_stats_copy(d, d->compat_tc_stats, &d->tc_stats,
			sizeof(d->tc_stats)) < 0)
			return -1;

	if (d->compat_xstats && d->xstats) {
		if (gnet_stats_copy(d, d->compat_xstats, RTA_DATA(d->xstats),
			RTA_PAYLOAD(d->xstats)) < 0)
			return -1;
	}

	spin_unlock_bh(d->lock);
	return 0;
}


EXPORT_SYMBOL(gnet_stats_start_copy);
EXPORT_SYMBOL(gnet_stats_copy_basic);
EXPORT_SYMBOL(gnet_stats_copy_rate_est);
EXPORT_SYMBOL(gnet_stats_copy_queue);
EXPORT_SYMBOL(gnet_stats_copy_app);
EXPORT_SYMBOL(gnet_stats_finish_copy);
