/*
 * 'struct utrace' data structure for kernel/utrace.c private use.
 *
 * Copyright (C) 2006-2009 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#ifndef _LINUX_UTRACE_STRUCT_H
#define _LINUX_UTRACE_STRUCT_H	1

#ifdef CONFIG_UTRACE

#include <linux/list.h>
#include <linux/spinlock.h>

/*
 * Per-thread structure private to utrace implementation.  This properly
 * belongs in kernel/utrace.c and its use is entirely private to the code
 * there.  It is only defined in a header file so that it can be embedded
 * in the struct task_struct layout.  It is here rather than in utrace.h
 * to avoid header nesting order issues getting too complex.
 *
 */
struct utrace {
	struct task_struct *cloning;

	struct list_head attached, attaching;
	spinlock_t lock;

	struct utrace_engine *reporting;

	unsigned int stopped:1;
	unsigned int report:1;
	unsigned int interrupt:1;
	unsigned int signal_handler:1;
	unsigned int vfork_stop:1; /* need utrace_stop() before vfork wait */
	unsigned int death:1;	/* in utrace_report_death() now */
	unsigned int reap:1;	/* release_task() has run */
	unsigned int pending_attach:1; /* need splice_attaching() */
};

# define INIT_UTRACE(tsk)						      \
	.utrace_flags = 0,						      \
	.utrace = {							      \
		.lock = __SPIN_LOCK_UNLOCKED(tsk.utrace.lock),		      \
		.attached = LIST_HEAD_INIT(tsk.utrace.attached),	      \
		.attaching = LIST_HEAD_INIT(tsk.utrace.attaching),	      \
	},

#else

# define INIT_UTRACE(tsk)	/* Nothing. */

#endif	/* CONFIG_UTRACE */

#endif	/* linux/utrace_struct.h */
