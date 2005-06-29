/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Transaction manager daemon. See ktxnmgrd.c for comments. */

#ifndef __KTXNMGRD_H__
#define __KTXNMGRD_H__

#include "kcond.h"
#include "txnmgr.h"
#include "spin_macros.h"

#include <linux/fs.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>
#include <linux/sched.h>	/* for struct task_struct */

/* in this structure all data necessary to start up, shut down and communicate
 * with ktxnmgrd are kept. */
struct ktxnmgrd_context {
	/* conditional variable used to synchronize start up of ktxnmgrd */
	kcond_t startup;
	/* completion used to synchronize shut down of ktxnmgrd */
	struct completion finish;
	/* condition variable on which ktxnmgrd sleeps */
	kcond_t wait;
	/* spin lock protecting all fields of this structure */
	spinlock_t guard;
	/* timeout of sleeping on ->wait */
	signed long timeout;
	/* kernel thread running ktxnmgrd */
	struct task_struct *tsk;
	/* list of all file systems served by this ktxnmgrd */
	txn_mgrs_list_head queue;
	/* is ktxnmgrd being shut down? */
	int done:1;
	/* should ktxnmgrd repeat scanning of atoms? */
	int rescan:1;
};

extern int  init_ktxnmgrd_context(txn_mgr *);
extern void done_ktxnmgrd_context(txn_mgr *);

extern int  start_ktxnmgrd(txn_mgr *);
extern void stop_ktxnmgrd(txn_mgr *);

extern void ktxnmgrd_kick(txn_mgr * mgr);

extern int is_current_ktxnmgrd(void);

/* __KTXNMGRD_H__ */
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
