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
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <asm/mmu_context.h>

#define BITMAP_SIZE ((MAX_PRIO+7)/8)
#define PRIO_INTERACTIVE	(MAX_RT_PRIO + (MAX_PRIO - MAX_RT_PRIO) / 3)
#define TASK_INTERACTIVE(p)	((p)->prio >= MAX_RT_PRIO && (p)->prio <= PRIO_INTERACTIVE)
#define JSLEEP_TO_PRIO(t)	(((t) * 20) / HZ)

typedef struct runqueue runqueue_t;

struct prio_array {
	int nr_active;
	spinlock_t *lock;
	runqueue_t *rq;
	char bitmap[BITMAP_SIZE];
	list_t queue[MAX_PRIO];
};

/*
 * This is the main, per-CPU runqueue data structure.
 *
 * Locking rule: those places that want to lock multiple runqueues
 * (such as the load balancing or the process migration code), lock
 * acquire operations must be ordered by rq->cpu.
 *
 * The RT event id is used to avoid calling into the the RT scheduler
 * if there is a RT task active in an SMP system but there is no
 * RT scheduling activity otherwise.
 */
static struct runqueue {
	int cpu;
	spinlock_t lock;
	unsigned long nr_running, nr_switches;
	task_t *curr, *idle;
	prio_array_t *active, *expired, arrays[2];
	char __pad [SMP_CACHE_BYTES];
} runqueues [NR_CPUS] __cacheline_aligned;

#define this_rq()		(runqueues + smp_processor_id())
#define task_rq(p)		(runqueues + (p)->cpu)
#define cpu_rq(cpu)		(runqueues + (cpu))
#define cpu_curr(cpu)		(runqueues[(cpu)].curr)
#define rt_task(p)		((p)->policy != SCHED_OTHER)

#define lock_task_rq(rq,p,flags)				\
do {								\
repeat_lock_task:						\
	rq = task_rq(p);					\
	spin_lock_irqsave(&rq->lock, flags);			\
	if (unlikely((rq)->cpu != (p)->cpu)) {			\
		spin_unlock_irqrestore(&rq->lock, flags);	\
		goto repeat_lock_task;				\
	}							\
} while (0)

#define unlock_task_rq(rq,p,flags)				\
	spin_unlock_irqrestore(&rq->lock, flags)

/*
 * Adding/removing a task to/from a priority array:
 */
static inline void dequeue_task(struct task_struct *p, prio_array_t *array)
{
	array->nr_active--;
	list_del_init(&p->run_list);
	if (list_empty(array->queue + p->prio))
		__set_bit(p->prio, array->bitmap);
}

static inline void enqueue_task(struct task_struct *p, prio_array_t *array)
{
	list_add_tail(&p->run_list, array->queue + p->prio);
	__clear_bit(p->prio, array->bitmap);
	array->nr_active++;
	p->array = array;
}

static inline void activate_task(task_t *p, runqueue_t *rq)
{
	prio_array_t *array = rq->active;

	if (!rt_task(p)) {
		unsigned long prio_bonus = JSLEEP_TO_PRIO(jiffies - p->sleep_jtime);

		if (prio_bonus > MAX_PRIO)
			prio_bonus = MAX_PRIO;
		p->prio -= prio_bonus;
		if (p->prio < MAX_RT_PRIO)
			p->prio = MAX_RT_PRIO;
	}
	enqueue_task(p, array);
	rq->nr_running++;
}

static inline void deactivate_task(struct task_struct *p, runqueue_t *rq)
{
	rq->nr_running--;
	dequeue_task(p, p->array);
	p->array = NULL;
	p->sleep_jtime = jiffies;
}

static inline void resched_task(task_t *p)
{
	int need_resched;

	need_resched = p->need_resched;
	wmb();
	p->need_resched = 1;
	if (!need_resched && (p->cpu != smp_processor_id()))
		smp_send_reschedule(p->cpu);
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
	rq = task_rq(p);
	while (unlikely(rq->curr == p)) {
		cpu_relax();
		barrier();
	}
	lock_task_rq(rq, p, flags);
	if (unlikely(rq->curr == p)) {
		unlock_task_rq(rq, p, flags);
		goto repeat;
	}
	unlock_task_rq(rq, p, flags);
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
static int try_to_wake_up(task_t * p, int synchronous)
{
	unsigned long flags;
	int success = 0;
	runqueue_t *rq;

	lock_task_rq(rq, p, flags);
	p->state = TASK_RUNNING;
	if (!p->array) {
		activate_task(p, rq);
		if ((rq->curr == rq->idle) || (p->prio < rq->curr->prio))
			resched_task(rq->curr);
		success = 1;
	}
	unlock_task_rq(rq, p, flags);
	return success;
}

inline int wake_up_process(task_t * p)
{
	return try_to_wake_up(p, 0);
}

void wake_up_forked_process(task_t * p)
{
	runqueue_t *rq = this_rq();

	spin_lock_irq(&rq->lock);
	p->state = TASK_RUNNING;
	if (!rt_task(p)) {
		p->prio += MAX_USER_PRIO/10;
		if (p->prio > MAX_PRIO-1)
			p->prio = MAX_PRIO-1;
	}
	activate_task(p, rq);
	spin_unlock_irq(&rq->lock);
}

asmlinkage void schedule_tail(task_t *prev)
{
	spin_unlock_irq(&this_rq()->lock);
}

static inline void context_switch(task_t *prev, task_t *next)
{
	struct mm_struct *mm = next->mm;
	struct mm_struct *oldmm = prev->active_mm;

	prepare_to_switch();

	if (!mm) {
		next->active_mm = oldmm;
		atomic_inc(&oldmm->mm_count);
		enter_lazy_tlb(oldmm, next, smp_processor_id());
	} else
		switch_mm(oldmm, mm, next, smp_processor_id());

	if (!prev->mm) {
		prev->active_mm = NULL;
		mmdrop(oldmm);
	}

	/*
	 * Here we just switch the register state and the stack. There are
	 * 3 processes affected by a context switch:
	 *
	 * prev ==> .... ==> (last => next)
	 *
	 * It's the 'much more previous' 'prev' that is on next's stack,
	 * but prev is set to (the just run) 'last' process by switch_to().
	 * This might sound slightly confusing but makes tons of sense.
	 */
	switch_to(prev, next, prev);
}

unsigned long nr_running(void)
{
	unsigned long i, sum = 0;

	for (i = 0; i < smp_num_cpus; i++)
		sum += cpu_rq(i)->nr_running;

	return sum;
}

unsigned long nr_context_switches(void)
{
	unsigned long i, sum = 0;

	for (i = 0; i < smp_num_cpus; i++)
		sum += cpu_rq(i)->nr_switches;

	return sum;
}

static inline unsigned long max_rq_len(void)
{
	unsigned long i, curr, max = 0;

	for (i = 0; i < smp_num_cpus; i++) {
		curr = cpu_rq(i)->nr_running;
		if (curr > max)
			max = curr;
	}
	return max;
}

/*
 * Current runqueue is empty, try to find work on
 * other runqueues.
 *
 * We call this with the current runqueue locked,
 * irqs disabled.
 */
static void load_balance(runqueue_t *this_rq)
{
	int nr_tasks, load, prev_max_load, max_load, idx, i;
	task_t *next = this_rq->idle, *tmp;
	runqueue_t *busiest, *rq_tmp;
	prio_array_t *array;
	list_t *head, *curr;

	prev_max_load = max_rq_len();
	nr_tasks = prev_max_load - this_rq->nr_running;
	/*
	 * It needs an at least ~10% imbalance to trigger balancing:
	 */
	if (nr_tasks <= 1 + prev_max_load/8)
		return;
	prev_max_load++;

repeat_search:
	/*
	 * We search all runqueues to find the most busy one.
	 * We do this lockless to reduce cache-bouncing overhead,
	 * we re-check the source CPU with the lock held.
	 */
	busiest = NULL;
	max_load = 0;
	for (i = 0; i < smp_num_cpus; i++) {
		rq_tmp = cpu_rq(i);
		load = rq_tmp->nr_running;
		if ((load > max_load) && (load < prev_max_load) &&
						(rq_tmp != this_rq)) {
			busiest = rq_tmp;
			max_load = load;
		}
	}

	if (likely(!busiest))
		return;
	if (max_load <= this_rq->nr_running)
		return;
	prev_max_load = max_load;
	if (busiest->cpu < this_rq->cpu) {
		spin_unlock(&this_rq->lock);
		spin_lock(&busiest->lock);
		spin_lock(&this_rq->lock);
	} else
		spin_lock(&busiest->lock);
	if (busiest->nr_running <= this_rq->nr_running + 1)
		goto out_unlock;

	/*
	 * We first consider expired tasks. Those will likely not run
	 * in the near future, thus switching CPUs has the least effect
	 * on them.
	 */
	if (busiest->expired->nr_active)
		array = busiest->expired;
	else
		array = busiest->active;

new_array:
	/*
	 * Load-balancing does not affect RT tasks, so we start the
	 * searching at priority 128.
	 */
	idx = MAX_RT_PRIO;
skip_bitmap:
	idx = find_next_zero_bit(array->bitmap, MAX_PRIO, idx);
	if (idx == MAX_PRIO) {
		if (array == busiest->expired) {
			array = busiest->active;
			goto new_array;
		}
		spin_unlock(&busiest->lock);
		goto repeat_search;
	}

	head = array->queue + idx;
	curr = head->next;
skip_queue:
	tmp = list_entry(curr, task_t, run_list);
	if ((tmp == busiest->curr) || !(tmp->cpus_allowed & (1 << smp_processor_id()))) {
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
	next->cpu = smp_processor_id();
	this_rq->nr_running++;
	enqueue_task(next, this_rq->active);
	if (next->prio < current->prio)
		current->need_resched = 1;
	if (--nr_tasks) {
		if (array == busiest->expired) {
			array = busiest->active;
			goto new_array;
		}
		spin_unlock(&busiest->lock);
		goto repeat_search;
	}
out_unlock:
	spin_unlock(&busiest->lock);
}

/* Rebalance every 250 msecs */
#define REBALANCE_TICK (HZ/4)

void idle_tick(void)
{
	unsigned long flags;

	if (!(jiffies % REBALANCE_TICK) && likely(this_rq()->curr != NULL)) {
		spin_lock_irqsave(&this_rq()->lock, flags);
		load_balance(this_rq());
		spin_unlock_irqrestore(&this_rq()->lock, flags);
	}
}

void expire_task(task_t *p)
{
	runqueue_t *rq = this_rq();
	unsigned long flags;

	if (p->array != rq->active) {
		p->need_resched = 1;
		return;
	}
	/*
	 * The task cannot change CPUs because it's the current task.
	 */
	spin_lock_irqsave(&rq->lock, flags);
	if ((p->policy != SCHED_FIFO) && !--p->time_slice) {
		prio_array_t *array = rq->active;
		p->need_resched = 1;
		dequeue_task(p, rq->active);
		if (!rt_task(p)) {
			if (++p->prio >= MAX_PRIO)
				p->prio = MAX_PRIO - 1;
			if (!TASK_INTERACTIVE(p))
				array = rq->expired;
		}
		enqueue_task(p, array);
		p->time_slice = NICE_TO_TIMESLICE(p->__nice);
	}
	spin_unlock_irqrestore(&rq->lock, flags);
}

void scheduling_functions_start_here(void) { }

/*
 * 'schedule()' is the main scheduler function.
 */
asmlinkage void schedule(void)
{
	task_t *prev, *next;
	prio_array_t *array;
	runqueue_t *rq;
	list_t *queue;
	int idx;

	if (unlikely(in_interrupt()))
		BUG();
need_resched_back:
	prev = current;
	release_kernel_lock(prev, smp_processor_id());
	rq = this_rq();
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
	}
pick_next_task:
	if (unlikely(!rq->nr_running)) {
		load_balance(rq);
		if (rq->nr_running)
			goto pick_next_task;
		next = rq->idle;
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
	}

	idx = sched_find_first_zero_bit(array->bitmap);
	queue = array->queue + idx;
	next = list_entry(queue->next, task_t, run_list);

switch_tasks:
	prev->need_resched = 0;

	if (likely(prev != next)) {
		rq->nr_switches++;
		rq->curr = next;
		next->cpu = prev->cpu;
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
	if (unlikely(current->need_resched))
		goto need_resched_back;
	return;
}

/*
 * The core wakeup function.  Non-exclusive wakeups (nr_exclusive == 0) just
 * wake everything up.  If it's an exclusive wakeup (nr_exclusive == small +ve
 * number) then we wake all the non-exclusive tasks and one exclusive task.
 *
 * There are circumstances in which we can try to wake a task which has already
 * started to run but is not in state TASK_RUNNING.  try_to_wake_up() returns
 * zero in this (rare) case, and we handle it by continuing to scan the queue.
 */
static inline void __wake_up_common (wait_queue_head_t *q, unsigned int mode,
			 	     int nr_exclusive, const int sync)
{
	struct list_head *tmp;
	task_t *p;

	list_for_each(tmp,&q->task_list) {
		unsigned int state;
		wait_queue_t *curr = list_entry(tmp, wait_queue_t, task_list);

		p = curr->task;
		state = p->state;
		if ((state & mode) &&
				try_to_wake_up(p, sync) &&
				((curr->flags & WQ_FLAG_EXCLUSIVE) &&
					!--nr_exclusive))
			break;
	}
}

void __wake_up(wait_queue_head_t *q, unsigned int mode, int nr)
{
	if (q) {
		unsigned long flags;
		wq_read_lock_irqsave(&q->lock, flags);
		__wake_up_common(q, mode, nr, 0);
		wq_read_unlock_irqrestore(&q->lock, flags);
	}
}

void __wake_up_sync(wait_queue_head_t *q, unsigned int mode, int nr)
{
	if (q) {
		unsigned long flags;
		wq_read_lock_irqsave(&q->lock, flags);
		__wake_up_common(q, mode, nr, 1);
		wq_read_unlock_irqrestore(&q->lock, flags);
	}
}

void complete(struct completion *x)
{
	unsigned long flags;

	spin_lock_irqsave(&x->wait.lock, flags);
	x->done++;
	__wake_up_common(&x->wait, TASK_UNINTERRUPTIBLE | TASK_INTERRUPTIBLE, 1, 0);
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

/*
 * Change the current task's CPU affinity. Migrate the process to a
 * proper CPU and schedule away if the current CPU is removed from
 * the allowed bitmask.
 */
void set_cpus_allowed(task_t *p, unsigned long new_mask)
{
	runqueue_t *this_rq = this_rq(), *target_rq;
	unsigned long this_mask = 1UL << smp_processor_id();
	int target_cpu;

	new_mask &= cpu_online_map;
	if (!new_mask)
		BUG();
	p->cpus_allowed = new_mask;
	/*
	 * Can the task run on the current CPU? If not then
	 * migrate the process off to a proper CPU.
	 */
	if (new_mask & this_mask)
		return;
	target_cpu = ffz(~new_mask);
	target_rq = cpu_rq(target_cpu);
	if (target_cpu < smp_processor_id()) {
		spin_lock_irq(&target_rq->lock);
		spin_lock(&this_rq->lock);
	} else {
		spin_lock_irq(&this_rq->lock);
		spin_lock(&target_rq->lock);
	}
	dequeue_task(p, p->array);
	this_rq->nr_running--;
	target_rq->nr_running++;
	enqueue_task(p, target_rq->active);
	target_rq->curr->need_resched = 1;
	spin_unlock(&target_rq->lock);

	/*
	 * The easiest solution is to context switch into
	 * the idle thread - which will pick the best task
	 * afterwards:
	 */
	this_rq->nr_switches++;
	this_rq->curr = this_rq->idle;
	this_rq->idle->need_resched = 1;
	context_switch(current, this_rq->idle);
	barrier();
	spin_unlock_irq(&this_rq()->lock);
}

void scheduling_functions_end_here(void) { }

void set_user_nice(task_t *p, long nice)
{
	unsigned long flags;
	prio_array_t *array;
	runqueue_t *rq;

	if (p->__nice == nice)
		return;
	/*
	 * We have to be careful, if called from sys_setpriority(),
	 * the task might be in the middle of scheduling on another CPU.
	 */
	lock_task_rq(rq, p, flags);
	if (rt_task(p)) {
		p->__nice = nice;
		goto out_unlock;
	}
	array = p->array;
	if (array) {
		dequeue_task(p, array);
	}
	p->__nice = nice;
	p->prio = NICE_TO_PRIO(nice);
	if (array) {
		enqueue_task(p, array);
		/*
		 * If the task is runnable and lowered its priority,
		 * or increased its priority then reschedule its CPU:
		 */
		if ((nice < p->__nice) ||
				((p->__nice < nice) && (p == rq->curr)))
			resched_task(rq->curr);
	}
out_unlock:
	unlock_task_rq(rq, p, flags);
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

	nice = current->__nice + increment;
	if (nice < -20)
		nice = -20;
	if (nice > 19)
		nice = 19;
	set_user_nice(current, nice);
	return 0;
}

#endif

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
	lock_task_rq(rq,p,flags);

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
	if (rt_task(p))
		p->prio = 99-p->rt_priority;
	else
		p->prio = NICE_TO_PRIO(p->__nice);
	if (array)
		activate_task(p, task_rq(p));

out_unlock:
	unlock_task_rq(rq,p,flags);
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
	runqueue_t *rq = this_rq();
	prio_array_t *array;

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
	dequeue_task(current, array);
	if (likely(!rt_task(current)))
		if (current->prio < MAX_PRIO-1)
			current->prio++;
	enqueue_task(current, array);
	spin_unlock_irq(&rq->lock);

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
					 0 : NICE_TO_TIMESLICE(p->__nice), &t);
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
	state = p->state ? ffz(~p->state) + 1 : 0;
	if (((unsigned) state) < sizeof(stat_nam)/sizeof(char *))
		printk(stat_nam[state]);
	else
		printk(" ");
#if (BITS_PER_LONG == 32)
	if (p == current)
		printk(" current  ");
	else
		printk(" %08lX ", thread_saved_pc(&p->thread));
#else
	if (p == current)
		printk("   current task   ");
	else
		printk(" %016lx ", thread_saved_pc(&p->thread));
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

extern unsigned long wait_init_idle;

static inline void double_rq_lock(runqueue_t *rq1, runqueue_t *rq2)
{
	if (rq1 == rq2)
		spin_lock(&rq1->lock);
	else {
		if (rq1->cpu < rq2->cpu) {
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

void __init init_idle(void)
{
	runqueue_t *this_rq = this_rq(), *rq = current->array->rq;
	unsigned long flags;

	__save_flags(flags);
	__cli();
	double_rq_lock(this_rq, rq);

	this_rq->curr = this_rq->idle = current;
	deactivate_task(current, rq);
	current->array = NULL;
	current->prio = MAX_PRIO;
	current->state = TASK_RUNNING;
	clear_bit(smp_processor_id(), &wait_init_idle);
	double_rq_unlock(this_rq, rq);
	while (wait_init_idle) {
		cpu_relax();
		barrier();
	}
	current->need_resched = 1;
	__sti();
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

		rq->active = rq->arrays + 0;
		rq->expired = rq->arrays + 1;
		spin_lock_init(&rq->lock);
		rq->cpu = i;

		for (j = 0; j < 2; j++) {
			array = rq->arrays + j;
			array->rq = rq;
			array->lock = &rq->lock;
			for (k = 0; k < MAX_PRIO; k++)
				INIT_LIST_HEAD(array->queue + k);
			memset(array->bitmap, 0xff, BITMAP_SIZE);
			// zero delimiter for bitsearch
			clear_bit(MAX_PRIO, array->bitmap);
		}
	}
	/*
	 * We have to do a little magic to get the first
	 * process right in SMP mode.
	 */
	rq = this_rq();
	rq->curr = current;
	rq->idle = NULL;
	wake_up_process(current);

	for (i = 0; i < PIDHASH_SZ; i++)
		pidhash[i] = NULL;

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
