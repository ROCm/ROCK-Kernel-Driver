/*
 * net/sched/sch_netem.c	Network emulator
 *
 * 		This program is free software; you can redistribute it and/or
 * 		modify it under the terms of the GNU General Public License
 * 		as published by the Free Software Foundation; either version
 * 		2 of the License, or (at your option) any later version.
 *
 * Authors:	Stephen Hemminger <shemminger@osdl.org>
 *		Catalin(ux aka Dino) BOIE <catab at umbrella dot ro>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>

#include <net/pkt_sched.h>

/*	Network emulator
 *
 *	This scheduler can alters spacing and order
 *	Similar to NISTnet and BSD Dummynet.
 */

struct netem_sched_data {
	struct sk_buff_head qnormal;
	struct sk_buff_head qdelay;
	struct timer_list timer;

	u32 latency;
	u32 loss;
	u32 counter;
	u32 gap;
};

/* Time stamp put into socket buffer control block */
struct netem_skb_cb {
	psched_time_t	time_to_send;
};

/* Enqueue packets with underlying discipline (fifo)
 * but mark them with current time first.
 */
static int netem_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct netem_sched_data *q = (struct netem_sched_data *)sch->data;
	struct netem_skb_cb *cb = (struct netem_skb_cb *)skb->cb;

	pr_debug("netem_enqueue skb=%p @%lu\n", skb, jiffies);

	/* Random packet drop 0 => none, ~0 => all */
	if (q->loss && q->loss >= net_random()) {
		sch->stats.drops++;
		return 0;	/* lie about loss so TCP doesn't know */
	}

	if (q->qnormal.qlen < sch->dev->tx_queue_len) {
		PSCHED_GET_TIME(cb->time_to_send);
		PSCHED_TADD(cb->time_to_send, q->latency);

		__skb_queue_tail(&q->qnormal, skb);
		sch->q.qlen++;
		sch->stats.bytes += skb->len;
		sch->stats.packets++;
		return 0;
	}

	sch->stats.drops++;
	kfree_skb(skb);
	return NET_XMIT_DROP;
}

/* Requeue packets but don't change time stamp */
static int netem_requeue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct netem_sched_data *q = (struct netem_sched_data *)sch->data;

	__skb_queue_head(&q->qnormal, skb);
	sch->q.qlen++;
	return 0;
}

/*
 * Check the look aside buffer list, and see if any freshly baked buffers.
 * If head of queue is not baked, set timer.
 */
static struct sk_buff *netem_get_delayed(struct netem_sched_data *q)
{
	struct sk_buff *skb;
	psched_time_t now;
	long delay;

	skb = skb_peek(&q->qdelay);
	if (skb) {
		const struct netem_skb_cb *cb
			= (const struct netem_skb_cb *)skb->cb;

		PSCHED_GET_TIME(now);
		delay = PSCHED_US2JIFFIE(PSCHED_TDIFF(cb->time_to_send, now));
		pr_debug("netem_dequeue: delay queue %p@%lu %ld\n",
			 skb, jiffies, delay);

		/* it's baked enough */
		if (delay <= 0) {
			__skb_unlink(skb, &q->qdelay);
			del_timer(&q->timer);
			return skb;
		}

		if (!timer_pending(&q->timer)) {
			q->timer.expires = jiffies + delay;
			add_timer(&q->timer);
		}
	}
	return NULL;
}

/* Dequeue packet.
 * If packet needs to be held up, then put in the delay
 * queue and set timer to wakeup later.
 */
static struct sk_buff *netem_dequeue(struct Qdisc *sch)
{
	struct netem_sched_data *q = (struct netem_sched_data *)sch->data;
	struct sk_buff *skb;

	skb = netem_get_delayed(q);
	if (!skb && (skb = __skb_dequeue(&q->qnormal))) {
		/* are we doing out of order packet skip? */
		if (q->counter < q->gap) {
			pr_debug("netem_dequeue: send %p normally\n", skb);
			q->counter++;
		} else {
			/* don't send now hold for later */
			pr_debug("netem_dequeue: hold [%p]@%lu\n", skb, jiffies);
			__skb_queue_tail(&q->qdelay, skb);
			q->counter = 0;
			skb = netem_get_delayed(q);
		}
	}

	if (skb)
		sch->q.qlen--;
	return skb;
}

static void netem_timer(unsigned long arg)
{
	struct Qdisc *sch = (struct Qdisc *)arg;

	pr_debug("netem_timer: fired @%lu\n", jiffies);
	netif_schedule(sch->dev);
}

static void netem_reset(struct Qdisc *sch)
{
	struct netem_sched_data *q = (struct netem_sched_data *)sch->data;

	skb_queue_purge(&q->qnormal);
	skb_queue_purge(&q->qdelay);

	sch->q.qlen = 0;
	del_timer_sync(&q->timer);
}

static int netem_change(struct Qdisc *sch, struct rtattr *opt)
{
	struct netem_sched_data *q = (struct netem_sched_data *)sch->data;
	struct tc_netem_qopt *qopt = RTA_DATA(opt);

	if (qopt->limit)
		sch->dev->tx_queue_len = qopt->limit;

	q->gap = qopt->gap;
	q->loss = qopt->loss;
	q->latency = qopt->latency;

	return 0;
}

static int netem_init(struct Qdisc *sch, struct rtattr *opt)
{
	struct netem_sched_data *q = (struct netem_sched_data *)sch->data;

	if (!opt)
		return -EINVAL;

	skb_queue_head_init(&q->qnormal);
	skb_queue_head_init(&q->qdelay);
	init_timer(&q->timer);
	q->timer.function = netem_timer;
	q->timer.data = (unsigned long) sch;
	q->counter = 0;

	return netem_change(sch, opt);
}

static void netem_destroy(struct Qdisc *sch)
{
	struct netem_sched_data *q = (struct netem_sched_data *)sch->data;

	del_timer_sync(&q->timer);
}

static int netem_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct netem_sched_data *q = (struct netem_sched_data *)sch->data;
	unsigned char	 *b = skb->tail;
	struct tc_netem_qopt qopt;

	qopt.latency = q->latency;
	qopt.limit = sch->dev->tx_queue_len;
	qopt.loss = q->loss;
	qopt.gap = q->gap;

	RTA_PUT(skb, TCA_OPTIONS, sizeof(qopt), &qopt);

	return skb->len;

rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static struct Qdisc_ops netem_qdisc_ops = {
	.id		=	"netem",
	.priv_size	=	sizeof(struct netem_sched_data),
	.enqueue	=	netem_enqueue,
	.dequeue	=	netem_dequeue,
	.requeue	=	netem_requeue,
	.init		=	netem_init,
	.reset		=	netem_reset,
	.destroy	=	netem_destroy,
	.change		=	netem_change,
	.dump		=	netem_dump,
	.owner		=	THIS_MODULE,
};


static int __init netem_module_init(void)
{
	return register_qdisc(&netem_qdisc_ops);
}
static void __exit netem_module_exit(void)
{
	unregister_qdisc(&netem_qdisc_ops);
}
module_init(netem_module_init)
module_exit(netem_module_exit)
MODULE_LICENSE("GPL");
