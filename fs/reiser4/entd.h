/* Copyright 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Ent daemon. */

#ifndef __ENTD_H__
#define __ENTD_H__

#include "kcond.h"
#include "context.h"

#include <linux/fs.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/sched.h>	/* for struct task_struct */
#include "type_safe_list.h"

TYPE_SAFE_LIST_DECLARE(wbq);

/* write-back request. */
struct wbq {
	wbq_list_link link;
	struct writeback_control * wbc;
	struct page * page;
	struct semaphore sem;
	int    nr_entd_iters;
};

/* ent-thread context. This is used to synchronize starting/stopping ent
 * threads. */
typedef struct entd_context {
	/*
	 * condition variable that is signaled by ent thread after it
	 * successfully started up.
	 */
	kcond_t             startup;
	/*
	 * completion that is signaled by ent thread just before it
	 * terminates.
	 */
	struct completion   finish;
	/*
	 * condition variable that ent thread waits on for more work. It's
	 * signaled by write_page_by_ent().
	 */
	kcond_t             wait;
	/* spinlock protecting other fields */
	spinlock_t          guard;
	/* ent thread */
	struct task_struct *tsk;
	/* set to indicate that ent thread should leave. */
	int                 done;
	/* counter of active flushers */
	int                 flushers;
#if REISER4_DEBUG
	/* list of all active flushers */
	flushers_list_head  flushers_list;
#endif
	int                 nr_all_requests;
	int                 nr_synchronous_requests;
	wbq_list_head       wbq_list;
} entd_context;

extern void init_entd_context(struct super_block *super);
extern void done_entd_context(struct super_block *super);

extern void enter_flush(struct super_block *super);
extern void leave_flush(struct super_block *super);

extern void write_page_by_ent(struct page *, struct writeback_control *);
extern int  wbq_available (void);
extern void ent_writes_page (struct super_block *, struct page *);
/* __ENTD_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   End:
*/
