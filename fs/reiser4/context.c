/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Manipulation of reiser4_context */

/*
 * global context used during system call. Variable of this type is allocated
 * on the stack at the beginning of the reiser4 part of the system call and
 * pointer to it is stored in the current->fs_context. This allows us to avoid
 * passing pointer to current transaction and current lockstack (both in
 * one-to-one mapping with threads) all over the call chain.
 *
 * It's kind of like those global variables the prof used to tell you not to
 * use in CS1, except thread specific.;-) Nikita, this was a good idea.
 *
 * In some situations it is desirable to have ability to enter reiser4_context
 * more than once for the same thread (nested contexts). For example, there
 * are some functions that can be called either directly from VFS/VM or from
 * already active reiser4 context (->writepage, for example).
 *
 * In such situations "child" context acts like dummy: all activity is
 * actually performed in the top level context, and get_current_context()
 * always returns top level context. Of course, init_context()/done_context()
 * have to be properly nested any way.
 *
 * Note that there is an important difference between reiser4 uses
 * ->fs_context and the way other file systems use it. Other file systems
 * (ext3 and reiserfs) use ->fs_context only for the duration of _transaction_
 * (this is why ->fs_context was initially called ->journal_info). This means,
 * that when ext3 or reiserfs finds that ->fs_context is not NULL on the entry
 * to the file system, they assume that some transaction is already underway,
 * and usually bail out, because starting nested transaction would most likely
 * lead to the deadlock. This gives false positives with reiser4, because we
 * set ->fs_context before starting transaction.
 */

#include "debug.h"
#include "super.h"
#include "context.h"

#include <linux/writeback.h> /* balance_dirty_pages() */

#if REISER4_DEBUG_CONTEXTS
/* List of all currently active contexts, used for debugging purposes.  */
context_list_head active_contexts;
/* lock protecting access to active_contexts. */
spinlock_t active_contexts_lock;

void
check_contexts(void)
{
	reiser4_context *ctx;

	spin_lock(&active_contexts_lock);
	for_all_type_safe_list(context, &active_contexts, ctx) {
		assert("vs-$BIGNUM", ctx->magic == context_magic);
	}
	spin_unlock(&active_contexts_lock);
}
/* REISER4_DEBUG_CONTEXTS */
#endif

struct {
	void *task;
	void *context;
	void *path[16];
} context_ok;



reiser4_internal void get_context_ok(reiser4_context *ctx)
{
	int i;
	void *addr = NULL, *frame = NULL;

#define CTX_FRAME(nr)						\
	case (nr):						\
		addr  = __builtin_return_address((nr));	 	\
                frame = __builtin_frame_address(nr);		\
		break

	memset(&context_ok, 0, sizeof(context_ok));

	context_ok.task = current;
	context_ok.context = ctx;
	for (i = 0; i < 16; i ++) {
		switch(i) {
			CTX_FRAME(0);
			CTX_FRAME(1);
			CTX_FRAME(2);
			CTX_FRAME(3);
			CTX_FRAME(4);
			CTX_FRAME(5);
			CTX_FRAME(6);
			CTX_FRAME(7);
			CTX_FRAME(8);
			CTX_FRAME(9);
			CTX_FRAME(10);
			CTX_FRAME(11);
			CTX_FRAME(12);
			CTX_FRAME(13);
			CTX_FRAME(14);
			CTX_FRAME(15);
		default:
			impossible("", "");
		}
		if (frame > (void *)ctx)
			break;
		context_ok.path[i] = addr;
	}
#undef CTX_FRAME
}


/* initialise context and bind it to the current thread

   This function should be called at the beginning of reiser4 part of
   syscall.
*/
reiser4_internal int
init_context(reiser4_context * context	/* pointer to the reiser4 context
					 * being initalised */ ,
	     struct super_block *super	/* super block we are going to
					 * work with */)
{
	assert("nikita-2662", !in_interrupt() && !in_irq());
	assert("nikita-3356", context != NULL);
	assert("nikita-3357", super != NULL);
	assert("nikita-3358", super->s_op == NULL || is_reiser4_super(super));

	xmemset(context, 0, sizeof *context);

	if (is_in_reiser4_context()) {
		reiser4_context *parent;

		parent = (reiser4_context *) current->journal_info;
		/* NOTE-NIKITA this is dubious */
		if (parent->super == super) {
			context->parent = parent;
#if (REISER4_DEBUG)
			++context->parent->nr_children;
#endif
			return 0;
		}
	}

	context->super = super;
	context->magic = context_magic;
	context->outer = current->journal_info;
	current->journal_info = (void *) context;

	init_lock_stack(&context->stack);

	txn_begin(context);

	context->parent = context;
	tap_list_init(&context->taps);
#if REISER4_DEBUG
#if REISER4_DEBUG_CONTEXTS
	context_list_clean(context);	/* to satisfy assertion */
	spin_lock(&active_contexts_lock);
	context_list_check(&active_contexts);
	context_list_push_front(&active_contexts, context);
	/*check_contexts();*/
	spin_unlock(&active_contexts_lock);
#endif
	context->task = current;
#endif
	grab_space_enable();
	return 0;
}

/* cast lock stack embedded into reiser4 context up to its container */
reiser4_internal reiser4_context *
get_context_by_lock_stack(lock_stack * owner)
{
	return container_of(owner, reiser4_context, stack);
}

/* true if there is already _any_ reiser4 context for the current thread */
reiser4_internal int
is_in_reiser4_context(void)
{
	reiser4_context *ctx;

	ctx = current->journal_info;
	return
		ctx != NULL &&
		((unsigned long) ctx->magic) == context_magic;
}

/*
 * call balance dirty pages for the current context.
 *
 * File system is expected to call balance_dirty_pages_ratelimited() whenever
 * it dirties a page. reiser4 does this for unformatted nodes (that is, during
 * write---this covers vast majority of all dirty traffic), but we cannot do
 * this immediately when formatted node is dirtied, because long term lock is
 * usually held at that time. To work around this, dirtying of formatted node
 * simply increases ->nr_marked_dirty counter in the current reiser4
 * context. When we are about to leave this context,
 * balance_dirty_pages_ratelimited() is called, if necessary.
 *
 * This introduces another problem: sometimes we do not want to run
 * balance_dirty_pages_ratelimited() when leaving a context, for example
 * because some important lock (like ->i_sem on the parent directory) is
 * held. To achieve this, ->nobalance flag can be set in the current context.
 */
static void
balance_dirty_pages_at(reiser4_context * context)
{
	reiser4_super_info_data * sbinfo = get_super_private(context->super);

	/*
	 * call balance_dirty_pages_ratelimited() to process formatted nodes
	 * dirtied during this system call.
	 */
	if (context->nr_marked_dirty != 0 &&   /* were any nodes dirtied? */
	    /* aren't we called early during mount? */
	    sbinfo->fake &&
	    /* don't call balance dirty pages from ->writepage(): it's
	     * deadlock prone */
	    !(current->flags & PF_MEMALLOC) &&
	    /* and don't stall pdflush */
	    !current_is_pdflush())
		balance_dirty_pages_ratelimited(sbinfo->fake->i_mapping);
}

/*
 * exit reiser4 context. Call balance_dirty_pages_at() if necessary. Close
 * transaction. Call done_context() to do context related book-keeping.
 */
reiser4_internal void reiser4_exit_context(reiser4_context * context)
{
	assert("nikita-3021", schedulable());

	if (context == context->parent) {
		if (!context->nobalance) {
			txn_restart(context);
			balance_dirty_pages_at(context);
		}
		txn_end(context);
	}
	done_context(context);
}

/* release resources associated with context.

   This function should be called at the end of "session" with reiser4,
   typically just before leaving reiser4 driver back to VFS.

   This is good place to put some degugging consistency checks, like that
   thread released all locks and closed transcrash etc.

*/
reiser4_internal void
done_context(reiser4_context * context /* context being released */)
{
	reiser4_context *parent;
	assert("nikita-860", context != NULL);

	parent = context->parent;
	assert("nikita-2174", parent != NULL);
	assert("nikita-2093", parent == parent->parent);
	assert("nikita-859", parent->magic == context_magic);
	assert("vs-646", (reiser4_context *) current->journal_info == parent);
	assert("zam-686", !in_interrupt() && !in_irq());

	/* only do anything when leaving top-level reiser4 context. All nested
	 * contexts are just dummies. */
	if (parent == context) {
		assert("jmacd-673", parent->trans == NULL);
		assert("jmacd-1002", lock_stack_isclean(&parent->stack));
		assert("nikita-1936", no_counters_are_held());
		assert("nikita-3403", !delayed_inode_updates(context->dirty));
		assert("nikita-2626", tap_list_empty(taps_list()));
		assert("zam-1004", get_super_private(context->super)->delete_sema_owner != current);

		/* release all grabbed but as yet unused blocks */
		if (context->grabbed_blocks != 0)
			all_grabbed2free();

		/*
		 * synchronize against longterm_unlock_znode():
		 * wake_up_requestor() wakes up requestors without holding
		 * zlock (otherwise they will immediately bump into that lock
		 * after wake up on another CPU). To work around (rare)
		 * situation where requestor has been woken up asynchronously
		 * and managed to run until completion (and destroy its
		 * context and lock stack) before wake_up_requestor() called
		 * wake_up() on it, wake_up_requestor() synchronize on lock
		 * stack spin lock. It has actually been observed that spin
		 * lock _was_ locked at this point, because
		 * wake_up_requestor() took interrupt.
		 */
		spin_lock_stack(&context->stack);
		spin_unlock_stack(&context->stack);

#if REISER4_DEBUG_CONTEXTS
		/* remove from active contexts */
		spin_lock(&active_contexts_lock);
		/*check_contexts();*/
		context_list_remove(parent);
		spin_unlock(&active_contexts_lock);
#endif
		assert("zam-684", context->nr_children == 0);
		/* restore original ->fs_context value */
		current->journal_info = context->outer;
	} else {
#if REISER4_DEBUG
		parent->nr_children--;
		assert("zam-685", parent->nr_children >= 0);
#endif
	}
}

/* Initialize list of all contexts */
reiser4_internal int
init_context_mgr(void)
{
#if REISER4_DEBUG_CONTEXTS
	spin_lock_init(&active_contexts_lock);
	context_list_init(&active_contexts);
#endif
	return 0;
}

#if REISER4_DEBUG_OUTPUT
/* debugging function: output reiser4 context contexts in the human readable
 * form  */
reiser4_internal void
print_context(const char *prefix, reiser4_context * context)
{
	if (context == NULL) {
		printk("%s: null context\n", prefix);
		return;
	}
#if REISER4_TRACE
	printk("%s: trace_flags: %x\n", prefix, context->trace_flags);
#endif
	print_lock_counters("\tlocks", &context->locks);
#if REISER4_DEBUG
	printk("pid: %i, comm: %s\n", context->task->pid, context->task->comm);
#endif
	print_lock_stack("\tlock stack", &context->stack);
	info_atom("\tatom", context->trans_in_ctx.atom);
}

#if REISER4_DEBUG_CONTEXTS
/* debugging: dump contents of all active contexts */
void
print_contexts(void)
{
	reiser4_context *context;

	spin_lock(&active_contexts_lock);

	for_all_type_safe_list(context, &active_contexts, context) {
		print_context("context", context);
	}

	spin_unlock(&active_contexts_lock);
}
#endif
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
