/*
 * Read-Copy Update mechanism for mutual exclusion 
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
 * Copyright (C) IBM Corporation, 2001
 *
 * Author: Dipankar Sarma <dipankar@in.ibm.com>
 * 
 * Based on the original work by Paul McKenney <paul.mckenney@us.ibm.com>
 * and inputs from Rusty Russell, Andrea Arcangeli and Andi Kleen.
 * Papers:
 * http://www.rdrop.com/users/paulmck/paper/rclockpdcsproof.pdf
 * http://lse.sourceforge.net/locking/rclock_OLS.2001.05.01c.sc.pdf (OLS2001)
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 * 		http://lse.sourceforge.net/locking/rcupdate.html
 *
 */

#ifndef __LINUX_RCUPDATE_H
#define __LINUX_RCUPDATE_H

#ifdef __KERNEL__

#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/seqlock.h>

/**
 * struct rcu_head - callback structure for use with RCU
 * @next: next update requests in a list
 * @func: actual update function to call after the grace period.
 */
struct rcu_head {
	struct rcu_head *next;
	void (*func)(struct rcu_head *head);
};

#define RCU_HEAD_INIT(head) { .next = NULL, .func = NULL }
#define RCU_HEAD(head) struct rcu_head head = RCU_HEAD_INIT(head)
#define INIT_RCU_HEAD(ptr) do { \
       (ptr)->next = NULL; (ptr)->func = NULL; \
} while (0)



/* Global control variables for rcupdate callback mechanism. */
struct rcu_ctrlblk {
	long	cur;		/* Current batch number.                      */
	long	completed;	/* Number of the last completed batch         */
	int	next_pending;	/* Is the next batch already waiting?         */
	seqcount_t lock;	/* For atomic reads of cur and next_pending.  */
} ____cacheline_maxaligned_in_smp;

/* Is batch a before batch b ? */
static inline int rcu_batch_before(long a, long b)
{
        return (a - b) < 0;
}

/* Is batch a after batch b ? */
static inline int rcu_batch_after(long a, long b)
{
        return (a - b) > 0;
}

/*
 * Per-CPU data for Read-Copy UPdate.
 * nxtlist - new callbacks are added here
 * curlist - current batch for which quiescent cycle started if any
 */
struct rcu_data {
	/* 1) quiescent state handling : */
	long		quiescbatch;     /* Batch # for grace period */
	long		qsctr;		 /* User-mode/idle loop etc. */
	long            last_qsctr;	 /* value of qsctr at beginning */
					 /* of rcu grace period */
	int		qs_pending;	 /* core waits for quiesc state */

	/* 2) batch handling */
	long  	       	batch;           /* Batch # for current RCU batch */
	struct rcu_head *nxtlist;
	struct rcu_head **nxttail;
	struct rcu_head *curlist;
	struct rcu_head **curtail;
	struct rcu_head *donelist;
	struct rcu_head **donetail;
	int cpu;
};

DECLARE_PER_CPU(struct rcu_data, rcu_data);
DECLARE_PER_CPU(struct rcu_data, rcu_bh_data);
extern struct rcu_ctrlblk rcu_ctrlblk;
extern struct rcu_ctrlblk rcu_bh_ctrlblk;

/*
 * Increment the quiscent state counter.
 */
static inline void rcu_qsctr_inc(int cpu)
{
	struct rcu_data *rdp = &per_cpu(rcu_data, cpu);
	rdp->qsctr++;
}
static inline void rcu_bh_qsctr_inc(int cpu)
{
	struct rcu_data *rdp = &per_cpu(rcu_bh_data, cpu);
	rdp->qsctr++;
}

static inline int __rcu_pending(struct rcu_ctrlblk *rcp,
						struct rcu_data *rdp)
{
	/* This cpu has pending rcu entries and the grace period
	 * for them has completed.
	 */
	if (rdp->curlist && !rcu_batch_before(rcp->completed, rdp->batch))
		return 1;

	/* This cpu has no pending entries, but there are new entries */
	if (!rdp->curlist && rdp->nxtlist)
		return 1;

	/* This cpu has finished callbacks to invoke */
	if (rdp->donelist)
		return 1;

	/* The rcu core waits for a quiescent state from the cpu */
	if (rdp->quiescbatch != rcp->cur || rdp->qs_pending)
		return 1;

	/* nothing to do */
	return 0;
}

static inline int rcu_pending(int cpu)
{
	return __rcu_pending(&rcu_ctrlblk, &per_cpu(rcu_data, cpu)) ||
		__rcu_pending(&rcu_bh_ctrlblk, &per_cpu(rcu_bh_data, cpu));
}

#define rcu_read_lock()		preempt_disable()
#define rcu_read_unlock()	preempt_enable()
#define rcu_read_lock_bh()	local_bh_disable()
#define rcu_read_unlock_bh()	local_bh_enable()

extern void rcu_init(void);
extern void rcu_check_callbacks(int cpu, int user);
extern void rcu_restart_cpu(int cpu);

/* Exported interfaces */
extern void FASTCALL(call_rcu(struct rcu_head *head, 
				void (*func)(struct rcu_head *head)));
extern void FASTCALL(call_rcu_bh(struct rcu_head *head,
				void (*func)(struct rcu_head *head)));
extern void synchronize_kernel(void);

#endif /* __KERNEL__ */
#endif /* __LINUX_RCUPDATE_H */
