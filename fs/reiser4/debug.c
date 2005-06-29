/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Debugging facilities. */

/*
 * This file contains generic debugging functions used by reiser4. Roughly
 * following:
 *
 *     panicking: reiser4_do_panic(), reiser4_print_prefix().
 *
 *     locking: schedulable(), lock_counters(), print_lock_counters(),
 *     no_counters_are_held(), commit_check_locks()
 *
 *     {debug,trace,log}_flags: reiser4_are_all_debugged(),
 *     reiser4_is_debugged(), get_current_trace_flags(),
 *     get_current_log_flags().
 *
 *     kmalloc/kfree leak detection: reiser4_kmalloc(), reiser4_kfree(),
 *     reiser4_kfree_in_sb().
 *
 *     error code monitoring (see comment before RETERR macro): return_err(),
 *     report_err().
 *
 *     stack back-tracing: fill_backtrace()
 *
 *     miscellaneous: preempt_point(), call_on_each_assert(), debugtrap().
 *
 */

#include "reiser4.h"
#include "context.h"
#include "super.h"
#include "txnmgr.h"
#include "znode.h"

#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/kallsyms.h>
#include <linux/vmalloc.h>
#include <linux/ctype.h>
#include <linux/sysctl.h>
#include <linux/hardirq.h>

#if REISER4_DEBUG
static void report_err(void);
#else
#define report_err() noop
#endif

/*
 * global buffer where message given to reiser4_panic is formatted.
 */
static char panic_buf[REISER4_PANIC_MSG_BUFFER_SIZE];

/*
 * lock protecting consistency of panic_buf under concurrent panics
 */
static spinlock_t panic_guard = SPIN_LOCK_UNLOCKED;

#if REISER4_DEBUG
static int
reiser4_is_debugged(struct super_block *super, __u32 flag);
#endif

/* Your best friend. Call it on each occasion.  This is called by
    fs/reiser4/debug.h:reiser4_panic(). */
reiser4_internal void
reiser4_do_panic(const char *format /* format string */ , ... /* rest */)
{
	static int in_panic = 0;
	va_list args;

	/*
	 * check for recursive panic.
	 */
	if (in_panic == 0) {
		in_panic = 1;

		spin_lock(&panic_guard);
		va_start(args, format);
		vsnprintf(panic_buf, sizeof(panic_buf), format, args);
		va_end(args);
		printk(KERN_EMERG "reiser4 panicked cowardly: %s", panic_buf);
		spin_unlock(&panic_guard);

		/*
		 * if kernel debugger is configured---drop in. Early dropping
		 * into kgdb is not always convenient, because panic message
		 * is not yet printed most of the times. But:
		 *
		 *     (1) message can be extracted from printk_buf[]
		 *     (declared static inside of printk()), and
		 *
		 *     (2) sometimes serial/kgdb combo dies while printing
		 *     long panic message, so it's more prudent to break into
		 *     debugger earlier.
		 *
		 */
		DEBUGON(1);

#if REISER4_DEBUG
		if (get_current_context_check() != NULL) {
			struct super_block *super;
			reiser4_context *ctx;

			/*
			 * if we are within reiser4 context, print it contents:
			 */

			/* lock counters... */
			ON_DEBUG(print_lock_counters("pins held", lock_counters()));
			/* other active contexts... */
			ON_DEBUG(print_contexts());
			ctx = get_current_context();
			super = ctx->super;
			if (get_super_private(super) != NULL &&
			    reiser4_is_debugged(super, REISER4_VERBOSE_PANIC))
				/* znodes... */
				print_znodes("znodes", current_tree);
			{
				extern spinlock_t active_contexts_lock;

				/*
				 * remove context from the list of active
				 * contexts. This is precaution measure:
				 * current is going to die, and leaving
				 * context on the list would render latter
				 * corrupted.
				 */
				spin_lock(&active_contexts_lock);
				context_list_remove(ctx->parent);
				spin_unlock(&active_contexts_lock);
			}
		}
#endif
	}
	BUG();
	/* to make gcc happy about noreturn attribute */
	panic("%s", panic_buf);
}

reiser4_internal void
reiser4_print_prefix(const char *level, int reperr, const char *mid,
		     const char *function, const char *file, int lineno)
{
	const char *comm;
	int   pid;

	if (unlikely(in_interrupt() || in_irq())) {
		comm = "interrupt";
		pid  = 0;
	} else {
		comm = current->comm;
		pid  = current->pid;
	}
	printk("%sreiser4[%.16s(%i)]: %s (%s:%i)[%s]:\n",
	       level, comm, pid, function, file, lineno, mid);
	if (reperr)
		report_err();
}

/* Preemption point: this should be called periodically during long running
   operations (carry, allocate, and squeeze are best examples) */
reiser4_internal int
preempt_point(void)
{
	assert("nikita-3008", schedulable());
	cond_resched();
	return signal_pending(current);
}

#if REISER4_DEBUG

/* check that no spinlocks are held */
int schedulable(void)
{
	if (get_current_context_check() != NULL) {
		if (!LOCK_CNT_NIL(spin_locked)) {
			print_lock_counters("in atomic", lock_counters());
			return 0;
		}
	}
	might_sleep();
	return 1;
}
#endif

#if REISER4_DEBUG
/* Debugging aid: return struct where information about locks taken by current
   thread is accumulated. This can be used to formulate lock ordering
   constraints and various assertions.

*/
lock_counters_info *
lock_counters(void)
{
	reiser4_context *ctx = get_current_context();
	assert("jmacd-1123", ctx != NULL);
	return &ctx->locks;
}

/*
 * print human readable information about locks held by the reiser4 context.
 */
void
print_lock_counters(const char *prefix, const lock_counters_info * info)
{
	printk("%s: jnode: %i, tree: %i (r:%i,w:%i), dk: %i (r:%i,w:%i)\n"
	       "jload: %i, "
	       "txnh: %i, atom: %i, stack: %i, txnmgr: %i, "
	       "ktxnmgrd: %i, fq: %i, reiser4_sb: %i\n"
	       "inode: %i, "
	       "cbk_cache: %i (r:%i,w%i), "
	       "epoch: %i, eflush: %i, "
	       "zlock: %i (r:%i, w:%i)\n"
	       "spin: %i, long: %i inode_sem: (r:%i,w:%i)\n"
	       "d: %i, x: %i, t: %i\n", prefix,
	       info->spin_locked_jnode,
	       info->rw_locked_tree, info->read_locked_tree,
	       info->write_locked_tree,

	       info->rw_locked_dk, info->read_locked_dk, info->write_locked_dk,

	       info->spin_locked_jload,
	       info->spin_locked_txnh,
	       info->spin_locked_atom, info->spin_locked_stack,
	       info->spin_locked_txnmgr, info->spin_locked_ktxnmgrd,
	       info->spin_locked_fq, info->spin_locked_super,
	       info->spin_locked_inode_object,

	       info->rw_locked_cbk_cache,
	       info->read_locked_cbk_cache,
	       info->write_locked_cbk_cache,

	       info->spin_locked_epoch,
	       info->spin_locked_super_eflush,

	       info->rw_locked_zlock,
	       info->read_locked_zlock,
	       info->write_locked_zlock,

	       info->spin_locked,
	       info->long_term_locked_znode,
	       info->inode_sem_r, info->inode_sem_w,
	       info->d_refs, info->x_refs, info->t_refs);
}

/*
 * return true, iff no locks are held.
 */
int
no_counters_are_held(void)
{
	lock_counters_info *counters;

	counters = lock_counters();
	return
		(counters->rw_locked_zlock == 0) &&
		(counters->read_locked_zlock == 0) &&
		(counters->write_locked_zlock == 0) &&
		(counters->spin_locked_jnode == 0) &&
		(counters->rw_locked_tree == 0) &&
		(counters->read_locked_tree == 0) &&
		(counters->write_locked_tree == 0) &&
		(counters->rw_locked_dk == 0) &&
		(counters->read_locked_dk == 0) &&
		(counters->write_locked_dk == 0) &&
		(counters->spin_locked_txnh == 0) &&
		(counters->spin_locked_atom == 0) &&
		(counters->spin_locked_stack == 0) &&
		(counters->spin_locked_txnmgr == 0) &&
		(counters->spin_locked_inode_object == 0) &&
		(counters->spin_locked == 0) &&
		(counters->long_term_locked_znode == 0) &&
		(counters->inode_sem_r == 0) &&
		(counters->inode_sem_w == 0);
}

/*
 * return true, iff transaction commit can be done under locks held by the
 * current thread.
 */
int
commit_check_locks(void)
{
	lock_counters_info *counters;
	int inode_sem_r;
	int inode_sem_w;
	int result;

	/*
	 * inode's read/write semaphore is the only reiser4 lock that can be
	 * held during commit.
	 */

	counters = lock_counters();
	inode_sem_r = counters->inode_sem_r;
	inode_sem_w = counters->inode_sem_w;

	counters->inode_sem_r = counters->inode_sem_w = 0;
	result = no_counters_are_held();
	counters->inode_sem_r = inode_sem_r;
	counters->inode_sem_w = inode_sem_w;
	return result;
}

/*
 * check that some bits specified by @flags are set in ->debug_flags of the
 * super block.
 */
static int
reiser4_is_debugged(struct super_block *super, __u32 flag)
{
	return get_super_private(super)->debug_flags & flag;
}

/* REISER4_DEBUG */
#endif

/* allocate memory. This calls kmalloc(), performs some additional checks, and
   keeps track of how many memory was allocated on behalf of current super
   block. */
reiser4_internal void *
reiser4_kmalloc(size_t size /* number of bytes to allocate */ ,
		int gfp_flag /* allocation flag */ )
{
	void *result;

	assert("nikita-3009", ergo(gfp_flag & __GFP_WAIT, schedulable()));

	result = kmalloc(size, gfp_flag);
#if REISER4_DEBUG
	if (result != NULL) {
		reiser4_super_info_data *sbinfo;

		sbinfo = get_current_super_private();
		assert("nikita-1407", sbinfo != NULL);
		reiser4_spin_lock_sb(sbinfo);
		sbinfo->kmallocs ++;
		reiser4_spin_unlock_sb(sbinfo);
	}
#endif
	return result;
}

/* release memory allocated by reiser4_kmalloc() and update counter. */
reiser4_internal void
reiser4_kfree(void *area /* memory to from */)
{
	assert("nikita-1410", area != NULL);
	return reiser4_kfree_in_sb(area, reiser4_get_current_sb());
}

/* release memory allocated by reiser4_kmalloc() for the specified
 * super-block. This is useful when memory is released outside of reiser4
 * context */
reiser4_internal void
reiser4_kfree_in_sb(void *area /* memory to from */, struct super_block *sb)
{
	assert("nikita-2729", area != NULL);
#if REISER4_DEBUG
	{
		reiser4_super_info_data *sbinfo;

		sbinfo = get_super_private(sb);
		reiser4_spin_lock_sb(sbinfo);
		assert("nikita-2730", sbinfo->kmallocs > 0);
		sbinfo->kmallocs --;
		reiser4_spin_unlock_sb(sbinfo);
	}
#endif
	kfree(area);
}

#if REISER4_DEBUG

/*
 * fill "error site" in the current reiser4 context. See comment before RETERR
 * macro for more details.
 */
void
return_err(int code, const char *file, int line)
{
	if (code < 0 && is_in_reiser4_context()) {
		reiser4_context *ctx = get_current_context();

		if (ctx != NULL) {
			ctx->err.code = code;
			ctx->err.file = file;
			ctx->err.line = line;
#ifdef CONFIG_FRAME_POINTER
			ctx->err.bt[0] =__builtin_return_address(0);
			ctx->err.bt[1] =__builtin_return_address(1);
			ctx->err.bt[2] =__builtin_return_address(2);
			ctx->err.bt[3] =__builtin_return_address(3);
			ctx->err.bt[4] =__builtin_return_address(4);
#endif
		}
	}
}

/*
 * report error information recorder by return_err().
 */
static void
report_err(void)
{
	reiser4_context *ctx = get_current_context_check();

	if (ctx != NULL) {
		if (ctx->err.code != 0) {
			printk("code: %i at %s:%i\n",
			       ctx->err.code, ctx->err.file, ctx->err.line);
		}
	}
}

#endif /* REISER4_DEBUG */

#if KERNEL_DEBUGGER
/*
 * this functions just drops into kernel debugger. It is a convenient place to
 * put breakpoint in.
 */
void debugtrap(void)
{
	/* do nothing. Put break point here. */
#if defined(CONFIG_KGDB) && !defined(CONFIG_REISER4_FS_MODULE)
	extern void breakpoint(void);
	breakpoint();
#endif
}
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
