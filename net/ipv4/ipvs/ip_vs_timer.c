/*
 * IPVS         An implementation of the IP virtual server support for the
 *              LINUX operating system.  IPVS is now implemented as a module
 *              over the Netfilter framework. IPVS can be used to build a
 *              high-performance and highly available server based on a
 *              cluster of servers.
 *
 * Version:     $Id: ip_vs_timer.c,v 1.11 2003/06/08 09:31:19 wensong Exp $
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *              Julian Anastasov <ja@ssi.bg>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Changes:
 *
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/timer.h>

#include <net/ip_vs.h>

/*
 * The following block implements slow timers for IPVS, most code is stolen
 * from linux/kernel/timer.c.
 * Slow timer is used to avoid the overhead of cascading timers, when lots
 * of connection entries (>50,000) are cluttered in the system.
 */
#define SHIFT_BITS 6
#define TVN_BITS 8
#define TVR_BITS 10
#define TVN_SIZE (1 << TVN_BITS)
#define TVR_SIZE (1 << TVR_BITS)
#define TVN_MASK (TVN_SIZE - 1)
#define TVR_MASK (TVR_SIZE - 1)

struct sltimer_vec {
	int index;
	struct list_head vec[TVN_SIZE];
};

struct sltimer_vec_root {
	int index;
	struct list_head vec[TVR_SIZE];
};

static struct sltimer_vec sltv3 = { 0 };
static struct sltimer_vec sltv2 = { 0 };
static struct sltimer_vec_root sltv1 = { 0 };

static struct sltimer_vec * const sltvecs[] = {
	(struct sltimer_vec *)&sltv1, &sltv2, &sltv3
};

#define NOOF_SLTVECS (sizeof(sltvecs) / sizeof(sltvecs[0]))

static void init_sltimervecs(void)
{
	int i;

	for (i = 0; i < TVN_SIZE; i++) {
		INIT_LIST_HEAD(sltv3.vec + i);
		INIT_LIST_HEAD(sltv2.vec + i);
	}
	for (i = 0; i < TVR_SIZE; i++)
		INIT_LIST_HEAD(sltv1.vec + i);
}

static unsigned long sltimer_jiffies = 0;

static inline void internal_add_sltimer(struct sltimer_list *timer)
{
	/*
	 * must hold the sltimer lock when calling this
	 */
	unsigned long expires = timer->expires;
	unsigned long idx = expires - sltimer_jiffies;
	struct list_head * vec;

	if (idx < 1 << (SHIFT_BITS + TVR_BITS)) {
		int i = (expires >> SHIFT_BITS) & TVR_MASK;
		vec = sltv1.vec + i;
	} else if (idx < 1 << (SHIFT_BITS + TVR_BITS + TVN_BITS)) {
		int i = (expires >> (SHIFT_BITS+TVR_BITS)) & TVN_MASK;
		vec = sltv2.vec + i;
	} else if ((signed long) idx < 0) {
		/*
		 * can happen if you add a timer with expires == jiffies,
		 * or you set a timer to go off in the past
		 */
		vec = sltv1.vec + sltv1.index;
	} else if (idx <= 0xffffffffUL) {
		int i = (expires >> (SHIFT_BITS+TVR_BITS+TVN_BITS)) & TVN_MASK;
		vec = sltv3.vec + i;
	} else {
		/* Can only get here on architectures with 64-bit jiffies */
		INIT_LIST_HEAD(&timer->list);
	}
	/*
	 * Timers are FIFO!
	 */
	list_add(&timer->list, vec->prev);
}


static spinlock_t __ip_vs_sltimerlist_lock = SPIN_LOCK_UNLOCKED;

void add_sltimer(struct sltimer_list *timer)
{
	spin_lock(&__ip_vs_sltimerlist_lock);
	if (timer->list.next)
		goto bug;
	internal_add_sltimer(timer);
  out:
	spin_unlock(&__ip_vs_sltimerlist_lock);
	return;

  bug:
	printk("bug: kernel sltimer added twice at %p.\n",
	       __builtin_return_address(0));
	goto out;
}

static inline int detach_sltimer(struct sltimer_list *timer)
{
	if (!sltimer_pending(timer))
		return 0;
	list_del(&timer->list);
	return 1;
}

void mod_sltimer(struct sltimer_list *timer, unsigned long expires)
{
	int ret;

	spin_lock(&__ip_vs_sltimerlist_lock);
	timer->expires = expires;
	ret = detach_sltimer(timer);
	internal_add_sltimer(timer);
	spin_unlock(&__ip_vs_sltimerlist_lock);
}

int del_sltimer(struct sltimer_list * timer)
{
	int ret;

	spin_lock(&__ip_vs_sltimerlist_lock);
	ret = detach_sltimer(timer);
	timer->list.next = timer->list.prev = 0;
	spin_unlock(&__ip_vs_sltimerlist_lock);
	return ret;
}


static inline void cascade_sltimers(struct sltimer_vec *tv)
{
	/*
	 * cascade all the timers from tv up one level
	 */
	struct list_head *head, *curr, *next;

	head = tv->vec + tv->index;
	curr = head->next;

	/*
	 * We are removing _all_ timers from the list, so we don't  have to
	 * detach them individually, just clear the list afterwards.
	 */
	while (curr != head) {
		struct sltimer_list *tmp;

		tmp = list_entry(curr, struct sltimer_list, list);
		next = curr->next;
		list_del(curr); // not needed
		internal_add_sltimer(tmp);
		curr = next;
	}
	INIT_LIST_HEAD(head);
	tv->index = (tv->index + 1) & TVN_MASK;
}

static inline void run_sltimer_list(void)
{
	spin_lock(&__ip_vs_sltimerlist_lock);
	while ((long)(jiffies - sltimer_jiffies) >= 0) {
		struct list_head *head, *curr;
		if (!sltv1.index) {
			int n = 1;
			do {
				cascade_sltimers(sltvecs[n]);
			} while (sltvecs[n]->index == 1 && ++n < NOOF_SLTVECS);
		}
	  repeat:
		head = sltv1.vec + sltv1.index;
		curr = head->next;
		if (curr != head) {
			struct sltimer_list *timer;
			void (*fn)(unsigned long);
			unsigned long data;

			timer = list_entry(curr, struct sltimer_list, list);
			fn = timer->function;
			data= timer->data;

			detach_sltimer(timer);
			timer->list.next = timer->list.prev = NULL;
			spin_unlock(&__ip_vs_sltimerlist_lock);
			fn(data);
			spin_lock(&__ip_vs_sltimerlist_lock);
			goto repeat;
		}
		sltimer_jiffies += 1<<SHIFT_BITS;
		sltv1.index = (sltv1.index + 1) & TVR_MASK;
	}
	spin_unlock(&__ip_vs_sltimerlist_lock);
}

static struct timer_list slow_timer;

/*
 *  Slow timer handler is activated every second
 */
#define SLTIMER_PERIOD	     1*HZ

static void sltimer_handler(unsigned long data)
{
	run_sltimer_list();

	update_defense_level();
	if (atomic_read(&ip_vs_dropentry))
		ip_vs_random_dropentry();

	mod_timer(&slow_timer, (jiffies + SLTIMER_PERIOD));
}


void ip_vs_sltimer_init(void)
{
	/* initialize the slow timer vectors */
	init_sltimervecs();

	/* initialize the slow timer jiffies and the vector indexes */
	sltimer_jiffies = jiffies;
	sltv1.index = (sltimer_jiffies >> SHIFT_BITS) & TVR_MASK;
	sltv2.index = (sltimer_jiffies >> (SHIFT_BITS + TVR_BITS)) & TVN_MASK;
	sltv3.index = (sltimer_jiffies >> (SHIFT_BITS + TVR_BITS + TVN_BITS))
		& TVN_MASK;

	/* Hook the slow_timer handler in the system timer */
	init_timer(&slow_timer);
	slow_timer.function = sltimer_handler;
	slow_timer.expires = jiffies+SLTIMER_PERIOD;
	add_timer(&slow_timer);
}


void ip_vs_sltimer_cleanup(void)
{
	del_timer_sync(&slow_timer);
}
