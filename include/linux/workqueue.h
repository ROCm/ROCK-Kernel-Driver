/*
 * workqueue.h --- work queue handling for Linux.
 */

#ifndef _LINUX_WORKQUEUE_H
#define _LINUX_WORKQUEUE_H

#include <linux/timer.h>
#include <linux/linkage.h>

struct workqueue_struct;

struct work_struct {
	unsigned long pending;
	struct list_head entry;
	void (*func)(void *);
	void *data;
	void *wq_data;
	struct timer_list timer;
};

#define __WORK_INITIALIZER(n, f, d) {				\
        .entry	= { &(n).entry, &(n).entry },			\
	.func = (f),						\
	.data = (d),						\
	.timer = TIMER_INITIALIZER(NULL, 0, 0),			\
	}

#define DECLARE_WORK(n, f, d)					\
	struct work_struct n = __WORK_INITIALIZER(n, f, d)

/*
 * initialize a work-struct's func and data pointers:
 */
#define PREPARE_WORK(_work, _func, _data)			\
	do {							\
		(_work)->func = _func;				\
		(_work)->data = _data;				\
	} while (0)

/*
 * initialize all of a work-struct:
 */
#define INIT_WORK(_work, _func, _data)				\
	do {							\
		INIT_LIST_HEAD(&(_work)->entry);		\
		(_work)->pending = 0;				\
		PREPARE_WORK((_work), (_func), (_data));	\
		init_timer(&(_work)->timer);			\
	} while (0)

extern struct workqueue_struct *create_workqueue(const char *name);
extern void destroy_workqueue(struct workqueue_struct *wq);

extern int FASTCALL(queue_work(struct workqueue_struct *wq, struct work_struct *work));
extern int FASTCALL(queue_delayed_work(struct workqueue_struct *wq, struct work_struct *work, unsigned long delay));
extern void FASTCALL(flush_workqueue(struct workqueue_struct *wq));

extern int FASTCALL(schedule_work(struct work_struct *work));
extern int FASTCALL(schedule_delayed_work(struct work_struct *work, unsigned long delay));
extern void flush_scheduled_work(void);
extern int current_is_keventd(void);

extern void init_workqueues(void);

/*
 * Kill off a pending schedule_delayed_work().  Note that the work callback
 * function may still be running on return from cancel_delayed_work().  Run
 * flush_scheduled_work() to wait on it.
 */
static inline int cancel_delayed_work(struct work_struct *work)
{
	return del_timer_sync(&work->timer);
}

#endif

