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
};

DECLARE_PER_CPU(struct rcu_data, rcu_data);
extern struct rcu_ctrlblk rcu_ctrlblk;

#define RCU_quiescbatch(cpu)	(per_cpu(rcu_data, (cpu)).quiescbatch)
#define RCU_qsctr(cpu) 		(per_cpu(rcu_data, (cpu)).qsctr)
#define RCU_last_qsctr(cpu) 	(per_cpu(rcu_data, (cpu)).last_qsctr)
#define RCU_qs_pending(cpu)	(per_cpu(rcu_data, (cpu)).qs_pending)
#define RCU_batch(cpu) 		(per_cpu(rcu_data, (cpu)).batch)
#define RCU_nxtlist(cpu) 	(per_cpu(rcu_data, (cpu)).nxtlist)
#define RCU_curlist(cpu) 	(per_cpu(rcu_data, (cpu)).curlist)
#define RCU_nxttail(cpu) 	(per_cpu(rcu_data, (cpu)).nxttail)

static inline int rcu_pending(int cpu) 
{
	/* This cpu has pending rcu entries and the grace period
	 * for them has completed.
	 */
	if (RCU_curlist(cpu) &&
		  !rcu_batch_before(rcu_ctrlblk.completed,RCU_batch(cpu)))
		return 1;

	/* This cpu has no pending entries, but there are new entries */
	if (!RCU_curlist(cpu) && RCU_nxtlist(cpu))
		return 1;

	/* The rcu core waits for a quiescent state from the cpu */
	if (RCU_quiescbatch(cpu) != rcu_ctrlblk.cur || RCU_qs_pending(cpu))
		return 1;

	/* nothing to do */
	return 0;
}

#define rcu_read_lock()		preempt_disable()
#define rcu_read_unlock()	preempt_enable()

extern void rcu_init(void);
extern void rcu_check_callbacks(int cpu, int user);
extern void rcu_restart_cpu(int cpu);

/* Exported interfaces */
extern void FASTCALL(call_rcu(struct rcu_head *head, 
				void (*func)(struct rcu_head *head)));
extern void synchronize_kernel(void);

#endif /* __KERNEL__ */
#endif /* __LINUX_RCUPDATE_H */
