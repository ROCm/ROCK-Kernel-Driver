/* delayacct.c - per-task delay accounting
 *
 * Copyright (C) Shailabh Nagar, IBM Corp. 2006
 * Copyright (C) Balbir Singh, IBM Corp. 2006
 *
 * This program is free software;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License for more details.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/sysctl.h>
#include <linux/delayacct.h>
#include <linux/taskstats.h>
#include <linux/mutex.h>

int delayacct_on = 0;		/* Delay accounting turned on/off */
kmem_cache_t *delayacct_cache;
static DEFINE_MUTEX(delayacct_exit_mutex);

static int __init delayacct_setup_enable(char *str)
{
	delayacct_on = 1;
	return 1;
}
__setup("delayacct", delayacct_setup_enable);

void delayacct_init(void)
{
	delayacct_cache = kmem_cache_create("delayacct_cache",
					    sizeof(struct task_delay_info),
					    0,
					    SLAB_PANIC,
					    NULL, NULL);
	delayacct_tsk_init(&init_task);
}

void __delayacct_tsk_init(struct task_struct *tsk)
{
	tsk->delays = kmem_cache_alloc(delayacct_cache, SLAB_KERNEL);
	if (tsk->delays) {
		memset(tsk->delays, 0, sizeof(*tsk->delays));
		spin_lock_init(&tsk->delays->lock);
	}
}

void __delayacct_tsk_exit(struct task_struct *tsk)
{
	/*
	 * Protect against racing thread group exits
	 */
	mutex_lock(&delayacct_exit_mutex);
	taskstats_exit_pid(tsk);
	if (tsk->delays) {
		kmem_cache_free(delayacct_cache, tsk->delays);
		tsk->delays = NULL;
	}
	mutex_unlock(&delayacct_exit_mutex);
}

/*
 * Start accounting for a delay statistic using
 * its starting timestamp (@start)
 */

static inline void delayacct_start(struct timespec *start)
{
	do_posix_clock_monotonic_gettime(start);
}

/*
 * Finish delay accounting for a statistic using
 * its timestamps (@start, @end), accumalator (@total) and @count
 */

static inline void delayacct_end(struct timespec *start, struct timespec *end,
				u64 *total, u32 *count)
{
	struct timespec ts;
	nsec_t ns;

	do_posix_clock_monotonic_gettime(end);
	ts.tv_sec = end->tv_sec - start->tv_sec;
	ts.tv_nsec = end->tv_nsec - start->tv_nsec;
	ns = timespec_to_ns(&ts);
	if (ns < 0)
		return;

	spin_lock(&current->delays->lock);
	*total += ns;
	(*count)++;
	spin_unlock(&current->delays->lock);
}

void __delayacct_blkio_start(void)
{
	delayacct_start(&current->delays->blkio_start);
}

void __delayacct_blkio_end(void)
{
	if (current->flags & PF_SWAPIN)	/* Swapping a page in */
		delayacct_end(&current->delays->blkio_start,
				&current->delays->blkio_end,
				&current->delays->swapin_delay,
				&current->delays->swapin_count);
	else	/* Other block I/O */
		delayacct_end(&current->delays->blkio_start,
				&current->delays->blkio_end,
				&current->delays->blkio_delay,
				&current->delays->blkio_count);
}
#ifdef CONFIG_TASKSTATS
int __delayacct_add_tsk(struct taskstats *d, struct task_struct *tsk)
{
	nsec_t tmp;
	struct timespec ts;
	unsigned long t1,t2,t3;

	/* zero XXX_total,non-zero XXX_count implies XXX stat overflowed */

	tmp = (nsec_t)d->cpu_run_real_total ;
	tmp += (u64)(tsk->utime+tsk->stime)*TICK_NSEC;
	d->cpu_run_real_total = (tmp < (nsec_t)d->cpu_run_real_total)? 0: tmp;

	/* No locking available for sched_info. Take snapshot first. */
	t1 = tsk->sched_info.pcnt;
	t2 = tsk->sched_info.run_delay;
	t3 = tsk->sched_info.cpu_time;

	d->cpu_count += t1;

	jiffies_to_timespec(t2, &ts);
	tmp = (nsec_t)d->cpu_delay_total + timespec_to_ns(&ts);
	d->cpu_delay_total = (tmp < (nsec_t)d->cpu_delay_total)? 0: tmp;

	tmp = (nsec_t)d->cpu_run_virtual_total
		+ (nsec_t)jiffies_to_usecs(t3) * 1000;
	d->cpu_run_virtual_total = (tmp < (nsec_t)d->cpu_run_virtual_total) ?
					0 : tmp;

	spin_lock(&tsk->delays->lock);
	tmp = d->blkio_delay_total + tsk->delays->blkio_delay;
	d->blkio_delay_total = (tmp < d->blkio_delay_total)? 0: tmp;
	tmp = d->swapin_delay_total + tsk->delays->swapin_delay;
	d->swapin_delay_total = (tmp < d->swapin_delay_total)? 0: tmp;
	d->blkio_count += tsk->delays->blkio_count;
	d->swapin_count += tsk->delays->swapin_count;
	spin_unlock(&tsk->delays->lock);
	return 0;
}
#endif /* CONFIG_TASKSTATS */
