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

#include "kattr.h"
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

extern void cond_resched(void);

/*
 * global buffer where message given to reiser4_panic is formatted.
 */
static char panic_buf[REISER4_PANIC_MSG_BUFFER_SIZE];

/*
 * lock protecting consistency of panic_buf under concurrent panics
 */
static spinlock_t panic_guard = SPIN_LOCK_UNLOCKED;

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

		if (get_current_context_check() != NULL) {
			struct super_block *super;
			reiser4_context *ctx;

			/*
			 * if we are within reiser4 context, print it contents:
			 */

			/* lock counters... */
			print_lock_counters("pins held", lock_counters());
			/* other active contexts... */
			print_contexts();
			ctx = get_current_context();
			super = ctx->super;
			if (get_super_private(super) != NULL &&
			    reiser4_is_debugged(super, REISER4_VERBOSE_PANIC))
				/* znodes... */
				print_znodes("znodes", current_tree);
#if REISER4_DEBUG_CONTEXTS
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
#endif
		}
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

#if REISER4_DEBUG_SPIN_LOCKS
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

#if REISER4_DEBUG_OUTPUT
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

/* REISER4_DEBUG_OUTPUT */
#endif

/* REISER4_DEBUG_SPIN_LOCKS */
#endif

/*
 * check that all bits specified by @flags are set in ->debug_flags of the
 * super block.
 */
reiser4_internal int
reiser4_are_all_debugged(struct super_block *super, __u32 flags)
{
	return (get_super_private(super)->debug_flags & flags) == flags;
}

/*
 * check that some bits specified by @flags are set in ->debug_flags of the
 * super block.
 */
reiser4_internal int
reiser4_is_debugged(struct super_block *super, __u32 flag)
{
	return get_super_private(super)->debug_flags & flag;
}

#if REISER4_TRACE
/* tracing setup: global trace flags stored in global variable plus
   per-thread trace flags plus per-fs trace flags.
   */
__u32 get_current_trace_flags(void)
{
	__u32 flags;
	reiser4_context *ctx;

	flags = 0;
	ctx = get_current_context_check();
	if (ctx) {
		flags |= ctx->trace_flags;
		flags |= get_super_private(ctx->super)->trace_flags;
	}
	return flags;
}
#endif

#if REISER4_LOG

/* log flags are stored in super block */
__u32 get_current_log_flags(void)
{
	__u32 flags;
	reiser4_context *ctx;

	flags = 0;
	ctx = get_current_context_check();
	if (ctx)
		flags = get_super_private(ctx->super)->log_flags;
	return flags;
}

/* oid of file page events of which are to be logged */
__u32 get_current_oid_to_log(void)
{
	__u32 oid;
	reiser4_context *ctx;

	oid = 0;
	ctx = get_current_context_check();
	if (ctx)
		oid = get_super_private(ctx->super)->oid_to_log;
	return oid;
}

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
	if (REISER4_DEBUG && result != NULL) {
		unsigned int usedsize;
		reiser4_super_info_data *sbinfo;

		usedsize = ksize(result);

		sbinfo = get_current_super_private();

		assert("nikita-3459", usedsize >= size);
		assert("nikita-1407", sbinfo != NULL);
		reiser4_spin_lock_sb(sbinfo);
		ON_DEBUG(sbinfo->kmalloc_allocated += usedsize);
		reiser4_spin_unlock_sb(sbinfo);
	}
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
	if (REISER4_DEBUG) {
		unsigned int size;
		reiser4_super_info_data *sbinfo;

		size = ksize(area);

		sbinfo = get_super_private(sb);

		reiser4_spin_lock_sb(sbinfo);
		assert("nikita-2730", sbinfo->kmalloc_allocated >= (int) size);
		ON_DEBUG(sbinfo->kmalloc_allocated -= size);
		reiser4_spin_unlock_sb(sbinfo);
	}
	kfree(area);
}


#if defined(CONFIG_REISER4_NOOPT)
void __you_cannot_kmalloc_that_much(void)
{
	BUG();
}
#endif

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
			fill_backtrace(&ctx->err.path,
				       REISER4_BACKTRACE_DEPTH, 0);
			ctx->err.code = code;
			ctx->err.file = file;
			ctx->err.line = line;
		}
	}
}

/*
 * report error information recorder by return_err().
 */
void
report_err(void)
{
	reiser4_context *ctx = get_current_context_check();

	if (ctx != NULL) {
		if (ctx->err.code != 0) {
#ifdef CONFIG_FRAME_POINTER
			int i;
			for (i = 0; i < REISER4_BACKTRACE_DEPTH ; ++ i)
				printk("0x%p ", ctx->err.path.trace[i]);
			printk("\n");
#endif
			printk("code: %i at %s:%i\n",
			       ctx->err.code, ctx->err.file, ctx->err.line);
		}
	}
}

#ifdef CONFIG_FRAME_POINTER

extern int kswapd(void *);

#include <linux/personality.h>
#include "ktxnmgrd.h"
#include "repacker.h"

/*
 * true iff @addr is between @start and @end
 */
static int is_addr_in(void *addr, void *start, void *end)
{
	return start < addr && addr < end;
}

/*
 * stack back-tracing. Also see comments before REISER4_BACKTRACE_DEPTH in
 * debug.h.
 *
 * Stack beck-trace is collected through __builtin_return_address() gcc
 * builtin, which requires kernel to be compiled with frame pointers
 * (CONFIG_FRAME_POINTER). Unfortunately, __builtin_return_address() doesn't
 * provide means to detect when bottom of the stack is reached, and just
 * crashed when trying to access non-existent frame.
 *
 * is_last_frame() function works around this (also see more advanced version
 * in the proc-sleep patch that requires modification of core kernel code).
 *
 * This functions checks for common cases trying to detect that last stack
 * frame was reached.
 */
static int is_last_frame(void *addr)
{
	if (addr == NULL)
		return 1;
	if (is_addr_in(addr, kswapd, wakeup_kswapd))
		return 1;
	else if (is_addr_in(addr, reiser4_repacker, repacker_d))
		return 1;
	else if (is_addr_in(addr, init_ktxnmgrd_context, ktxnmgrd_kick))
		return 1;
	else if (is_addr_in(addr, init_entd_context, done_entd_context))
		return 1;
	else if (!kernel_text_address((unsigned long)addr))
		return 1;
	else
		return 0;
}

/*
 * fill stack back-trace.
 */
reiser4_internal void
fill_backtrace(backtrace_path *path, int depth, int shift)
{
	int i;
	void *addr;

	cassert(REISER4_BACKTRACE_DEPTH == 4);
	assert("nikita-3229", shift < 6);

	/* long live Duff! */

#define FRAME(nr)						\
	case (nr):						\
		addr  = __builtin_return_address((nr) + 2);	\
		break

	xmemset(path, 0, sizeof *path);
	addr = NULL;
	/*
	 * we need this silly loop, because __builtin_return_address() only
	 * accepts _constant_ arguments. It reminds of the duff device
	 * (http://www.faqs.org/docs/jargon/D/Duff's-device.html) which
	 * explains the reference above.
	 */
	for (i = 0; i < depth; ++ i) {
		switch(i + shift) {
			FRAME(0);
			FRAME(1);
			FRAME(2);
			FRAME(3);
			FRAME(4);
			FRAME(5);
			FRAME(6);
			FRAME(7);
			FRAME(8);
			FRAME(9);
			FRAME(10);
		default:
			impossible("nikita-3230", "everything is wrong");
		}
		path->trace[i] = addr;
		if (is_last_frame(addr))
			break;
	}
}
#endif

/*
 * assert() macro calls this function on each invocation. This is convenient
 * place to put some debugging code that has to be executed very
 * frequently. _Very_.
 */
void call_on_each_assert(void)
{
	return;
	/*
	 * DON'T USE ASSERTIONS HERE :)
	 */
	if (is_in_reiser4_context()) {
		reiser4_super_info_data *sinfo;
		reiser4_context *ctx;

		ctx = (reiser4_context *) current->journal_info;
		sinfo = ctx->super->s_fs_info;
		/* put checks here */
	}
}

/* REISER4_DEBUG */
#endif

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


/* debugging tool
   use clog_op to make a record
   use print_clog to see last CLOG_LENGTH record
 */
#define CLOG_LENGTH 256
static spinlock_t clog_lock = SPIN_LOCK_UNLOCKED;

typedef struct {
	int id;
	pid_t pid;
	int op;
	void *data1;
	void *data2;
} clog_t;

clog_t clog[CLOG_LENGTH];

int clog_start = 0;
int clog_length = 0;
int clog_id = 0;

void
clog_op(int op, void *data1, void *data2)
{
	spin_lock(&clog_lock);

	if (clog_length == CLOG_LENGTH) {
		clog[clog_start].id = clog_id ++;
		clog[clog_start].op = op;
		clog[clog_start].pid = current->pid;
		clog[clog_start].data1 = data1;
		clog[clog_start].data2 = data2;
		clog_start ++;
		clog_start %= CLOG_LENGTH;
	} else {
		assert("vs-1672", clog_start == 0);
		clog[clog_length].id = clog_id ++;
		clog[clog_length].op = op;
		clog[clog_length].pid = current->pid;
		clog[clog_length].data1 = data1;
		clog[clog_length].data2 = data2;
		clog_length ++;
	}

	spin_unlock(&clog_lock);
}

static const char *
op2str(int op)
{
	static const char *op_names[OP_NUM] = {
		"get-user-page",
		"put_user-page",
		"ex-write-in",
		"ex-write-out",
		"readp-in",
		"readp-out",
		"ex-write-in-nr-locks",
		"ex-write-out-nr-locks",
		"link-object",
		"unlink-object"
	};
	assert("vs-1673", op < OP_NUM);
	return op_names[op];
}

void
print_clog(void)
{
	int i, j;

	j = clog_start;
	for (i = 0; i < clog_length; i ++) {
		printk("%d(%d): id %d: pid %d, op %s, data1 %p, data2 %p\n",
		       i, j, clog[j].id, clog[j].pid, op2str(clog[j].op), clog[j].data1, clog[j].data2);
		j ++;
		j %= CLOG_LENGTH;
	}
	printk("clog length %d\n", clog_length);
}

#if 0
void
print_symname(unsigned long address)
{
	char         *module;
	const char   *name;
	char          namebuf[128];
	unsigned long offset;
	unsigned long size;

	name = kallsyms_lookup(address, &size, &offset, &module, namebuf);
	if (name != NULL)
		printk("  %s[%lx/%lx]", name, offset, size);
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
