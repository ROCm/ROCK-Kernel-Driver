/*
 *  linux/kernel/sched.c
 *
 *  Kernel scheduler and related syscalls
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1996-12-23  Modified by Dave Grothe to fix bugs in semaphores and
 *              make semaphores SMP safe
 *  1998-11-19	Implemented schedule_timeout() and related stuff
 *		by Andrea Arcangeli
 *  1998-12-28  Implemented better SMP scheduling by Ingo Molnar
 */

#include <linux/mm.h>
#include <linux/nmi.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/highmem.h>
#include <linux/smp_lock.h>
#include <asm/mmu_context.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/kernel_stat.h>

/*
 * Priority of a process goes from 0 to 139. The 0-99
 * priority range is allocated to RT tasks, the 100-139
 * range is for SCHED_OTHER tasks. Priority values are
 * inverted: lower p->prio value means higher priority.
 */
#define MAX_RT_PRIO		100
#define MAX_PRIO		(MAX_RT_PRIO + 40)

/*
 * Convert user-nice values [ -20 ... 0 ... 19 ]
 * to static priority [ 100 ... 139 (MAX_PRIO-1) ],
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

/*
 * These are the 'tuning knobs' of the scheduler:
 *
 * Minimum timeslice is 10 msecs, default timeslice is 150 msecs,
 * maximum timeslice is 300 msecs. Timeslices get refilled after
 * they expire.
 */
#define MIN_TIMESLICE		( 10 * HZ / 1000)
#define MAX_TIMESLICE		(300 * HZ / 1000)
#define CHILD_PENALTY		95
#define PARENT_PENALTY		100
#define EXIT_WEIGHT		3
#define PRIO_BONUS_RATIO	25
#define INTERACTIVE_DELTA	2
#define MAX_SLEEP_AVG		(2*HZ)
#define STARVATION_LIMIT	(2*HZ)

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

#define SCALE(v1,v1_max,v2_max) \
	(v1) * (v2_max) / (v1_max)

#define DELTA(p) \
	(SCALE(TASK_NICE(p), 40, MAX_USER_PRIO*PRIO_BONUS_RATIO/100) + \
		INTERACTIVE_DELTA)

#define TASK_INTERACTIVE(p) \
	((p)->prio <= (p)->static_prio - DELTA(p))

/*
 * TASK_TIMESLICE scales user-nice values [ -20 ... 19 ]
 * to time slice values.
 *
 * The higher a process's priority, the bigger timeslices
 * it gets during one round of execution. But even the lowest
 * priority process gets MIN_TIMESLICE worth of execution time.
 */

#define TASK_TIMESLICE(p) (MIN_TIMESLICE + \
	((MAX_TIMESLICE - MIN_TIMESLICE) * (MAX_PRIO-1-(p)->static_prio)/39))

/*
 * These are the runqueue data structures:
 */

#define BITMAP_SIZE ((((MAX_PRIO+1+7)/8)+sizeof(long)-1)/sizeof(long))

typedef struct runqueue runqueue_t;

struct prio_array {
	int nr_active;
	unsigned long bitmap[BITMAP_SIZE];
	list_t queue[MAX_PRIO];
};

/*
 * This is the main, per-CPU runqueue data structure.
 *
 * Locking rule: those places that want to lock multiple runqueues
 * (such as the load balancing or the process migration code), lock
 * acquire operations must be ordered by ascending &runqueue.
 */
struct runqueue {
	spinlock_t lock;
	unsigned long nr_running, nr_switches, expired_timestamp;
	task_t *curr, *idle;
	prio_array_t *active, *expired, arrays[2];
	int prev_nr_running[NR_CPUS];
	task_t *migration_thread;
	list_t migration_queue;
} ____cacheline_aligned;

static struct runqueue runqueues[NR_CPUS] __cacheline_aligned;

#define cpu_rq(cpu)		(runqueues + (cpu))
#define this_rq()		cpu_rq(smp_processor_id())
#define task_rq(p)		cpu_rq((p)->thread_info->cpu)
#define cpu_curr(cpu)		(cpu_rq(cpu)->curr)
#define rt_task(p)		((p)->prio < MAX_RT_PRIO)

static inline runqueue_t *task_rq_lock(task_t *p, unsigned long *flags)
{
	struct runqueue *rq;

repeat_lock_task:
	preempt_disable();
	rq = task_rq(p);
	spin_lock_irqsave(&rq->lock, *flags);
	if (unlikely(rq != task_rq(p))) {
		spin_unlock_irqrestore(&rq->lock, *flags);
		preempt_enable();
		goto repeat_lock_task;
	}
	return rq;
}

static inline void task_rq_unlock(runqueue_t *rq, unsigned long *flags)
{
	spin_unlock_irqrestore(&rq->lock, *flags);
	preempt_enable();
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

static inline int effective_prio(task_t *p)
{
	int bonus, prio;

	/*
	 * Here we scale the actual sleep average [0 .... MAX_SLEEP_AVG]
	 * into the -5 ... 0 ... +5 bonus/penalty range.
	 *
	 * We use 25% of the full 0...39 priority range so that:
	 *
	 * 1) nice +19 interactive tasks do not preempt nice 0 CPU hogs.
	 * 2) nice -20 CPU hogs do not get preempted by nice 0 tasks.
	 *
	 * Both properties are important to certain workloads.
	 */
	bonus = MAX_USER_PRIO*PRIO_BONUS_RATIO*p->sleep_avg/MAX_SLEEP_AVG/100 -
			MAX_USER_PRIO*PRIO_BONUS_RATIO/100/2;

	prio = p->static_prio - bonus;
	if (prio < MAX_RT_PRIO)
		prio = MAX_RT_PRIO;
	if (prio > MAX_PRIO-1)
		prio = MAX_PRIO-1;
	return prio;
}

static inline void activate_task(task_t *p, runqueue_t *rq)
{
	unsigned long sleep_time = jiffies - p->sleep_timestamp;
	prio_array_t *array = rq->active;

	if (!rt_task(p) && sleep_time) {
		/*
		 * This code gives a bonus to interactive tasks. We update
		 * an 'average sleep time' value here, based on
		 * sleep_timestamp. The more time a task spends sleeping,
		 * the higher the average gets - and the higher the priority
		 * boost gets as well.
		 */
		p->sleep_avg += sleep_time;
		if (p->sleep_avg > MAX_SLEEP_AVG)
			p->sleep_avg = MAX_SLEEP_AVG;
		p->prio = effective_prio(p);
	}
	enqueue_task(p, array);
	rq->nr_running++;
}

static inline void deactivate_task(struct task_struct *p, runqueue_t *rq)
{
	rq->nr_running--;
	dequeue_task(p, p->array);
	p->array = NULL;
}

static inline void resched_task(task_t *p)
{
#ifdef CONFIG_SMP
	int need_resched, nrpolling;

	preempt_disable();
	/* minimise the chance of sending an interrupt to poll_idle() */
	nrpolling = test_tsk_thread_flag(p,TIF_POLLING_NRFLAG);
	need_resched = test_and_set_tsk_thread_flag(p,TIF_NEED_RESCHED);
	nrpolling |= test_tsk_thread_flag(p,TIF_POLLING_NRFLAG);

	if (!need_resched && !nrpolling && (p->thread_info->cpu != smp_processor_id()))
		smp_send_reschedule(p->thread_info->cpu);
	preempt_enable();
#else
	set_tsk_need_resched(p);
#endif
}

#ifdef CONFIG_SMP

/*
 * Wait for a process to unschedule. This is used by the exit() and
 * ptrace() code.
 */
void wait_task_inactive(task_t * p)
{
	unsigned long flags;
	runqueue_t *rq;

repeat:
	preempt_disable();
	rq = task_rq(p);
	while (unlikely(rq->curr == p)) {
		cpu_relax();
		barrier();
	}
	rq = task_rq_lock(p, &flags);
	if (unlikely(rq->curr == p)) {
		task_rq_unlock(rq, &flags);
		preempt_enable();
		goto repeat;
	}
	task_rq_unlock(rq, &flags);
	preempt_enable();
}

/*
 * Kick the remote CPU if the task is running currently,
 * this code is used by the signal code to signal tasks
 * which are in user-mode as quickly as possible.
 *
 * (Note that we do this lockless - if the task does anything
 * while the message is in flight then it will notice the
 * sigpending condition anyway.)
 */
void kick_if_running(task_t * p)
{
	if (p == task_rq(p)->curr)
		resched_task(p);
}
#endif

/*
 * Wake up a process. Put it on the run-queue if it's not
 * already there.  The "current" process is always on the
 * run-queue (except when the actual re-schedule is in
 * progress), and as such you're allowed to do the simpler
 * "current->state = TASK_RUNNING" to mark yourself runnable
 * without the overhead of this.
 */
static int try_to_wake_up(task_t * p)
{
	unsigned long flags;
	int success = 0;
	runqueue_t *rq;

	rq = task_rq_lock(p, &flags);
	p->state = TASK_RUNNING;
	if (!p->array) {
		activate_task(p, rq);
		if (p->prio < rq->curr->prio)
			resched_task(rq->curr);
		success = 1;
	}
	task_rq_unlock(rq, &flags);
	return success;
}

int wake_up_process(task_t * p)
{
	return try_to_wake_up(p);
}

void wake_up_forked_process(task_t * p)
{
	runqueue_t *rq;

	preempt_disable();
	rq = this_rq();
	spin_lock_irq(&rq->lock);

	p->state = TASK_RUNNING;
	if (!rt_task(p)) {
		/*
		 * We decrease the sleep average of forking parents
		 * and children as well, to keep max-interactive tasks
		 * from forking tasks that are max-interactive.
		 */
		current->sleep_avg = current->sleep_avg * PARENT_PENALTY / 100;
		p->sleep_avg = p->sleep_avg * CHILD_PENALTY / 100;
		p->prio = effective_prio(p);
	}
	p->thread_info->cpu = smp_processor_id();
	activate_task(p, rq);

	spin_unlock_irq(&rq->lock);
	preempt_enable();
}

/*
 * Potentially available exiting-child timeslices are
 * retrieved here - this way the parent does not get
 * penalized for creating too many processes.
 *
 * (this cannot be used to 'generate' timeslices
 * artificially, because any timeslice recovered here
 * was given away by the parent in the first place.)
 */
void sched_exit(task_t * p)
{
	__cli();
	current->time_slice += p->time_slice;
	if (unlikely(current->time_slice > MAX_TIMESLICE))
		current->time_slice = MAX_TIMESLICE;
	__sti();
	/*
	 * If the child was a (relative-) CPU hog then decrease
	 * the sleep_avg of the parent as well.
	 */
	if (p->sleep_avg < current->sleep_avg)
		current->sleep_avg = (current->sleep_avg * EXIT_WEIGHT +
			p->sleep_avg) / (EXIT_WEIGHT + 1);
}

#if CONFIG_SMP || CONFIG_PREEMPT
asmlinkage void schedule_tail(void)
{
	spin_unlock_irq(&this_rq()->lock);
}
#endif

static inline void context_switch(task_t *prev, task_t *next)
{
	struct mm_struct *mm = next->mm;
	struct mm_struct *oldmm = prev->active_mm;

	prepare_to_switch();

	if (unlikely(!mm)) {
		next->active_mm = oldmm;
		atomic_inc(&oldmm->mm_count);
		enter_lazy_tlb(oldmm, next, smp_processor_id());
	} else
		switch_mm(oldmm, mm, next, smp_processor_id());

	if (unlikely(!prev->mm)) {
		prev->active_mm = NULL;
		mmdrop(oldmm);
	}

	/* Here we just switch the register state and the stack. */
	switch_to(prev, next);
}

unsigned long nr_running(void)
{
	unsigned long i, sum = 0;

	for (i = 0; i < smp_num_cpus; i++)
		sum += cpu_rq(cpu_logical_map(i))->nr_running;

	return sum;
}

unsigned long nr_context_switches(void)
{
	unsigned long i, sum = 0;

	for (i = 0; i < smp_num_cpus; i++)
		sum += cpu_rq(cpu_logical_map(i))->nr_switches;

	return sum;
}

#if CONFIG_SMP
/*
 * Lock the busiest runqueue as well, this_rq is locked already.
 * Recalculate nr_running if we have to drop the runqueue lock.
 */
static inline unsigned int double_lock_balance(runqueue_t *this_rq,
	runqueue_t *busiest, int this_cpu, int idle, unsigned int nr_running)
{
	if (unlikely(!spin_trylock(&busiest->lock))) {
		if (busiest < this_rq) {
			spin_unlock(&this_rq->lock);
			spin_lock(&busiest->lock);
			spin_lock(&this_rq->lock);
			/* Need to recalculate nr_running */
			if (idle || (this_rq->nr_running > this_rq->prev_nr_running[this_cpu]))
				nr_running = this_rq->nr_running;
			else
				nr_running = this_rq->prev_nr_running[this_cpu];
		} else
			spin_lock(&busiest->lock);
	}
	return nr_running;
}

/*
 * Current runqueue is empty, or rebalance tick: if there is an
 * inbalance (current runqueue is too short) then pull from
 * busiest runqueue(s).
 *
 * We call this with the current runqueue locked,
 * irqs disabled.
 */
static void load_balance(runqueue_t *this_rq, int idle)
{
	int imbalance, nr_running, load, max_load,
		idx, i, this_cpu = smp_processor_id();
	task_t *next = this_rq->idle, *tmp;
	runqueue_t *busiest, *rq_src;
	prio_array_t *array;
	list_t *head, *curr;

	/*
	 * We search all runqueues to find the most busy one.
	 * We do this lockless to reduce cache-bouncing overhead,
	 * we re-check the 'best' source CPU later on again, with
	 * the lock held.
	 *
	 * We fend off statistical fluctuations in runqueue lengths by
	 * saving the runqueue length during the previous load-balancing
	 * operation and using the smaller one the current and saved lengths.
	 * If a runqueue is long enough for a longer amount of time then
	 * we recognize it and pull tasks from it.
	 *
	 * The 'current runqueue length' is a statistical maximum variable,
	 * for that one we take the longer one - to avoid fluctuations in
	 * the other direction. So for a load-balance to happen it needs
	 * stable long runqueue on the target CPU and stable short runqueue
	 * on the local runqueue.
	 *
	 * We make an exception if this CPU is about to become idle - in
	 * that case we are less picky about moving a task across CPUs and
	 * take what can be taken.
	 */
	if (idle || (this_rq->nr_running > this_rq->prev_nr_running[this_cpu]))
		nr_running = this_rq->nr_running;
	else
		nr_running = this_rq->prev_nr_running[this_cpu];

	busiest = NULL;
	max_load = 1;
	for (i = 0; i < smp_num_cpus; i++) {
		rq_src = cpu_rq(cpu_logical_map(i));
		if (idle || (rq_src->nr_running < this_rq->prev_nr_running[i]))
			load = rq_src->nr_running;
		else
			load = this_rq->prev_nr_running[i];
		this_rq->prev_nr_running[i] = rq_src->nr_running;

		if ((load > max_load) && (rq_src != this_rq)) {
			busiest = rq_src;
			max_load = load;
		}
	}

	if (likely(!busiest))
		return;

	imbalance = (max_load - nr_running) / 2;

	/* It needs an at least ~25% imbalance to trigger balancing. */
	if (!idle && (imbalance < (max_load + 3)/4))
		return;

	nr_running = double_lock_balance(this_rq, busiest, this_cpu, idle, nr_running);
	/*
	 * Make sure nothing changed since we checked the
	 * runqueue length.
	 */
	if (busiest->nr_running <= nr_running + 1)
		goto out_unlock;

	/*
	 * We first consider expired tasks. Those will likely not be
	 * executed in the near future, and they are most likely to
	 * be cache-cold, thus switching CPUs has the least effect
	 * on them.
	 */
	if (busiest->expired->nr_active)
		array = busiest->expired;
	else
		array = busiest->active;

new_array:
	/* Start searching at priority 0: */
	idx = 0;
skip_bitmap:
	if (!idx)
		idx = sched_find_first_bit(array->bitmap);
	else
		idx = find_next_bit(array->bitmap, MAX_PRIO, idx);
	if (idx == MAX_PRIO) {
		if (array == busiest->expired) {
			array = busiest->active;
			goto new_array;
		}
		goto out_unlock;
	}

	head = array->queue + idx;
	curr = head->prev;
skip_queue:
	tmp = list_entry(curr, task_t, run_list);

	/*
	 * We do not migrate tasks that are:
	 * 1) running (obviously), or
	 * 2) cannot be migrated to this CPU due to cpus_allowed, or
	 * 3) are cache-hot on their current CPU.
	 */

#define CAN_MIGRATE_TASK(p,rq,this_cpu)					\
	((jiffies - (p)->sleep_timestamp > cache_decay_ticks) &&	\
		((p) != (rq)->curr) &&					\
			(tmp->cpus_allowed & (1 << (this_cpu))))

	if (!CAN_MIGRATE_TASK(tmp, busiest, this_cpu)) {
		curr = curr->next;
		if (curr != head)
			goto skip_queue;
		idx++;
		goto skip_bitmap;
	}
	next = tmp;
	/*
	 * take the task out of the other runqueue and
	 * put it into this one:
	 */
	dequeue_task(next, array);
	busiest->nr_running--;
	next->thread_info->cpu = this_cpu;
	this_rq->nr_running++;
	enqueue_task(next, this_rq->active);
	if (next->prio < current->prio)
		set_need_resched();
	if (!idle && --imbalance) {
		if (array == busiest->expired) {
			array = busiest->active;
			goto new_array;
		}
	}
out_unlock:
	spin_unlock(&busiest->lock);
}

/*
 * One of the idle_cpu_tick() or the busy_cpu_tick() function will
 * gets called every timer tick, on every CPU. Our balancing action
 * frequency and balancing agressivity depends on whether the CPU is
 * idle or not.
 *
 * busy-rebalance every 250 msecs. idle-rebalance every 1 msec. (or on
 * systems with HZ=100, every 10 msecs.)
 */
#define BUSY_REBALANCE_TICK (HZ/4 ?: 1)
#define IDLE_REBALANCE_TICK (HZ/1000 ?: 1)

static inline void idle_tick(void)
{
	if (jiffies % IDLE_REBALANCE_TICK)
		return;
	spin_lock(&this_rq()->lock);
	load_balance(this_rq(), 1);
	spin_unlock(&this_rq()->lock);
}

#endif

/*
 * We place interactive tasks back into the active array, if possible.
 *
 * To guarantee that this does not starve expired tasks we ignore the
 * interactivity of a task if the first expired task had to wait more
 * than a 'reasonable' amount of time. This deadline timeout is
 * load-dependent, as the frequency of array switched decreases with
 * increasing number of running tasks:
 */
#define EXPIRED_STARVING(rq) \
		((rq)->expired_timestamp && \
		(jiffies - (rq)->expired_timestamp >= \
			STARVATION_LIMIT * ((rq)->nr_running) + 1))

/*
 * This function gets called by the timer code, with HZ frequency.
 * We call it with interrupts disabled.
 */
void scheduler_tick(int user_tick, int system)
{
	int cpu = smp_processor_id();
	runqueue_t *rq = this_rq();
	task_t *p = current;

	if (p == rq->idle) {
		if (local_bh_count(cpu) || local_irq_count(cpu) > 1)
			kstat.per_cpu_system[cpu] += system;
#if CONFIG_SMP
		idle_tick();
#endif
		return;
	}
	if (TASK_NICE(p) > 0)
		kstat.per_cpu_nice[cpu] += user_tick;
	else
		kstat.per_cpu_user[cpu] += user_tick;
	kstat.per_cpu_system[cpu] += system;

	/* Task might have expired already, but not scheduled off yet */
	if (p->array != rq->active) {
		set_tsk_need_resched(p);
		return;
	}
	spin_lock(&rq->lock);
	if (unlikely(rt_task(p))) {
		/*
		 * RR tasks need a special form of timeslice management.
		 * FIFO tasks have no timeslices.
		 */
		if ((p->policy == SCHED_RR) && !--p->time_slice) {
			p->time_slice = TASK_TIMESLICE(p);
			set_tsk_need_resched(p);

			/* put it at the end of the queue: */
			dequeue_task(p, rq->active);
			enqueue_task(p, rq->active);
		}
		goto out;
	}
	/*
	 * The task was running during this tick - update the
	 * time slice counter and the sleep average. Note: we
	 * do not update a process's priority until it either
	 * goes to sleep or uses up its timeslice. This makes
	 * it possible for interactive tasks to use up their
	 * timeslices at their highest priority levels.
	 */
	if (p->sleep_avg)
		p->sleep_avg--;
	if (!--p->time_slice) {
		dequeue_task(p, rq->active);
		set_tsk_need_resched(p);
		p->prio = effective_prio(p);
		p->time_slice = TASK_TIMESLICE(p);

		if (!TASK_INTERACTIVE(p) || EXPIRED_STARVING(rq)) {
			if (!rq->expired_timestamp)
				rq->expired_timestamp = jiffies;
			enqueue_task(p, rq->expired);
		} else
			enqueue_task(p, rq->active);
	}
out:
#if CONFIG_SMP
	if (!(jiffies % BUSY_REBALANCE_TICK))
		load_balance(rq, 0);
#endif
	spin_unlock(&rq->lock);
}

void scheduling_functions_start_here(void) { }

/*
 * 'schedule()' is the main scheduler function.
 */
asmlinkage void schedule(void)
{
	task_t *prev, *next;
	runqueue_t *rq;
	prio_array_t *array;
	list_t *queue;
	int idx;

	if (unlikely(in_interrupt()))
		BUG();
#if CONFIG_DEBUG_HIGHMEM
	check_highmem_ptes();
#endif
need_resched:
	preempt_disable();
	prev = current;
	rq = this_rq();

	release_kernel_lock(prev, smp_processor_id());
	prev->sleep_timestamp = jiffies;
	spin_lock_irq(&rq->lock);

	switch (prev->state) {
	case TASK_INTERRUPTIBLE:
		if (unlikely(signal_pending(prev))) {
			prev->state = TASK_RUNNING;
			break;
		}
	default:
		deactivate_task(prev, rq);
	case TASK_RUNNING:
		;
	}
#if CONFIG_SMP
pick_next_task:
#endif
	if (unlikely(!rq->nr_running)) {
#if CONFIG_SMP
		load_balance(rq, 1);
		if (rq->nr_running)
			goto pick_next_task;
#endif
		next = rq->idle;
		rq->expired_timestamp = 0;
		goto switch_tasks;
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
	}

	idx = sched_find_first_bit(array->bitmap);
	queue = array->queue + idx;
	next = list_entry(queue->next, task_t, run_list);

switch_tasks:
	prefetch(next);
	clear_tsk_need_resched(prev);

	if (likely(prev != next)) {
		rq->nr_switches++;
		rq->curr = next;
		context_switch(prev, next);
		/*
		 * The runqueue pointer might be from another CPU
		 * if the new task was last running on a different
		 * CPU - thus re-load it.
		 */
		barrier();
		rq = this_rq();
	}
	spin_unlock_irq(&rq->lock);

	reacquire_kernel_lock(current);
	preempt_enable_no_resched();
	if (test_thread_flag(TIF_NEED_RESCHED))
		goto need_resched;
	return;
}

#ifdef CONFIG_PREEMPT
/*
 * this is is the entry point to schedule() from in-kernel preemption.
 */
asmlinkage void preempt_schedule(void)
{
	if (unlikely(preempt_get_count()))
		return;
	current->state = TASK_RUNNING;
	schedule();
}
#endif /* CONFIG_PREEMPT */

/*
 * The core wakeup function.  Non-exclusive wakeups (nr_exclusive == 0) just
 * wake everything up.  If it's an exclusive wakeup (nr_exclusive == small +ve
 * number) then we wake all the non-exclusive tasks and one exclusive task.
 *
 * There are circumstances in which we can try to wake a task which has already
 * started to run but is not in state TASK_RUNNING.  try_to_wake_up() returns
 * zero in this (rare) case, and we handle it by continuing to scan the queue.
 */
static inline void __wake_up_common(wait_queue_head_t *q, unsigned int mode, int nr_exclusive)
{
	struct list_head *tmp;
	unsigned int state;
	wait_queue_t *curr;
	task_t *p;

	list_for_each(tmp, &q->task_list) {
		curr = list_entry(tmp, wait_queue_t, task_list);
		p = curr->task;
		state = p->state;
		if ((state & mode) && try_to_wake_up(p) &&
			((curr->flags & WQ_FLAG_EXCLUSIVE) && !--nr_exclusive))
				break;
	}
}

void __wake_up(wait_queue_head_t *q, unsigned int mode, int nr_exclusive)
{
	unsigned long flags;

	if (unlikely(!q))
		return;

	wq_read_lock_irqsave(&q->lock, flags);
	__wake_up_common(q, mode, nr_exclusive);
	wq_read_unlock_irqrestore(&q->lock, flags);
}

void complete(struct completion *x)
{
	unsigned long flags;

	spin_lock_irqsave(&x->wait.lock, flags);
	x->done++;
	__wake_up_common(&x->wait, TASK_UNINTERRUPTIBLE | TASK_INTERRUPTIBLE, 1);
	spin_unlock_irqrestore(&x->wait.lock, flags);
}

void wait_for_completion(struct completion *x)
{
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

#define	SLEEP_ON_VAR				\
	unsigned long flags;			\
	wait_queue_t wait;			\
	init_waitqueue_entry(&wait, current);

#define	SLEEP_ON_HEAD					\
	wq_write_lock_irqsave(&q->lock,flags);		\
	__add_wait_queue(q, &wait);			\
	wq_write_unlock(&q->lock);

#define	SLEEP_ON_TAIL						\
	wq_write_lock_irq(&q->lock);				\
	__remove_wait_queue(q, &wait);				\
	wq_write_unlock_irqrestore(&q->lock,flags);

void interruptible_sleep_on(wait_queue_head_t *q)
{
	SLEEP_ON_VAR

	current->state = TASK_INTERRUPTIBLE;

	SLEEP_ON_HEAD
	schedule();
	SLEEP_ON_TAIL
}

long interruptible_sleep_on_timeout(wait_queue_head_t *q, long timeout)
{
	SLEEP_ON_VAR

	current->state = TASK_INTERRUPTIBLE;

	SLEEP_ON_HEAD
	timeout = schedule_timeout(timeout);
	SLEEP_ON_TAIL

	return timeout;
}

void sleep_on(wait_queue_head_t *q)
{
	SLEEP_ON_VAR
	
	current->state = TASK_UNINTERRUPTIBLE;

	SLEEP_ON_HEAD
	schedule();
	SLEEP_ON_TAIL
}

long sleep_on_timeout(wait_queue_head_t *q, long timeout)
{
	SLEEP_ON_VAR
	
	current->state = TASK_UNINTERRUPTIBLE;

	SLEEP_ON_HEAD
	timeout = schedule_timeout(timeout);
	SLEEP_ON_TAIL

	return timeout;
}

void scheduling_functions_end_here(void) { }

void set_user_nice(task_t *p, long nice)
{
	unsigned long flags;
	prio_array_t *array;
	runqueue_t *rq;

	if (TASK_NICE(p) == nice || nice < -20 || nice > 19)
		return;
	/*
	 * We have to be careful, if called from sys_setpriority(),
	 * the task might be in the middle of scheduling on another CPU.
	 */
	rq = task_rq_lock(p, &flags);
	if (rt_task(p)) {
		p->static_prio = NICE_TO_PRIO(nice);
		goto out_unlock;
	}
	array = p->array;
	if (array)
		dequeue_task(p, array);
	p->static_prio = NICE_TO_PRIO(nice);
	p->prio = NICE_TO_PRIO(nice);
	if (array) {
		enqueue_task(p, array);
		/*
		 * If the task is running and lowered its priority,
		 * or increased its priority then reschedule its CPU:
		 */
		if ((NICE_TO_PRIO(nice) < p->static_prio) || (p == rq->curr))
			resched_task(rq->curr);
	}
out_unlock:
	task_rq_unlock(rq, &flags);
}

#ifndef __alpha__

/*
 * This has been replaced by sys_setpriority.  Maybe it should be
 * moved into the arch dependent tree for those ports that require
 * it for backward compatibility?
 */

asmlinkage long sys_nice(int increment)
{
	long nice;

	/*
	 *	Setpriority might change our priority at the same moment.
	 *	We don't have to worry. Conceptually one call occurs first
	 *	and we have a single winner.
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
	set_user_nice(current, nice);
	return 0;
}

#endif

/*
 * This is the priority value as seen by users in /proc
 *
 * RT tasks are offset by -200. Normal tasks are centered
 * around 0, value goes from -16 to +15.
 */
int task_prio(task_t *p)
{
	return p->prio - 100;
}

int task_nice(task_t *p)
{
	return TASK_NICE(p);
}

int idle_cpu(int cpu)
{
	return cpu_curr(cpu) == cpu_rq(cpu)->idle;
}

static inline task_t *find_process_by_pid(pid_t pid)
{
	return pid ? find_task_by_pid(pid) : current;
}

static int setscheduler(pid_t pid, int policy, struct sched_param *param)
{
	struct sched_param lp;
	prio_array_t *array;
	unsigned long flags;
	runqueue_t *rq;
	int retval;
	task_t *p;

	retval = -EINVAL;
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
				policy != SCHED_OTHER)
			goto out_unlock;
	}
	
	/*
	 * Valid priorities for SCHED_FIFO and SCHED_RR are 1..99, valid
	 * priority for SCHED_OTHER is 0.
	 */
	retval = -EINVAL;
	if (lp.sched_priority < 0 || lp.sched_priority > 99)
		goto out_unlock;
	if ((policy == SCHED_OTHER) != (lp.sched_priority == 0))
		goto out_unlock;

	retval = -EPERM;
	if ((policy == SCHED_FIFO || policy == SCHED_RR) &&
	    !capable(CAP_SYS_NICE))
		goto out_unlock;
	if ((current->euid != p->euid) && (current->euid != p->uid) &&
	    !capable(CAP_SYS_NICE))
		goto out_unlock;

	array = p->array;
	if (array)
		deactivate_task(p, task_rq(p));
	retval = 0;
	p->policy = policy;
	p->rt_priority = lp.sched_priority;
	if (policy != SCHED_OTHER)
		p->prio = 99 - p->rt_priority;
	else
		p->prio = p->static_prio;
	if (array)
		activate_task(p, task_rq(p));

out_unlock:
	task_rq_unlock(rq, &flags);
out_unlock_tasklist:
	read_unlock_irq(&tasklist_lock);

out_nounlock:
	return retval;
}

asmlinkage long sys_sched_setscheduler(pid_t pid, int policy,
				      struct sched_param *param)
{
	return setscheduler(pid, policy, param);
}

asmlinkage long sys_sched_setparam(pid_t pid, struct sched_param *param)
{
	return setscheduler(pid, -1, param);
}

asmlinkage long sys_sched_getscheduler(pid_t pid)
{
	task_t *p;
	int retval;

	retval = -EINVAL;
	if (pid < 0)
		goto out_nounlock;

	retval = -ESRCH;
	read_lock(&tasklist_lock);
	p = find_process_by_pid(pid);
	if (p)
		retval = p->policy;
	read_unlock(&tasklist_lock);

out_nounlock:
	return retval;
}

asmlinkage long sys_sched_getparam(pid_t pid, struct sched_param *param)
{
	task_t *p;
	struct sched_param lp;
	int retval;

	retval = -EINVAL;
	if (!param || pid < 0)
		goto out_nounlock;

	read_lock(&tasklist_lock);
	p = find_process_by_pid(pid);
	retval = -ESRCH;
	if (!p)
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

asmlinkage long sys_sched_yield(void)
{
	runqueue_t *rq;
	prio_array_t *array;

	preempt_disable();
	rq = this_rq();

	/*
	 * Decrease the yielding task's priority by one, to avoid
	 * livelocks. This priority loss is temporary, it's recovered
	 * once the current timeslice expires.
	 *
	 * If priority is already MAX_PRIO-1 then we still
	 * roundrobin the task within the runlist.
	 */
	spin_lock_irq(&rq->lock);
	array = current->array;
	/*
	 * If the task has reached maximum priority (or is a RT task)
	 * then just requeue the task to the end of the runqueue:
	 */
	if (likely(current->prio == MAX_PRIO-1 || rt_task(current))) {
		list_del(&current->run_list);
		list_add_tail(&current->run_list, array->queue + current->prio);
	} else {
		list_del(&current->run_list);
		if (list_empty(array->queue + current->prio))
			__clear_bit(current->prio, array->bitmap);
		current->prio++;
		list_add_tail(&current->run_list, array->queue + current->prio);
		__set_bit(current->prio, array->bitmap);
	}
	spin_unlock(&rq->lock);
	preempt_enable_no_resched();

	schedule();

	return 0;
}
asmlinkage long sys_sched_get_priority_max(int policy)
{
	int ret = -EINVAL;

	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
		ret = 99;
		break;
	case SCHED_OTHER:
		ret = 0;
		break;
	}
	return ret;
}

asmlinkage long sys_sched_get_priority_min(int policy)
{
	int ret = -EINVAL;

	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
		ret = 1;
		break;
	case SCHED_OTHER:
		ret = 0;
	}
	return ret;
}

asmlinkage long sys_sched_rr_get_interval(pid_t pid, struct timespec *interval)
{
	struct timespec t;
	task_t *p;
	int retval = -EINVAL;

	if (pid < 0)
		goto out_nounlock;

	retval = -ESRCH;
	read_lock(&tasklist_lock);
	p = find_process_by_pid(pid);
	if (p)
		jiffies_to_timespec(p->policy & SCHED_FIFO ?
					 0 : TASK_TIMESLICE(p), &t);
	read_unlock(&tasklist_lock);
	if (p)
		retval = copy_to_user(interval, &t, sizeof(t)) ? -EFAULT : 0;
out_nounlock:
	return retval;
}

static void show_task(task_t * p)
{
	unsigned long free = 0;
	int state;
	static const char * stat_nam[] = { "R", "S", "D", "Z", "T", "W" };

	printk("%-13.13s ", p->comm);
	state = p->state ? __ffs(p->state) + 1 : 0;
	if (((unsigned) state) < sizeof(stat_nam)/sizeof(char *))
		printk(stat_nam[state]);
	else
		printk(" ");
#if (BITS_PER_LONG == 32)
	if (p == current)
		printk(" current  ");
	else
		printk(" %08lX ", thread_saved_pc(p));
#else
	if (p == current)
		printk("   current task   ");
	else
		printk(" %016lx ", thread_saved_pc(p));
#endif
	{
		unsigned long * n = (unsigned long *) (p+1);
		while (!*n)
			n++;
		free = (unsigned long) n - (unsigned long)(p+1);
	}
	printk("%5lu %5d %6d ", free, p->pid, p->p_pptr->pid);
	if (p->p_cptr)
		printk("%5d ", p->p_cptr->pid);
	else
		printk("      ");
	if (p->p_ysptr)
		printk("%7d", p->p_ysptr->pid);
	else
		printk("       ");
	if (p->p_osptr)
		printk(" %5d", p->p_osptr->pid);
	else
		printk("      ");
	if (!p->mm)
		printk(" (L-TLB)\n");
	else
		printk(" (NOTLB)\n");

	{
		extern void show_trace_task(task_t *tsk);
		show_trace_task(p);
	}
}

char * render_sigset_t(sigset_t *set, char *buffer)
{
	int i = _NSIG, x;
	do {
		i -= 4, x = 0;
		if (sigismember(set, i+1)) x |= 1;
		if (sigismember(set, i+2)) x |= 2;
		if (sigismember(set, i+3)) x |= 4;
		if (sigismember(set, i+4)) x |= 8;
		*buffer++ = (x < 10 ? '0' : 'a' - 10) + x;
	} while (i >= 4);
	*buffer = 0;
	return buffer;
}

void show_state(void)
{
	task_t *p;

#if (BITS_PER_LONG == 32)
	printk("\n"
	       "                         free                        sibling\n");
	printk("  task             PC    stack   pid father child younger older\n");
#else
	printk("\n"
	       "                                 free                        sibling\n");
	printk("  task                 PC        stack   pid father child younger older\n");
#endif
	read_lock(&tasklist_lock);
	for_each_task(p) {
		/*
		 * reset the NMI-timeout, listing all files on a slow
		 * console might take alot of time:
		 */
		touch_nmi_watchdog();
		show_task(p);
	}
	read_unlock(&tasklist_lock);
}

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

static inline void double_rq_unlock(runqueue_t *rq1, runqueue_t *rq2)
{
	spin_unlock(&rq1->lock);
	if (rq1 != rq2)
		spin_unlock(&rq2->lock);
}

void __init init_idle(task_t *idle, int cpu)
{
	runqueue_t *idle_rq = cpu_rq(cpu), *rq = cpu_rq(idle->thread_info->cpu);
	unsigned long flags;

	__save_flags(flags);
	__cli();
	double_rq_lock(idle_rq, rq);

	idle_rq->curr = idle_rq->idle = idle;
	deactivate_task(idle, rq);
	idle->array = NULL;
	idle->prio = MAX_PRIO;
	idle->state = TASK_RUNNING;
	idle->thread_info->cpu = cpu;
	double_rq_unlock(idle_rq, rq);
	set_tsk_need_resched(idle);
	__restore_flags(flags);

	/* Set the preempt count _outside_ the spinlocks! */
	idle->thread_info->preempt_count = (idle->lock_depth >= 0);
}

extern void init_timervecs(void);
extern void timer_bh(void);
extern void tqueue_bh(void);
extern void immediate_bh(void);

void __init sched_init(void)
{
	runqueue_t *rq;
	int i, j, k;

	for (i = 0; i < NR_CPUS; i++) {
		runqueue_t *rq = cpu_rq(i);
		prio_array_t *array;

		rq->active = rq->arrays;
		rq->expired = rq->arrays + 1;
		spin_lock_init(&rq->lock);
		INIT_LIST_HEAD(&rq->migration_queue);

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
	 * process right in SMP mode.
	 */
	rq = this_rq();
	rq->curr = current;
	rq->idle = current;
	wake_up_process(current);

	init_timervecs();
	init_bh(TIMER_BH, timer_bh);
	init_bh(TQUEUE_BH, tqueue_bh);
	init_bh(IMMEDIATE_BH, immediate_bh);

	/*
	 * The boot idle thread does lazy MMU switching as well:
	 */
	atomic_inc(&init_mm.mm_count);
	enter_lazy_tlb(&init_mm, current, smp_processor_id());
}

#if CONFIG_SMP

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

typedef struct {
	list_t list;
	task_t *task;
	struct semaphore sem;
} migration_req_t;

/*
 * Change a given task's CPU affinity. Migrate the process to a
 * proper CPU and schedule it away if the CPU it's executing on
 * is removed from the allowed bitmask.
 *
 * NOTE: the caller must have a valid reference to the task, the
 * task must not exit() & deallocate itself prematurely.
 */
void set_cpus_allowed(task_t *p, unsigned long new_mask)
{
	unsigned long flags;
	migration_req_t req;
	runqueue_t *rq;

	new_mask &= cpu_online_map;
	if (!new_mask)
		BUG();

	rq = task_rq_lock(p, &flags);
	p->cpus_allowed = new_mask;
	/*
	 * Can the task run on the task's current CPU? If not then
	 * migrate the process off to a proper CPU.
	 */
	if (new_mask & (1UL << p->thread_info->cpu)) {
		task_rq_unlock(rq, &flags);
		return;
	}

	init_MUTEX_LOCKED(&req.sem);
	req.task = p;
	list_add(&req.list, &rq->migration_queue);
	task_rq_unlock(rq, &flags);
	wake_up_process(rq->migration_thread);

	down(&req.sem);
}

static volatile unsigned long migration_mask;

static int migration_thread(void * unused)
{
	struct sched_param param = { sched_priority: 99 };
	runqueue_t *rq;
	int ret;

	daemonize();
	sigfillset(&current->blocked);
	set_fs(KERNEL_DS);
	ret = setscheduler(0, SCHED_FIFO, &param);

	/*
	 * We have to migrate manually - there is no migration thread
	 * to do this for us yet :-)
	 *
	 * We use the following property of the Linux scheduler. At
	 * this point no other task is running, so by keeping all
	 * migration threads running, the load-balancer will distribute
	 * them between all CPUs equally. At that point every migration
	 * task binds itself to the current CPU.
	 */

	/* wait for all migration threads to start up. */
	while (!migration_mask)
		yield();

	for (;;) {
		preempt_disable();
		if (test_and_clear_bit(smp_processor_id(), &migration_mask))
			current->cpus_allowed = 1 << smp_processor_id();
		if (test_thread_flag(TIF_NEED_RESCHED))
			schedule();
		if (!migration_mask)
			break;
		preempt_enable();
	}
	rq = this_rq();
	rq->migration_thread = current;
	preempt_enable();

	sprintf(current->comm, "migration_CPU%d", smp_processor_id());

	for (;;) {
		runqueue_t *rq_src, *rq_dest;
		struct list_head *head;
		int cpu_src, cpu_dest;
		migration_req_t *req;
		unsigned long flags;
		task_t *p;

		spin_lock_irqsave(&rq->lock, flags);
		head = &rq->migration_queue;
		current->state = TASK_INTERRUPTIBLE;
		if (list_empty(head)) {
			spin_unlock_irqrestore(&rq->lock, flags);
			schedule();
			continue;
		}
		req = list_entry(head->next, migration_req_t, list);
		list_del_init(head->next);
		spin_unlock_irqrestore(&rq->lock, flags);

		p = req->task;
		cpu_dest = __ffs(p->cpus_allowed);
		rq_dest = cpu_rq(cpu_dest);
repeat:
		cpu_src = p->thread_info->cpu;
		rq_src = cpu_rq(cpu_src);

		double_rq_lock(rq_src, rq_dest);
		if (p->thread_info->cpu != cpu_src) {
			double_rq_unlock(rq_src, rq_dest);
			goto repeat;
		}
		if (rq_src == rq) {
			p->thread_info->cpu = cpu_dest;
			if (p->array) {
				deactivate_task(p, rq_src);
				activate_task(p, rq_dest);
			}
		}
		double_rq_unlock(rq_src, rq_dest);

		up(&req->sem);
	}
}

void __init migration_init(void)
{
	int cpu;

	for (cpu = 0; cpu < smp_num_cpus; cpu++)
		if (kernel_thread(migration_thread, NULL,
				CLONE_FS | CLONE_FILES | CLONE_SIGNAL) < 0)
			BUG();

	migration_mask = (1 << smp_num_cpus) - 1;

	for (cpu = 0; cpu < smp_num_cpus; cpu++)
		while (!cpu_rq(cpu)->migration_thread)
			schedule_timeout(2);
	if (migration_mask)
		BUG();
}
#endif
