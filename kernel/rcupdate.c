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
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <asm/atomic.h>
#include <asm/bitops.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/rcupdate.h>
#include <linux/cpu.h>

/* Definition for rcupdate control block. */
struct rcu_ctrlblk rcu_ctrlblk = 
	{ .cur = -300, .completed = -300 , .lock = SEQCNT_ZERO };

/* Bookkeeping of the progress of the grace period */
struct {
	spinlock_t	mutex; /* Guard this struct and writes to rcu_ctrlblk */
	cpumask_t	rcu_cpu_mask; /* CPUs that need to switch in order    */
	                              /* for current batch to proceed.        */
} rcu_state ____cacheline_maxaligned_in_smp =
	  {.mutex = SPIN_LOCK_UNLOCKED, .rcu_cpu_mask = CPU_MASK_NONE };


DEFINE_PER_CPU(struct rcu_data, rcu_data) = { 0L };

/* Fake initialization required by compiler */
static DEFINE_PER_CPU(struct tasklet_struct, rcu_tasklet) = {NULL};
#define RCU_tasklet(cpu) (per_cpu(rcu_tasklet, cpu))

/**
 * call_rcu - Queue an RCU update request.
 * @head: structure to be used for queueing the RCU updates.
 * @func: actual update function to be invoked after the grace period
 *
 * The update function will be invoked as soon as all CPUs have performed 
 * a context switch or been seen in the idle loop or in a user process. 
 * The read-side of critical section that use call_rcu() for updation must 
 * be protected by rcu_read_lock()/rcu_read_unlock().
 */
void fastcall call_rcu(struct rcu_head *head,
				void (*func)(struct rcu_head *rcu))
{
	int cpu;
	unsigned long flags;

	head->func = func;
	head->next = NULL;
	local_irq_save(flags);
	cpu = smp_processor_id();
	*RCU_nxttail(cpu) = head;
	RCU_nxttail(cpu) = &head->next;
	local_irq_restore(flags);
}

/*
 * Invoke the completed RCU callbacks. They are expected to be in
 * a per-cpu list.
 */
static void rcu_do_batch(struct rcu_head *list)
{
	struct rcu_head *next;

	while (list) {
		next = list->next;
		list->func(list);
		list = next;
	}
}

/*
 * Grace period handling:
 * The grace period handling consists out of two steps:
 * - A new grace period is started.
 *   This is done by rcu_start_batch. The start is not broadcasted to
 *   all cpus, they must pick this up by comparing rcu_ctrlblk.cur with
 *   RCU_quiescbatch(cpu). All cpus are recorded  in the
 *   rcu_state.rcu_cpu_mask bitmap.
 * - All cpus must go through a quiescent state.
 *   Since the start of the grace period is not broadcasted, at least two
 *   calls to rcu_check_quiescent_state are required:
 *   The first call just notices that a new grace period is running. The
 *   following calls check if there was a quiescent state since the beginning
 *   of the grace period. If so, it updates rcu_state.rcu_cpu_mask. If
 *   the bitmap is empty, then the grace period is completed.
 *   rcu_check_quiescent_state calls rcu_start_batch(0) to start the next grace
 *   period (if necessary).
 */
/*
 * Register a new batch of callbacks, and start it up if there is currently no
 * active batch and the batch to be registered has not already occurred.
 * Caller must hold rcu_state.mutex.
 */
static void rcu_start_batch(int next_pending)
{
	if (next_pending)
		rcu_ctrlblk.next_pending = 1;

	if (rcu_ctrlblk.next_pending &&
			rcu_ctrlblk.completed == rcu_ctrlblk.cur) {
		/* Can't change, since spin lock held. */
		cpus_andnot(rcu_state.rcu_cpu_mask, cpu_online_map,
							nohz_cpu_mask);
		write_seqcount_begin(&rcu_ctrlblk.lock);
		rcu_ctrlblk.next_pending = 0;
		rcu_ctrlblk.cur++;
		write_seqcount_end(&rcu_ctrlblk.lock);
	}
}

/*
 * cpu went through a quiescent state since the beginning of the grace period.
 * Clear it from the cpu mask and complete the grace period if it was the last
 * cpu. Start another grace period if someone has further entries pending
 */
static void cpu_quiet(int cpu)
{
	cpu_clear(cpu, rcu_state.rcu_cpu_mask);
	if (cpus_empty(rcu_state.rcu_cpu_mask)) {
		/* batch completed ! */
		rcu_ctrlblk.completed = rcu_ctrlblk.cur;
		rcu_start_batch(0);
	}
}

/*
 * Check if the cpu has gone through a quiescent state (say context
 * switch). If so and if it already hasn't done so in this RCU
 * quiescent cycle, then indicate that it has done so.
 */
static void rcu_check_quiescent_state(void)
{
	int cpu = smp_processor_id();

	if (RCU_quiescbatch(cpu) != rcu_ctrlblk.cur) {
		/* new grace period: record qsctr value. */
		RCU_qs_pending(cpu) = 1;
		RCU_last_qsctr(cpu) = RCU_qsctr(cpu);
		RCU_quiescbatch(cpu) = rcu_ctrlblk.cur;
		return;
	}

	/* Grace period already completed for this cpu?
	 * qs_pending is checked instead of the actual bitmap to avoid
	 * cacheline trashing.
	 */
	if (!RCU_qs_pending(cpu))
		return;

	/* 
	 * Races with local timer interrupt - in the worst case
	 * we may miss one quiescent state of that CPU. That is
	 * tolerable. So no need to disable interrupts.
	 */
	if (RCU_qsctr(cpu) == RCU_last_qsctr(cpu))
		return;
	RCU_qs_pending(cpu) = 0;

	spin_lock(&rcu_state.mutex);
	/*
	 * RCU_quiescbatch/batch.cur and the cpu bitmap can come out of sync
	 * during cpu startup. Ignore the quiescent state.
	 */
	if (likely(RCU_quiescbatch(cpu) == rcu_ctrlblk.cur))
		cpu_quiet(cpu);

	spin_unlock(&rcu_state.mutex);
}


#ifdef CONFIG_HOTPLUG_CPU

/* warning! helper for rcu_offline_cpu. do not use elsewhere without reviewing
 * locking requirements, the list it's pulling from has to belong to a cpu
 * which is dead and hence not processing interrupts.
 */
static void rcu_move_batch(struct rcu_head *list)
{
	int cpu;

	local_irq_disable();

	cpu = smp_processor_id();

	while (list != NULL) {
		*RCU_nxttail(cpu) = list;
		RCU_nxttail(cpu) = &list->next;
		list = list->next;
	}
	local_irq_enable();
}

static void rcu_offline_cpu(int cpu)
{
	/* if the cpu going offline owns the grace period
	 * we can block indefinitely waiting for it, so flush
	 * it here
	 */
	spin_lock_bh(&rcu_state.mutex);
	if (rcu_ctrlblk.cur != rcu_ctrlblk.completed)
		cpu_quiet(cpu);
	spin_unlock_bh(&rcu_state.mutex);

	rcu_move_batch(RCU_curlist(cpu));
	rcu_move_batch(RCU_nxtlist(cpu));

	tasklet_kill_immediate(&RCU_tasklet(cpu), cpu);
}

#endif

void rcu_restart_cpu(int cpu)
{
	spin_lock_bh(&rcu_state.mutex);
	RCU_quiescbatch(cpu) = rcu_ctrlblk.completed;
	RCU_qs_pending(cpu) = 0;
	spin_unlock_bh(&rcu_state.mutex);
}

/*
 * This does the RCU processing work from tasklet context. 
 */
static void rcu_process_callbacks(unsigned long unused)
{
	int cpu = smp_processor_id();
	struct rcu_head *rcu_list = NULL;

	if (RCU_curlist(cpu) &&
	    !rcu_batch_before(rcu_ctrlblk.completed, RCU_batch(cpu))) {
		rcu_list = RCU_curlist(cpu);
		RCU_curlist(cpu) = NULL;
	}

	local_irq_disable();
	if (RCU_nxtlist(cpu) && !RCU_curlist(cpu)) {
		int next_pending, seq;

		RCU_curlist(cpu) = RCU_nxtlist(cpu);
		RCU_nxtlist(cpu) = NULL;
		RCU_nxttail(cpu) = &RCU_nxtlist(cpu);
		local_irq_enable();

		/*
		 * start the next batch of callbacks
		 */
		do {
			seq = read_seqcount_begin(&rcu_ctrlblk.lock);
			/* determine batch number */
			RCU_batch(cpu) = rcu_ctrlblk.cur + 1;
			next_pending = rcu_ctrlblk.next_pending;
		} while (read_seqcount_retry(&rcu_ctrlblk.lock, seq));

		if (!next_pending) {
			/* and start it/schedule start if it's a new batch */
			spin_lock(&rcu_state.mutex);
			rcu_start_batch(1);
			spin_unlock(&rcu_state.mutex);
		}
	} else {
		local_irq_enable();
	}
	rcu_check_quiescent_state();
	if (rcu_list)
		rcu_do_batch(rcu_list);
}

void rcu_check_callbacks(int cpu, int user)
{
	if (user || 
	    (idle_cpu(cpu) && !in_softirq() && 
				hardirq_count() <= (1 << HARDIRQ_SHIFT)))
		RCU_qsctr(cpu)++;
	tasklet_schedule(&RCU_tasklet(cpu));
}

static void __devinit rcu_online_cpu(int cpu)
{
	memset(&per_cpu(rcu_data, cpu), 0, sizeof(struct rcu_data));
	tasklet_init(&RCU_tasklet(cpu), rcu_process_callbacks, 0UL);
	RCU_nxttail(cpu) = &RCU_nxtlist(cpu);
	RCU_quiescbatch(cpu) = rcu_ctrlblk.completed;
	RCU_qs_pending(cpu) = 0;
}

static int __devinit rcu_cpu_notify(struct notifier_block *self, 
				unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;
	switch (action) {
	case CPU_UP_PREPARE:
		rcu_online_cpu(cpu);
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_DEAD:
		rcu_offline_cpu(cpu);
		break;
#endif
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __devinitdata rcu_nb = {
	.notifier_call	= rcu_cpu_notify,
};

/*
 * Initializes rcu mechanism.  Assumed to be called early.
 * That is before local timer(SMP) or jiffie timer (uniproc) is setup.
 * Note that rcu_qsctr and friends are implicitly
 * initialized due to the choice of ``0'' for RCU_CTR_INVALID.
 */
void __init rcu_init(void)
{
	rcu_cpu_notify(&rcu_nb, CPU_UP_PREPARE,
			(void *)(long)smp_processor_id());
	/* Register notifier for non-boot CPUs */
	register_cpu_notifier(&rcu_nb);
}

struct rcu_synchronize {
	struct rcu_head head;
	struct completion completion;
};

/* Because of FASTCALL declaration of complete, we use this wrapper */
static void wakeme_after_rcu(struct rcu_head  *head)
{
	struct rcu_synchronize *rcu;

	rcu = container_of(head, struct rcu_synchronize, head);
	complete(&rcu->completion);
}

/**
 * synchronize-kernel - wait until all the CPUs have gone
 * through a "quiescent" state. It may sleep.
 */
void synchronize_kernel(void)
{
	struct rcu_synchronize rcu;

	init_completion(&rcu.completion);
	/* Will wake me after RCU finished */
	call_rcu(&rcu.head, wakeme_after_rcu);

	/* Wait for it */
	wait_for_completion(&rcu.completion);
}


EXPORT_SYMBOL(call_rcu);
EXPORT_SYMBOL(synchronize_kernel);
