/*
 *  linux/arch/arm/kernel/ptrace.h
 *
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
extern void __ptrace_cancel_bpt(struct task_struct *);
extern int ptrace_set_bpt(struct task_struct *);

/*
 * Clear a breakpoint, if one exists.
 */
static inline int ptrace_cancel_bpt(struct task_struct *tsk)
{
	int nsaved = tsk->thread.debug.nsaved;

	if (nsaved)
		__ptrace_cancel_bpt(tsk);

	return nsaved;
}

