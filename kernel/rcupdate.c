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
	{ .mutex = SPIN_LOCK_UNLOCKED, .curbatch = 1, 
	  .maxbatch = 1, .rcu_cpu_mask = CPU_MASK_NONE };
DEFINE_PER_CPU(struct rcu_data, rcu_data) = { 0L };

/* Fake initialization required by compiler */
static DEFINE_PER_CPU(struct tasklet_struct, rcu_tasklet) = {NULL};
#define RCU_tasklet(cpu) (per_cpu(rcu_tasklet, cpu))

/**
 * call_rcu - Queue an RCU update request.
 * @head: structure to be used for queueing the RCU updates.
 * @func: actual update function to be invoked after the grace period
 * @arg: argument to be passed to the update function
 *
 * The update function will be invoked as soon as all CPUs have performed 
 * a context switch or been seen in the idle loop or in a user process. 
 * The read-side of critical section that use call_rcu() for updation must 
 * be protected by rcu_read_lock()/rcu_read_unlock().
 */
void fastcall call_rcu(struct rcu_head *head, void (*func)(void *arg), void *arg)
{
	int cpu;
	unsigned long flags;

	head->func = func;
	head->arg = arg;
	local_irq_save(flags);
	cpu = smp_processor_id();
	list_add_tail(&head->list, &RCU_nxtlist(cpu));
	local_irq_restore(flags);
}

/*
 * Invoke the completed RCU callbacks. They are expected to be in
 * a per-cpu list.
 */
static void rcu_do_batch(struct list_head *list)
{
	struct list_head *entry;
	struct rcu_head *head;

	while (!list_empty(list)) {
		entry = list->next;
		list_del(entry);
		head = list_entry(entry, struct rcu_head, list);
		head->func(head->arg);
	}
}

/*
 * Register a new batch of callbacks, and start it up if there is currently no
 * active batch and the batch to be registered has not already occurred.
 * Caller must hold the rcu_ctrlblk lock.
 */
static void rcu_start_batch(long newbatch)
{
	if (rcu_batch_before(rcu_ctrlblk.maxbatch, newbatch)) {
		rcu_ctrlblk.maxbatch = newbatch;
	}
	if (rcu_batch_before(rcu_ctrlblk.maxbatch, rcu_ctrlblk.curbatch) ||
	    !cpus_empty(rcu_ctrlblk.rcu_cpu_mask)) {
		return;
	}
	/* Can't change, since spin lock held. */
	rcu_ctrlblk.rcu_cpu_mask = cpu_online_map;
}

/*
 * Check if the cpu has gone through a quiescent state (say context
 * switch). If so and if it already hasn't done so in this RCU
 * quiescent cycle, then indicate that it has done so.
 */
static void rcu_check_quiescent_state(void)
{
	int cpu = smp_processor_id();

	if (!cpu_isset(cpu, rcu_ctrlblk.rcu_cpu_mask))
		return;

	/* 
	 * Races with local timer interrupt - in the worst case
	 * we may miss one quiescent state of that CPU. That is
	 * tolerable. So no need to disable interrupts.
	 */
	if (RCU_last_qsctr(cpu) == RCU_QSCTR_INVALID) {
		RCU_last_qsctr(cpu) = RCU_qsctr(cpu);
		return;
	}
	if (RCU_qsctr(cpu) == RCU_last_qsctr(cpu))
		return;

	spin_lock(&rcu_ctrlblk.mutex);
	if (!cpu_isset(cpu, rcu_ctrlblk.rcu_cpu_mask))
		goto out_unlock;

	cpu_clear(cpu, rcu_ctrlblk.rcu_cpu_mask);
	RCU_last_qsctr(cpu) = RCU_QSCTR_INVALID;
	if (!cpus_empty(rcu_ctrlblk.rcu_cpu_mask))
		goto out_unlock;

	rcu_ctrlblk.curbatch++;
	rcu_start_batch(rcu_ctrlblk.maxbatch);

out_unlock:
	spin_unlock(&rcu_ctrlblk.mutex);
}


#ifdef CONFIG_HOTPLUG_CPU

/* warning! helper for rcu_offline_cpu. do not use elsewhere without reviewing
 * locking requirements, the list it's pulling from has to belong to a cpu
 * which is dead and hence not processing interrupts.
 */
static void rcu_move_batch(struct list_head *list)
{
	struct list_head *entry;
	int cpu = smp_processor_id();

	local_irq_disable();
	while (!list_empty(list)) {
		entry = list->next;
		list_del(entry);
		list_add_tail(entry, &RCU_nxtlist(cpu));
	}
	local_irq_enable();
}

static void rcu_offline_cpu(int cpu)
{
	/* if the cpu going offline owns the grace period
	 * we can block indefinitely waiting for it, so flush
	 * it here
	 */
	spin_lock_irq(&rcu_ctrlblk.mutex);
	if (cpus_empty(rcu_ctrlblk.rcu_cpu_mask))
		goto unlock;

	cpu_clear(cpu, rcu_ctrlblk.rcu_cpu_mask);
	if (cpus_empty(rcu_ctrlblk.rcu_cpu_mask)) {
		rcu_ctrlblk.curbatch++;
		/* We may avoid calling start batch if
		 * we are starting the batch only
		 * because of the DEAD CPU (the current
		 * CPU will start a new batch anyway for
		 * the callbacks we will move to current CPU).
		 * However, we will avoid this optimisation
		 * for now.
		 */
		rcu_start_batch(rcu_ctrlblk.maxbatch);
	}
unlock:
	spin_unlock_irq(&rcu_ctrlblk.mutex);

	rcu_move_batch(&RCU_curlist(cpu));
	rcu_move_batch(&RCU_nxtlist(cpu));

	tasklet_kill_immediate(&RCU_tasklet(cpu), cpu);
}

#endif

/*
 * This does the RCU processing work from tasklet context. 
 */
static void rcu_process_callbacks(unsigned long unused)
{
	int cpu = smp_processor_id();
	LIST_HEAD(list);

	if (!list_empty(&RCU_curlist(cpu)) &&
	    rcu_batch_after(rcu_ctrlblk.curbatch, RCU_batch(cpu))) {
		list_splice(&RCU_curlist(cpu), &list);
		INIT_LIST_HEAD(&RCU_curlist(cpu));
	}

	local_irq_disable();
	if (!list_empty(&RCU_nxtlist(cpu)) && list_empty(&RCU_curlist(cpu))) {
		list_splice(&RCU_nxtlist(cpu), &RCU_curlist(cpu));
		INIT_LIST_HEAD(&RCU_nxtlist(cpu));
		local_irq_enable();

		/*
		 * start the next batch of callbacks
		 */
		spin_lock(&rcu_ctrlblk.mutex);
		RCU_batch(cpu) = rcu_ctrlblk.curbatch + 1;
		rcu_start_batch(RCU_batch(cpu));
		spin_unlock(&rcu_ctrlblk.mutex);
	} else {
		local_irq_enable();
	}
	rcu_check_quiescent_state();
	if (!list_empty(&list))
		rcu_do_batch(&list);
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
	INIT_LIST_HEAD(&RCU_nxtlist(cpu));
	INIT_LIST_HEAD(&RCU_curlist(cpu));
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


/* Because of FASTCALL declaration of complete, we use this wrapper */
static void wakeme_after_rcu(void *completion)
{
	complete(completion);
}

/**
 * synchronize-kernel - wait until all the CPUs have gone
 * through a "quiescent" state. It may sleep.
 */
void synchronize_kernel(void)
{
	struct rcu_head rcu;
	DECLARE_COMPLETION(completion);

	/* Will wake me after RCU finished */
	call_rcu(&rcu, wakeme_after_rcu, &completion);

	/* Wait for it */
	wait_for_completion(&completion);
}


EXPORT_SYMBOL(call_rcu);
EXPORT_SYMBOL(synchronize_kernel);
