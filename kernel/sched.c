/*
 *  kernel/sched.c
 *
 *  Kernel scheduler and related syscalls
 *
 *  Copyright (C) 1991-2002  Linus Torvalds
 *
 *  1996-12-23  Modified by Dave Grothe to fix bugs in semaphores and
 *		make semaphores SMP safe
 *  1998-11-19	Implemented schedule_timeout() and related stuff
 *		by Andrea Arcangeli
 *  2002-01-04	New ultra-scalable O(1) scheduler by Ingo Molnar:
 *		hybrid priority-list and round-robin design with
 *		an array-switch method of distributing timeslices
 *		and per-CPU runqueues.  Cleanups and useful suggestions
 *		by Davide Libenzi, preemptible kernel bits by Robert Love.
 *  2003-09-03	Interactivity tuning by Con Kolivas.
 *  2004-04-02	Scheduler domains code by Nick Piggin
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/nmi.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/highmem.h>
#include <linux/smp_lock.h>
#include <asm/mmu_context.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/kernel_stat.h>
#include <linux/security.h>
#include <linux/notifier.h>
#include <linux/suspend.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/timer.h>
#include <linux/rcupdate.h>
#include <linux/cpu.h>
#include <linux/percpu.h>
#include <linux/kthread.h>

#ifdef CONFIG_NUMA
#define cpu_to_node_mask(cpu) node_to_cpumask(cpu_to_node(cpu))
#else
#define cpu_to_node_mask(cpu) (cpu_online_map)
#endif

/*
 * Convert user-nice values [ -20 ... 0 ... 19 ]
 * to static priority [ MAX_RT_PRIO..MAX_PRIO-1 ],
 * and back.
 */
#define NICE_TO_PRIO(nice)	(MAX_RT_PRIO + (nice) + 20)
#define PRIO_TO_NICE(prio)	((prio) - MAX_RT_PRIO - 20)
#define TASK_NICE(p)		PRIO_TO_NICE((p)->static_prio)

/*
 * 'User priority' is the nice value converted to something we
 * can work with better when scaling various scheduler parameters,
 * it's a [ 0 ... 39 ] range.
 */
#define USER_PRIO(p)		((p)-MAX_RT_PRIO)
#define TASK_USER_PRIO(p)	USER_PRIO((p)->static_prio)
#define MAX_USER_PRIO		(USER_PRIO(MAX_PRIO))
#define AVG_TIMESLICE	(MIN_TIMESLICE + ((MAX_TIMESLICE - MIN_TIMESLICE) *\
			(MAX_PRIO-1-NICE_TO_PRIO(0))/(MAX_USER_PRIO - 1)))

/*
 * Some helpers for converting nanosecond timing to jiffy resolution
 */
#define NS_TO_JIFFIES(TIME)	((TIME) / (1000000000 / HZ))
#define JIFFIES_TO_NS(TIME)	((TIME) * (1000000000 / HZ))

/*
 * These are the 'tuning knobs' of the scheduler:
 *
 * Minimum timeslice is 10 msecs, default timeslice is 100 msecs,
 * maximum timeslice is 200 msecs. Timeslices get refilled after
 * they expire.
 */
#define MIN_TIMESLICE		( 10 * HZ / 1000)
#define MAX_TIMESLICE		(200 * HZ / 1000)
#define ON_RUNQUEUE_WEIGHT	 30
#define CHILD_PENALTY		 95
#define PARENT_PENALTY		100
#define EXIT_WEIGHT		  3
#define PRIO_BONUS_RATIO	 25
#define MAX_BONUS		(MAX_USER_PRIO * PRIO_BONUS_RATIO / 100)
#define INTERACTIVE_DELTA	  2
#define MAX_SLEEP_AVG		(AVG_TIMESLICE * MAX_BONUS)
#define STARVATION_LIMIT	(MAX_SLEEP_AVG)
#define NS_MAX_SLEEP_AVG	(JIFFIES_TO_NS(MAX_SLEEP_AVG))
#define CREDIT_LIMIT		100

/*
 * If a task is 'interactive' then we reinsert it in the active
 * array after it has expired its current timeslice. (it will not
 * continue to run immediately, it will still roundrobin with
 * other interactive tasks.)
 *
 * This part scales the interactivity limit depending on niceness.
 *
 * We scale it linearly, offset by the INTERACTIVE_DELTA delta.
 * Here are a few examples of different nice levels:
 *
 *  TASK_INTERACTIVE(-20): [1,1,1,1,1,1,1,1,1,0,0]
 *  TASK_INTERACTIVE(-10): [1,1,1,1,1,1,1,0,0,0,0]
 *  TASK_INTERACTIVE(  0): [1,1,1,1,0,0,0,0,0,0,0]
 *  TASK_INTERACTIVE( 10): [1,1,0,0,0,0,0,0,0,0,0]
 *  TASK_INTERACTIVE( 19): [0,0,0,0,0,0,0,0,0,0,0]
 *
 * (the X axis represents the possible -5 ... 0 ... +5 dynamic
 *  priority range a task can explore, a value of '1' means the
 *  task is rated interactive.)
 *
 * Ie. nice +19 tasks can never get 'interactive' enough to be
 * reinserted into the active array. And only heavily CPU-hog nice -20
 * tasks will be expired. Default nice 0 tasks are somewhere between,
 * it takes some effort for them to get interactive, but it's not
 * too hard.
 */

#define CURRENT_BONUS(p) \
	(NS_TO_JIFFIES((p)->sleep_avg) * MAX_BONUS / \
		MAX_SLEEP_AVG)

#ifdef CONFIG_SMP
#define TIMESLICE_GRANULARITY(p)	(MIN_TIMESLICE * \
		(1 << (((MAX_BONUS - CURRENT_BONUS(p)) ? : 1) - 1)) * \
			num_online_cpus())
#else
#define TIMESLICE_GRANULARITY(p)	(MIN_TIMESLICE * \
		(1 << (((MAX_BONUS - CURRENT_BONUS(p)) ? : 1) - 1)))
#endif

#define SCALE(v1,v1_max,v2_max) \
	(v1) * (v2_max) / (v1_max)

#define DELTA(p) \
	(SCALE(TASK_NICE(p), 40, MAX_USER_PRIO*PRIO_BONUS_RATIO/100) + \
		INTERACTIVE_DELTA)

#define TASK_INTERACTIVE(p) \
	((p)->prio <= (p)->static_prio - DELTA(p))

#define INTERACTIVE_SLEEP(p) \
	(JIFFIES_TO_NS(MAX_SLEEP_AVG * \
		(MAX_BONUS / 2 + DELTA((p)) + 1) / MAX_BONUS - 1))

#define HIGH_CREDIT(p) \
	((p)->interactive_credit > CREDIT_LIMIT)

#define LOW_CREDIT(p) \
	((p)->interactive_credit < -CREDIT_LIMIT)

#define TASK_PREEMPTS_CURR(p, rq) \
	((p)->prio < (rq)->curr->prio)

/*
 * BASE_TIMESLICE scales user-nice values [ -20 ... 19 ]
 * to time slice values.
 *
 * The higher a thread's priority, the bigger timeslices
 * it gets during one round of execution. But even the lowest
 * priority thread gets MIN_TIMESLICE worth of execution time.
 *
 * task_timeslice() is the interface that is used by the scheduler.
 */

#define BASE_TIMESLICE(p) (MIN_TIMESLICE + \
		((MAX_TIMESLICE - MIN_TIMESLICE) * \
			(MAX_PRIO-1 - (p)->static_prio) / (MAX_USER_PRIO-1)))

static inline unsigned int task_timeslice(task_t *p)
{
	return BASE_TIMESLICE(p);
}

/*
 * These are the runqueue data structures:
 */

#define BITMAP_SIZE ((((MAX_PRIO+1+7)/8)+sizeof(long)-1)/sizeof(long))

typedef struct runqueue runqueue_t;

struct prio_array {
	unsigned int nr_active;
	unsigned long bitmap[BITMAP_SIZE];
	struct list_head queue[MAX_PRIO];
};

#define SCHED_LOAD_SHIFT 7	/* increase resolution of load calculations */
#define SCHED_LOAD_SCALE (1 << SCHED_LOAD_SHIFT)

/*
 * This is the main, per-CPU runqueue data structure.
 *
 * Locking rule: those places that want to lock multiple runqueues
 * (such as the load balancing or the thread migration code), lock
 * acquire operations must be ordered by ascending &runqueue.
 */
struct runqueue {
	spinlock_t lock;
	unsigned long long nr_switches;
	unsigned long nr_running, expired_timestamp, nr_uninterruptible;
	unsigned long long timestamp_last_tick;
	task_t *curr, *idle;
	struct mm_struct *prev_mm;
	prio_array_t *active, *expired, arrays[2];
	int best_expired_prio;

	atomic_t nr_iowait;

#ifdef CONFIG_SMP
	unsigned long cpu_load[NR_CPUS];
#endif
	/* For active balancing */
	int active_balance;
	int push_cpu;

	task_t *migration_thread;
	struct list_head migration_queue;
};

static DEFINE_PER_CPU(struct runqueue, runqueues);

#ifdef CONFIG_SMP
/* Mandatory scheduling domains */
DEFINE_PER_CPU(struct sched_domain, base_domains);
#endif

#define cpu_rq(cpu)		(&per_cpu(runqueues, (cpu)))
#define this_rq()		(&__get_cpu_var(runqueues))
#define task_rq(p)		cpu_rq(task_cpu(p))
#define cpu_curr(cpu)		(cpu_rq(cpu)->curr)

extern unsigned long __scheduling_functions_start_here;
extern unsigned long __scheduling_functions_end_here;
const unsigned long scheduling_functions_start_here =
			(unsigned long)&__scheduling_functions_start_here;
const unsigned long scheduling_functions_end_here =
			(unsigned long)&__scheduling_functions_end_here;

/*
 * Default context-switch locking:
 */
#ifndef prepare_arch_switch
# define prepare_arch_switch(rq, next)	do { } while (0)
# define finish_arch_switch(rq, next)	spin_unlock_irq(&(rq)->lock)
# define task_running(rq, p)		((rq)->curr == (p))
#endif

static inline void nr_running_inc(runqueue_t *rq)
{
	rq->nr_running++;
}

static inline void nr_running_dec(runqueue_t *rq)
{
	rq->nr_running--;
}

/*
 * task_rq_lock - lock the runqueue a given task resides on and disable
 * interrupts.  Note the ordering: we can safely lookup the task_rq without
 * explicitly disabling preemption.
 */
static inline runqueue_t *task_rq_lock(task_t *p, unsigned long *flags)
{
	struct runqueue *rq;

repeat_lock_task:
	local_irq_save(*flags);
	rq = task_rq(p);
	spin_lock(&rq->lock);
	if (unlikely(rq != task_rq(p))) {
		spin_unlock_irqrestore(&rq->lock, *flags);
		goto repeat_lock_task;
	}
	return rq;
}

static inline void task_rq_unlock(runqueue_t *rq, unsigned long *flags)
{
	spin_unlock_irqrestore(&rq->lock, *flags);
}

/*
 * rq_lock - lock a given runqueue and disable interrupts.
 */
static inline runqueue_t *this_rq_lock(void)
{
	runqueue_t *rq;

	local_irq_disable();
	rq = this_rq();
	spin_lock(&rq->lock);

	return rq;
}

static inline void rq_unlock(runqueue_t *rq)
{
	spin_unlock_irq(&rq->lock);
}

/*
 * Adding/removing a task to/from a priority array:
 */
static inline void dequeue_task(struct task_struct *p, prio_array_t *array)
{
	array->nr_active--;
	list_del(&p->run_list);
	if (list_empty(array->queue + p->prio))
		__clear_bit(p->prio, array->bitmap);
}

static inline void enqueue_task(struct task_struct *p, prio_array_t *array)
{
	list_add_tail(&p->run_list, array->queue + p->prio);
	__set_bit(p->prio, array->bitmap);
	array->nr_active++;
	p->array = array;
}

/*
 * effective_prio - return the priority that is based on the static
 * priority but is modified by bonuses/penalties.
 *
 * We scale the actual sleep average [0 .... MAX_SLEEP_AVG]
 * into the -5 ... 0 ... +5 bonus/penalty range.
 *
 * We use 25% of the full 0...39 priority range so that:
 *
 * 1) nice +19 interactive tasks do not preempt nice 0 CPU hogs.
 * 2) nice -20 CPU hogs do not get preempted by nice 0 tasks.
 *
 * Both properties are important to certain workloads.
 */
static int effective_prio(task_t *p)
{
	int bonus, prio;

	if (rt_task(p))
		return p->prio;

	bonus = CURRENT_BONUS(p) - MAX_BONUS / 2;

	prio = p->static_prio - bonus;
	if (prio < MAX_RT_PRIO)
		prio = MAX_RT_PRIO;
	if (prio > MAX_PRIO-1)
		prio = MAX_PRIO-1;
	return prio;
}

/*
 * __activate_task - move a task to the runqueue.
 */
static inline void __activate_task(task_t *p, runqueue_t *rq)
{
	enqueue_task(p, rq->active);
	nr_running_inc(rq);
}

static void recalc_task_prio(task_t *p, unsigned long long now)
{
	unsigned long long __sleep_time = now - p->timestamp;
	unsigned long sleep_time;

	if (__sleep_time > NS_MAX_SLEEP_AVG)
		sleep_time = NS_MAX_SLEEP_AVG;
	else
		sleep_time = (unsigned long)__sleep_time;

	if (likely(sleep_time > 0)) {
		/*
		 * User tasks that sleep a long time are categorised as
		 * idle and will get just interactive status to stay active &
		 * prevent them suddenly becoming cpu hogs and starving
		 * other processes.
		 */
		if (p->mm && p->activated != -1 &&
			sleep_time > INTERACTIVE_SLEEP(p)) {
				p->sleep_avg = JIFFIES_TO_NS(MAX_SLEEP_AVG -
						AVG_TIMESLICE);
				if (!HIGH_CREDIT(p))
					p->interactive_credit++;
		} else {
			/*
			 * The lower the sleep avg a task has the more
			 * rapidly it will rise with sleep time.
			 */
			sleep_time *= (MAX_BONUS - CURRENT_BONUS(p)) ? : 1;

			/*
			 * Tasks with low interactive_credit are limited to
			 * one timeslice worth of sleep avg bonus.
			 */
			if (LOW_CREDIT(p) &&
			    sleep_time > JIFFIES_TO_NS(task_timeslice(p)))
				sleep_time = JIFFIES_TO_NS(task_timeslice(p));

			/*
			 * Non high_credit tasks waking from uninterruptible
			 * sleep are limited in their sleep_avg rise as they
			 * are likely to be cpu hogs waiting on I/O
			 */
			if (p->activated == -1 && !HIGH_CREDIT(p) && p->mm) {
				if (p->sleep_avg >= INTERACTIVE_SLEEP(p))
					sleep_time = 0;
				else if (p->sleep_avg + sleep_time >=
						INTERACTIVE_SLEEP(p)) {
					p->sleep_avg = INTERACTIVE_SLEEP(p);
					sleep_time = 0;
				}
			}

			/*
			 * This code gives a bonus to interactive tasks.
			 *
			 * The boost works by updating the 'average sleep time'
			 * value here, based on ->timestamp. The more time a
			 * task spends sleeping, the higher the average gets -
			 * and the higher the priority boost gets as well.
			 */
			p->sleep_avg += sleep_time;

			if (p->sleep_avg > NS_MAX_SLEEP_AVG) {
				p->sleep_avg = NS_MAX_SLEEP_AVG;
				if (!HIGH_CREDIT(p))
					p->interactive_credit++;
			}
		}
	}

	p->prio = effective_prio(p);
}

/*
 * activate_task - move a task to the runqueue and do priority recalculation
 *
 * Update all the scheduling statistics stuff. (sleep average
 * calculation, priority modifiers, etc.)
 */
static inline void activate_task(task_t *p, runqueue_t *rq)
{
	unsigned long long now = sched_clock();

	recalc_task_prio(p, now);

	/*
	 * This checks to make sure it's not an uninterruptible task
	 * that is now waking up.
	 */
	if (!p->activated) {
		/*
		 * Tasks which were woken up by interrupts (ie. hw events)
		 * are most likely of interactive nature. So we give them
		 * the credit of extending their sleep time to the period
		 * of time they spend on the runqueue, waiting for execution
		 * on a CPU, first time around:
		 */
		if (in_interrupt())
			p->activated = 2;
		else {
			/*
			 * Normal first-time wakeups get a credit too for
			 * on-runqueue time, but it will be weighted down:
			 */
			p->activated = 1;
		}
	}
	p->timestamp = now;

	__activate_task(p, rq);
}

/*
 * deactivate_task - remove a task from the runqueue.
 */
static inline void deactivate_task(struct task_struct *p, runqueue_t *rq)
{
	nr_running_dec(rq);
	if (p->state == TASK_UNINTERRUPTIBLE)
		rq->nr_uninterruptible++;
	dequeue_task(p, p->array);
	p->array = NULL;
}

/*
 * resched_task - mark a task 'to be rescheduled now'.
 *
 * On UP this means the setting of the need_resched flag, on SMP it
 * might also involve a cross-CPU call to trigger the scheduler on
 * the target CPU.
 */
static inline void resched_task(task_t *p)
{
#ifdef CONFIG_SMP
	int need_resched, nrpolling;

	preempt_disable();
	/* minimise the chance of sending an interrupt to poll_idle() */
	nrpolling = test_tsk_thread_flag(p,TIF_POLLING_NRFLAG);
	need_resched = test_and_set_tsk_thread_flag(p,TIF_NEED_RESCHED);
	nrpolling |= test_tsk_thread_flag(p,TIF_POLLING_NRFLAG);

	if (!need_resched && !nrpolling && (task_cpu(p) != smp_processor_id()))
		smp_send_reschedule(task_cpu(p));
	preempt_enable();
#else
	set_tsk_need_resched(p);
#endif
}

/**
 * task_curr - is this task currently executing on a CPU?
 * @p: the task in question.
 */
inline int task_curr(task_t *p)
{
	return cpu_curr(task_cpu(p)) == p;
}

#ifdef CONFIG_SMP
typedef struct {
	struct list_head list;
	task_t *task;
	int dest_cpu;
	struct completion done;
} migration_req_t;

/*
 * The task's runqueue lock must be held.
 * Returns true if you have to wait for migration thread.
 */
static int migrate_task(task_t *p, int dest_cpu, migration_req_t *req)
{
	runqueue_t *rq = task_rq(p);

	/*
	 * If the task is not on a runqueue (and not running), then
	 * it is sufficient to simply update the task's cpu field.
	 */
	if (!p->array && !task_running(rq, p)) {
		set_task_cpu(p, dest_cpu);
		return 0;
	}

	init_completion(&req->done);
	req->task = p;
	req->dest_cpu = dest_cpu;
	list_add(&req->list, &rq->migration_queue);
	return 1;
}

/*
 * wait_task_inactive - wait for a thread to unschedule.
 *
 * The caller must ensure that the task *will* unschedule sometime soon,
 * else this function might spin for a *long* time. This function can't
 * be called with interrupts off, or it may introduce deadlock with
 * smp_call_function() if an IPI is sent by the same process we are
 * waiting to become inactive.
 */
void wait_task_inactive(task_t * p)
{
	unsigned long flags;
	runqueue_t *rq;
	int preempted;

repeat:
	rq = task_rq_lock(p, &flags);
	/* Must be off runqueue entirely, not preempted. */
	if (unlikely(p->array)) {
		/* If it's preempted, we yield.  It could be a while. */
		preempted = !task_running(rq, p);
		task_rq_unlock(rq, &flags);
		cpu_relax();
		if (preempted)
			yield();
		goto repeat;
	}
	task_rq_unlock(rq, &flags);
}

/***
 * kick_process - kick a running thread to enter/exit the kernel
 * @p: the to-be-kicked thread
 *
 * Cause a process which is running on another CPU to enter
 * kernel-mode, without any delay. (to get signals handled.)
 */
void kick_process(task_t *p)
{
	int cpu;

	preempt_disable();
	cpu = task_cpu(p);
	if ((cpu != smp_processor_id()) && task_curr(p))
		smp_send_reschedule(cpu);
	preempt_enable();
}

EXPORT_SYMBOL_GPL(kick_process);
/*
 * Return a low guess at the load of cpu. Update previous history if update
 * is true
 */
static inline unsigned long get_low_cpu_load(int cpu, int update)
{
	runqueue_t *rq = cpu_rq(cpu);
	runqueue_t *this_rq = this_rq();
	unsigned long nr = rq->nr_running << SCHED_LOAD_SHIFT;
	unsigned long load = this_rq->cpu_load[cpu];
	unsigned long ret = min(nr, load);

	if (update)
		this_rq->cpu_load[cpu] = (nr + load) / 2;

	return ret;
}

static inline unsigned long get_high_cpu_load(int cpu, int update)
{
	runqueue_t *rq = cpu_rq(cpu);
	runqueue_t *this_rq = this_rq();
	unsigned long nr = rq->nr_running << SCHED_LOAD_SHIFT;
	unsigned long load = this_rq->cpu_load[cpu];
	unsigned long ret = max(nr, load);

	if (update)
		this_rq->cpu_load[cpu] = (nr + load) / 2;

	return ret;
}

#endif

/*
 * sched_balance_wake can be used with SMT architectures to wake a
 * task onto an idle sibling if cpu is not idle. Returns cpu if
 * cpu is idle or no siblings are idle, otherwise returns an idle
 * sibling.
 */
#if defined(CONFIG_SMP) && defined(ARCH_HAS_SCHED_WAKE_BALANCE)
static int sched_balance_wake(int cpu, task_t *p)
{
	struct sched_domain *domain;
	int i;

	if (idle_cpu(cpu))
		return cpu;

	domain = cpu_sched_domain(cpu);
	if (!(domain->flags & SD_FLAG_WAKE))
		return cpu;

	for_each_cpu_mask(i, domain->span) {
		if (!cpu_online(i))
			continue;

		if (!cpu_isset(i, p->cpus_allowed))
			continue;

		if (idle_cpu(i))
			return i;
	}

	return cpu;
}
#else
static inline int sched_balance_wake(int cpu, task_t *p)
{
	return cpu;
}
#endif

/***
 * try_to_wake_up - wake up a thread
 * @p: the to-be-woken-up thread
 * @state: the mask of task states that can be woken
 * @sync: do a synchronous wakeup?
 *
 * Put it on the run-queue if it's not already there. The "current"
 * thread is always on the run-queue (except when the actual
 * re-schedule is in progress), and as such you're allowed to do
 * the simpler "current->state = TASK_RUNNING" to mark yourself
 * runnable without the overhead of this.
 *
 * returns failure only if the task is already active.
 */
static int try_to_wake_up(task_t * p, unsigned int state, int sync)
{
	unsigned long flags;
	int success = 0;
	long old_state;
	runqueue_t *rq;
	int cpu, this_cpu;
#ifdef CONFIG_SMP
	unsigned long long now;
	unsigned long load, this_load;
	int new_cpu;
	struct sched_domain *sd;
#endif

	rq = task_rq_lock(p, &flags);
	old_state = p->state;
	if (!(old_state & state))
		goto out;

	if (p->array)
		goto out_running;

	this_cpu = smp_processor_id();
	cpu = task_cpu(p);

#ifdef CONFIG_SMP
	if (cpu == this_cpu || unlikely(cpu_is_offline(this_cpu)))
		goto out_activate;

	if (unlikely(!cpu_isset(this_cpu, p->cpus_allowed)
				|| task_running(rq, p)))
		goto out_activate;

	/* Passive load balancing */
	load = get_low_cpu_load(cpu, 1);
	this_load = get_high_cpu_load(this_cpu, 1) + SCHED_LOAD_SCALE;
	if (load > this_load) {
		new_cpu = sched_balance_wake(this_cpu, p);
		set_task_cpu(p, new_cpu);
		goto repeat_lock_task;
	}

	now = sched_clock();
	sd = cpu_sched_domain(this_cpu);

	/*
	 * Fast-migrate the task if it's not running or
	 * runnable currently. Do not violate hard affinity.
	 */
	do {
		if (!(sd->flags & SD_FLAG_FASTMIGRATE))
			break;
		if (now - p->timestamp < sd->cache_hot_time)
			break;

		if (cpu_isset(cpu, sd->span)) {
			new_cpu = sched_balance_wake(this_cpu, p);
			set_task_cpu(p, new_cpu);
			goto repeat_lock_task;
		}
		sd = sd->parent;
	} while (sd);

	new_cpu = sched_balance_wake(cpu, p);
	if (new_cpu != cpu) {
		set_task_cpu(p, new_cpu);
		goto repeat_lock_task;
	}
	goto out_activate;

repeat_lock_task:
	task_rq_unlock(rq, &flags);
	rq = task_rq_lock(p, &flags);
	old_state = p->state;
	if (!(old_state & state))
		goto out;

	if (p->array)
		goto out_running;

	this_cpu = smp_processor_id();
	cpu = task_cpu(p);

out_activate:
#endif /* CONFIG_SMP */
	if (old_state == TASK_UNINTERRUPTIBLE) {
		rq->nr_uninterruptible--;
		/*
		 * Tasks on involuntary sleep don't earn
		 * sleep_avg beyond just interactive state.
		 */
		p->activated = -1;
	}

	if (sync && cpu == this_cpu) {
		__activate_task(p, rq);
	} else {
		activate_task(p, rq);
		if (TASK_PREEMPTS_CURR(p, rq))
			resched_task(rq->curr);
	}
	success = 1;

out_running:
	p->state = TASK_RUNNING;
out:
	task_rq_unlock(rq, &flags);

	return success;
}
int fastcall wake_up_process(task_t * p)
{
	return try_to_wake_up(p, TASK_STOPPED |
		       		 TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE, 0);
}

EXPORT_SYMBOL(wake_up_process);

int fastcall wake_up_state(task_t *p, unsigned int state)
{
	return try_to_wake_up(p, state, 0);
}

/*
 * Perform scheduler related setup for a newly forked process p.
 * p is forked by current.
 */
void fastcall sched_fork(task_t *p)
{
	/*
	 * We mark the process as running here, but have not actually
	 * inserted it onto the runqueue yet. This guarantees that
	 * nobody will actually run it, and a signal or other external
	 * event cannot wake it up and insert it on the runqueue either.
	 */
	p->state = TASK_RUNNING;
	INIT_LIST_HEAD(&p->run_list);
	p->array = NULL;
	spin_lock_init(&p->switch_lock);
#ifdef CONFIG_PREEMPT
	/*
	 * During context-switch we hold precisely one spinlock, which
	 * schedule_tail drops. (in the common case it's this_rq()->lock,
	 * but it also can be p->switch_lock.) So we compensate with a count
	 * of 1. Also, we want to start with kernel preemption disabled.
	 */
	p->thread_info->preempt_count = 1;
#endif
	/*
	 * Share the timeslice between parent and child, thus the
	 * total amount of pending timeslices in the system doesn't change,
	 * resulting in more scheduling fairness.
	 */
	local_irq_disable();
	p->time_slice = (current->time_slice + 1) >> 1;
	/*
	 * The remainder of the first timeslice might be recovered by
	 * the parent if the child exits early enough.
	 */
	p->first_time_slice = 1;
	current->time_slice >>= 1;
	p->timestamp = sched_clock();
	if (!current->time_slice) {
		/*
		 * This case is rare, it happens when the parent has only
		 * a single jiffy left from its timeslice. Taking the
		 * runqueue lock is not a problem.
		 */
		current->time_slice = 1;
		preempt_disable();
		scheduler_tick(0, 0);
		local_irq_enable();
		preempt_enable();
	} else
		local_irq_enable();
}

/*
 * wake_up_forked_process - wake up a freshly forked process.
 *
 * This function will do some initial scheduler statistics housekeeping
 * that must be done for every newly created process.
 */
void fastcall wake_up_forked_process(task_t * p)
{
	unsigned long flags;
	runqueue_t *rq = task_rq_lock(current, &flags);

	BUG_ON(p->state != TASK_RUNNING);

	/*
	 * We decrease the sleep average of forking parents
	 * and children as well, to keep max-interactive tasks
	 * from forking tasks that are max-interactive.
	 */
	current->sleep_avg = JIFFIES_TO_NS(CURRENT_BONUS(current) *
		PARENT_PENALTY / 100 * MAX_SLEEP_AVG / MAX_BONUS);

	p->sleep_avg = JIFFIES_TO_NS(CURRENT_BONUS(p) *
		CHILD_PENALTY / 100 * MAX_SLEEP_AVG / MAX_BONUS);

	p->interactive_credit = 0;

	p->prio = effective_prio(p);
	set_task_cpu(p, smp_processor_id());

	if (unlikely(!current->array))
		__activate_task(p, rq);
	else {
		p->prio = current->prio;
		list_add_tail(&p->run_list, &current->run_list);
		p->array = current->array;
		p->array->nr_active++;
		nr_running_inc(rq);
	}
	task_rq_unlock(rq, &flags);
}

/*
 * Potentially available exiting-child timeslices are
 * retrieved here - this way the parent does not get
 * penalized for creating too many threads.
 *
 * (this cannot be used to 'generate' timeslices
 * artificially, because any timeslice recovered here
 * was given away by the parent in the first place.)
 */
void fastcall sched_exit(task_t * p)
{
	unsigned long flags;
	runqueue_t *rq;

	local_irq_save(flags);
	if (p->first_time_slice) {
		p->parent->time_slice += p->time_slice;
		if (unlikely(p->parent->time_slice > MAX_TIMESLICE))
			p->parent->time_slice = MAX_TIMESLICE;
	}
	local_irq_restore(flags);
	/*
	 * If the child was a (relative-) CPU hog then decrease
	 * the sleep_avg of the parent as well.
	 */
	rq = task_rq_lock(p->parent, &flags);
	if (p->sleep_avg < p->parent->sleep_avg)
		p->parent->sleep_avg = p->parent->sleep_avg /
		(EXIT_WEIGHT + 1) * EXIT_WEIGHT + p->sleep_avg /
		(EXIT_WEIGHT + 1);
	task_rq_unlock(rq, &flags);
}

/**
 * finish_task_switch - clean up after a task-switch
 * @prev: the thread we just switched away from.
 *
 * We enter this with the runqueue still locked, and finish_arch_switch()
 * will unlock it along with doing any other architecture-specific cleanup
 * actions.
 *
 * Note that we may have delayed dropping an mm in context_switch(). If
 * so, we finish that here outside of the runqueue lock.  (Doing it
 * with the lock held can cause deadlocks; see schedule() for
 * details.)
 */
static inline void finish_task_switch(task_t *prev)
{
	runqueue_t *rq = this_rq();
	struct mm_struct *mm = rq->prev_mm;
	unsigned long prev_task_flags;

	rq->prev_mm = NULL;

	/*
	 * A task struct has one reference for the use as "current".
	 * If a task dies, then it sets TASK_ZOMBIE in tsk->state and calls
	 * schedule one last time. The schedule call will never return,
	 * and the scheduled task must drop that reference.
	 * The test for TASK_ZOMBIE must occur while the runqueue locks are
	 * still held, otherwise prev could be scheduled on another cpu, die
	 * there before we look at prev->state, and then the reference would
	 * be dropped twice.
	 *		Manfred Spraul <manfred@colorfullife.com>
	 */
	prev_task_flags = prev->flags;
	finish_arch_switch(rq, prev);
	if (mm)
		mmdrop(mm);
	if (unlikely(prev_task_flags & PF_DEAD))
		put_task_struct(prev);
}

/**
 * schedule_tail - first thing a freshly forked thread must call.
 * @prev: the thread we just switched away from.
 */
asmlinkage void schedule_tail(task_t *prev)
{
	finish_task_switch(prev);

	if (current->set_child_tid)
		put_user(current->pid, current->set_child_tid);
}

/*
 * context_switch - switch to the new MM and the new
 * thread's register state.
 */
static inline
task_t * context_switch(runqueue_t *rq, task_t *prev, task_t *next)
{
	struct mm_struct *mm = next->mm;
	struct mm_struct *oldmm = prev->active_mm;

	if (unlikely(!mm)) {
		next->active_mm = oldmm;
		atomic_inc(&oldmm->mm_count);
		enter_lazy_tlb(oldmm, next);
	} else
		switch_mm(oldmm, mm, next);

	if (unlikely(!prev->mm)) {
		prev->active_mm = NULL;
		WARN_ON(rq->prev_mm);
		rq->prev_mm = oldmm;
	}

	/* Here we just switch the register state and the stack. */
	switch_to(prev, next, prev);

	return prev;
}

/*
 * nr_running, nr_uninterruptible and nr_context_switches:
 *
 * externally visible scheduler statistics: current number of runnable
 * threads, current number of uninterruptible-sleeping threads, total
 * number of context switches performed since bootup.
 */
unsigned long nr_running(void)
{
	unsigned long i, sum = 0;

	for_each_cpu(i)
		sum += cpu_rq(i)->nr_running;

	return sum;
}

unsigned long nr_uninterruptible(void)
{
	unsigned long i, sum = 0;

	for_each_cpu(i)
		sum += cpu_rq(i)->nr_uninterruptible;

	return sum;
}

unsigned long long nr_context_switches(void)
{
	unsigned long long i, sum = 0;

	for_each_cpu(i)
		sum += cpu_rq(i)->nr_switches;

	return sum;
}

unsigned long nr_iowait(void)
{
	unsigned long i, sum = 0;

	for_each_cpu(i)
		sum += atomic_read(&cpu_rq(i)->nr_iowait);

	return sum;
}

/*
 * double_rq_lock - safely lock two runqueues
 *
 * Note this does not disable interrupts like task_rq_lock,
 * you need to do so manually before calling.
 */
static inline void double_rq_lock(runqueue_t *rq1, runqueue_t *rq2)
{
	if (rq1 == rq2)
		spin_lock(&rq1->lock);
	else {
		if (rq1 < rq2) {
			spin_lock(&rq1->lock);
			spin_lock(&rq2->lock);
		} else {
			spin_lock(&rq2->lock);
			spin_lock(&rq1->lock);
		}
	}
}

/*
 * double_rq_unlock - safely unlock two runqueues
 *
 * Note this does not restore interrupts like task_rq_unlock,
 * you need to do so manually after calling.
 */
static inline void double_rq_unlock(runqueue_t *rq1, runqueue_t *rq2)
{
	spin_unlock(&rq1->lock);
	if (rq1 != rq2)
		spin_unlock(&rq2->lock);
}

enum idle_type
{
	IDLE,
	NOT_IDLE,
	NEWLY_IDLE,
};

#ifdef CONFIG_SMP
#ifdef CONFIG_NUMA
/*
 * If dest_cpu is allowed for this process, migrate the task to it.
 * This is accomplished by forcing the cpu_allowed mask to only
 * allow dest_cpu, which will force the cpu onto dest_cpu.  Then
 * the cpu_allowed mask is restored.
 */
static void sched_migrate_task(task_t *p, int dest_cpu)
{
	runqueue_t *rq;
	migration_req_t req;
	unsigned long flags;

	lock_cpu_hotplug();
	rq = task_rq_lock(p, &flags);
	if (!cpu_isset(dest_cpu, p->cpus_allowed))
		goto out;

	/* force the process onto the specified CPU */
	if (migrate_task(p, dest_cpu, &req)) {
		/* Need to wait for migration thread. */
		task_rq_unlock(rq, &flags);
		wake_up_process(rq->migration_thread);
		wait_for_completion(&req.done);
		return;
	}
out:
	task_rq_unlock(rq, &flags);
	unlock_cpu_hotplug();
}

/*
 * Find the least loaded CPU.  Slightly favor the current CPU by
 * setting its runqueue length as the minimum to start.
 */
static int sched_best_cpu(struct task_struct *p, struct sched_domain *domain)
{
	int i, min_load, this_cpu, best_cpu;

	best_cpu = this_cpu = task_cpu(p);
	min_load = INT_MAX;

	for_each_online_cpu(i) {
		unsigned long load;
		if (!cpu_isset(i, domain->span))
			continue;

		if (i == this_cpu)
			load = get_low_cpu_load(i, 0);
		else
			load = get_high_cpu_load(i, 0) + SCHED_LOAD_SCALE;

		if (min_load > load) {
			best_cpu = i;
			min_load = load;
		}

	}
	return best_cpu;
}

void sched_balance_exec(void)
{
	struct sched_domain *domain = this_sched_domain();
	int new_cpu;
	int this_cpu = smp_processor_id();
	if (numnodes == 1)
		return;

	if (this_rq()->nr_running <= 1)
		return;

	while (domain->parent && !(domain->flags & SD_FLAG_EXEC))
		domain = domain->parent;

	if (domain->flags & SD_FLAG_EXEC) {
		new_cpu = sched_best_cpu(current, domain);
		if (new_cpu != this_cpu)
			sched_migrate_task(current, new_cpu);
	}
}
#endif /* CONFIG_NUMA */

/*
 * double_lock_balance - lock the busiest runqueue, this_rq is locked already.
 */
static inline void double_lock_balance(runqueue_t *this_rq, runqueue_t *busiest)
{
	if (unlikely(!spin_trylock(&busiest->lock))) {
		if (busiest < this_rq) {
			spin_unlock(&this_rq->lock);
			spin_lock(&busiest->lock);
			spin_lock(&this_rq->lock);
		} else
			spin_lock(&busiest->lock);
	}
}

/*
 * pull_task - move a task from a remote runqueue to the local runqueue.
 * Both runqueues must be locked.
 */
static inline void pull_task(runqueue_t *src_rq, prio_array_t *src_array,
		task_t *p, runqueue_t *this_rq, prio_array_t *this_array,
		int this_cpu)
{
	dequeue_task(p, src_array);
	nr_running_dec(src_rq);
	set_task_cpu(p, this_cpu);
	nr_running_inc(this_rq);
	enqueue_task(p, this_array);
	p->timestamp = sched_clock() -
				(src_rq->timestamp_last_tick - p->timestamp);
	/*
	 * Note that idle threads have a prio of MAX_PRIO, for this test
	 * to be always true for them.
	 */
	if (TASK_PREEMPTS_CURR(p, this_rq))
		resched_task(this_rq->curr);
}

/*
 * can_migrate_task - may task p from runqueue rq be migrated to this_cpu?
 */
static inline
int can_migrate_task(task_t *p, runqueue_t *rq, int this_cpu,
		struct sched_domain *domain, enum idle_type idle)
{
	/*
	 * We do not migrate tasks that are:
	 * 1) running (obviously), or
	 * 2) cannot be migrated to this CPU due to cpus_allowed, or
	 * 3) are cache-hot on their current CPU.
	 */
	if (task_running(rq, p))
		return 0;
	if (!cpu_isset(this_cpu, p->cpus_allowed))
		return 0;

	/* Aggressive migration if we've failed balancing */
	if (idle == NEWLY_IDLE ||
			domain->nr_balance_failed < domain->cache_nice_tries) {
		if ((rq->timestamp_last_tick - p->timestamp)
						< domain->cache_hot_time)
			return 0;
	}

	return 1;
}

/*
 * move_tasks tries to move up to max_nr_move tasks from busiest to this_rq,
 * as part of a balancing operation within "domain". Returns the number of
 * tasks moved.
 *
 * Called with both runqueues locked.
 */
static int move_tasks(runqueue_t *this_rq, int this_cpu, runqueue_t *busiest,
			unsigned long max_nr_move, struct sched_domain *domain,
			enum idle_type idle)
{
	int idx;
	int pulled = 0;
	prio_array_t *array, *dst_array;
	struct list_head *head, *curr;
	task_t *tmp;

	if (max_nr_move <= 0 || busiest->nr_running <= 1)
		goto out;

	/*
	 * We first consider expired tasks. Those will likely not be
	 * executed in the near future, and they are most likely to
	 * be cache-cold, thus switching CPUs has the least effect
	 * on them.
	 */
	if (busiest->expired->nr_active) {
		array = busiest->expired;
		dst_array = this_rq->expired;
	} else {
		array = busiest->active;
		dst_array = this_rq->active;
	}

new_array:
	/* Start searching at priority 0: */
	idx = 0;
skip_bitmap:
	if (!idx)
		idx = sched_find_first_bit(array->bitmap);
	else
		idx = find_next_bit(array->bitmap, MAX_PRIO, idx);
	if (idx >= MAX_PRIO) {
		if (array == busiest->expired) {
			array = busiest->active;
			dst_array = this_rq->active;
			goto new_array;
		}
		goto out;
	}

	head = array->queue + idx;
	curr = head->prev;
skip_queue:
	tmp = list_entry(curr, task_t, run_list);

	curr = curr->prev;

	if (!can_migrate_task(tmp, busiest, this_cpu, domain, idle)) {
		if (curr != head)
			goto skip_queue;
		idx++;
		goto skip_bitmap;
	}
	pull_task(busiest, array, tmp, this_rq, dst_array, this_cpu);
	pulled++;

	/* We only want to steal up to the prescribed number of tasks. */
	if (pulled < max_nr_move) {
		if (curr != head)
			goto skip_queue;
		idx++;
		goto skip_bitmap;
	}
out:
	return pulled;
}

/*
 * find_busiest_group finds and returns the busiest CPU group within the
 * domain. It calculates and returns the number of tasks which should be
 * moved to restore balance via the imbalance parameter.
 */
static struct sched_group *
find_busiest_group(struct sched_domain *domain, int this_cpu,
				unsigned long *imbalance, enum idle_type idle)
{
	unsigned long max_load, avg_load, total_load, this_load;
	int modify, total_nr_cpus, busiest_nr_cpus, this_nr_cpus;
	enum idle_type package_idle = IDLE;
	struct sched_group *busiest = NULL, *group = domain->groups;

	max_load = 0;
	this_load = 0;
	total_load = 0;
	total_nr_cpus = 0;
	busiest_nr_cpus = 0;
	this_nr_cpus = 0;

	if (group == NULL)
		goto out_balanced;

	/*
	 * Don't modify when we newly become idle because that ruins our
	 * statistics: its triggered by some value of nr_running (ie. 0).
	 * Timer based balancing is a good statistic though.
	 */
	if (idle == NEWLY_IDLE)
		modify = 0;
	else
		modify = 1;

	do {
		unsigned long load;
		int local_group;
		int i, nr_cpus = 0;

		local_group = cpu_isset(this_cpu, group->cpumask);

		/* Tally up the load of all CPUs in the group */
		avg_load = 0;
		for_each_cpu_mask(i, group->cpumask) {
			if (!cpu_online(i))
				continue;

			/* Bias balancing toward cpus of our domain */
			if (local_group) {
				load = get_high_cpu_load(i, modify);
				if (!idle_cpu(i))
					package_idle = NOT_IDLE;
			} else
				load = get_low_cpu_load(i, modify);

			nr_cpus++;
			avg_load += load;
		}

		if (!nr_cpus)
			goto nextgroup;

		total_load += avg_load;

		/*
		 * Load is cumulative over SD_FLAG_IDLE domains, but
		 * spread over !SD_FLAG_IDLE domains. For example, 2
		 * processes running on an SMT CPU puts a load of 2 on
		 * that CPU, however 2 processes running on 2 CPUs puts
		 * a load of 1 on that domain.
		 *
		 * This should be configurable so as SMT siblings become
		 * more powerful, they can "spread" more load - for example,
		 * the above case might only count as a load of 1.7.
		 */
		if (!(domain->flags & SD_FLAG_IDLE)) {
			avg_load /= nr_cpus;
			total_nr_cpus += nr_cpus;
		} else
			total_nr_cpus++;

		if (avg_load > max_load)
			max_load = avg_load;

		if (local_group) {
			this_load = avg_load;
			this_nr_cpus = nr_cpus;
		} else if (avg_load >= max_load) {
			busiest = group;
			busiest_nr_cpus = nr_cpus;
		}
nextgroup:
		group = group->next;
	} while (group != domain->groups);

	if (!busiest)
		goto out_balanced;

	avg_load = total_load / total_nr_cpus;

	if (this_load >= avg_load)
		goto out_balanced;

	if (idle == NOT_IDLE && 100*max_load <= domain->imbalance_pct*this_load)
		goto out_balanced;

	/*
	 * We're trying to get all the cpus to the average_load, so we don't
	 * want to push ourselves above the average load, nor do we wish to
	 * reduce the max loaded cpu below the average load, as either of these
	 * actions would just result in more rebalancing later, and ping-pong
	 * tasks around. Thus we look for the minimum possible imbalance.
	 * Negative imbalances (*we* are more loaded than anyone else) will
	 * be counted as no imbalance for these purposes -- we can't fix that
	 * by pulling tasks to us.  Be careful of negative numbers as they'll
	 * appear as very large values with unsigned longs.
	 */
	*imbalance = (min(max_load - avg_load, avg_load - this_load) + 1) / 2;
	/* Get rid of the scaling factor, rounding *up* as we divide */
	*imbalance = (*imbalance + SCHED_LOAD_SCALE/2 + 1)
					>> SCHED_LOAD_SHIFT;

	if (*imbalance == 0)
		goto out_balanced;

	/* How many tasks to actually move to equalise the imbalance */
	*imbalance *= min(busiest_nr_cpus, this_nr_cpus);

	return busiest;

out_balanced:
	if (busiest && idle == NEWLY_IDLE) {
		*imbalance = 1;
		return busiest;
	}

	*imbalance = 0;
	return NULL;
}

/*
 * find_busiest_queue - find the busiest runqueue among the cpus in group.
 */
static runqueue_t *find_busiest_queue(struct sched_group *group)
{
	int i;
	unsigned long max_load = 0;
	runqueue_t *busiest = NULL;

	for_each_cpu_mask(i, group->cpumask) {
		unsigned long load;

		if (!cpu_online(i))
			continue;

		load = get_low_cpu_load(i, 0);

		if (load > max_load) {
			max_load = load;
			busiest = cpu_rq(i);
		}
	}

	return busiest;
}

/*
 * Check this_cpu to ensure it is balanced within domain. Attempt to move
 * tasks if there is an imbalance.
 *
 * Called with this_rq unlocked.
 */
static int load_balance(int this_cpu, runqueue_t *this_rq,
			struct sched_domain *domain, enum idle_type idle)
{
	struct sched_group *group;
	runqueue_t *busiest = NULL;
	unsigned long imbalance;
	int balanced = 0, failed = 0;
	int nr_moved = 0;

	spin_lock(&this_rq->lock);

	group = find_busiest_group(domain, this_cpu, &imbalance, idle);
	if (!group) {
		balanced = 1;
		goto out;
	}

	busiest = find_busiest_queue(group);
	if (!busiest || busiest == this_rq) {
		balanced = 1;
		goto out;
	}

	/* Attempt to move tasks */
	double_lock_balance(this_rq, busiest);

	nr_moved = move_tasks(this_rq, this_cpu, busiest,
					imbalance, domain, idle);
	spin_unlock(&busiest->lock);
out:
	spin_unlock(&this_rq->lock);

	if (!balanced && nr_moved == 0)
		failed = 1;

	if (domain->flags & SD_FLAG_IDLE && failed && busiest &&
	   		domain->nr_balance_failed > domain->cache_nice_tries) {
		int i;
		for_each_cpu_mask(i, group->cpumask) {
			int wake = 0;

			if (!cpu_online(i))
				continue;

			busiest = cpu_rq(i);
			spin_lock(&busiest->lock);
			if (!busiest->active_balance) {
				busiest->active_balance = 1;
				busiest->push_cpu = this_cpu;
				wake = 1;
			}
			spin_unlock(&busiest->lock);
			if (wake)
				wake_up_process(busiest->migration_thread);
		}
	}

	if (failed)
		domain->nr_balance_failed++;
	else
		domain->nr_balance_failed = 0;

	if (balanced) {
		if (domain->balance_interval < domain->max_interval)
			domain->balance_interval *= 2;
	} else {
		domain->balance_interval = domain->min_interval;
	}

	return nr_moved;
}

/*
 * Check this_cpu to ensure it is balanced within domain. Attempt to move
 * tasks if there is an imbalance.
 *
 * Called from schedule when this_rq is about to become idle (NEWLY_IDLE).
 * this_rq is locked.
 */
static int load_balance_newidle(int this_cpu, runqueue_t *this_rq,
			struct sched_domain *domain)
{
	struct sched_group *group;
	runqueue_t *busiest = NULL;
	unsigned long imbalance;
	int nr_moved = 0;

	group = find_busiest_group(domain, this_cpu, &imbalance, NEWLY_IDLE);
	if (!group)
		goto out;

	busiest = find_busiest_queue(group);
	if (!busiest || busiest == this_rq)
		goto out;

	/* Attempt to move tasks */
	double_lock_balance(this_rq, busiest);

	nr_moved = move_tasks(this_rq, this_cpu, busiest,
					imbalance, domain, NEWLY_IDLE);

	spin_unlock(&busiest->lock);

out:
	return nr_moved;
}

/*
 * idle_balance is called by schedule() if this_cpu is about to become
 * idle. Attempts to pull tasks from other CPUs.
 */
static inline void idle_balance(int this_cpu, runqueue_t *this_rq)
{
	struct sched_domain *domain = this_sched_domain();

	if (unlikely(cpu_is_offline(this_cpu)))
		return;

	do {
		if (unlikely(!domain->groups))
			/* hasn't been setup yet */
			break;

		if (domain->flags & SD_FLAG_NEWIDLE) {
			if (load_balance_newidle(this_cpu, this_rq, domain)) {
				/* We've pulled tasks over so stop searching */
				break;
			}
		}

		domain = domain->parent;
	} while (domain);
}

/*
 * active_load_balance is run by migration threads. It pushes a running
 * task off the cpu. It can be required to correctly have at least 1 task
 * running on each physical CPU where possible, and not have a physical /
 * logical imbalance.
 *
 * Called with busiest locked.
 */
static void active_load_balance(runqueue_t *busiest, int busiest_cpu)
{
	int i;
	struct sched_domain *sd = cpu_sched_domain(busiest_cpu);
	struct sched_group *group, *busy_group;

	if (busiest->nr_running <= 1)
		return;

	/* sd->parent should never cause a NULL dereference, if it did so,
 	 * then push_cpu was set to a buggy value */
	while (!cpu_isset(busiest->push_cpu, sd->span)) {
 		sd = sd->parent;
		if (!sd->parent && !cpu_isset(busiest->push_cpu, sd->span)) {
			WARN_ON(1);
			return;
		}
	}

	if (!sd->groups) {
		WARN_ON(1);
		return;
	}

 	group = sd->groups;
	while (!cpu_isset(busiest_cpu, group->cpumask)) {
 		group = group->next;
		if (group == sd->groups) {
			WARN_ON(1);
			return;
		}
	}
 	busy_group = group;

 	group = sd->groups;
 	do {
		runqueue_t *rq;
 		int push_cpu = 0, nr = 0;

 		if (group == busy_group)
 			goto next_group;

 		for_each_cpu_mask(i, group->cpumask) {
			if (!cpu_online(i))
				continue;

			if (!idle_cpu(i))
				goto next_group;
 			push_cpu = i;
 			nr++;
 		}
 		if (nr == 0)
 			goto next_group;

		rq = cpu_rq(push_cpu);
		double_lock_balance(busiest, rq);
		move_tasks(rq, push_cpu, busiest, 1, sd, IDLE);
		spin_unlock(&rq->lock);
next_group:
		group = group->next;
	} while (group != sd->groups);
}

/*
 * rebalance_tick will get called every timer tick, on every CPU.
 *
 * It checks each scheduling domain to see if it is due to be balanced,
 * and initiates a balancing operation if so.
 *
 * Balancing parameters are set up in arch_init_sched_domains.
 */

/* Don't have all balancing operations going off at once */
#define CPU_OFFSET(cpu) (HZ * cpu / NR_CPUS)

static void rebalance_tick(int this_cpu, runqueue_t *this_rq, enum idle_type idle)
{
	unsigned long j = jiffies + CPU_OFFSET(this_cpu);
	struct sched_domain *domain = this_sched_domain();

	if (unlikely(cpu_is_offline(this_cpu)))
		return;

	/* Run through all this CPU's domains */
	do {
		unsigned long interval;

		if (unlikely(!domain->groups))
			break;

		interval = domain->balance_interval;
		if (idle != IDLE)
			interval *= domain->busy_factor;

		/* scale ms to jiffies */
		interval = interval * HZ / 1000;
		if (unlikely(interval == 0))
			interval = 1;

		if (j - domain->last_balance >= interval) {
			if (load_balance(this_cpu, this_rq, domain, idle)) {
				/* We've pulled tasks over so no longer idle */
				idle = NOT_IDLE;
			}
			domain->last_balance += interval;
		}

		domain = domain->parent;
	} while (domain);
}
#else
/*
 * on UP we do not need to balance between CPUs:
 */
static inline void rebalance_tick(int this_cpu, runqueue_t *this_rq, enum idle_type idle)
{
}
#endif

DEFINE_PER_CPU(struct kernel_stat, kstat);

EXPORT_PER_CPU_SYMBOL(kstat);

/*
 * We place interactive tasks back into the active array, if possible.
 *
 * To guarantee that this does not starve expired tasks we ignore the
 * interactivity of a task if the first expired task had to wait more
 * than a 'reasonable' amount of time. This deadline timeout is
 * load-dependent, as the frequency of array switched decreases with
 * increasing number of running tasks. We also ignore the interactivity
 * if a better static_prio task has expired:
 */
#define EXPIRED_STARVING(rq) \
	((STARVATION_LIMIT && ((rq)->expired_timestamp && \
		(jiffies - (rq)->expired_timestamp >= \
			STARVATION_LIMIT * ((rq)->nr_running) + 1))) || \
			((rq)->curr->static_prio > (rq)->best_expired_prio))

/*
 * This function gets called by the timer code, with HZ frequency.
 * We call it with interrupts disabled.
 *
 * It also gets called by the fork code, when changing the parent's
 * timeslices.
 */
void scheduler_tick(int user_ticks, int sys_ticks)
{
	int cpu = smp_processor_id();
	struct cpu_usage_stat *cpustat = &kstat_this_cpu.cpustat;
	runqueue_t *rq = this_rq();
	task_t *p = current;

	rq->timestamp_last_tick = sched_clock();

	if (rcu_pending(cpu))
		rcu_check_callbacks(cpu, user_ticks);

	/* note: this timer irq context must be accounted for as well */
	if (hardirq_count() - HARDIRQ_OFFSET) {
		cpustat->irq += sys_ticks;
		sys_ticks = 0;
	} else if (softirq_count()) {
		cpustat->softirq += sys_ticks;
		sys_ticks = 0;
	}

	if (p == rq->idle) {
		if (atomic_read(&rq->nr_iowait) > 0)
			cpustat->iowait += sys_ticks;
		else
			cpustat->idle += sys_ticks;
		rebalance_tick(cpu, rq, IDLE);
		return;
	}
	if (TASK_NICE(p) > 0)
		cpustat->nice += user_ticks;
	else
		cpustat->user += user_ticks;
	cpustat->system += sys_ticks;

	/* Task might have expired already, but not scheduled off yet */
	if (p->array != rq->active) {
		set_tsk_need_resched(p);
		goto out;
	}
	spin_lock(&rq->lock);
	/*
	 * The task was running during this tick - update the
	 * time slice counter. Note: we do not update a thread's
	 * priority until it either goes to sleep or uses up its
	 * timeslice. This makes it possible for interactive tasks
	 * to use up their timeslices at their highest priority levels.
	 */
	if (unlikely(rt_task(p))) {
		/*
		 * RR tasks need a special form of timeslice management.
		 * FIFO tasks have no timeslices.
		 */
		if ((p->policy == SCHED_RR) && !--p->time_slice) {
			p->time_slice = task_timeslice(p);
			p->first_time_slice = 0;
			set_tsk_need_resched(p);

			/* put it at the end of the queue: */
			dequeue_task(p, rq->active);
			enqueue_task(p, rq->active);
		}
		goto out_unlock;
	}
	if (!--p->time_slice) {
		dequeue_task(p, rq->active);
		set_tsk_need_resched(p);
		p->prio = effective_prio(p);
		p->time_slice = task_timeslice(p);
		p->first_time_slice = 0;

		if (!rq->expired_timestamp)
			rq->expired_timestamp = jiffies;
		if (!TASK_INTERACTIVE(p) || EXPIRED_STARVING(rq)) {
			enqueue_task(p, rq->expired);
			if (p->static_prio < rq->best_expired_prio)
				rq->best_expired_prio = p->static_prio;
		} else
			enqueue_task(p, rq->active);
	} else {
		/*
		 * Prevent a too long timeslice allowing a task to monopolize
		 * the CPU. We do this by splitting up the timeslice into
		 * smaller pieces.
		 *
		 * Note: this does not mean the task's timeslices expire or
		 * get lost in any way, they just might be preempted by
		 * another task of equal priority. (one with higher
		 * priority would have preempted this task already.) We
		 * requeue this task to the end of the list on this priority
		 * level, which is in essence a round-robin of tasks with
		 * equal priority.
		 *
		 * This only applies to tasks in the interactive
		 * delta range with at least TIMESLICE_GRANULARITY to requeue.
		 */
		if (TASK_INTERACTIVE(p) && !((task_timeslice(p) -
			p->time_slice) % TIMESLICE_GRANULARITY(p)) &&
			(p->time_slice >= TIMESLICE_GRANULARITY(p)) &&
			(p->array == rq->active)) {

			dequeue_task(p, rq->active);
			set_tsk_need_resched(p);
			p->prio = effective_prio(p);
			enqueue_task(p, rq->active);
		}
	}
out_unlock:
	spin_unlock(&rq->lock);
out:
	rebalance_tick(cpu, rq, NOT_IDLE);
}

/*
 * schedule() is the main scheduler function.
 */
asmlinkage void __sched schedule(void)
{
	long *switch_count;
	task_t *prev, *next;
	runqueue_t *rq;
	prio_array_t *array;
	struct list_head *queue;
	unsigned long long now;
	unsigned long run_time;
	int idx;

	/*
	 * Test if we are atomic.  Since do_exit() needs to call into
	 * schedule() atomically, we ignore that path for now.
	 * Otherwise, whine if we are scheduling when we should not be.
	 */
	if (likely(!(current->state & (TASK_DEAD | TASK_ZOMBIE)))) {
		if (unlikely(in_atomic())) {
			printk(KERN_ERR "bad: scheduling while atomic!\n");
			dump_stack();
		}
	}

need_resched:
	preempt_disable();
	prev = current;
	rq = this_rq();

	release_kernel_lock(prev);
	now = sched_clock();
	if (likely(now - prev->timestamp < NS_MAX_SLEEP_AVG))
		run_time = now - prev->timestamp;
	else
		run_time = NS_MAX_SLEEP_AVG;

	/*
	 * Tasks with interactive credits get charged less run_time
	 * at high sleep_avg to delay them losing their interactive
	 * status
	 */
	if (HIGH_CREDIT(prev))
		run_time /= (CURRENT_BONUS(prev) ? : 1);

	spin_lock_irq(&rq->lock);

	/*
	 * if entering off of a kernel preemption go straight
	 * to picking the next task.
	 */
	switch_count = &prev->nivcsw;
	if (prev->state && !(preempt_count() & PREEMPT_ACTIVE)) {
		switch_count = &prev->nvcsw;
		if (unlikely((prev->state & TASK_INTERRUPTIBLE) &&
				unlikely(signal_pending(prev))))
			prev->state = TASK_RUNNING;
		else
			deactivate_task(prev, rq);
	}

	if (unlikely(!rq->nr_running)) {
#ifdef CONFIG_SMP
		idle_balance(smp_processor_id(), rq);
#endif
		if (!rq->nr_running) {
			next = rq->idle;
			rq->expired_timestamp = 0;
			goto switch_tasks;
		}
	}

	array = rq->active;
	if (unlikely(!array->nr_active)) {
		/*
		 * Switch the active and expired arrays.
		 */
		rq->active = rq->expired;
		rq->expired = array;
		array = rq->active;
		rq->expired_timestamp = 0;
		rq->best_expired_prio = MAX_PRIO;
	}

	idx = sched_find_first_bit(array->bitmap);
	queue = array->queue + idx;
	next = list_entry(queue->next, task_t, run_list);

	if (!rt_task(next) && next->activated > 0) {
		unsigned long long delta = now - next->timestamp;

		if (next->activated == 1)
			delta = delta * (ON_RUNQUEUE_WEIGHT * 128 / 100) / 128;

		array = next->array;
		dequeue_task(next, array);
		recalc_task_prio(next, next->timestamp + delta);
		enqueue_task(next, array);
	}
	next->activated = 0;
switch_tasks:
	prefetch(next);
	clear_tsk_need_resched(prev);
	RCU_qsctr(task_cpu(prev))++;

	prev->sleep_avg -= run_time;
	if ((long)prev->sleep_avg <= 0) {
		prev->sleep_avg = 0;
		if (!(HIGH_CREDIT(prev) || LOW_CREDIT(prev)))
			prev->interactive_credit--;
	}
	prev->timestamp = now;

	if (likely(prev != next)) {
		next->timestamp = now;
		rq->nr_switches++;
		rq->curr = next;
		++*switch_count;

		prepare_arch_switch(rq, next);
		prev = context_switch(rq, prev, next);
		barrier();

		finish_task_switch(prev);
	} else
		spin_unlock_irq(&rq->lock);

	reacquire_kernel_lock(current);
	preempt_enable_no_resched();
	if (test_thread_flag(TIF_NEED_RESCHED))
		goto need_resched;
}

EXPORT_SYMBOL(schedule);

#ifdef CONFIG_PREEMPT
/*
 * this is is the entry point to schedule() from in-kernel preemption
 * off of preempt_enable.  Kernel preemptions off return from interrupt
 * occur there and call schedule directly.
 */
asmlinkage void __sched preempt_schedule(void)
{
	struct thread_info *ti = current_thread_info();

	/*
	 * If there is a non-zero preempt_count or interrupts are disabled,
	 * we do not want to preempt the current task.  Just return..
	 */
	if (unlikely(ti->preempt_count || irqs_disabled()))
		return;

need_resched:
	ti->preempt_count = PREEMPT_ACTIVE;
	schedule();
	ti->preempt_count = 0;

	/* we could miss a preemption opportunity between schedule and now */
	barrier();
	if (unlikely(test_thread_flag(TIF_NEED_RESCHED)))
		goto need_resched;
}

EXPORT_SYMBOL(preempt_schedule);
#endif /* CONFIG_PREEMPT */

int default_wake_function(wait_queue_t *curr, unsigned mode, int sync)
{
	task_t *p = curr->task;
	return try_to_wake_up(p, mode, sync);
}

EXPORT_SYMBOL(default_wake_function);

/*
 * The core wakeup function.  Non-exclusive wakeups (nr_exclusive == 0) just
 * wake everything up.  If it's an exclusive wakeup (nr_exclusive == small +ve
 * number) then we wake all the non-exclusive tasks and one exclusive task.
 *
 * There are circumstances in which we can try to wake a task which has already
 * started to run but is not in state TASK_RUNNING.  try_to_wake_up() returns
 * zero in this (rare) case, and we handle it by continuing to scan the queue.
 */
static void __wake_up_common(wait_queue_head_t *q, unsigned int mode,
			     int nr_exclusive, int sync)
{
	struct list_head *tmp, *next;

	list_for_each_safe(tmp, next, &q->task_list) {
		wait_queue_t *curr;
		unsigned flags;
		curr = list_entry(tmp, wait_queue_t, task_list);
		flags = curr->flags;
		if (curr->func(curr, mode, sync) &&
		    (flags & WQ_FLAG_EXCLUSIVE) &&
		    !--nr_exclusive)
			break;
	}
}

/**
 * __wake_up - wake up threads blocked on a waitqueue.
 * @q: the waitqueue
 * @mode: which threads
 * @nr_exclusive: how many wake-one or wake-many threads to wake up
 */
void fastcall __wake_up(wait_queue_head_t *q, unsigned int mode, int nr_exclusive)
{
	unsigned long flags;

	spin_lock_irqsave(&q->lock, flags);
	__wake_up_common(q, mode, nr_exclusive, 0);
	spin_unlock_irqrestore(&q->lock, flags);
}

EXPORT_SYMBOL(__wake_up);

/*
 * Same as __wake_up but called with the spinlock in wait_queue_head_t held.
 */
void fastcall __wake_up_locked(wait_queue_head_t *q, unsigned int mode)
{
	__wake_up_common(q, mode, 1, 0);
}

/**
 * __wake_up - sync- wake up threads blocked on a waitqueue.
 * @q: the waitqueue
 * @mode: which threads
 * @nr_exclusive: how many wake-one or wake-many threads to wake up
 *
 * The sync wakeup differs that the waker knows that it will schedule
 * away soon, so while the target thread will be woken up, it will not
 * be migrated to another CPU - ie. the two threads are 'synchronized'
 * with each other. This can prevent needless bouncing between CPUs.
 *
 * On UP it can prevent extra preemption.
 */
void fastcall __wake_up_sync(wait_queue_head_t *q, unsigned int mode, int nr_exclusive)
{
	unsigned long flags;

	if (unlikely(!q))
		return;

	spin_lock_irqsave(&q->lock, flags);
	if (likely(nr_exclusive))
		__wake_up_common(q, mode, nr_exclusive, 1);
	else
		__wake_up_common(q, mode, nr_exclusive, 0);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL_GPL(__wake_up_sync);	/* For internal use only */

void fastcall complete(struct completion *x)
{
	unsigned long flags;

	spin_lock_irqsave(&x->wait.lock, flags);
	x->done++;
	__wake_up_common(&x->wait, TASK_UNINTERRUPTIBLE | TASK_INTERRUPTIBLE,
			 1, 0);
	spin_unlock_irqrestore(&x->wait.lock, flags);
}
EXPORT_SYMBOL(complete);

void fastcall complete_all(struct completion *x)
{
	unsigned long flags;

	spin_lock_irqsave(&x->wait.lock, flags);
	x->done += UINT_MAX/2;
	__wake_up_common(&x->wait, TASK_UNINTERRUPTIBLE | TASK_INTERRUPTIBLE,
			 0, 0);
	spin_unlock_irqrestore(&x->wait.lock, flags);
}
EXPORT_SYMBOL(complete_all);

void fastcall __sched wait_for_completion(struct completion *x)
{
	might_sleep();
	spin_lock_irq(&x->wait.lock);
	if (!x->done) {
		DECLARE_WAITQUEUE(wait, current);

		wait.flags |= WQ_FLAG_EXCLUSIVE;
		__add_wait_queue_tail(&x->wait, &wait);
		do {
			__set_current_state(TASK_UNINTERRUPTIBLE);
			spin_unlock_irq(&x->wait.lock);
			schedule();
			spin_lock_irq(&x->wait.lock);
		} while (!x->done);
		__remove_wait_queue(&x->wait, &wait);
	}
	x->done--;
	spin_unlock_irq(&x->wait.lock);
}
EXPORT_SYMBOL(wait_for_completion);

#define	SLEEP_ON_VAR					\
	unsigned long flags;				\
	wait_queue_t wait;				\
	init_waitqueue_entry(&wait, current);

#define SLEEP_ON_HEAD					\
	spin_lock_irqsave(&q->lock,flags);		\
	__add_wait_queue(q, &wait);			\
	spin_unlock(&q->lock);

#define	SLEEP_ON_TAIL					\
	spin_lock_irq(&q->lock);			\
	__remove_wait_queue(q, &wait);			\
	spin_unlock_irqrestore(&q->lock, flags);

void fastcall __sched interruptible_sleep_on(wait_queue_head_t *q)
{
	SLEEP_ON_VAR

	current->state = TASK_INTERRUPTIBLE;

	SLEEP_ON_HEAD
	schedule();
	SLEEP_ON_TAIL
}

EXPORT_SYMBOL(interruptible_sleep_on);

long fastcall __sched interruptible_sleep_on_timeout(wait_queue_head_t *q, long timeout)
{
	SLEEP_ON_VAR

	current->state = TASK_INTERRUPTIBLE;

	SLEEP_ON_HEAD
	timeout = schedule_timeout(timeout);
	SLEEP_ON_TAIL

	return timeout;
}

EXPORT_SYMBOL(interruptible_sleep_on_timeout);

void fastcall __sched sleep_on(wait_queue_head_t *q)
{
	SLEEP_ON_VAR

	current->state = TASK_UNINTERRUPTIBLE;

	SLEEP_ON_HEAD
	schedule();
	SLEEP_ON_TAIL
}

EXPORT_SYMBOL(sleep_on);

long fastcall __sched sleep_on_timeout(wait_queue_head_t *q, long timeout)
{
	SLEEP_ON_VAR

	current->state = TASK_UNINTERRUPTIBLE;

	SLEEP_ON_HEAD
	timeout = schedule_timeout(timeout);
	SLEEP_ON_TAIL

	return timeout;
}

EXPORT_SYMBOL(sleep_on_timeout);

void set_user_nice(task_t *p, long nice)
{
	unsigned long flags;
	prio_array_t *array;
	runqueue_t *rq;
	int old_prio, new_prio, delta;

	if (TASK_NICE(p) == nice || nice < -20 || nice > 19)
		return;
	/*
	 * We have to be careful, if called from sys_setpriority(),
	 * the task might be in the middle of scheduling on another CPU.
	 */
	rq = task_rq_lock(p, &flags);
	/*
	 * The RT priorities are set via setscheduler(), but we still
	 * allow the 'normal' nice value to be set - but as expected
	 * it wont have any effect on scheduling until the task is
	 * not SCHED_NORMAL:
	 */
	if (rt_task(p)) {
		p->static_prio = NICE_TO_PRIO(nice);
		goto out_unlock;
	}
	array = p->array;
	if (array)
		dequeue_task(p, array);

	old_prio = p->prio;
	new_prio = NICE_TO_PRIO(nice);
	delta = new_prio - old_prio;
	p->static_prio = NICE_TO_PRIO(nice);
	p->prio += delta;

	if (array) {
		enqueue_task(p, array);
		/*
		 * If the task increased its priority or is running and
		 * lowered its priority, then reschedule its CPU:
		 */
		if (delta < 0 || (delta > 0 && task_running(rq, p)))
			resched_task(rq->curr);
	}
out_unlock:
	task_rq_unlock(rq, &flags);
}

EXPORT_SYMBOL(set_user_nice);

#ifndef __alpha__

/*
 * sys_nice - change the priority of the current process.
 * @increment: priority increment
 *
 * sys_setpriority is a more generic, but much slower function that
 * does similar things.
 */
asmlinkage long sys_nice(int increment)
{
	int retval;
	long nice;

	/*
	 * Setpriority might change our priority at the same moment.
	 * We don't have to worry. Conceptually one call occurs first
	 * and we have a single winner.
	 */
	if (increment < 0) {
		if (!capable(CAP_SYS_NICE))
			return -EPERM;
		if (increment < -40)
			increment = -40;
	}
	if (increment > 40)
		increment = 40;

	nice = PRIO_TO_NICE(current->static_prio) + increment;
	if (nice < -20)
		nice = -20;
	if (nice > 19)
		nice = 19;

	retval = security_task_setnice(current, nice);
	if (retval)
		return retval;

	set_user_nice(current, nice);
	return 0;
}

#endif

/**
 * task_prio - return the priority value of a given task.
 * @p: the task in question.
 *
 * This is the priority value as seen by users in /proc.
 * RT tasks are offset by -200. Normal tasks are centered
 * around 0, value goes from -16 to +15.
 */
int task_prio(task_t *p)
{
	return p->prio - MAX_RT_PRIO;
}

/**
 * task_nice - return the nice value of a given task.
 * @p: the task in question.
 */
int task_nice(task_t *p)
{
	return TASK_NICE(p);
}

EXPORT_SYMBOL(task_nice);

/**
 * idle_cpu - is a given cpu idle currently?
 * @cpu: the processor in question.
 */
int idle_cpu(int cpu)
{
	return cpu_curr(cpu) == cpu_rq(cpu)->idle;
}

EXPORT_SYMBOL_GPL(idle_cpu);

/**
 * find_process_by_pid - find a process with a matching PID value.
 * @pid: the pid in question.
 */
static inline task_t *find_process_by_pid(pid_t pid)
{
	return pid ? find_task_by_pid(pid) : current;
}

/* Actually do priority change: must hold rq lock. */
static void __setscheduler(struct task_struct *p, int policy, int prio)
{
	BUG_ON(p->array);
	p->policy = policy;
	p->rt_priority = prio;
	if (policy != SCHED_NORMAL)
		p->prio = MAX_USER_RT_PRIO-1 - p->rt_priority;
	else
		p->prio = p->static_prio;
}

/*
 * setscheduler - change the scheduling policy and/or RT priority of a thread.
 */
static int setscheduler(pid_t pid, int policy, struct sched_param __user *param)
{
	struct sched_param lp;
	int retval = -EINVAL;
	int oldprio;
	prio_array_t *array;
	unsigned long flags;
	runqueue_t *rq;
	task_t *p;

	if (!param || pid < 0)
		goto out_nounlock;

	retval = -EFAULT;
	if (copy_from_user(&lp, param, sizeof(struct sched_param)))
		goto out_nounlock;

	/*
	 * We play safe to avoid deadlocks.
	 */
	read_lock_irq(&tasklist_lock);

	p = find_process_by_pid(pid);

	retval = -ESRCH;
	if (!p)
		goto out_unlock_tasklist;

	/*
	 * To be able to change p->policy safely, the apropriate
	 * runqueue lock must be held.
	 */
	rq = task_rq_lock(p, &flags);

	if (policy < 0)
		policy = p->policy;
	else {
		retval = -EINVAL;
		if (policy != SCHED_FIFO && policy != SCHED_RR &&
				policy != SCHED_NORMAL)
			goto out_unlock;
	}

	/*
	 * Valid priorities for SCHED_FIFO and SCHED_RR are
	 * 1..MAX_USER_RT_PRIO-1, valid priority for SCHED_NORMAL is 0.
	 */
	retval = -EINVAL;
	if (lp.sched_priority < 0 || lp.sched_priority > MAX_USER_RT_PRIO-1)
		goto out_unlock;
	if ((policy == SCHED_NORMAL) != (lp.sched_priority == 0))
		goto out_unlock;

	retval = -EPERM;
	if ((policy == SCHED_FIFO || policy == SCHED_RR) &&
	    !capable(CAP_SYS_NICE))
		goto out_unlock;
	if ((current->euid != p->euid) && (current->euid != p->uid) &&
	    !capable(CAP_SYS_NICE))
		goto out_unlock;

	retval = security_task_setscheduler(p, policy, &lp);
	if (retval)
		goto out_unlock;

	array = p->array;
	if (array)
		deactivate_task(p, task_rq(p));
	retval = 0;
	oldprio = p->prio;
	__setscheduler(p, policy, lp.sched_priority);
	if (array) {
		__activate_task(p, task_rq(p));
		/*
		 * Reschedule if we are currently running on this runqueue and
		 * our priority decreased, or if we are not currently running on
		 * this runqueue and our priority is higher than the current's
		 */
		if (task_running(rq, p)) {
			if (p->prio > oldprio)
				resched_task(rq->curr);
		} else if (p->prio < rq->curr->prio)
			resched_task(rq->curr);
	}

out_unlock:
	task_rq_unlock(rq, &flags);
out_unlock_tasklist:
	read_unlock_irq(&tasklist_lock);

out_nounlock:
	return retval;
}

/**
 * sys_sched_setscheduler - set/change the scheduler policy and RT priority
 * @pid: the pid in question.
 * @policy: new policy
 * @param: structure containing the new RT priority.
 */
asmlinkage long sys_sched_setscheduler(pid_t pid, int policy,
				       struct sched_param __user *param)
{
	return setscheduler(pid, policy, param);
}

/**
 * sys_sched_setparam - set/change the RT priority of a thread
 * @pid: the pid in question.
 * @param: structure containing the new RT priority.
 */
asmlinkage long sys_sched_setparam(pid_t pid, struct sched_param __user *param)
{
	return setscheduler(pid, -1, param);
}

/**
 * sys_sched_getscheduler - get the policy (scheduling class) of a thread
 * @pid: the pid in question.
 */
asmlinkage long sys_sched_getscheduler(pid_t pid)
{
	int retval = -EINVAL;
	task_t *p;

	if (pid < 0)
		goto out_nounlock;

	retval = -ESRCH;
	read_lock(&tasklist_lock);
	p = find_process_by_pid(pid);
	if (p) {
		retval = security_task_getscheduler(p);
		if (!retval)
			retval = p->policy;
	}
	read_unlock(&tasklist_lock);

out_nounlock:
	return retval;
}

/**
 * sys_sched_getscheduler - get the RT priority of a thread
 * @pid: the pid in question.
 * @param: structure containing the RT priority.
 */
asmlinkage long sys_sched_getparam(pid_t pid, struct sched_param __user *param)
{
	struct sched_param lp;
	int retval = -EINVAL;
	task_t *p;

	if (!param || pid < 0)
		goto out_nounlock;

	read_lock(&tasklist_lock);
	p = find_process_by_pid(pid);
	retval = -ESRCH;
	if (!p)
		goto out_unlock;

	retval = security_task_getscheduler(p);
	if (retval)
		goto out_unlock;

	lp.sched_priority = p->rt_priority;
	read_unlock(&tasklist_lock);

	/*
	 * This one might sleep, we cannot do it with a spinlock held ...
	 */
	retval = copy_to_user(param, &lp, sizeof(*param)) ? -EFAULT : 0;

out_nounlock:
	return retval;

out_unlock:
	read_unlock(&tasklist_lock);
	return retval;
}

/**
 * sys_sched_setaffinity - set the cpu affinity of a process
 * @pid: pid of the process
 * @len: length in bytes of the bitmask pointed to by user_mask_ptr
 * @user_mask_ptr: user-space pointer to the new cpu mask
 */
asmlinkage long sys_sched_setaffinity(pid_t pid, unsigned int len,
				      unsigned long __user *user_mask_ptr)
{
	cpumask_t new_mask;
	int retval;
	task_t *p;

	if (len < sizeof(new_mask))
		return -EINVAL;

	if (copy_from_user(&new_mask, user_mask_ptr, sizeof(new_mask)))
		return -EFAULT;

	lock_cpu_hotplug();
	read_lock(&tasklist_lock);

	p = find_process_by_pid(pid);
	if (!p) {
		read_unlock(&tasklist_lock);
		unlock_cpu_hotplug();
		return -ESRCH;
	}

	/*
	 * It is not safe to call set_cpus_allowed with the
	 * tasklist_lock held.  We will bump the task_struct's
	 * usage count and then drop tasklist_lock.
	 */
	get_task_struct(p);
	read_unlock(&tasklist_lock);

	retval = -EPERM;
	if ((current->euid != p->euid) && (current->euid != p->uid) &&
			!capable(CAP_SYS_NICE))
		goto out_unlock;

	retval = set_cpus_allowed(p, new_mask);

out_unlock:
	put_task_struct(p);
	unlock_cpu_hotplug();
	return retval;
}

/**
 * sys_sched_getaffinity - get the cpu affinity of a process
 * @pid: pid of the process
 * @len: length in bytes of the bitmask pointed to by user_mask_ptr
 * @user_mask_ptr: user-space pointer to hold the current cpu mask
 */
asmlinkage long sys_sched_getaffinity(pid_t pid, unsigned int len,
				      unsigned long __user *user_mask_ptr)
{
	unsigned int real_len;
	cpumask_t mask;
	int retval;
	task_t *p;

	real_len = sizeof(mask);
	if (len < real_len)
		return -EINVAL;

	read_lock(&tasklist_lock);

	retval = -ESRCH;
	p = find_process_by_pid(pid);
	if (!p)
		goto out_unlock;

	retval = 0;
	cpus_and(mask, p->cpus_allowed, cpu_possible_map);

out_unlock:
	read_unlock(&tasklist_lock);
	if (retval)
		return retval;
	if (copy_to_user(user_mask_ptr, &mask, real_len))
		return -EFAULT;
	return real_len;
}

/**
 * sys_sched_yield - yield the current processor to other threads.
 *
 * this function yields the current CPU by moving the calling thread
 * to the expired array. If there are no other threads running on this
 * CPU then this function will return.
 */
asmlinkage long sys_sched_yield(void)
{
	runqueue_t *rq = this_rq_lock();
	prio_array_t *array = current->array;
	prio_array_t *target = rq->expired;

	/*
	 * We implement yielding by moving the task into the expired
	 * queue.
	 *
	 * (special rule: RT tasks will just roundrobin in the active
	 *  array.)
	 */
	if (unlikely(rt_task(current)))
		target = rq->active;

	dequeue_task(current, array);
	enqueue_task(current, target);

	/*
	 * Since we are going to call schedule() anyway, there's
	 * no need to preempt:
	 */
	_raw_spin_unlock(&rq->lock);
	preempt_enable_no_resched();

	schedule();

	return 0;
}

void __sched __cond_resched(void)
{
	set_current_state(TASK_RUNNING);
	schedule();
}

EXPORT_SYMBOL(__cond_resched);

/**
 * yield - yield the current processor to other threads.
 *
 * this is a shortcut for kernel-space yielding - it marks the
 * thread runnable and calls sys_sched_yield().
 */
void __sched yield(void)
{
	set_current_state(TASK_RUNNING);
	sys_sched_yield();
}

EXPORT_SYMBOL(yield);

/*
 * This task is about to go to sleep on IO.  Increment rq->nr_iowait so
 * that process accounting knows that this is a task in IO wait state.
 *
 * But don't do that if it is a deliberate, throttling IO wait (this task
 * has set its backing_dev_info: the queue against which it should throttle)
 */
void __sched io_schedule(void)
{
	struct runqueue *rq = this_rq();

	atomic_inc(&rq->nr_iowait);
	schedule();
	atomic_dec(&rq->nr_iowait);
}

EXPORT_SYMBOL(io_schedule);

long __sched io_schedule_timeout(long timeout)
{
	struct runqueue *rq = this_rq();
	long ret;

	atomic_inc(&rq->nr_iowait);
	ret = schedule_timeout(timeout);
	atomic_dec(&rq->nr_iowait);
	return ret;
}

/**
 * sys_sched_get_priority_max - return maximum RT priority.
 * @policy: scheduling class.
 *
 * this syscall returns the maximum rt_priority that can be used
 * by a given scheduling class.
 */
asmlinkage long sys_sched_get_priority_max(int policy)
{
	int ret = -EINVAL;

	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
		ret = MAX_USER_RT_PRIO-1;
		break;
	case SCHED_NORMAL:
		ret = 0;
		break;
	}
	return ret;
}

/**
 * sys_sched_get_priority_min - return minimum RT priority.
 * @policy: scheduling class.
 *
 * this syscall returns the minimum rt_priority that can be used
 * by a given scheduling class.
 */
asmlinkage long sys_sched_get_priority_min(int policy)
{
	int ret = -EINVAL;

	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
		ret = 1;
		break;
	case SCHED_NORMAL:
		ret = 0;
	}
	return ret;
}

/**
 * sys_sched_rr_get_interval - return the default timeslice of a process.
 * @pid: pid of the process.
 * @interval: userspace pointer to the timeslice value.
 *
 * this syscall writes the default timeslice value of a given process
 * into the user-space timespec buffer. A value of '0' means infinity.
 */
asmlinkage
long sys_sched_rr_get_interval(pid_t pid, struct timespec __user *interval)
{
	int retval = -EINVAL;
	struct timespec t;
	task_t *p;

	if (pid < 0)
		goto out_nounlock;

	retval = -ESRCH;
	read_lock(&tasklist_lock);
	p = find_process_by_pid(pid);
	if (!p)
		goto out_unlock;

	retval = security_task_getscheduler(p);
	if (retval)
		goto out_unlock;

	jiffies_to_timespec(p->policy & SCHED_FIFO ?
				0 : task_timeslice(p), &t);
	read_unlock(&tasklist_lock);
	retval = copy_to_user(interval, &t, sizeof(t)) ? -EFAULT : 0;
out_nounlock:
	return retval;
out_unlock:
	read_unlock(&tasklist_lock);
	return retval;
}

static inline struct task_struct *eldest_child(struct task_struct *p)
{
	if (list_empty(&p->children)) return NULL;
	return list_entry(p->children.next,struct task_struct,sibling);
}

static inline struct task_struct *older_sibling(struct task_struct *p)
{
	if (p->sibling.prev==&p->parent->children) return NULL;
	return list_entry(p->sibling.prev,struct task_struct,sibling);
}

static inline struct task_struct *younger_sibling(struct task_struct *p)
{
	if (p->sibling.next==&p->parent->children) return NULL;
	return list_entry(p->sibling.next,struct task_struct,sibling);
}

static void show_task(task_t * p)
{
	task_t *relative;
	unsigned state;
	unsigned long free = 0;
	static const char *stat_nam[] = { "R", "S", "D", "T", "Z", "W" };

	printk("%-13.13s ", p->comm);
	state = p->state ? __ffs(p->state) + 1 : 0;
	if (state < ARRAY_SIZE(stat_nam))
		printk(stat_nam[state]);
	else
		printk("?");
#if (BITS_PER_LONG == 32)
	if (state == TASK_RUNNING)
		printk(" running ");
	else
		printk(" %08lX ", thread_saved_pc(p));
#else
	if (state == TASK_RUNNING)
		printk("  running task   ");
	else
		printk(" %016lx ", thread_saved_pc(p));
#endif
#ifdef CONFIG_DEBUG_STACK_USAGE
	{
		unsigned long * n = (unsigned long *) (p->thread_info+1);
		while (!*n)
			n++;
		free = (unsigned long) n - (unsigned long)(p->thread_info+1);
	}
#endif
	printk("%5lu %5d %6d ", free, p->pid, p->parent->pid);
	if ((relative = eldest_child(p)))
		printk("%5d ", relative->pid);
	else
		printk("      ");
	if ((relative = younger_sibling(p)))
		printk("%7d", relative->pid);
	else
		printk("       ");
	if ((relative = older_sibling(p)))
		printk(" %5d", relative->pid);
	else
		printk("      ");
	if (!p->mm)
		printk(" (L-TLB)\n");
	else
		printk(" (NOTLB)\n");

	if (state != TASK_RUNNING)
		show_stack(p, NULL);
}

void show_state(void)
{
	task_t *g, *p;

#if (BITS_PER_LONG == 32)
	printk("\n"
	       "                                               sibling\n");
	printk("  task             PC      pid father child younger older\n");
#else
	printk("\n"
	       "                                                       sibling\n");
	printk("  task                 PC          pid father child younger older\n");
#endif
	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		/*
		 * reset the NMI-timeout, listing all files on a slow
		 * console might take alot of time:
		 */
		touch_nmi_watchdog();
		show_task(p);
	} while_each_thread(g, p);

	read_unlock(&tasklist_lock);
}

void __init init_idle(task_t *idle, int cpu)
{
	runqueue_t *idle_rq = cpu_rq(cpu), *rq = cpu_rq(task_cpu(idle));
	unsigned long flags;

	local_irq_save(flags);
	double_rq_lock(idle_rq, rq);

	idle_rq->curr = idle_rq->idle = idle;
	deactivate_task(idle, rq);
	idle->array = NULL;
	idle->prio = MAX_PRIO;
	idle->state = TASK_RUNNING;
	set_task_cpu(idle, cpu);
	double_rq_unlock(idle_rq, rq);
	set_tsk_need_resched(idle);
	local_irq_restore(flags);

	/* Set the preempt count _outside_ the spinlocks! */
#ifdef CONFIG_PREEMPT
	idle->thread_info->preempt_count = (idle->lock_depth >= 0);
#else
	idle->thread_info->preempt_count = 0;
#endif
}

/*
 * In a system that switches off the HZ timer idle_cpu_mask
 * indicates which cpus entered this state. This is used
 * in the rcu update to wait only for active cpus. For system
 * which do not switch off the HZ timer idle_cpu_mask should
 * always be CPU_MASK_NONE.
 */
cpumask_t idle_cpu_mask = CPU_MASK_NONE;

#ifdef CONFIG_SMP
/*
 * This is how migration works:
 *
 * 1) we queue a migration_req_t structure in the source CPU's
 *    runqueue and wake up that CPU's migration thread.
 * 2) we down() the locked semaphore => thread blocks.
 * 3) migration thread wakes up (implicitly it forces the migrated
 *    thread off the CPU)
 * 4) it gets the migration request and checks whether the migrated
 *    task is still in the wrong runqueue.
 * 5) if it's in the wrong runqueue then the migration thread removes
 *    it and puts it into the right queue.
 * 6) migration thread up()s the semaphore.
 * 7) we wake up and the migration is done.
 */

/*
 * Change a given task's CPU affinity. Migrate the thread to a
 * proper CPU and schedule it away if the CPU it's executing on
 * is removed from the allowed bitmask.
 *
 * NOTE: the caller must have a valid reference to the task, the
 * task must not exit() & deallocate itself prematurely.  The
 * call is not atomic; no spinlocks may be held.
 */
int set_cpus_allowed(task_t *p, cpumask_t new_mask)
{
	unsigned long flags;
	int ret = 0;
	migration_req_t req;
	runqueue_t *rq;

	rq = task_rq_lock(p, &flags);
	if (any_online_cpu(new_mask) == NR_CPUS) {
		ret = -EINVAL;
		goto out;
	}

	p->cpus_allowed = new_mask;
	/* Can the task run on the task's current CPU? If so, we're done */
	if (cpu_isset(task_cpu(p), new_mask))
		goto out;

	if (migrate_task(p, any_online_cpu(new_mask), &req)) {
		/* Need help from migration thread: drop lock and wait. */
		task_rq_unlock(rq, &flags);
		wake_up_process(rq->migration_thread);
		wait_for_completion(&req.done);
		return 0;
	}
out:
	task_rq_unlock(rq, &flags);
	return ret;
}

EXPORT_SYMBOL_GPL(set_cpus_allowed);

/*
 * Move (not current) task off this cpu, onto dest cpu.  We're doing
 * this because either it can't run here any more (set_cpus_allowed()
 * away from this CPU, or CPU going down), or because we're
 * attempting to rebalance this task on exec (sched_balance_exec).
 *
 * So we race with normal scheduler movements, but that's OK, as long
 * as the task is no longer on this CPU.
 */
static void __migrate_task(struct task_struct *p, int dest_cpu)
{
	runqueue_t *rq_dest;

	rq_dest = cpu_rq(dest_cpu);

	double_rq_lock(this_rq(), rq_dest);
	/* Already moved. */
	if (task_cpu(p) != smp_processor_id())
		goto out;
	/* Affinity changed (again). */
	if (!cpu_isset(dest_cpu, p->cpus_allowed))
		goto out;

	set_task_cpu(p, dest_cpu);
	if (p->array) {
		deactivate_task(p, this_rq());
		activate_task(p, rq_dest);
		if (TASK_PREEMPTS_CURR(p, rq_dest))
			resched_task(rq_dest->curr);
	}
	p->timestamp = rq_dest->timestamp_last_tick;

out:
	double_rq_unlock(this_rq(), rq_dest);
}

/*
 * migration_thread - this is a highprio system thread that performs
 * thread migration by bumping thread off CPU then 'pushing' onto
 * another runqueue.
 */
static int migration_thread(void * data)
{
	runqueue_t *rq;
	int cpu = (long)data;

	rq = cpu_rq(cpu);
	BUG_ON(rq->migration_thread != current);

	while (!kthread_should_stop()) {
		struct list_head *head;
		migration_req_t *req;

		if (current->flags & PF_FREEZE)
			refrigerator(PF_FREEZE);

		spin_lock_irq(&rq->lock);
		if (rq->active_balance) {
			active_load_balance(rq, cpu);
			rq->active_balance = 0;
		}

		head = &rq->migration_queue;

		current->state = TASK_INTERRUPTIBLE;
		if (list_empty(head)) {
			spin_unlock_irq(&rq->lock);
			schedule();
			continue;
		}
		req = list_entry(head->next, migration_req_t, list);
		list_del_init(head->next);
		spin_unlock(&rq->lock);

		__migrate_task(req->task, req->dest_cpu);
		local_irq_enable();
		complete(&req->done);
	}
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
/* migrate_all_tasks - function to migrate all the tasks from the
 * current cpu caller must have already scheduled this to the target
 * cpu via set_cpus_allowed.  Machine is stopped.  */
void migrate_all_tasks(void)
{
	struct task_struct *tsk, *t;
	int dest_cpu, src_cpu;
	unsigned int node;

	/* We're nailed to this CPU. */
	src_cpu = smp_processor_id();

	/* Not required, but here for neatness. */
	write_lock(&tasklist_lock);

	/* watch out for per node tasks, let's stay on this node */
	node = cpu_to_node(src_cpu);

	do_each_thread(t, tsk) {
		cpumask_t mask;
		if (tsk == current)
			continue;

		if (task_cpu(tsk) != src_cpu)
			continue;

		/* Figure out where this task should go (attempting to
		 * keep it on-node), and check if it can be migrated
		 * as-is.  NOTE that kernel threads bound to more than
		 * one online cpu will be migrated. */
		mask = node_to_cpumask(node);
		cpus_and(mask, mask, tsk->cpus_allowed);
		dest_cpu = any_online_cpu(mask);
		if (dest_cpu == NR_CPUS)
			dest_cpu = any_online_cpu(tsk->cpus_allowed);
		if (dest_cpu == NR_CPUS) {
			cpus_clear(tsk->cpus_allowed);
			cpus_complement(tsk->cpus_allowed);
			dest_cpu = any_online_cpu(tsk->cpus_allowed);

			/* Don't tell them about moving exiting tasks
			   or kernel threads (both mm NULL), since
			   they never leave kernel. */
			if (tsk->mm && printk_ratelimit())
				printk(KERN_INFO "process %d (%s) no "
				       "longer affine to cpu%d\n",
				       tsk->pid, tsk->comm, src_cpu);
		}

		__migrate_task(tsk, dest_cpu);
	} while_each_thread(t, tsk);

	write_unlock(&tasklist_lock);
}
#endif /* CONFIG_HOTPLUG_CPU */

/*
 * migration_call - callback that gets triggered when a CPU is added.
 * Here we can start up the necessary migration thread for the new CPU.
 */
static int migration_call(struct notifier_block *nfb, unsigned long action,
			  void *hcpu)
{
	int cpu = (long)hcpu;
	struct task_struct *p;
	struct runqueue *rq;
	unsigned long flags;

	switch (action) {
	case CPU_UP_PREPARE:
		p = kthread_create(migration_thread, hcpu, "migration/%d",cpu);
		if (IS_ERR(p))
			return NOTIFY_BAD;
		kthread_bind(p, cpu);
		/* Must be high prio: stop_machine expects to yield to it. */
		rq = task_rq_lock(p, &flags);
		__setscheduler(p, SCHED_FIFO, MAX_RT_PRIO-1);
		task_rq_unlock(rq, &flags);
		cpu_rq(cpu)->migration_thread = p;
		break;
	case CPU_ONLINE:
		/* Strictly unneccessary, as first user will wake it. */
		wake_up_process(cpu_rq(cpu)->migration_thread);
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_UP_CANCELED:
		/* Unbind it from offline cpu so it can run.  Fall thru. */
		kthread_bind(cpu_rq(cpu)->migration_thread,smp_processor_id());
	case CPU_DEAD:
		kthread_stop(cpu_rq(cpu)->migration_thread);
		cpu_rq(cpu)->migration_thread = NULL;
 		BUG_ON(cpu_rq(cpu)->nr_running != 0);
 		break;
#endif
	}
	return NOTIFY_OK;
}

static struct notifier_block __devinitdata migration_notifier = {
	.notifier_call = migration_call,
};

int __init migration_init(void)
{
	void *cpu = (void *)(long)smp_processor_id();
	/* Start one for boot CPU. */
	migration_call(&migration_notifier, CPU_UP_PREPARE, cpu);
	migration_call(&migration_notifier, CPU_ONLINE, cpu);
	register_cpu_notifier(&migration_notifier);
	return 0;
}
#endif

/*
 * The 'big kernel lock'
 *
 * This spinlock is taken and released recursively by lock_kernel()
 * and unlock_kernel().  It is transparently dropped and reaquired
 * over schedule().  It is used to protect legacy code that hasn't
 * been migrated to a proper locking design yet.
 *
 * Don't use in new code.
 *
 * Note: spinlock debugging needs this even on !CONFIG_SMP.
 */
spinlock_t kernel_flag __cacheline_aligned_in_smp = SPIN_LOCK_UNLOCKED;
EXPORT_SYMBOL(kernel_flag);

#ifdef CONFIG_SMP
#ifdef ARCH_HAS_SCHED_DOMAIN
extern void __init arch_init_sched_domains(void);
#else
static struct sched_group sched_group_cpus[NR_CPUS];
#ifdef CONFIG_NUMA
static struct sched_group sched_group_nodes[MAX_NUMNODES];
DEFINE_PER_CPU(struct sched_domain, node_domains);
static void __init arch_init_sched_domains(void)
{
	int i;
	struct sched_group *first_node = NULL, *last_node = NULL;

	/* Set up domains */
	for_each_cpu_mask(i, cpu_online_map) {
		int node = cpu_to_node(i);
		cpumask_t nodemask = node_to_cpumask(node);
		struct sched_domain *node_domain = &per_cpu(node_domains, i);
		struct sched_domain *cpu_domain = cpu_sched_domain(i);

		*node_domain = SD_NODE_INIT;
		node_domain->span = cpu_online_map;

		*cpu_domain = SD_CPU_INIT;
		cpus_and(cpu_domain->span, nodemask, cpu_online_map);
		cpu_domain->parent = node_domain;
	}

	/* Set up groups */
	for (i = 0; i < MAX_NUMNODES; i++) {
		struct sched_group *first_cpu = NULL, *last_cpu = NULL;
		int j;
		cpumask_t nodemask;
		struct sched_group *node = &sched_group_nodes[i];
		cpumask_t tmp = node_to_cpumask(i);

		cpus_and(nodemask, tmp, cpu_online_map);

		if (cpus_empty(nodemask))
			continue;

		node->cpumask = nodemask;

		for_each_cpu_mask(j, node->cpumask) {
			struct sched_group *cpu = &sched_group_cpus[j];

			cpus_clear(cpu->cpumask);
			cpu_set(j, cpu->cpumask);

			if (!first_cpu)
				first_cpu = cpu;
			if (last_cpu)
				last_cpu->next = cpu;
			last_cpu = cpu;
		}
		last_cpu->next = first_cpu;

		if (!first_node)
			first_node = node;
		if (last_node)
			last_node->next = node;
		last_node = node;
	}
	last_node->next = first_node;

	mb();
	for_each_cpu_mask(i, cpu_online_map) {
		struct sched_domain *node_domain = &per_cpu(node_domains, i);
		struct sched_domain *cpu_domain = cpu_sched_domain(i);
		node_domain->groups = &sched_group_nodes[cpu_to_node(i)];
		cpu_domain->groups = &sched_group_cpus[i];
	}
}

#else /* CONFIG_NUMA */
static void __init arch_init_sched_domains(void)
{
	int i;
	struct sched_group *first_cpu = NULL, *last_cpu = NULL;

	/* Set up domains */
	for_each_cpu_mask(i, cpu_online_map) {
		struct sched_domain *cpu_domain = cpu_sched_domain(i);

		*cpu_domain = SD_CPU_INIT;
		cpu_domain->span = cpu_online_map;
	}

	/* Set up CPU groups */
	for_each_cpu_mask(i, cpu_online_map) {
		struct sched_group *cpu = &sched_group_cpus[i];

		cpus_clear(cpu->cpumask);
		cpu_set(i, cpu->cpumask);

		if (!first_cpu)
			first_cpu = cpu;
		if (last_cpu)
			last_cpu->next = cpu;
		last_cpu = cpu;
	}
	last_cpu->next = first_cpu;

	mb();
	for_each_cpu_mask(i, cpu_online_map) {
		struct sched_domain *cpu_domain = cpu_sched_domain(i);
		cpu_domain->groups = &sched_group_cpus[i];
	}
}

#endif /* CONFIG_NUMA */
#endif /* ARCH_HAS_SCHED_DOMAIN */

#undef SCHED_DOMAIN_DEBUG
#ifdef SCHED_DOMAIN_DEBUG
void sched_domain_debug(void)
{
	int i;

	for_each_cpu(i) {
		int level = 0;
		struct sched_domain *cpu_domain = cpu_sched_domain(i);

		printk(KERN_DEBUG "CPU%d: %s\n",
				i, (cpu_online(i) ? " online" : "offline"));

		do {
			int j;
			char str[NR_CPUS];
			struct sched_group *group = cpu_domain->groups;
			cpumask_t groupmask, tmp;

			cpumask_snprintf(str, NR_CPUS, cpu_domain->span);
			cpus_clear(groupmask);

			printk(KERN_DEBUG);
			for (j = 0; j < level + 1; j++)
				printk(" ");
			printk("domain %d: span %s\n", level, str);

			if (!cpu_isset(i, cpu_domain->span))
				printk(KERN_DEBUG "ERROR domain->span does not contain CPU%d\n", i);
			if (!cpu_isset(i, group->cpumask))
				printk(KERN_DEBUG "ERROR domain->groups does not contain CPU%d\n", i);

			printk(KERN_DEBUG);
			for (j = 0; j < level + 2; j++)
				printk(" ");
			printk("groups:");
			do {
				if (group == NULL) {
					printk(" ERROR: NULL");
					break;
				}

				if (cpus_weight(group->cpumask) == 0)
					printk(" ERROR empty group:");

				cpus_and(tmp, groupmask, group->cpumask);
				if (cpus_weight(tmp) > 0)
					printk(" ERROR repeated CPUs:");

				cpus_or(groupmask, groupmask, group->cpumask);

				cpumask_snprintf(str, NR_CPUS, group->cpumask);
				printk(" %s", str);

				group = group->next;
			} while (group != cpu_domain->groups);
			printk("\n");

			if (!cpus_equal(cpu_domain->span, groupmask))
				printk(KERN_DEBUG "ERROR groups don't span domain->span\n");

			level++;
			cpu_domain = cpu_domain->parent;

			if (cpu_domain) {
				cpus_and(tmp, groupmask, cpu_domain->span);
				if (!cpus_equal(tmp, groupmask))
					printk(KERN_DEBUG "ERROR parent span is not a superset of domain->span\n");
			}

		} while (cpu_domain);
	}
}
#else
#define sched_domain_debug() {}
#endif

void __init sched_init_smp(void)
{
	arch_init_sched_domains();
	sched_domain_debug();
}
#else
void __init sched_init_smp(void)
{
}
#endif /* CONFIG_SMP */

void __init sched_init(void)
{
	runqueue_t *rq;
	int i, j, k;

	for (i = 0; i < NR_CPUS; i++) {
		prio_array_t *array;
#ifdef CONFIG_SMP
		struct sched_domain *domain;
		domain = cpu_sched_domain(i);
		memset(domain, 0, sizeof(struct sched_domain));
#endif

		rq = cpu_rq(i);
		rq->active = rq->arrays;
		rq->expired = rq->arrays + 1;
		rq->best_expired_prio = MAX_PRIO;

		spin_lock_init(&rq->lock);
		INIT_LIST_HEAD(&rq->migration_queue);
		atomic_set(&rq->nr_iowait, 0);

		for (j = 0; j < 2; j++) {
			array = rq->arrays + j;
			for (k = 0; k < MAX_PRIO; k++) {
				INIT_LIST_HEAD(array->queue + k);
				__clear_bit(k, array->bitmap);
			}
			// delimiter for bitsearch
			__set_bit(MAX_PRIO, array->bitmap);
		}
	}
	/*
	 * We have to do a little magic to get the first
	 * thread right in SMP mode.
	 */
	rq = this_rq();
	rq->curr = current;
	rq->idle = current;
	set_task_cpu(current, smp_processor_id());
	wake_up_forked_process(current);

	init_timers();

	/*
	 * The boot idle thread does lazy MMU switching as well:
	 */
	atomic_inc(&init_mm.mm_count);
	enter_lazy_tlb(&init_mm, current);
}

#ifdef CONFIG_DEBUG_SPINLOCK_SLEEP
void __might_sleep(char *file, int line)
{
#if defined(in_atomic)
	static unsigned long prev_jiffy;	/* ratelimiting */

	if ((in_atomic() || irqs_disabled()) &&
	    system_state == SYSTEM_RUNNING) {
		if (time_before(jiffies, prev_jiffy + HZ) && prev_jiffy)
			return;
		prev_jiffy = jiffies;
		printk(KERN_ERR "Debug: sleeping function called from invalid"
				" context at %s:%d\n", file, line);
		printk("in_atomic():%d, irqs_disabled():%d\n",
			in_atomic(), irqs_disabled());
		dump_stack();
	}
#endif
}
EXPORT_SYMBOL(__might_sleep);
#endif


#if defined(CONFIG_SMP) && defined(CONFIG_PREEMPT)
/*
 * This could be a long-held lock.  If another CPU holds it for a long time,
 * and that CPU is not asked to reschedule then *this* CPU will spin on the
 * lock for a long time, even if *this* CPU is asked to reschedule.
 *
 * So what we do here, in the slow (contended) path is to spin on the lock by
 * hand while permitting preemption.
 *
 * Called inside preempt_disable().
 */
void __sched __preempt_spin_lock(spinlock_t *lock)
{
	if (preempt_count() > 1) {
		_raw_spin_lock(lock);
		return;
	}
	do {
		preempt_enable();
		while (spin_is_locked(lock))
			cpu_relax();
		preempt_disable();
	} while (!_raw_spin_trylock(lock));
}

EXPORT_SYMBOL(__preempt_spin_lock);

void __sched __preempt_write_lock(rwlock_t *lock)
{
	if (preempt_count() > 1) {
		_raw_write_lock(lock);
		return;
	}

	do {
		preempt_enable();
		while (rwlock_is_locked(lock))
			cpu_relax();
		preempt_disable();
	} while (!_raw_write_trylock(lock));
}

EXPORT_SYMBOL(__preempt_write_lock);
#endif /* defined(CONFIG_SMP) && defined(CONFIG_PREEMPT) */
