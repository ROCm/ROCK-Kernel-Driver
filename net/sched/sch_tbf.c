/*
 * net/sched/sch_tbf.c	Token Bucket Filter queue.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/notifier.h>
#include <net/ip.h>
#include <net/route.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/pkt_sched.h>


/*	Simple Token Bucket Filter.
	=======================================

	SOURCE.
	-------

	None.

	Description.
	------------

	A data flow obeys TBF with rate R and depth B, if for any
	time interval t_i...t_f the number of transmitted bits
	does not exceed B + R*(t_f-t_i).

	Packetized version of this definition:
	The sequence of packets of sizes s_i served at moments t_i
	obeys TBF, if for any i<=k:

	s_i+....+s_k <= B + R*(t_k - t_i)

	Algorithm.
	----------
	
	Let N(t_i) be B/R initially and N(t) grow continuously with time as:

	N(t+delta) = min{B/R, N(t) + delta}

	If the first packet in queue has length S, it may be
	transmited only at the time t_* when S/R <= N(t_*),
	and in this case N(t) jumps:

	N(t_* + 0) = N(t_* - 0) - S/R.



	Actually, QoS requires two TBF to be applied to a data stream.
	One of them controls steady state burst size, another
	one with rate P (peak rate) and depth M (equal to link MTU)
	limits bursts at a smaller time scale.

	It is easy to see that P>R, and B>M. If P is infinity, this double
	TBF is equivalent to a single one.

	When TBF works in reshaping mode, latency is estimated as:

	lat = max ((L-B)/R, (L-M)/P)


	NOTES.
	------

	If TBF throttles, it starts a watchdog timer, which will wake it up
	when it is ready to transmit.
	Note that the minimal timer resolution is 1/HZ.
	If no new packets arrive during this period,
	or if the device is not awaken by EOI for some previous packet,
	TBF can stop its activity for 1/HZ.


	This means, that with depth B, the maximal rate is

	R_crit = B*HZ

	F.e. for 10Mbit ethernet and HZ=100 the minimal allowed B is ~10Kbytes.

	Note that the peak rate TBF is much more tough: with MTU 1500
	P_crit = 150Kbytes/sec. So, if you need greater peak
	rates, use alpha with HZ=1000 :-)
*/

struct tbf_sched_data
{
/* Parameters */
	u32		limit;		/* Maximal length of backlog: bytes */
	u32		buffer;		/* Token bucket depth/rate: MUST BE >= MTU/B */
	u32		mtu;
	u32		max_size;
	struct qdisc_rate_table	*R_tab;
	struct qdisc_rate_table	*P_tab;

/* Variables */
	long	tokens;			/* Current number of B tokens */
	long	ptokens;		/* Current number of P tokens */
	psched_time_t	t_c;		/* Time check-point */
	struct timer_list wd_timer;	/* Watchdog timer */
};

#define L2T(q,L)   ((q)->R_tab->data[(L)>>(q)->R_tab->rate.cell_log])
#define L2T_P(q,L) ((q)->P_tab->data[(L)>>(q)->P_tab->rate.cell_log])

static int
tbf_enqueue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;

	if (skb->len > q->max_size)
		goto drop;
	__skb_queue_tail(&sch->q, skb);
	if ((sch->stats.backlog += skb->len) <= q->limit) {
		sch->stats.bytes += skb->len;
		sch->stats.packets++;
		return 0;
	}

	/* Drop action: undo the things that we just did,
	 * i.e. make tail drop
	 */

	__skb_unlink(skb, &sch->q);
	sch->stats.backlog -= skb->len;

drop:
	sch->stats.drops++;
#ifdef CONFIG_NET_CLS_POLICE
	if (sch->reshape_fail==NULL || sch->reshape_fail(skb, sch))
#endif
		kfree_skb(skb);
	return NET_XMIT_DROP;
}

static int
tbf_requeue(struct sk_buff *skb, struct Qdisc* sch)
{
	__skb_queue_head(&sch->q, skb);
	sch->stats.backlog += skb->len;
	return 0;
}

static int
tbf_drop(struct Qdisc* sch)
{
	struct sk_buff *skb;

	skb = __skb_dequeue_tail(&sch->q);
	if (skb) {
		sch->stats.backlog -= skb->len;
		sch->stats.drops++;
		kfree_skb(skb);
		return 1;
	}
	return 0;
}

static void tbf_watchdog(unsigned long arg)
{
	struct Qdisc *sch = (struct Qdisc*)arg;

	sch->flags &= ~TCQ_F_THROTTLED;
	netif_schedule(sch->dev);
}

static struct sk_buff *
tbf_dequeue(struct Qdisc* sch)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;
	struct sk_buff *skb;
	
	skb = __skb_dequeue(&sch->q);

	if (skb) {
		psched_time_t now;
		long toks;
		long ptoks = 0;

		PSCHED_GET_TIME(now);

		toks = PSCHED_TDIFF_SAFE(now, q->t_c, q->buffer, 0);

		if (q->P_tab) {
			ptoks = toks + q->ptokens;
			if (ptoks > (long)q->mtu)
				ptoks = q->mtu;
			ptoks -= L2T_P(q, skb->len);
		}
		toks += q->tokens;
		if (toks > (long)q->buffer)
			toks = q->buffer;
		toks -= L2T(q, skb->len);

		if ((toks|ptoks) >= 0) {
			q->t_c = now;
			q->tokens = toks;
			q->ptokens = ptoks;
			sch->stats.backlog -= skb->len;
			sch->flags &= ~TCQ_F_THROTTLED;
			return skb;
		}

		if (!netif_queue_stopped(sch->dev)) {
			long delay = PSCHED_US2JIFFIE(max(-toks, -ptoks));

			if (delay == 0)
				delay = 1;

			mod_timer(&q->wd_timer, jiffies+delay);
		}

		/* Maybe we have a shorter packet in the queue,
		   which can be sent now. It sounds cool,
		   but, however, this is wrong in principle.
		   We MUST NOT reorder packets under these circumstances.

		   Really, if we split the flow into independent
		   subflows, it would be a very good solution.
		   This is the main idea of all FQ algorithms
		   (cf. CSZ, HPFQ, HFSC)
		 */
		__skb_queue_head(&sch->q, skb);

		sch->flags |= TCQ_F_THROTTLED;
		sch->stats.overlimits++;
	}
	return NULL;
}


static void
tbf_reset(struct Qdisc* sch)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;

	skb_queue_purge(&sch->q);
	sch->stats.backlog = 0;
	PSCHED_GET_TIME(q->t_c);
	q->tokens = q->buffer;
	q->ptokens = q->mtu;
	sch->flags &= ~TCQ_F_THROTTLED;
	del_timer(&q->wd_timer);
}

static int tbf_change(struct Qdisc* sch, struct rtattr *opt)
{
	int err = -EINVAL;
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;
	struct rtattr *tb[TCA_TBF_PTAB];
	struct tc_tbf_qopt *qopt;
	struct qdisc_rate_table *rtab = NULL;
	struct qdisc_rate_table *ptab = NULL;
	int max_size;

	if (rtattr_parse(tb, TCA_TBF_PTAB, RTA_DATA(opt), RTA_PAYLOAD(opt)) ||
	    tb[TCA_TBF_PARMS-1] == NULL ||
	    RTA_PAYLOAD(tb[TCA_TBF_PARMS-1]) < sizeof(*qopt))
		goto done;

	qopt = RTA_DATA(tb[TCA_TBF_PARMS-1]);
	rtab = qdisc_get_rtab(&qopt->rate, tb[TCA_TBF_RTAB-1]);
	if (rtab == NULL)
		goto done;

	if (qopt->peakrate.rate) {
		if (qopt->peakrate.rate > qopt->rate.rate)
			ptab = qdisc_get_rtab(&qopt->peakrate, tb[TCA_TBF_PTAB-1]);
		if (ptab == NULL)
			goto done;
	}

	max_size = psched_mtu(sch->dev);
	if (ptab) {
		int n = max_size>>qopt->peakrate.cell_log;
		while (n>0 && ptab->data[n-1] > qopt->mtu) {
			max_size -= (1<<qopt->peakrate.cell_log);
			n--;
		}
	}
	if (rtab->data[max_size>>qopt->rate.cell_log] > qopt->buffer)
		goto done;

	sch_tree_lock(sch);
	q->limit = qopt->limit;
	q->mtu = qopt->mtu;
	q->max_size = max_size;
	q->buffer = qopt->buffer;
	q->tokens = q->buffer;
	q->ptokens = q->mtu;
	rtab = xchg(&q->R_tab, rtab);
	ptab = xchg(&q->P_tab, ptab);
	sch_tree_unlock(sch);
	err = 0;
done:
	if (rtab)
		qdisc_put_rtab(rtab);
	if (ptab)
		qdisc_put_rtab(ptab);
	return err;
}

static int tbf_init(struct Qdisc* sch, struct rtattr *opt)
{
	int err;
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;
	
	if (opt == NULL)
		return -EINVAL;
	
	MOD_INC_USE_COUNT;
	
	PSCHED_GET_TIME(q->t_c);
	init_timer(&q->wd_timer);
	q->wd_timer.function = tbf_watchdog;
	q->wd_timer.data = (unsigned long)sch;
	
	if ((err = tbf_change(sch, opt)) != 0) {
		MOD_DEC_USE_COUNT;
	}
	return err;
}

static void tbf_destroy(struct Qdisc *sch)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;

	del_timer(&q->wd_timer);

	if (q->P_tab)
		qdisc_put_rtab(q->P_tab);
	if (q->R_tab)
		qdisc_put_rtab(q->R_tab);

	MOD_DEC_USE_COUNT;
}

#ifdef CONFIG_RTNETLINK
static int tbf_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct tbf_sched_data *q = (struct tbf_sched_data *)sch->data;
	unsigned char	 *b = skb->tail;
	struct rtattr *rta;
	struct tc_tbf_qopt opt;
	
	rta = (struct rtattr*)b;
	RTA_PUT(skb, TCA_OPTIONS, 0, NULL);
	
	opt.limit = q->limit;
	opt.rate = q->R_tab->rate;
	if (q->P_tab)
		opt.peakrate = q->P_tab->rate;
	else
		memset(&opt.peakrate, 0, sizeof(opt.peakrate));
	opt.mtu = q->mtu;
	opt.buffer = q->buffer;
	RTA_PUT(skb, TCA_TBF_PARMS, sizeof(opt), &opt);
	rta->rta_len = skb->tail - b;

	return skb->len;

rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}
#endif

struct Qdisc_ops tbf_qdisc_ops =
{
	NULL,
	NULL,
	"tbf",
	sizeof(struct tbf_sched_data),

	tbf_enqueue,
	tbf_dequeue,
	tbf_requeue,
	tbf_drop,

	tbf_init,
	tbf_reset,
	tbf_destroy,
	tbf_change,

#ifdef CONFIG_RTNETLINK
	tbf_dump,
#endif
};


#ifdef MODULE
int init_module(void)
{
	return register_qdisc(&tbf_qdisc_ops);
}

void cleanup_module(void) 
{
	unregister_qdisc(&tbf_qdisc_ops);
}
#endif
