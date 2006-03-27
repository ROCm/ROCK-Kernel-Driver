/* delayacct.h - per-task delay accounting
 *
 * Copyright (C) Shailabh Nagar, IBM Corp. 2006
 *
 * This program is free software;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY;  without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License for more details.
 *
 */

#ifndef _LINUX_TASKDELAYS_H
#define _LINUX_TASKDELAYS_H

#include <linux/sched.h>
#include <linux/taskstats.h>

#ifdef CONFIG_TASK_DELAY_ACCT
extern int delayacct_on;	/* Delay accounting turned on/off */
extern kmem_cache_t *delayacct_cache;
extern void delayacct_init(void);
extern void __delayacct_tsk_init(struct task_struct *);
extern void __delayacct_tsk_exit(struct task_struct *);
extern void __delayacct_blkio_start(void);
extern void __delayacct_blkio_end(void);
extern int __delayacct_add_tsk(struct taskstats *, struct task_struct *);

static inline void delayacct_tsk_init(struct task_struct *tsk)
{
	/* reinitialize in case parent's non-null pointer was dup'ed*/
	tsk->delays = NULL;
	if (unlikely(delayacct_on))
		__delayacct_tsk_init(tsk);
}

static inline void delayacct_tsk_exit(struct task_struct *tsk)
{
	if (tsk->delays)
		__delayacct_tsk_exit(tsk);
}

static inline void delayacct_blkio_start(void)
{
	if (unlikely(current->delays))
		__delayacct_blkio_start();
}
static inline void delayacct_blkio_end(void)
{
	if (unlikely(current->delays))
		__delayacct_blkio_end();
}
#else
static inline void delayacct_init(void)
{}
static inline void delayacct_tsk_init(struct task_struct *tsk)
{}
static inline void delayacct_tsk_exit(struct task_struct *tsk)
{}
static inline void delayacct_blkio_start(void)
{}
static inline void delayacct_blkio_end(void)
{}
#endif /* CONFIG_TASK_DELAY_ACCT */
#ifdef CONFIG_TASKSTATS
static inline int delayacct_add_tsk(struct taskstats *d,
				    struct task_struct *tsk)
{
	if (!tsk->delays)
		return -EINVAL;
	return __delayacct_add_tsk(d, tsk);
}
#endif
#endif /* _LINUX_TASKDELAYS_H */
