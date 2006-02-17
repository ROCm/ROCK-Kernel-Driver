/* delayacct.h - per-task delay accounting
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

#ifndef _LINUX_TASKDELAYS_H
#define _LINUX_TASKDELAYS_H

#include <linux/sched.h>
#include <linux/sysctl.h>

#ifdef CONFIG_TASK_DELAY_ACCT
extern int delayacct_on;	/* Delay accounting turned on/off */
extern kmem_cache_t *delayacct_cache;
extern int delayacct_sysctl_handler(ctl_table *, int , struct file *,
			     void __user *, size_t *, loff_t *);
extern int delayacct_init(void);
extern void __delayacct_tsk_init(struct task_struct *tsk);
extern void __delayacct_tsk_exit(struct task_struct *tsk);
extern void __delayacct_blkio(void);
extern void __delayacct_swapin(void);

static inline void delayacct_tsk_init(struct task_struct *tsk)
{
	/* reinitialize in case parent's non-null pointer was dup'ed*/
	tsk->delays = NULL;
	if (unlikely(delayacct_on))
		__delayacct_tsk_init(tsk);
}

static inline void delayacct_tsk_exit(struct task_struct *tsk)
{
	if (unlikely(tsk->delays))
		__delayacct_tsk_exit(tsk);
}

static inline void delayacct_timestamp_start(void)
{
	if (unlikely(current->delays && delayacct_on))
		do_posix_clock_monotonic_gettime(&current->delays->start);
}

static inline void delayacct_blkio(void)
{
	if (unlikely(current->delays && delayacct_on))
		__delayacct_blkio();
}

static inline void delayacct_swapin(void)
{
	if (unlikely(current->delays && delayacct_on))
		__delayacct_swapin();
}
#else
static inline void delayacct_tsk_init(struct task_struct *tsk)
{}
static inline void delayacct_tsk_exit(struct task_struct *tsk)
{}
static inline void delayacct_timestamp_start(void)
{}
static inline void delayacct_blkio(void)
{}
static inline void delayacct_swapin(void)
{}
static inline int delayacct_init(void)
{ return 0; }
#endif /* CONFIG_TASK_DELAY_ACCT */
#endif /* _LINUX_TASKDELAYS_H */
