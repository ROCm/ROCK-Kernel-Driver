/*
 * tqueue.h --- task queue handling for Linux.
 *
 * Modified version of previous incarnations of task-queues,
 * written by:
 *
 * (C) 1994 Kai Petzke, wpp@marie.physik.tu-berlin.de
 * Modified for use in the Linux kernel by Theodore Ts'o,
 * tytso@mit.edu.
 */

#ifndef _LINUX_TQUEUE_H
#define _LINUX_TQUEUE_H

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <asm/system.h>

struct tq_struct {
	struct list_head list;		/* linked list of active tq's */
	unsigned long sync;		/* must be initialized to zero */
	void (*routine)(void *);	/* function to call */
	void *data;			/* argument to function */
};

/*
 * Emit code to initialise a tq_struct's routine and data pointers
 */
#define PREPARE_TQUEUE(_tq, _routine, _data)			\
	do {							\
		(_tq)->routine = _routine;			\
		(_tq)->data = _data;				\
	} while (0)

/*
 * Emit code to initialise all of a tq_struct
 */
#define INIT_TQUEUE(_tq, _routine, _data)			\
	do {							\
		INIT_LIST_HEAD(&(_tq)->list);			\
		(_tq)->sync = 0;				\
		PREPARE_TQUEUE((_tq), (_routine), (_data));	\
	} while (0)

#define DECLARE_TASK_QUEUE(q)	LIST_HEAD(q)

/* Schedule a tq to run in process context */
extern int schedule_task(struct tq_struct *task);

/* finish all currently pending tasks - do not call from irq context */
extern void flush_scheduled_tasks(void);

#endif

