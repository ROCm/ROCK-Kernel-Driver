/* Copyright 2001, 2002, 2003, 2004 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Reiser4 context. See context.c for details. */

#if !defined( __REISER4_CONTEXT_H__ )
#define __REISER4_CONTEXT_H__

#include "forward.h"
#include "debug.h"
#include "spin_macros.h"
#include "dformat.h"
#include "type_safe_list.h"
#include "tap.h"
#include "lock.h"

#include <linux/types.h>	/* for __u??  */
#include <linux/fs.h>		/* for struct super_block  */
#include <linux/spinlock.h>
#include <linux/sched.h>	/* for struct task_struct */

/* list of active lock stacks */
#if REISER4_DEBUG_CONTEXTS
TYPE_SAFE_LIST_DECLARE(context);
#endif

ON_DEBUG(TYPE_SAFE_LIST_DECLARE(flushers);)

#if REISER4_DEBUG

/*
 * Stat-data update tracking.
 *
 * Some reiser4 functions (reiser4_{del,add}_nlink() take an additional
 * parameter indicating whether stat-data update should be performed. This is
 * because sometimes fields of the same inode are modified several times
 * during single system and updating stat-data (which implies tree lookup and,
 * sometimes, tree balancing) on each inode modification is too expensive. To
 * avoid unnecessary stat-data updates, we pass flag to not update it during
 * inode field updates, and update it manually at the end of the system call.
 *
 * This introduces a possibility of "missed stat data update" when final
 * stat-data update is not performed in some code path. To detect and track
 * down such situations following code was developed.
 *
 * dirty_inode_info is an array of slots. Each slot keeps information about
 * "delayed stat data update", that is about a call to a function modifying
 * inode field that was instructed to not update stat data. Direct call to
 * reiser4_update_sd() clears corresponding slot. On leaving reiser4 context
 * all slots are scanned and information about still not forced updates is
 * printed.
 */

/* how many delayed stat data update slots to remember */
#define TRACKED_DELAYED_UPDATE (0)

typedef struct {
	ino_t ino;      /* inode number of object with delayed stat data
			 * update */
	int   delayed;  /* 1 if update is delayed, 0 if update for forced */
	void *stack[4]; /* stack back-trace of the call chain where update was
			 * delayed */
} dirty_inode_info[TRACKED_DELAYED_UPDATE];

extern void mark_inode_update(struct inode *object, int immediate);
extern int  delayed_inode_updates(dirty_inode_info info);

#else

typedef struct {} dirty_inode_info;

#define mark_inode_update(object, immediate) noop
#define delayed_inode_updates(info) noop

#endif

/* reiser4 per-thread context */
struct reiser4_context {
	/* magic constant. For identification of reiser4 contexts. */
	__u32 magic;

	/* current lock stack. See lock.[ch]. This is where list of all
	   locks taken by current thread is kept. This is also used in
	   deadlock detection. */
	lock_stack stack;

	/* current transcrash. */
	txn_handle *trans;
	/* transaction handle embedded into reiser4_context. ->trans points
	 * here by default. */
	txn_handle trans_in_ctx;

	/* super block we are working with.  To get the current tree
	   use &get_super_private (reiser4_get_current_sb ())->tree. */
	struct super_block *super;

	/* parent fs activation */
	struct fs_activation *outer;

	/* per-thread grabbed (for further allocation) blocks counter */
	reiser4_block_nr grabbed_blocks;

	/* parent context */
	reiser4_context *parent;

	/* list of taps currently monitored. See tap.c */
	tap_list_head taps;

	/* grabbing space is enabled */
	int grab_enabled  :1;
    	/* should be set when we are write dirty nodes to disk in jnode_flush or
	 * reiser4_write_logs() */
	int writeout_mode :1;
	/* true, if current thread is an ent thread */
	int entd          :1;
	/* true, if balance_dirty_pages() should not be run when leaving this
	 * context. This is used to avoid lengthly balance_dirty_pages()
	 * operation when holding some important resource, like directory
	 * ->i_sem */
	int nobalance     :1;

	/* count non-trivial jnode_set_dirty() calls */
	unsigned long nr_marked_dirty;
#if REISER4_DEBUG
	/* A link of all active contexts. */
	context_list_link contexts_link;
	/* debugging information about reiser4 locks held by the current
	 * thread */
	lock_counters_info locks;
	int nr_children;	/* number of child contexts */
	struct task_struct *task; /* so we can easily find owner of the stack */

	/*
	 * disk space grabbing debugging support
	 */
	/* how many disk blocks were grabbed by the first call to
	 * reiser4_grab_space() in this context */
	reiser4_block_nr grabbed_initially;
	/* stack back-trace of the first call to reiser4_grab_space() in this
	 * context */
	backtrace_path   grabbed_at;

	/* list of all threads doing flush currently */
	flushers_list_link  flushers_link;
	/* information about last error encountered by reiser4 */
	err_site err;
	/* information about delayed stat data updates. See above. */
	dirty_inode_info dirty;
#endif

#if REISER4_TRACE
	/* per-thread tracing flags. Use reiser4_trace_flags enum to set
	   bits in it. */
	__u32 trace_flags;
#endif
#if REISER4_DEBUG_NODE
	/*
	 * don't perform node consistency checks while this is greater than
	 * zero. Used during operations that temporary violate node
	 * consistency.
	 */
	int disable_node_check;
#endif
};

#if REISER4_DEBUG_CONTEXTS
TYPE_SAFE_LIST_DEFINE(context, reiser4_context, contexts_link);
#endif
#if REISER4_DEBUG
TYPE_SAFE_LIST_DEFINE(flushers, reiser4_context, flushers_link);
#endif

extern reiser4_context *get_context_by_lock_stack(lock_stack *);

/* Debugging helps. */
extern int init_context_mgr(void);
#if REISER4_DEBUG_OUTPUT
extern void print_context(const char *prefix, reiser4_context * ctx);
#else
#define print_context(p,c) noop
#endif

#if REISER4_DEBUG_CONTEXTS && REISER4_DEBUG_OUTPUT
extern void print_contexts(void);
#else
#define print_contexts() noop
#endif

#if REISER4_DEBUG_CONTEXTS
extern void check_contexts(void);
#else
#define check_contexts() noop
#endif

#define current_tree (&(get_super_private(reiser4_get_current_sb())->tree))
#define current_blocksize reiser4_get_current_sb()->s_blocksize
#define current_blocksize_bits reiser4_get_current_sb()->s_blocksize_bits

extern int init_context(reiser4_context * context, struct super_block *super);
extern void done_context(reiser4_context * context);

/* magic constant we store in reiser4_context allocated at the stack. Used to
   catch accesses to staled or uninitialized contexts. */
#define context_magic ((__u32) 0x4b1b5d0b)

extern int is_in_reiser4_context(void);

/* return context associated with given thread */

void get_context_ok(reiser4_context *);

/*
 * return reiser4_context for the thread @tsk
 */
static inline reiser4_context *
get_context(const struct task_struct *tsk)
{
	assert("vs-1682", ((reiser4_context *) tsk->journal_info)->magic == context_magic);
	return (reiser4_context *) tsk->journal_info;
}

/*
 * return reiser4 context of the current thread, or NULL if there is none.
 */
static inline reiser4_context *
get_current_context_check(void)
{
	if (is_in_reiser4_context())
		return get_context(current);
	else
		return NULL;
}

static inline reiser4_context * get_current_context(void);/* __attribute__((const));*/

/* return context associated with current thread */
static inline reiser4_context *
get_current_context(void)
{
	return get_context(current);
}

/*
 * true if current thread is in the write-out mode. Thread enters write-out
 * mode during jnode_flush and reiser4_write_logs().
 */
static inline int is_writeout_mode(void)
{
	return get_current_context()->writeout_mode;
}

/*
 * enter write-out mode
 */
static inline void writeout_mode_enable(void)
{
	assert("zam-941", !get_current_context()->writeout_mode);
	get_current_context()->writeout_mode = 1;
}

/*
 * leave write-out mode
 */
static inline void writeout_mode_disable(void)
{
	assert("zam-942", get_current_context()->writeout_mode);
	get_current_context()->writeout_mode = 0;
}

static inline void grab_space_enable(void)
{
	get_current_context()->grab_enabled = 1;
}

static inline void grab_space_disable(void)
{
	get_current_context()->grab_enabled = 0;
}

static inline void grab_space_set_enabled (int enabled)
{
	get_current_context()->grab_enabled = enabled;
}

static inline int is_grab_enabled(reiser4_context *ctx)
{
	return ctx->grab_enabled;
}

/* mark transaction handle in @ctx as TXNH_DONT_COMMIT, so that no commit or
 * flush would be performed when it is closed. This is necessary when handle
 * has to be closed under some coarse semaphore, like i_sem of
 * directory. Commit will be performed by ktxnmgrd. */
static inline void context_set_commit_async(reiser4_context * context)
{
	context = context->parent;
	context->nobalance = 1;
	context->trans->flags |= TXNH_DONT_COMMIT;
}

extern void reiser4_exit_context(reiser4_context * context);

/* __REISER4_CONTEXT_H__ */
#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
