/*
 * linux/kernel/workqueue.c
 *
 * Generic mechanism for defining kernel helper threads for running
 * arbitrary tasks in process context.
 *
 * Started by Ingo Molnar, Copyright (C) 2002
 *
 * Derived from the taskqueue/keventd code by:
 *
 *   David Woodhouse <dwmw2@redhat.com>
 *   Andrew Morton <andrewm@uow.edu.au>
 *   Kai Petzke <wpp@marie.physik.tu-berlin.de>
 *   Theodore Ts'o <tytso@mit.edu>
 */

#define __KERNEL_SYSCALLS__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/unistd.h>
#include <linux/signal.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/slab.h>

/*
 * The per-CPU workqueue.
 *
 * The sequence counters are for flush_scheduled_work().  It wants to wait
 * until until all currently-scheduled works are completed, but it doesn't
 * want to be livelocked by new, incoming ones.  So it waits until
 * remove_sequence is >= the insert_sequence which pertained when
 * flush_scheduled_work() was called.
 */
struct cpu_workqueue_struct {

	spinlock_t lock;

	long remove_sequence;	/* Least-recently added (next to run) */
	long insert_sequence;	/* Next to add */

	struct list_head worklist;
	wait_queue_head_t more_work;
	wait_queue_head_t work_done;

	struct workqueue_struct *wq;
	task_t *thread;
	struct completion exit;

} ____cacheline_aligned;

/*
 * The externally visible workqueue abstraction is an array of
 * per-CPU workqueues:
 */
struct workqueue_struct {
	struct cpu_workqueue_struct cpu_wq[NR_CPUS];
};

/*
 * Queue work on a workqueue. Return non-zero if it was successfully
 * added.
 *
 * We queue the work to the CPU it was submitted, but there is no
 * guarantee that it will be processed by that CPU.
 */
int queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
	unsigned long flags;
	int ret = 0, cpu = get_cpu();
	struct cpu_workqueue_struct *cwq = wq->cpu_wq + cpu;

	if (!test_and_set_bit(0, &work->pending)) {
		BUG_ON(!list_empty(&work->entry));
		work->wq_data = cwq;

		spin_lock_irqsave(&cwq->lock, flags);
		list_add_tail(&work->entry, &cwq->worklist);
		cwq->insert_sequence++;
		wake_up(&cwq->more_work);
		spin_unlock_irqrestore(&cwq->lock, flags);
		ret = 1;
	}
	put_cpu();
	return ret;
}

static void delayed_work_timer_fn(unsigned long __data)
{
	struct work_struct *work = (struct work_struct *)__data;
	struct cpu_workqueue_struct *cwq = work->wq_data;
	unsigned long flags;

	/*
	 * Do the wakeup within the spinlock, so that flushing
	 * can be done in a guaranteed way.
	 */
	spin_lock_irqsave(&cwq->lock, flags);
	list_add_tail(&work->entry, &cwq->worklist);
	cwq->insert_sequence++;
	wake_up(&cwq->more_work);
	spin_unlock_irqrestore(&cwq->lock, flags);
}

int queue_delayed_work(struct workqueue_struct *wq,
			struct work_struct *work, unsigned long delay)
{
	int ret = 0, cpu = get_cpu();
	struct timer_list *timer = &work->timer;
	struct cpu_workqueue_struct *cwq = wq->cpu_wq + cpu;

	if (!test_and_set_bit(0, &work->pending)) {
		BUG_ON(timer_pending(timer));
		BUG_ON(!list_empty(&work->entry));

		work->wq_data = cwq;
		timer->expires = jiffies + delay;
		timer->data = (unsigned long)work;
		timer->function = delayed_work_timer_fn;
		add_timer(timer);
		ret = 1;
	}
	put_cpu();
	return ret;
}

static inline void run_workqueue(struct cpu_workqueue_struct *cwq)
{
	unsigned long flags;

	/*
	 * Keep taking off work from the queue until
	 * done.
	 */
	spin_lock_irqsave(&cwq->lock, flags);
	while (!list_empty(&cwq->worklist)) {
		struct work_struct *work = list_entry(cwq->worklist.next,
						struct work_struct, entry);
		void (*f) (void *) = work->func;
		void *data = work->data;

		list_del_init(cwq->worklist.next);
		spin_unlock_irqrestore(&cwq->lock, flags);

		BUG_ON(work->wq_data != cwq);
		clear_bit(0, &work->pending);
		f(data);

		spin_lock_irqsave(&cwq->lock, flags);
		cwq->remove_sequence++;
		wake_up(&cwq->work_done);
	}
	spin_unlock_irqrestore(&cwq->lock, flags);
}

typedef struct startup_s {
	struct cpu_workqueue_struct *cwq;
	struct completion done;
	const char *name;
} startup_t;

static int worker_thread(void *__startup)
{
	startup_t *startup = __startup;
	struct cpu_workqueue_struct *cwq = startup->cwq;
	int cpu = cwq - cwq->wq->cpu_wq;
	DECLARE_WAITQUEUE(wait, current);
	struct k_sigaction sa;

	daemonize("%s/%d", startup->name, cpu);
	allow_signal(SIGCHLD);
	current->flags |= PF_IOTHREAD;
	cwq->thread = current;

	set_user_nice(current, -10);
	set_cpus_allowed(current, cpumask_of_cpu(cpu));

	complete(&startup->done);

	/* Install a handler so SIGCLD is delivered */
	sa.sa.sa_handler = SIG_IGN;
	sa.sa.sa_flags = 0;
	siginitset(&sa.sa.sa_mask, sigmask(SIGCHLD));
	do_sigaction(SIGCHLD, &sa, (struct k_sigaction *)0);

	for (;;) {
		set_task_state(current, TASK_INTERRUPTIBLE);

		add_wait_queue(&cwq->more_work, &wait);
		if (!cwq->thread)
			break;
		if (list_empty(&cwq->worklist))
			schedule();
		else
			set_task_state(current, TASK_RUNNING);
		remove_wait_queue(&cwq->more_work, &wait);

		if (!list_empty(&cwq->worklist))
			run_workqueue(cwq);

		if (signal_pending(current)) {
			while (waitpid(-1, NULL, __WALL|WNOHANG) > 0)
				/* SIGCHLD - auto-reaping */ ;

			/* zap all other signals */
			flush_signals(current);
		}
	}
	remove_wait_queue(&cwq->more_work, &wait);
	complete(&cwq->exit);

	return 0;
}

/*
 * flush_workqueue - ensure that any scheduled work has run to completion.
 *
 * Forces execution of the workqueue and blocks until its completion.
 * This is typically used in driver shutdown handlers.
 *
 * This function will sample each workqueue's current insert_sequence number and
 * will sleep until the head sequence is greater than or equal to that.  This
 * means that we sleep until all works which were queued on entry have been
 * handled, but we are not livelocked by new incoming ones.
 *
 * This function used to run the workqueues itself.  Now we just wait for the
 * helper threads to do it.
 */
void flush_workqueue(struct workqueue_struct *wq)
{
	struct cpu_workqueue_struct *cwq;
	int cpu;

	might_sleep();

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		DEFINE_WAIT(wait);
		long sequence_needed;

		if (!cpu_online(cpu))
			continue;
		cwq = wq->cpu_wq + cpu;

		spin_lock_irq(&cwq->lock);
		sequence_needed = cwq->insert_sequence;

		while (sequence_needed - cwq->remove_sequence > 0) {
			prepare_to_wait(&cwq->work_done, &wait,
					TASK_UNINTERRUPTIBLE);
			spin_unlock_irq(&cwq->lock);
			schedule();
			spin_lock_irq(&cwq->lock);
		}
		finish_wait(&cwq->work_done, &wait);
		spin_unlock_irq(&cwq->lock);
	}
}

static int create_workqueue_thread(struct workqueue_struct *wq,
				   const char *name,
				   int cpu)
{
	startup_t startup;
	struct cpu_workqueue_struct *cwq = wq->cpu_wq + cpu;
	int ret;

	spin_lock_init(&cwq->lock);
	cwq->wq = wq;
	cwq->thread = NULL;
	cwq->insert_sequence = 0;
	cwq->remove_sequence = 0;
	INIT_LIST_HEAD(&cwq->worklist);
	init_waitqueue_head(&cwq->more_work);
	init_waitqueue_head(&cwq->work_done);
	init_completion(&cwq->exit);

	init_completion(&startup.done);
	startup.cwq = cwq;
	startup.name = name;
	ret = kernel_thread(worker_thread, &startup, CLONE_FS | CLONE_FILES);
	if (ret >= 0) {
		wait_for_completion(&startup.done);
		BUG_ON(!cwq->thread);
	}
	return ret;
}

struct workqueue_struct *create_workqueue(const char *name)
{
	int cpu, destroy = 0;
	struct workqueue_struct *wq;

	BUG_ON(strlen(name) > 10);

	wq = kmalloc(sizeof(*wq), GFP_KERNEL);
	if (!wq)
		return NULL;

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		if (!cpu_online(cpu))
			continue;
		if (create_workqueue_thread(wq, name, cpu) < 0)
			destroy = 1;
	}
	/*
	 * Was there any error during startup? If yes then clean up:
	 */
	if (destroy) {
		destroy_workqueue(wq);
		wq = NULL;
	}
	return wq;
}

static void cleanup_workqueue_thread(struct workqueue_struct *wq, int cpu)
{
	struct cpu_workqueue_struct *cwq;

	cwq = wq->cpu_wq + cpu;
	if (cwq->thread) {
		/* Tell thread to exit and wait for it. */
		cwq->thread = NULL;
		wake_up(&cwq->more_work);

		wait_for_completion(&cwq->exit);
	}
}

void destroy_workqueue(struct workqueue_struct *wq)
{
	int cpu;

	flush_workqueue(wq);

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		if (cpu_online(cpu))
			cleanup_workqueue_thread(wq, cpu);
	}
	kfree(wq);
}

static struct workqueue_struct *keventd_wq;

int schedule_work(struct work_struct *work)
{
	return queue_work(keventd_wq, work);
}

int schedule_delayed_work(struct work_struct *work, unsigned long delay)
{
	return queue_delayed_work(keventd_wq, work, delay);
}

void flush_scheduled_work(void)
{
	flush_workqueue(keventd_wq);
}

int current_is_keventd(void)
{
	struct cpu_workqueue_struct *cwq;
	int cpu;

	BUG_ON(!keventd_wq);

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		if (!cpu_online(cpu))
			continue;
		cwq = keventd_wq->cpu_wq + cpu;
		if (current == cwq->thread)
			return 1;
	}
	return 0;
}

void init_workqueues(void)
{
	keventd_wq = create_workqueue("events");
	BUG_ON(!keventd_wq);
}

EXPORT_SYMBOL_GPL(create_workqueue);
EXPORT_SYMBOL_GPL(queue_work);
EXPORT_SYMBOL_GPL(queue_delayed_work);
EXPORT_SYMBOL_GPL(flush_workqueue);
EXPORT_SYMBOL_GPL(destroy_workqueue);

EXPORT_SYMBOL(schedule_work);
EXPORT_SYMBOL(schedule_delayed_work);
EXPORT_SYMBOL(flush_scheduled_work);

