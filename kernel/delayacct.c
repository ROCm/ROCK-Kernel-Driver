/* delayacct.c - per-task delay accounting
 *
 * Copyright (C) Shailabh Nagar, IBM Corp. 2005
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/sysctl.h>
#include <linux/delayacct.h>

int delayacct_on = 0;		/* Delay accounting turned on/off */
kmem_cache_t *delayacct_cache;

static int __init delayacct_setup_enable(char *str)
{
	delayacct_on = 1;
	return 1;
}
__setup("delayacct", delayacct_setup_enable);

int delayacct_init(void)
{
	delayacct_cache = kmem_cache_create("delayacct_cache",
					    sizeof(struct task_delay_info),
					    0,
					    SLAB_PANIC,
					    NULL, NULL);
	if (!delayacct_cache)
		return -ENOMEM;
	delayacct_tsk_init(&init_task);
	return 0;
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
	kmem_cache_free(delayacct_cache, tsk->delays);
	tsk->delays = NULL;
}

static inline unsigned long long delayacct_measure(void)
{
	do_posix_clock_monotonic_gettime(&current->delays->end);
	return timespec_diff_ns(&current->delays->start, &current->delays->end);
}

void __delayacct_blkio(void)
{
	unsigned long long delay;

	delay = delayacct_measure();

	spin_lock(&current->delays->lock);
	current->delays->blkio_delay += delay;
	current->delays->blkio_count++;
	spin_unlock(&current->delays->lock);
}

void __delayacct_swapin(void)
{
	unsigned long long delay;

	delay = delayacct_measure();

	spin_lock(&current->delays->lock);
	current->delays->swapin_delay += delay;
	current->delays->swapin_count++;
	spin_unlock(&current->delays->lock);
}

/* Allocate task_delay_info for all tasks without one */
static int alloc_delays(void)
{
	int cnt=0, i;
	struct task_struct *g, *t;
	struct task_delay_info **delayp;

	read_lock(&tasklist_lock);
	do_each_thread(g, t) {
		if (!t->delays)
			cnt++;
	} while_each_thread(g, t);
	read_unlock(&tasklist_lock);

	if (!cnt)
		return 0;

	delayp = kmalloc(cnt *sizeof(struct task_delay_info *), GFP_KERNEL);
	if (!delayp)
		return -ENOMEM;
	for (i = 0; i < cnt; i++) {
		delayp[i] = kmem_cache_alloc(delayacct_cache, SLAB_KERNEL);
		if (!delayp[i])
			goto out;
		memset(delayp[i], 0, sizeof(*delayp[i]));
		spin_lock_init(&delayp[i]->lock);
	}

	read_lock(&tasklist_lock);
	do_each_thread(g, t) {
		if (t->delays)
			continue;
		t->delays = delayp[--i];
		if (i<0)
			break;
	} while_each_thread(g, t);
	read_unlock(&tasklist_lock);

	if (i)
		BUG();
	return 0;
out:
	--i;
	while (i >= 0)
		kmem_cache_free(delayacct_cache, delayp[--i]);
	return -ENOMEM;
}

/* Reset task_delay_info structs for all tasks */
static void reset_delays(void)
{
	struct task_struct *g, *t;

	read_lock(&tasklist_lock);
	do_each_thread(g, t) {
		if (!t->delays)
			BUG();
		memset(t->delays, 0, sizeof(struct task_delay_info));
		spin_lock_init(&t->delays->lock);
	} while_each_thread(g, t);
	read_unlock(&tasklist_lock);
}

int delayacct_sysctl_handler(ctl_table *table, int write, struct file *filp,
			      void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret, prev;

	prev = delayacct_on;
	ret = proc_dointvec(table, write, filp, buffer, lenp, ppos);
	if (ret || (prev == delayacct_on))
		return ret;

	if (delayacct_on)
		ret = alloc_delays();
	else
		reset_delays();
	if (ret)
		delayacct_on = prev;
	return ret;
}

