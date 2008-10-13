/*
 * utrace infrastructure interface for debugging user processes
 *
 * Copyright (C) 2006, 2007, 2008 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 *
 * Red Hat Author: Roland McGrath.
 */

#include <linux/utrace.h>
#include <linux/tracehook.h>
#include <linux/regset.h>
#include <asm/syscall.h>
#include <linux/ptrace.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/freezer.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/seq_file.h>


#define UTRACE_DEBUG 1
#ifdef UTRACE_DEBUG
#define CHECK_INIT(p)	atomic_set(&(p)->check_dead, 1)
#define CHECK_DEAD(p)	BUG_ON(!atomic_dec_and_test(&(p)->check_dead))
#else
#define CHECK_INIT(p)	do { } while (0)
#define CHECK_DEAD(p)	do { } while (0)
#endif

/*
 * Per-thread structure task_struct.utrace points to.
 *
 * The task itself never has to worry about this going away after
 * some event is found set in task_struct.utrace_flags.
 * Once created, this pointer is changed only when the task is quiescent
 * (TASK_TRACED or TASK_STOPPED with the siglock held, or dead).
 *
 * For other parties, the pointer to this is protected by RCU and
 * task_lock.  Since call_rcu is never used while the thread is alive and
 * using this struct utrace, we can overlay the RCU data structure used
 * only for a dead struct with some local state used only for a live utrace
 * on an active thread.
 *
 * The two lists @attached and @attaching work together for smooth
 * asynchronous attaching with low overhead.  Modifying either list
 * requires @lock.  The @attaching list can be modified any time while
 * holding @lock.  New engines being attached always go on this list.
 *
 * The @attached list is what the task itself uses for its reporting
 * loops.  When the task itself is not quiescent, it can use the
 * @attached list without taking any lock.  Noone may modify the list
 * when the task is not quiescent.  When it is quiescent, that means
 * that it won't run again without taking @lock itself before using
 * the list.
 *
 * At each place where we know the task is quiescent (or it's current),
 * while holding @lock, we call splice_attaching(), below.  This moves
 * the @attaching list members on to the end of the @attached list.
 * Since this happens at the start of any reporting pass, any new
 * engines attached asynchronously go on the stable @attached list
 * in time to have their callbacks seen.
 */
struct utrace {
	union {
		struct rcu_head dead;
		struct {
			struct task_struct *cloning;
		} live;
	} u;

	struct list_head attached, attaching;
	spinlock_t lock;
#ifdef UTRACE_DEBUG
	atomic_t check_dead;
#endif

	struct utrace_attached_engine *reporting;

	unsigned int stopped:1;
	unsigned int report:1;
	unsigned int interrupt:1;
	unsigned int signal_handler:1;
	unsigned int death:1;	/* in utrace_report_death() now */
	unsigned int reap:1;	/* release_task() has run */
};

static struct kmem_cache *utrace_cachep;
static struct kmem_cache *utrace_engine_cachep;
static const struct utrace_engine_ops utrace_detached_ops; /* forward decl */

static int __init utrace_init(void)
{
	utrace_cachep = KMEM_CACHE(utrace, SLAB_PANIC);
	utrace_engine_cachep = KMEM_CACHE(utrace_attached_engine, SLAB_PANIC);
	return 0;
}
subsys_initcall(utrace_init);


/*
 * Make sure target->utrace is allocated, and return with it locked on
 * success.  This function mediates startup races.  The creating parent
 * task has priority, and other callers will delay here to let its call
 * succeed and take the new utrace lock first.
 */
static struct utrace *utrace_first_engine(struct task_struct *target,
					  struct utrace_attached_engine *engine)
	__acquires(utrace->lock)
{
	struct utrace *utrace;

	/*
	 * If this is a newborn thread and we are not the creator,
	 * we have to wait for it.  The creator gets the first chance
	 * to attach.  The PF_STARTING flag is cleared after its
	 * report_clone hook has had a chance to run.
	 */
	if (target->flags & PF_STARTING) {
		utrace = current->utrace;
		if (utrace == NULL || utrace->u.live.cloning != target) {
			yield();
			if (signal_pending(current))
				return ERR_PTR(-ERESTARTNOINTR);
			return NULL;
		}
	}

	utrace = kmem_cache_zalloc(utrace_cachep, GFP_KERNEL);
	if (unlikely(utrace == NULL))
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&utrace->attached);
	INIT_LIST_HEAD(&utrace->attaching);
	list_add(&engine->entry, &utrace->attached);
	spin_lock_init(&utrace->lock);
	CHECK_INIT(utrace);

	spin_lock(&utrace->lock);
	task_lock(target);
	if (likely(target->utrace == NULL)) {
		rcu_assign_pointer(target->utrace, utrace);

		/*
		 * The task_lock protects us against another thread doing
		 * the same thing.  We might still be racing against
		 * tracehook_release_task.  It's called with ->exit_state
		 * set to EXIT_DEAD and then checks ->utrace with an
		 * smp_mb() in between.  If EXIT_DEAD is set, then
		 * release_task might have checked ->utrace already and saw
		 * it NULL; we can't attach.  If we see EXIT_DEAD not yet
		 * set after our barrier, then we know release_task will
		 * see our target->utrace pointer.
		 */
		smp_mb();
		if (likely(target->exit_state != EXIT_DEAD)) {
			task_unlock(target);
			return utrace;
		}

		/*
		 * The target has already been through release_task.
		 * Our caller will restart and notice it's too late now.
		 */
		target->utrace = NULL;
	}

	/*
	 * Another engine attached first, so there is a struct already.
	 * A null return says to restart looking for the existing one.
	 */
	task_unlock(target);
	spin_unlock(&utrace->lock);
	kmem_cache_free(utrace_cachep, utrace);

	return NULL;
}

static void utrace_free(struct rcu_head *rhead)
{
	struct utrace *utrace = container_of(rhead, struct utrace, u.dead);
	kmem_cache_free(utrace_cachep, utrace);
}

/*
 * Called with utrace locked.  Clean it up and free it via RCU.
 */
static void rcu_utrace_free(struct utrace *utrace)
	__releases(utrace->lock)
{
	CHECK_DEAD(utrace);
	spin_unlock(&utrace->lock);
	call_rcu(&utrace->u.dead, utrace_free);
}

/*
 * This is the exported function used by the utrace_engine_put() inline.
 */
void __utrace_engine_release(struct kref *kref)
{
	struct utrace_attached_engine *engine =
		container_of(kref, struct utrace_attached_engine, kref);
	BUG_ON(!list_empty(&engine->entry));
	kmem_cache_free(utrace_engine_cachep, engine);
}
EXPORT_SYMBOL_GPL(__utrace_engine_release);

static bool engine_matches(struct utrace_attached_engine *engine, int flags,
			   const struct utrace_engine_ops *ops, void *data)
{
	if ((flags & UTRACE_ATTACH_MATCH_OPS) && engine->ops != ops)
		return false;
	if ((flags & UTRACE_ATTACH_MATCH_DATA) && engine->data != data)
		return false;
	return engine->ops && engine->ops != &utrace_detached_ops;
}

static struct utrace_attached_engine *matching_engine(
	struct utrace *utrace, int flags,
	const struct utrace_engine_ops *ops, void *data)
{
	struct utrace_attached_engine *engine;
	list_for_each_entry(engine, &utrace->attached, entry)
		if (engine_matches(engine, flags, ops, data))
			return engine;
	list_for_each_entry(engine, &utrace->attaching, entry)
		if (engine_matches(engine, flags, ops, data))
			return engine;
	return NULL;
}

/*
 * Allocate a new engine structure.  It starts out with two refs:
 * one ref for utrace_attach_task() to return, and ref for being attached.
 */
static struct utrace_attached_engine *alloc_engine(void)
{
	struct utrace_attached_engine *engine;
	engine = kmem_cache_alloc(utrace_engine_cachep, GFP_KERNEL);
	if (likely(engine)) {
		engine->flags = 0;
		kref_set(&engine->kref, 2);
	}
	return engine;
}

/**
 * utrace_attach_task - attach new engine, or look up an attached engine
 * @target:	thread to attach to
 * @flags:	flag bits combined with OR, see below
 * @ops:	callback table for new engine
 * @data:	engine private data pointer
 *
 * The caller must ensure that the @target thread does not get freed,
 * i.e. hold a ref or be its parent.  It is always safe to call this
 * on @current, or on the @child pointer in a @report_clone callback.
 * For most other cases, it's easier to use utrace_attach_pid() instead.
 *
 * UTRACE_ATTACH_CREATE:
 * Create a new engine.  If %UTRACE_ATTACH_CREATE is not specified, you
 * only look up an existing engine already attached to the thread.
 *
 * UTRACE_ATTACH_EXCLUSIVE:
 * Attempting to attach a second (matching) engine fails with -%EEXIST.
 *
 * UTRACE_ATTACH_MATCH_OPS: Only consider engines matching @ops.
 * UTRACE_ATTACH_MATCH_DATA: Only consider engines matching @data.
 */
struct utrace_attached_engine *utrace_attach_task(
	struct task_struct *target, int flags,
	const struct utrace_engine_ops *ops, void *data)
{
	struct utrace *utrace;
	struct utrace_attached_engine *engine;

restart:
	rcu_read_lock();
	utrace = rcu_dereference(target->utrace);
	smp_rmb();
	if (unlikely(target->exit_state == EXIT_DEAD)) {
		/*
		 * The target has already been reaped.
		 * Check this first; a race with reaping may lead to restart.
		 */
		rcu_read_unlock();
		if (!(flags & UTRACE_ATTACH_CREATE))
			return ERR_PTR(-ENOENT);
		return ERR_PTR(-ESRCH);
	}

	if (utrace == NULL) {
		rcu_read_unlock();

		if (!(flags & UTRACE_ATTACH_CREATE))
			return ERR_PTR(-ENOENT);

		if (unlikely(target->flags & PF_KTHREAD))
			/*
			 * Silly kernel, utrace is for users!
			 */
			return ERR_PTR(-EPERM);

		engine = alloc_engine();
		if (unlikely(!engine))
			return ERR_PTR(-ENOMEM);

		goto first;
	}

	if (!(flags & UTRACE_ATTACH_CREATE)) {
		spin_lock(&utrace->lock);
		engine = matching_engine(utrace, flags, ops, data);
		if (engine)
			utrace_engine_get(engine);
		spin_unlock(&utrace->lock);
		rcu_read_unlock();
		return engine ?: ERR_PTR(-ENOENT);
	}
	rcu_read_unlock();

	if (unlikely(!ops) || unlikely(ops == &utrace_detached_ops))
		return ERR_PTR(-EINVAL);

	engine = alloc_engine();
	if (unlikely(!engine))
		return ERR_PTR(-ENOMEM);

	rcu_read_lock();
	utrace = rcu_dereference(target->utrace);
	if (unlikely(utrace == NULL)) { /* Race with detach.  */
		rcu_read_unlock();
		goto first;
	}
	spin_lock(&utrace->lock);

	if (flags & UTRACE_ATTACH_EXCLUSIVE) {
		struct utrace_attached_engine *old;
		old = matching_engine(utrace, flags, ops, data);
		if (old) {
			spin_unlock(&utrace->lock);
			rcu_read_unlock();
			kmem_cache_free(utrace_engine_cachep, engine);
			return ERR_PTR(-EEXIST);
		}
	}

	if (unlikely(rcu_dereference(target->utrace) != utrace)) {
		/*
		 * We lost a race with other CPUs doing a sequence
		 * of detach and attach before we got in.
		 */
		spin_unlock(&utrace->lock);
		rcu_read_unlock();
		kmem_cache_free(utrace_engine_cachep, engine);
		goto restart;
	}
	rcu_read_unlock();

	list_add_tail(&engine->entry, &utrace->attaching);
	utrace->report = 1;
	goto finish;

first:
	utrace = utrace_first_engine(target, engine);
	if (IS_ERR(utrace) || unlikely(utrace == NULL)) {
		kmem_cache_free(utrace_engine_cachep, engine);
		if (unlikely(utrace == NULL)) /* Race condition.  */
			goto restart;
		return ERR_PTR(PTR_ERR(utrace));
	}

finish:
	engine->ops = ops;
	engine->data = data;

	spin_unlock(&utrace->lock);

	return engine;
}
EXPORT_SYMBOL_GPL(utrace_attach_task);

/**
 * utrace_attach_pid - attach new engine, or look up an attached engine
 * @pid:	&struct pid pointer representing thread to attach to
 * @flags:	flag bits combined with OR, see utrace_attach_task()
 * @ops:	callback table for new engine
 * @data:	engine private data pointer
 *
 * This is the same as utrace_attach_task(), but takes a &struct pid
 * pointer rather than a &struct task_struct pointer.  The caller must
 * hold a ref on @pid, but does not need to worry about the task
 * staying valid.  If it's been reaped so that @pid points nowhere,
 * then this call returns -%ESRCH.
 */
struct utrace_attached_engine *utrace_attach_pid(
	struct pid *pid, int flags,
	const struct utrace_engine_ops *ops, void *data)
{
	struct utrace_attached_engine *engine = ERR_PTR(-ESRCH);
	struct task_struct *task = get_pid_task(pid, PIDTYPE_PID);
	if (task) {
		engine = utrace_attach_task(task, flags, ops, data);
		put_task_struct(task);
	}
	return engine;
}
EXPORT_SYMBOL_GPL(utrace_attach_pid);

/*
 * This is called with @utrace->lock held when the task is safely
 * quiescent, i.e. it won't consult utrace->attached without the lock.
 * Move any engines attached asynchronously from @utrace->attaching
 * onto the @utrace->attached list.
 */
static void splice_attaching(struct utrace *utrace)
{
	list_splice_tail_init(&utrace->attaching, &utrace->attached);
}

/*
 * When an engine is detached, the target thread may still see it and
 * make callbacks until it quiesces.  We install a special ops vector
 * with these two callbacks.  When the target thread quiesces, it can
 * safely free the engine itself.  For any event we will always get
 * the report_quiesce() callback first, so we only need this one
 * pointer to be set.  The only exception is report_reap(), so we
 * supply that callback too.
 */
static u32 utrace_detached_quiesce(enum utrace_resume_action action,
				   struct utrace_attached_engine *engine,
				   struct task_struct *task,
				   unsigned long event)
{
	return UTRACE_DETACH;
}

static void utrace_detached_reap(struct utrace_attached_engine *engine,
				 struct task_struct *task)
{
}

static const struct utrace_engine_ops utrace_detached_ops = {
	.report_quiesce = &utrace_detached_quiesce,
	.report_reap = &utrace_detached_reap
};

/*
 * Only these flags matter any more for a dead task (exit_state set).
 * We use this mask on flags installed in ->utrace_flags after
 * exit_notify (and possibly utrace_report_death) has run.
 * This ensures that utrace_release_task knows positively that
 * utrace_report_death will not run later.
 */
#define DEAD_FLAGS_MASK	(UTRACE_EVENT(REAP))
#define LIVE_FLAGS_MASK	(~0UL)

/*
 * Perform %UTRACE_STOP, i.e. block in TASK_TRACED until woken up.
 * @task == current, @utrace == current->utrace, which is not locked.
 * Return true if we were woken up by SIGKILL even though some utrace
 * engine may still want us to stay stopped.
 */
static bool utrace_stop(struct task_struct *task, struct utrace *utrace)
{
	/*
	 * @utrace->stopped is the flag that says we are safely
	 * inside this function.  It should never be set on entry.
	 */
	BUG_ON(utrace->stopped);

	/*
	 * The siglock protects us against signals.  As well as SIGKILL
	 * waking us up, we must synchronize with the signal bookkeeping
	 * for stop signals and SIGCONT.
	 */
	spin_lock(&utrace->lock);
	spin_lock_irq(&task->sighand->siglock);

	if (unlikely(sigismember(&task->pending.signal, SIGKILL))) {
		spin_unlock_irq(&task->sighand->siglock);
		spin_unlock(&utrace->lock);
		return true;
	}

	utrace->stopped = 1;
	__set_current_state(TASK_TRACED);

	/*
	 * If there is a group stop in progress,
	 * we must participate in the bookkeeping.
	 */
	if (task->signal->group_stop_count > 0)
		--task->signal->group_stop_count;

	spin_unlock_irq(&task->sighand->siglock);
	spin_unlock(&utrace->lock);

	schedule();

	/*
	 * While in TASK_TRACED, we were considered "frozen enough".
	 * Now that we woke up, it's crucial if we're supposed to be
	 * frozen that we freeze now before running anything substantial.
	 */
	try_to_freeze();

	/*
	 * utrace_wakeup() clears @utrace->stopped before waking us up.
	 * We're officially awake if it's clear.
	 */
	if (likely(!utrace->stopped))
		return false;

	/*
	 * If we're here with it still set, it must have been
	 * signal_wake_up() instead, waking us up for a SIGKILL.
	 */
	spin_lock(&utrace->lock);
	utrace->stopped = 0;
	spin_unlock(&utrace->lock);
	return true;
}

/*
 * The caller has to hold a ref on the engine.  If the attached flag is
 * true (all but utrace_barrier() calls), the engine is supposed to be
 * attached.  If the attached flag is false (utrace_barrier() only),
 * then return -ERESTARTSYS for an engine marked for detach but not yet
 * fully detached.  The task pointer can be invalid if the engine is
 * detached.
 *
 * Get the utrace lock for the target task.
 * Returns the struct if locked, or ERR_PTR(-errno).
 *
 * This has to be robust against races with:
 *	utrace_control(target, UTRACE_DETACH) calls
 *	UTRACE_DETACH after reports
 *	utrace_report_death
 *	utrace_release_task
 */
static struct utrace *get_utrace_lock(struct task_struct *target,
				      struct utrace_attached_engine *engine,
				      bool attached)
	__acquires(utrace->lock)
{
	struct utrace *utrace;

	/*
	 * You must hold a ref to be making a call.  A call from within
	 * a report_* callback in @target might only have the ref for
	 * being attached, not a second one of its own.
	 */
	if (unlikely(atomic_read(&engine->kref.refcount) < 1))
		return ERR_PTR(-EINVAL);

	rcu_read_lock();

	/*
	 * If this engine was already detached, bail out before we look at
	 * the task_struct pointer at all.  If it's detached after this
	 * check, then RCU is still keeping this task_struct pointer valid.
	 *
	 * The ops pointer is NULL when the engine is fully detached.
	 * It's &utrace_detached_ops when it's marked detached but still
	 * on the list.  In the latter case, utrace_barrier() still works,
	 * since the target might be in the middle of an old callback.
	 */
	if (unlikely(!engine->ops)) {
		rcu_read_unlock();
		return ERR_PTR(-ESRCH);
	}

	if (unlikely(engine->ops == &utrace_detached_ops)) {
		rcu_read_unlock();
		return attached ? ERR_PTR(-ESRCH) : ERR_PTR(-ERESTARTSYS);
	}

	utrace = rcu_dereference(target->utrace);
	smp_rmb();
	if (unlikely(!utrace) || unlikely(target->exit_state == EXIT_DEAD)) {
		/*
		 * If all engines detached already, utrace is clear.
		 * Otherwise, we're called after utrace_release_task might
		 * have started.  A call to this engine's report_reap
		 * callback might already be in progress.
		 */
		utrace = ERR_PTR(-ESRCH);
	} else {
		spin_lock(&utrace->lock);
		if (unlikely(rcu_dereference(target->utrace) != utrace) ||
		    unlikely(!engine->ops) ||
		    unlikely(engine->ops == &utrace_detached_ops)) {
			/*
			 * By the time we got the utrace lock,
			 * it had been reaped or detached already.
			 */
			spin_unlock(&utrace->lock);
			utrace = ERR_PTR(-ESRCH);
			if (!attached && engine->ops == &utrace_detached_ops)
				utrace = ERR_PTR(-ERESTARTSYS);
		}
	}
	rcu_read_unlock();

	return utrace;
}

/*
 * Now that we don't hold any locks, run through any
 * detached engines and free their references.  Each
 * engine had one implicit ref while it was attached.
 */
static void put_detached_list(struct list_head *list)
{
	struct utrace_attached_engine *engine, *next;
	list_for_each_entry_safe(engine, next, list, entry) {
		list_del_init(&engine->entry);
		utrace_engine_put(engine);
	}
}

/*
 * Called with utrace->lock held.
 * Notify and clean up all engines, then free utrace.
 */
static void utrace_reap(struct task_struct *target, struct utrace *utrace)
	__releases(utrace->lock)
{
	struct utrace_attached_engine *engine, *next;
	const struct utrace_engine_ops *ops;
	LIST_HEAD(detached);

restart:
	splice_attaching(utrace);
	list_for_each_entry_safe(engine, next, &utrace->attached, entry) {
		ops = engine->ops;
		engine->ops = NULL;
		list_move(&engine->entry, &detached);

		/*
		 * If it didn't need a callback, we don't need to drop
		 * the lock.  Now nothing else refers to this engine.
		 */
		if (!(engine->flags & UTRACE_EVENT(REAP)))
			continue;

		utrace->reporting = engine;
		spin_unlock(&utrace->lock);

		(*ops->report_reap)(engine, target);

		utrace->reporting = NULL;

		put_detached_list(&detached);

		spin_lock(&utrace->lock);
		goto restart;
	}

	rcu_utrace_free(utrace); /* Releases the lock.  */

	put_detached_list(&detached);
}

#define DEATH_EVENTS (UTRACE_EVENT(DEATH) | UTRACE_EVENT(QUIESCE))

/*
 * Called by release_task.  After this, target->utrace must be cleared.
 */
void utrace_release_task(struct task_struct *target)
{
	struct utrace *utrace;

	task_lock(target);
	utrace = rcu_dereference(target->utrace);
	rcu_assign_pointer(target->utrace, NULL);
	task_unlock(target);

	if (unlikely(utrace == NULL))
		return;

	spin_lock(&utrace->lock);
	/*
	 * If the list is empty, utrace is already on its way to be freed.
	 * We raced with detach and we won the task_lock race but lost the
	 * utrace->lock race.  All we have to do is let RCU run.
	 */
	if (likely(!list_empty(&utrace->attached))) {
		utrace->reap = 1;

		if (!(target->utrace_flags & DEATH_EVENTS)) {
			utrace_reap(target, utrace); /* Unlocks and frees.  */
			return;
		}

		/*
		 * The target will do some final callbacks but hasn't
		 * finished them yet.  We know because it clears these
		 * event bits after it's done.  Instead of cleaning up here
		 * and requiring utrace_report_death to cope with it, we
		 * delay the REAP report and the teardown until after the
		 * target finishes its death reports.
		 */
	}
	spin_unlock(&utrace->lock);
}

/*
 * We use an extra bit in utrace_attached_engine.flags past the event bits,
 * to record whether the engine is keeping the target thread stopped.
 */
#define ENGINE_STOP		(1UL << _UTRACE_NEVENTS)

static void mark_engine_wants_stop(struct utrace_attached_engine *engine)
{
	engine->flags |= ENGINE_STOP;
}

static void clear_engine_wants_stop(struct utrace_attached_engine *engine)
{
	engine->flags &= ~ENGINE_STOP;
}

static bool engine_wants_stop(struct utrace_attached_engine *engine)
{
	return (engine->flags & ENGINE_STOP) != 0;
}

/**
 * utrace_set_events - choose which event reports a tracing engine gets
 * @target:		thread to affect
 * @engine:		attached engine to affect
 * @events:		new event mask
 *
 * This changes the set of events for which @engine wants callbacks made.
 *
 * This fails with -%EALREADY and does nothing if you try to clear
 * %UTRACE_EVENT(%DEATH) when the @report_death callback may already have
 * begun, if you try to clear %UTRACE_EVENT(%REAP) when the @report_reap
 * callback may already have begun, or if you try to newly set
 * %UTRACE_EVENT(%DEATH) or %UTRACE_EVENT(%QUIESCE) when @target is
 * already dead or dying.
 *
 * This can fail with -%ESRCH when @target has already been detached,
 * including forcible detach on reaping.
 *
 * If @target was stopped before the call, then after a successful call,
 * no event callbacks not requested in @events will be made; if
 * %UTRACE_EVENT(%QUIESCE) is included in @events, then a @report_quiesce
 * callback will be made when @target resumes.  If @target was not stopped,
 * and was about to make a callback to @engine, this returns -%EINPROGRESS.
 * In this case, the callback in progress might be one excluded from the
 * new @events setting.  When this returns zero, you can be sure that no
 * event callbacks you've disabled in @events can be made.
 *
 * To synchronize after an -%EINPROGRESS return, see utrace_barrier().
 *
 * These rules provide for coherent synchronization based on %UTRACE_STOP,
 * even when %SIGKILL is breaking its normal simple rules.
 */
int utrace_set_events(struct task_struct *target,
		      struct utrace_attached_engine *engine,
		      unsigned long events)
{
	struct utrace *utrace;
	unsigned long old_flags, old_utrace_flags, set_utrace_flags;
	struct sighand_struct *sighand;
	unsigned long flags;
	int ret;

	utrace = get_utrace_lock(target, engine, true);
	if (unlikely(IS_ERR(utrace)))
		return PTR_ERR(utrace);

	old_utrace_flags = target->utrace_flags;
	set_utrace_flags = events;
	old_flags = engine->flags;

	if (target->exit_state &&
	    (((events & ~old_flags) & DEATH_EVENTS) ||
	     (utrace->death && ((old_flags & ~events) & DEATH_EVENTS)) ||
	     (utrace->reap && ((old_flags & ~events) & UTRACE_EVENT(REAP))))) {
		spin_unlock(&utrace->lock);
		return -EALREADY;
	}

	/*
	 * When it's in TASK_STOPPED state and UTRACE_EVENT(JCTL) is set,
	 * utrace_do_stop() will think it is still running and needs to
	 * finish utrace_report_jctl() before it's really stopped.  But
	 * if the bit wasn't already set, it can't be running in there
	 * and really is quiescent now in its existing job control stop.
	 */
	if (!utrace->stopped &&
	    ((set_utrace_flags & ~old_utrace_flags) & UTRACE_EVENT(JCTL))) {
		sighand = lock_task_sighand(target, &flags);
		if (likely(sighand)) {
			if (task_is_stopped(target))
				utrace->stopped = 1;
			unlock_task_sighand(target, &flags);
		}
	}

	/*
	 * When setting these flags, it's essential that we really
	 * synchronize with exit_notify().  They cannot be set after
	 * exit_notify() takes the tasklist_lock.  By holding the read
	 * lock here while setting the flags, we ensure that the calls
	 * to tracehook_notify_death() and tracehook_report_death() will
	 * see the new flags.  This ensures that utrace_release_task()
	 * knows positively that utrace_report_death() will be called or
	 * that it won't.
	 */
	if ((set_utrace_flags & ~old_utrace_flags) & DEATH_EVENTS) {
		read_lock(&tasklist_lock);
		if (unlikely(target->exit_state)) {
			read_unlock(&tasklist_lock);
			spin_unlock(&utrace->lock);
			return -EALREADY;
		}
		target->utrace_flags |= set_utrace_flags;
		read_unlock(&tasklist_lock);
	}

	engine->flags = events | (engine->flags & ENGINE_STOP);
	target->utrace_flags |= set_utrace_flags;

	if ((set_utrace_flags & UTRACE_EVENT_SYSCALL) &&
	    !(old_utrace_flags & UTRACE_EVENT_SYSCALL))
		set_tsk_thread_flag(target, TIF_SYSCALL_TRACE);

	ret = 0;
	if (!utrace->stopped && target != current) {
		smp_mb();
		if (utrace->reporting == engine)
			ret = -EINPROGRESS;
	}

	spin_unlock(&utrace->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(utrace_set_events);

/*
 * Asynchronously mark an engine as being detached.
 *
 * This must work while the target thread races with us doing
 * start_callback(), defined below.  It uses smp_rmb() between checking
 * @engine->flags and using @engine->ops.  Here we change @engine->ops
 * first, then use smp_wmb() before changing @engine->flags.  This ensures
 * it can check the old flags before using the old ops, or check the old
 * flags before using the new ops, or check the new flags before using the
 * new ops, but can never check the new flags before using the old ops.
 * Hence, utrace_detached_ops might be used with any old flags in place.
 * It has report_quiesce() and report_reap() callbacks to handle all cases.
 */
static void mark_engine_detached(struct utrace_attached_engine *engine)
{
	engine->ops = &utrace_detached_ops;
	smp_wmb();
	engine->flags = UTRACE_EVENT(QUIESCE);
}

/*
 * Get @target to stop and return true if it is already stopped now.
 * If we return false, it will make some event callback soonish.
 * Called with @utrace locked.
 */
static bool utrace_do_stop(struct task_struct *target, struct utrace *utrace)
{
	bool stopped;

	/*
	 * If it will call utrace_report_jctl() but has not gotten
	 * through it yet, then don't consider it quiescent yet.
	 * utrace_report_jctl() will take @utrace->lock and
	 * set @utrace->stopped itself once it finishes.  After that,
	 * it is considered quiescent; when it wakes up, it will go
	 * through utrace_get_signal() before doing anything else.
	 */
	if (task_is_stopped(target) &&
	    !(target->utrace_flags & UTRACE_EVENT(JCTL))) {
		utrace->stopped = 1;
		return true;
	}

	stopped = false;
	spin_lock_irq(&target->sighand->siglock);
	if (unlikely(target->exit_state)) {
		/*
		 * On the exit path, it's only truly quiescent
		 * if it has already been through
		 * utrace_report_death(), or never will.
		 */
		if (!(target->utrace_flags & DEATH_EVENTS))
			utrace->stopped = stopped = true;
	} else if (task_is_stopped(target)) {
		if (!(target->utrace_flags & UTRACE_EVENT(JCTL)))
			utrace->stopped = stopped = true;
	} else if (!utrace->report && !utrace->interrupt) {
		utrace->report = 1;
		set_notify_resume(target);
	}
	spin_unlock_irq(&target->sighand->siglock);

	return stopped;
}

/*
 * If the target is not dead it should not be in tracing
 * stop any more.  Wake it unless it's in job control stop.
 *
 * Called with @utrace->lock held and @utrace->stopped set.
 */
static void utrace_wakeup(struct task_struct *target, struct utrace *utrace)
{
	struct sighand_struct *sighand;
	unsigned long irqflags;

	utrace->stopped = 0;

	sighand = lock_task_sighand(target, &irqflags);
	if (unlikely(!sighand))
		return;

	if (likely(task_is_stopped_or_traced(target))) {
		if (target->signal->flags & SIGNAL_STOP_STOPPED)
			target->state = TASK_STOPPED;
		else
			wake_up_state(target, __TASK_STOPPED | __TASK_TRACED);
	}

	unlock_task_sighand(target, &irqflags);
}

/*
 * This is called when there might be some detached engines on the list or
 * some stale bits in @task->utrace_flags.  Clean them up and recompute the
 * flags.
 *
 * @wake is false when @task is current.  @wake is true when @task is
 * stopped and @utrace->stopped is set; wake it up if it should not be.
 *
 * Called with @utrace->lock held, returns with it released.
 */
static void utrace_reset(struct task_struct *task, struct utrace *utrace,
			 bool wake)
	__releases(utrace->lock)
{
	struct utrace_attached_engine *engine, *next;
	unsigned long flags = 0;
	LIST_HEAD(detached);

	splice_attaching(utrace);

	/*
	 * Update the set of events of interest from the union
	 * of the interests of the remaining tracing engines.
	 * For any engine marked detached, remove it from the list.
	 * We'll collect them on the detached list.
	 */
	list_for_each_entry_safe(engine, next, &utrace->attached, entry) {
		if (engine->ops == &utrace_detached_ops) {
			engine->ops = NULL;
			list_move(&engine->entry, &detached);
		} else {
			flags |= engine->flags | UTRACE_EVENT(REAP);
			wake = wake && !engine_wants_stop(engine);
		}
	}

	if (task->exit_state) {
		BUG_ON(utrace->death);
		flags &= DEAD_FLAGS_MASK;
		wake = false;
	} else if (!(flags & UTRACE_EVENT_SYSCALL) &&
		   test_tsk_thread_flag(task, TIF_SYSCALL_TRACE)) {
		clear_tsk_thread_flag(task, TIF_SYSCALL_TRACE);
	}

	task->utrace_flags = flags;

	if (wake)
		utrace_wakeup(task, utrace);

	/*
	 * If any engines are left, we're done.
	 */
	if (flags) {
		spin_unlock(&utrace->lock);
		goto done;
	}

	/*
	 * No more engines, clear out the utrace.  Here we can race with
	 * utrace_release_task().  If it gets task_lock() first, then it
	 * cleans up this struct for us.
	 */

	task_lock(task);

	if (unlikely(task->utrace != utrace)) {
		task_unlock(task);
		spin_unlock(&utrace->lock);
		goto done;
	}

	rcu_assign_pointer(task->utrace, NULL);

	task_unlock(task);

	rcu_utrace_free(utrace);

done:
	put_detached_list(&detached);
}

/**
 * utrace_control - control a thread being traced by a tracing engine
 * @target:		thread to affect
 * @engine:		attached engine to affect
 * @action:		&enum utrace_resume_action for thread to do
 *
 * This is how a tracing engine asks a traced thread to do something.
 * This call is controlled by the @action argument, which has the
 * same meaning as the &enum utrace_resume_action value returned by
 * event reporting callbacks.
 *
 * If @target is already dead (@target->exit_state nonzero),
 * all actions except %UTRACE_DETACH fail with -%ESRCH.
 *
 * The following sections describe each option for the @action argument.
 *
 * UTRACE_DETACH:
 *
 * After this, the @engine data structure is no longer accessible,
 * and the thread might be reaped.  The thread will start running
 * again if it was stopped and no longer has any attached engines
 * that want it stopped.
 *
 * If the @report_reap callback may already have begun, this fails
 * with -%ESRCH.  If the @report_death callback may already have
 * begun, this fails with -%EALREADY.
 *
 * If @target is not already stopped, then a callback to this engine
 * might be in progress or about to start on another CPU.  If so,
 * then this returns -%EINPROGRESS; the detach happens as soon as
 * the pending callback is finished.  To synchronize after an
 * -%EINPROGRESS return, see utrace_barrier().
 *
 * If @target is properly stopped before utrace_control() is called,
 * then after successful return it's guaranteed that no more callbacks
 * to the @engine->ops vector will be made.
 *
 * The only exception is %SIGKILL (and exec or group-exit by another
 * thread in the group), which can cause asynchronous @report_death
 * and/or @report_reap callbacks even when %UTRACE_STOP was used.
 * (In that event, this fails with -%ESRCH or -%EALREADY, see above.)
 *
 * UTRACE_STOP:
 * This asks that @target stop running.  This returns 0 only if
 * @target is already stopped, either for tracing or for job
 * control.  Then @target will remain stopped until another
 * utrace_control() call is made on @engine; @target can be woken
 * only by %SIGKILL (or equivalent, such as exec or termination by
 * another thread in the same thread group).
 *
 * This returns -%EINPROGRESS if @target is not already stopped.
 * Then the effect is like %UTRACE_REPORT.  A @report_quiesce or
 * @report_signal callback will be made soon.  Your callback can
 * then return %UTRACE_STOP to keep @target stopped.
 *
 * This does not interrupt system calls in progress, including ones
 * that sleep for a long time.  For that, use %UTRACE_INTERRUPT.
 * To interrupt system calls and then keep @target stopped, your
 * @report_signal callback can return %UTRACE_STOP.
 *
 * UTRACE_RESUME:
 *
 * Just let @target continue running normally, reversing the effect
 * of a previous %UTRACE_STOP.  If another engine is keeping @target
 * stopped, then it remains stopped until all engines let it resume.
 * If @target was not stopped, this has no effect.
 *
 * UTRACE_REPORT:
 *
 * This is like %UTRACE_RESUME, but also ensures that there will be
 * a @report_quiesce or @report_signal callback made soon.  If
 * @target had been stopped, then there will be a callback before it
 * resumes running normally.  If another engine is keeping @target
 * stopped, then there might be no callbacks until all engines let
 * it resume.
 *
 * UTRACE_INTERRUPT:
 *
 * This is like %UTRACE_REPORT, but ensures that @target will make a
 * @report_signal callback before it resumes or delivers signals.
 * If @target was in a system call or about to enter one, work in
 * progress will be interrupted as if by %SIGSTOP.  If another
 * engine is keeping @target stopped, then there might be no
 * callbacks until all engines let it resume.
 *
 * This gives @engine an opportunity to introduce a forced signal
 * disposition via its @report_signal callback.
 *
 * UTRACE_SINGLESTEP:
 *
 * It's invalid to use this unless arch_has_single_step() returned true.
 * This is like %UTRACE_RESUME, but resumes for one user instruction
 * only.  It's invalid to use this in utrace_control() unless @target
 * had been stopped by @engine previously.
 *
 * Note that passing %UTRACE_SINGLESTEP or %UTRACE_BLOCKSTEP to
 * utrace_control() or returning it from an event callback alone does
 * not necessarily ensure that stepping will be enabled.  If there are
 * more callbacks made to any engine before returning to user mode,
 * then the resume action is chosen only by the last set of callbacks.
 * To be sure, enable %UTRACE_EVENT(%QUIESCE) and look for the
 * @report_quiesce callback with a zero event mask, or the
 * @report_signal callback with %UTRACE_SIGNAL_REPORT.
 *
 * UTRACE_BLOCKSTEP:
 *
 * It's invalid to use this unless arch_has_block_step() returned true.
 * This is like %UTRACE_SINGLESTEP, but resumes for one whole basic
 * block of user instructions.
 *
 * %UTRACE_BLOCKSTEP devolves to %UTRACE_SINGLESTEP when another
 * tracing engine is using %UTRACE_SINGLESTEP at the same time.
 */
int utrace_control(struct task_struct *target,
		   struct utrace_attached_engine *engine,
		   enum utrace_resume_action action)
{
	struct utrace *utrace;
	bool resume;
	int ret;

	if (unlikely(action > UTRACE_DETACH))
		return -EINVAL;

	utrace = get_utrace_lock(target, engine, true);
	if (unlikely(IS_ERR(utrace)))
		return PTR_ERR(utrace);

	if (target->exit_state) {
		/*
		 * You can't do anything to a dead task but detach it.
		 * If release_task() has been called, you can't do that.
		 *
		 * On the exit path, DEATH and QUIESCE event bits are
		 * set only before utrace_report_death() has taken the
		 * lock.  At that point, the death report will come
		 * soon, so disallow detach until it's done.  This
		 * prevents us from racing with it detaching itself.
		 */
		if (action != UTRACE_DETACH ||
		    unlikely(utrace->reap)) {
			spin_unlock(&utrace->lock);
			return -ESRCH;
		} else if (unlikely(target->utrace_flags & DEATH_EVENTS) ||
			   unlikely(utrace->death)) {
			/*
			 * We have already started the death report, or
			 * are about to very soon.  We can't prevent
			 * the report_death and report_reap callbacks,
			 * so tell the caller they will happen.
			 */
			spin_unlock(&utrace->lock);
			return -EALREADY;
		}
	}

	resume = utrace->stopped;
	ret = 0;

	clear_engine_wants_stop(engine);
	switch (action) {
	case UTRACE_STOP:
		mark_engine_wants_stop(engine);
		if (!resume && !utrace_do_stop(target, utrace))
			ret = -EINPROGRESS;
		resume = false;
		break;

	case UTRACE_DETACH:
		mark_engine_detached(engine);
		resume = resume || utrace_do_stop(target, utrace);
		if (!resume) {
			smp_mb();
			if (utrace->reporting == engine)
				ret = -EINPROGRESS;
			break;
		}
		/* Fall through.  */

	case UTRACE_RESUME:
		/*
		 * This and all other cases imply resuming if stopped.
		 * There might not be another report before it just
		 * resumes, so make sure single-step is not left set.
		 */
		if (likely(resume))
			user_disable_single_step(target);
		break;

	case UTRACE_REPORT:
		/*
		 * Make the thread call tracehook_notify_resume() soon.
		 * But don't bother if it's already been stopped or
		 * interrupted.  In those cases, utrace_get_signal()
		 * will be reporting soon.
		 */
		if (!utrace->report && !utrace->interrupt && !utrace->stopped) {
			utrace->report = 1;
			set_notify_resume(target);
		}
		break;

	case UTRACE_INTERRUPT:
		/*
		 * Make the thread call tracehook_get_signal() soon.
		 */
		if (utrace->interrupt)
			break;
		utrace->interrupt = 1;

		/*
		 * If it's not already stopped, interrupt it now.
		 * We need the siglock here in case it calls
		 * recalc_sigpending() and clears its own
		 * TIF_SIGPENDING.  By taking the lock, we've
		 * serialized any later recalc_sigpending() after
		 * our setting of utrace->interrupt to force it on.
		 */
		if (resume) {
			/*
			 * This is really just to keep the invariant
			 * that TIF_SIGPENDING is set with utrace->interrupt.
			 * When it's stopped, we know it's always going
			 * through utrace_get_signal and will recalculate.
			 */
			set_tsk_thread_flag(target, TIF_SIGPENDING);
		} else {
			struct sighand_struct *sighand;
			unsigned long irqflags;
			sighand = lock_task_sighand(target, &irqflags);
			if (likely(sighand)) {
				signal_wake_up(target, 0);
				unlock_task_sighand(target, &irqflags);
			}
		}
		break;

	case UTRACE_BLOCKSTEP:
		/*
		 * Resume from stopped, step one block.
		 */
		if (unlikely(!arch_has_block_step())) {
			WARN_ON(1);
			/* Fall through to treat it as SINGLESTEP.  */
		} else if (likely(resume)) {
			user_enable_block_step(target);
			break;
		}

	case UTRACE_SINGLESTEP:
		/*
		 * Resume from stopped, step one instruction.
		 */
		if (unlikely(!arch_has_single_step())) {
			WARN_ON(1);
			resume = false;
			ret = -EOPNOTSUPP;
			break;
		}

		if (likely(resume))
			user_enable_single_step(target);
		else
			/*
			 * You were supposed to stop it before asking
			 * it to step.
			 */
			ret = -EAGAIN;
		break;
	}

	/*
	 * Let the thread resume running.  If it's not stopped now,
	 * there is nothing more we need to do.
	 */
	if (resume)
		utrace_reset(target, utrace, true);
	else
		spin_unlock(&utrace->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(utrace_control);

/**
 * utrace_barrier - synchronize with simultaneous tracing callbacks
 * @target:		thread to affect
 * @engine:		engine to affect (can be detached)
 *
 * This blocks while @target might be in the midst of making a callback to
 * @engine.  It can be interrupted by signals and will return -%ERESTARTSYS.
 * A return value of zero means no callback from @target to @engine was
 * in progress.
 *
 * It's not necessary to keep the @target pointer alive for this call.
 * It's only necessary to hold a ref on @engine.  This will return
 * safely even if @target has been reaped and has no task refs.
 *
 * A successful return from utrace_barrier() guarantees its ordering
 * with respect to utrace_set_events() and utrace_control() calls.  If
 * @target was not properly stopped, event callbacks just disabled might
 * still be in progress; utrace_barrier() waits until there is no chance
 * an unwanted callback can be in progress.
 */
int utrace_barrier(struct task_struct *target,
		   struct utrace_attached_engine *engine)
{
	struct utrace *utrace;
	int ret = -ERESTARTSYS;

	if (unlikely(target == current))
		return 0;

	do {
		utrace = get_utrace_lock(target, engine, false);
		if (unlikely(IS_ERR(utrace))) {
			ret = PTR_ERR(utrace);
			if (ret != -ERESTARTSYS)
				break;
		} else {
			if (utrace->stopped || utrace->reporting != engine)
				ret = 0;
			spin_unlock(&utrace->lock);
			if (!ret)
				break;
		}
		schedule_timeout_interruptible(1);
	} while (!signal_pending(current));

	return ret;
}
EXPORT_SYMBOL_GPL(utrace_barrier);

/*
 * This is local state used for reporting loops, perhaps optimized away.
 */
struct utrace_report {
	enum utrace_resume_action action;
	u32 result;
	bool detaches;
	bool takers;
	bool killed;
};

#define INIT_REPORT(var) \
	struct utrace_report var = { UTRACE_RESUME, 0, false, false, false }

/*
 * We are now making the report, so clear the flag saying we need one.
 */
static void start_report(struct utrace *utrace)
{
	BUG_ON(utrace->stopped);
	if (utrace->report) {
		spin_lock(&utrace->lock);
		utrace->report = 0;
		splice_attaching(utrace);
		spin_unlock(&utrace->lock);
	}
}

/*
 * Complete a normal reporting pass, pairing with a start_report() call.
 * This handles any UTRACE_DETACH or UTRACE_REPORT or UTRACE_INTERRUPT
 * returns from engine callbacks.  If any engine's last callback used
 * UTRACE_STOP, we do UTRACE_REPORT here to ensure we stop before user
 * mode.  If there were no callbacks made, it will recompute
 * @task->utrace_flags to avoid another false-positive.
 */
static void finish_report(struct utrace_report *report,
			  struct task_struct *task, struct utrace *utrace)
{
	bool clean = (report->takers && !report->detaches);

	if (report->action <= UTRACE_REPORT && !utrace->report) {
		spin_lock(&utrace->lock);
		utrace->report = 1;
		set_tsk_thread_flag(task, TIF_NOTIFY_RESUME);
	} else if (report->action == UTRACE_INTERRUPT && !utrace->interrupt) {
		spin_lock(&utrace->lock);
		utrace->interrupt = 1;
		set_tsk_thread_flag(task, TIF_SIGPENDING);
	} else if (clean) {
		return;
	} else {
		spin_lock(&utrace->lock);
	}

	if (clean)
		spin_unlock(&utrace->lock);
	else
		utrace_reset(task, utrace, false);
}

/*
 * Apply the return value of one engine callback to @report.
 * Returns true if @engine detached and should not get any more callbacks.
 */
static bool finish_callback(struct utrace *utrace,
			    struct utrace_report *report,
			    struct utrace_attached_engine *engine,
			    u32 ret)
{
	enum utrace_resume_action action = utrace_resume_action(ret);

	utrace->reporting = NULL;

	/*
	 * This is a good place to make sure tracing engines don't
	 * introduce too much latency under voluntary preemption.
	 */
	if (need_resched())
		cond_resched();

	report->result = ret & ~UTRACE_RESUME_MASK;

	/*
	 * If utrace_control() was used, treat that like UTRACE_DETACH here.
	 */
	if (action == UTRACE_DETACH || engine->ops == &utrace_detached_ops) {
		engine->ops = &utrace_detached_ops;
		report->detaches = true;
		return true;
	}

	if (action < report->action)
		report->action = action;

	if (action == UTRACE_STOP) {
		if (!engine_wants_stop(engine)) {
			spin_lock(&utrace->lock);
			mark_engine_wants_stop(engine);
			spin_unlock(&utrace->lock);
		}
	} else if (engine_wants_stop(engine)) {
		spin_lock(&utrace->lock);
		clear_engine_wants_stop(engine);
		spin_unlock(&utrace->lock);
	}

	return false;
}

/*
 * Start the callbacks for @engine to consider @event (a bit mask).
 * This makes the report_quiesce() callback first.  If @engine wants
 * a specific callback for @event, we return the ops vector to use.
 * If not, we return NULL.  The return value from the ops->callback
 * function called should be passed to finish_callback().
 */
static const struct utrace_engine_ops *start_callback(
	struct utrace *utrace, struct utrace_report *report,
	struct utrace_attached_engine *engine, struct task_struct *task,
	unsigned long event)
{
	const struct utrace_engine_ops *ops;
	unsigned long want;

	utrace->reporting = engine;
	smp_mb();

	/*
	 * This pairs with the barrier in mark_engine_detached().
	 * It makes sure that we never see the old ops vector with
	 * the new flags, in case the original vector had no report_quiesce.
	 */
	want = engine->flags;
	smp_rmb();
	ops = engine->ops;

	if (want & UTRACE_EVENT(QUIESCE)) {
		if (finish_callback(utrace, report, engine,
				    (*ops->report_quiesce)(report->action,
							   engine, task,
							   event)))
			goto nocall;

		utrace->reporting = engine;
		smp_mb();
		want = engine->flags;
	}

	if (want & ENGINE_STOP)
		report->action = UTRACE_STOP;

	if (want & event) {
		report->takers = true;
		return ops;
	}

nocall:
	utrace->reporting = NULL;
	return NULL;
}

/*
 * Do a normal reporting pass for engines interested in @event.
 * @callback is the name of the member in the ops vector, and remaining
 * args are the extras it takes after the standard three args.
 */
#define REPORT(task, utrace, report, event, callback, ...)		      \
	do {								      \
		start_report(utrace);					      \
		REPORT_CALLBACKS(task, utrace, report, event, callback,	      \
				 (report)->action, engine, current,	      \
				 ## __VA_ARGS__);  	   		      \
		finish_report(report, task, utrace);			      \
	} while (0)
#define REPORT_CALLBACKS(task, utrace, report, event, callback, ...)	      \
	do {								      \
		struct utrace_attached_engine *engine, *next;		      \
		const struct utrace_engine_ops *ops;			      \
		list_for_each_entry_safe(engine, next,			      \
					 &utrace->attached, entry) {	      \
			ops = start_callback(utrace, report, engine, task,    \
					     event);			      \
			if (!ops)					      \
				continue;				      \
			finish_callback(utrace, report, engine,		      \
					(*ops->callback)(__VA_ARGS__));	      \
		}							      \
	} while (0)

/*
 * Called iff UTRACE_EVENT(EXEC) flag is set.
 */
void utrace_report_exec(struct linux_binfmt *fmt, struct linux_binprm *bprm,
			struct pt_regs *regs)
{
	struct task_struct *task = current;
	struct utrace *utrace = task->utrace;
	INIT_REPORT(report);

	REPORT(task, utrace, &report, UTRACE_EVENT(EXEC),
	       report_exec, fmt, bprm, regs);
}

/*
 * Called iff UTRACE_EVENT(SYSCALL_ENTRY) flag is set.
 * Return true to prevent the system call.
 */
bool utrace_report_syscall_entry(struct pt_regs *regs)
{
	struct task_struct *task = current;
	struct utrace *utrace = task->utrace;
	INIT_REPORT(report);

	start_report(utrace);
	REPORT_CALLBACKS(task, utrace, &report, UTRACE_EVENT(SYSCALL_ENTRY),
			 report_syscall_entry, report.result | report.action,
			 engine, current, regs);
	finish_report(&report, task, utrace);

	if (report.action == UTRACE_STOP && unlikely(utrace_stop(task, utrace)))
		/*
		 * We are continuing despite UTRACE_STOP because of a
		 * SIGKILL.  Don't let the system call actually proceed.
		 */
		return true;

	if (unlikely(report.result == UTRACE_SYSCALL_ABORT))
		return true;

	if (signal_pending(task)) {
		/*
		 * Clear TIF_SIGPENDING if it no longer needs to be set.
		 * It may have been set as part of quiescence, and won't
		 * ever have been cleared by another thread.  For other
		 * reports, we can just leave it set and will go through
		 * utrace_get_signal() to reset things.  But here we are
		 * about to enter a syscall, which might bail out with an
		 * -ERESTART* error if it's set now.
		 */
		spin_lock_irq(&task->sighand->siglock);
		recalc_sigpending();
		spin_unlock_irq(&task->sighand->siglock);
	}

	return false;
}

/*
 * Called iff UTRACE_EVENT(SYSCALL_EXIT) flag is set.
 */
void utrace_report_syscall_exit(struct pt_regs *regs)
{
	struct task_struct *task = current;
	struct utrace *utrace = task->utrace;
	INIT_REPORT(report);

	REPORT(task, utrace, &report, UTRACE_EVENT(SYSCALL_EXIT),
	       report_syscall_exit, regs);
}

/*
 * Called iff UTRACE_EVENT(CLONE) flag is set.
 * This notification call blocks the wake_up_new_task call on the child.
 * So we must not quiesce here.  tracehook_report_clone_complete will do
 * a quiescence check momentarily.
 */
void utrace_report_clone(unsigned long clone_flags, struct task_struct *child)
{
	struct task_struct *task = current;
	struct utrace *utrace = task->utrace;
	INIT_REPORT(report);

	utrace->u.live.cloning = child;

	REPORT(task, utrace, &report, UTRACE_EVENT(CLONE),
	       report_clone, clone_flags, child);

	utrace->u.live.cloning = NULL;
}

/*
 * Called iff UTRACE_EVENT(JCTL) flag is set.
 */
void utrace_report_jctl(int notify, int what)
{
	struct task_struct *task = current;
	struct utrace *utrace = task->utrace;
	INIT_REPORT(report);
	bool was_stopped = task_is_stopped(task);

	/*
	 * We get here with CLD_STOPPED when we've just entered
	 * TASK_STOPPED, or with CLD_CONTINUED when we've just come
	 * out but not yet been through utrace_get_signal() again.
	 *
	 * While in TASK_STOPPED, we can be considered safely
	 * stopped by utrace_do_stop() and detached asynchronously.
	 * If we woke up and checked task->utrace_flags before that
	 * was finished, we might be here with utrace already
	 * removed or in the middle of being removed.
	 *
	 * RCU makes it safe to get the utrace->lock even if it's
	 * being freed.  Once we have that lock, either an external
	 * detach has finished and this struct has been freed, or
	 * else we know we are excluding any other detach attempt.
	 *
	 * If we are indeed attached, then make sure we are no
	 * longer considered stopped while we run callbacks.
	 */
	rcu_read_lock();
	utrace = rcu_dereference(task->utrace);
	if (unlikely(!utrace)) {
		rcu_read_unlock();
		return;
	}
	spin_lock(&utrace->lock);
	utrace->stopped = 0;
	utrace->report = 0;
	spin_unlock(&utrace->lock);
	rcu_read_unlock();

	REPORT(task, utrace, &report, UTRACE_EVENT(JCTL),
	       report_jctl, was_stopped ? CLD_STOPPED : CLD_CONTINUED, what);

	if (was_stopped && !task_is_stopped(task)) {
		/*
		 * The event report hooks could have blocked, though
		 * it should have been briefly.  Make sure we're in
		 * TASK_STOPPED state again to block properly, unless
		 * we've just come back out of job control stop.
		 */
		spin_lock_irq(&task->sighand->siglock);
		if (task->signal->flags & SIGNAL_STOP_STOPPED)
			__set_current_state(TASK_STOPPED);
		spin_unlock_irq(&task->sighand->siglock);
	}

	if (task_is_stopped(current)) {
		/*
		 * While in TASK_STOPPED, we can be considered safely
		 * stopped by utrace_do_stop() only once we set this.
		 */
		spin_lock(&utrace->lock);
		utrace->stopped = 1;
		spin_unlock(&utrace->lock);
	}
}

/*
 * Called iff UTRACE_EVENT(EXIT) flag is set.
 */
void utrace_report_exit(long *exit_code)
{
	struct task_struct *task = current;
	struct utrace *utrace = task->utrace;
	INIT_REPORT(report);
	long orig_code = *exit_code;

	REPORT(task, utrace, &report, UTRACE_EVENT(EXIT),
	       report_exit, orig_code, exit_code);

	if (report.action == UTRACE_STOP)
		utrace_stop(task, utrace);
}

/*
 * Called iff UTRACE_EVENT(DEATH) or UTRACE_EVENT(QUIESCE) flag is set.
 *
 * It is always possible that we are racing with utrace_release_task here.
 * For this reason, utrace_release_task checks for the event bits that get
 * us here, and delays its cleanup for us to do.
 */
void utrace_report_death(struct task_struct *task, struct utrace *utrace,
			 bool group_dead, int signal)
{
	INIT_REPORT(report);

	BUG_ON(!task->exit_state);

	/*
	 * We are presently considered "quiescent"--which is accurate
	 * inasmuch as we won't run any more user instructions ever again.
	 * But for utrace_control and utrace_set_events to be robust, they
	 * must be sure whether or not we will run any more callbacks.  If
	 * a call comes in before we do, taking the lock here synchronizes
	 * us so we don't run any callbacks just disabled.  Calls that come
	 * in while we're running the callbacks will see the exit.death
	 * flag and know that we are not yet fully quiescent for purposes
	 * of detach bookkeeping.
	 */
	spin_lock(&utrace->lock);
	BUG_ON(utrace->death);
	utrace->death = 1;
	utrace->report = 0;
	utrace->interrupt = 0;
	spin_unlock(&utrace->lock);

	REPORT_CALLBACKS(task, utrace, &report, UTRACE_EVENT(DEATH),
			 report_death, engine, task, group_dead, signal);

	spin_lock(&utrace->lock);

	/*
	 * After we unlock (possibly inside utrace_reap for callbacks) with
	 * this flag clear, competing utrace_control/utrace_set_events calls
	 * know that we've finished our callbacks and any detach bookkeeping.
	 */
	utrace->death = 0;

	if (utrace->reap)
		/*
		 * utrace_release_task() was already called in parallel.
		 * We must complete its work now.
		 */
		utrace_reap(task, utrace);
	else
		utrace_reset(task, utrace, false);
}

/*
 * Finish the last reporting pass before returning to user mode.
 *
 * Returns true if we might have been in TASK_TRACED and then resumed.
 * In that event, signal_pending() might not be set when it should be,
 * as the signals code passes us over while we're in TASK_TRACED.
 */
static bool finish_resume_report(struct utrace_report *report,
				 struct task_struct *task,
				 struct utrace *utrace)
{
	if (report->detaches || !report->takers) {
		spin_lock(&utrace->lock);
		utrace_reset(task, utrace, false);
	}

	switch (report->action) {
	case UTRACE_INTERRUPT:
		if (!signal_pending(task))
			set_tsk_thread_flag(task, TIF_SIGPENDING);
		break;

	case UTRACE_SINGLESTEP:
		user_enable_single_step(task);
		break;

	case UTRACE_BLOCKSTEP:
		user_enable_block_step(task);
		break;

	case UTRACE_STOP:
		report->killed = utrace_stop(task, utrace);
		return likely(!report->killed);

	case UTRACE_REPORT:
	case UTRACE_RESUME:
	default:
		user_disable_single_step(task);
		break;
	}

	return false;
}

/*
 * This is called when TIF_NOTIFY_RESUME had been set (and is now clear).
 * We are close to user mode, and this is the place to report or stop.
 * When we return, we're going to user mode or into the signals code.
 */
void utrace_resume(struct task_struct *task, struct pt_regs *regs)
{
	struct utrace *utrace = task->utrace;
	INIT_REPORT(report);
	struct utrace_attached_engine *engine, *next;

	/*
	 * Some machines get here with interrupts disabled.  The same arch
	 * code path leads to calling into get_signal_to_deliver(), which
	 * implicitly reenables them by virtue of spin_unlock_irq.
	 */
	local_irq_enable();

	/*
	 * If this flag is still set it's because there was a signal
	 * handler setup done but no report_signal following it.  Clear
	 * the flag before we get to user so it doesn't confuse us later.
	 */
	if (unlikely(utrace->signal_handler)) {
		int skip;
		spin_lock(&utrace->lock);
		utrace->signal_handler = 0;
		skip = !utrace->report;
		spin_unlock(&utrace->lock);
		if (skip)
			return;
	}

	/*
	 * If UTRACE_INTERRUPT was just used, we don't bother with a
	 * report here.  We will report and stop in utrace_get_signal().
	 */
	if (unlikely(utrace->interrupt)) {
		BUG_ON(!signal_pending(task));
		return;
	}

	/*
	 * Do a simple reporting pass, with no callback after report_quiesce.
	 */
	start_report(utrace);

	list_for_each_entry_safe(engine, next, &utrace->attached, entry)
		start_callback(utrace, &report, engine, task, 0);

	/*
	 * Finish the report and either stop or get ready to resume.
	 * If we stop and then signal_pending() is clear, we
	 * should recompute it before returning to user mode.
	 */
	if (finish_resume_report(&report, task, utrace) &&
	    !signal_pending(task)) {
		spin_lock_irq(&task->sighand->siglock);
		recalc_sigpending();
		spin_unlock_irq(&task->sighand->siglock);
	}
}

/*
 * Return true if current has forced signal_pending().
 *
 * This is called only when current->utrace_flags is nonzero, so we know
 * that current->utrace must be set.  It's not inlined in tracehook.h
 * just so that struct utrace can stay opaque outside this file.
 */
bool utrace_interrupt_pending(void)
{
	return current->utrace->interrupt;
}

/*
 * Take the siglock and push @info back on our queue.
 * Returns with @task->sighand->siglock held.
 */
static void push_back_signal(struct task_struct *task, siginfo_t *info)
	__acquires(task->sighand->siglock)
{
	struct sigqueue *q;

	if (unlikely(!info->si_signo)) { /* Oh, a wise guy! */
		spin_lock_irq(&task->sighand->siglock);
		return;
	}

	q = sigqueue_alloc();
	if (likely(q)) {
		q->flags = 0;
		copy_siginfo(&q->info, info);
	}

	spin_lock_irq(&task->sighand->siglock);

	sigaddset(&task->pending.signal, info->si_signo);
	if (likely(q))
		list_add(&q->list, &task->pending.list);

	set_tsk_thread_flag(task, TIF_SIGPENDING);
}

/*
 * This is the hook from the signals code, called with the siglock held.
 * Here is the ideal place to stop.  We also dequeue and intercept signals.
 */
int utrace_get_signal(struct task_struct *task, struct pt_regs *regs,
		      siginfo_t *info, struct k_sigaction *return_ka)
	__releases(task->sighand->siglock)
	__acquires(task->sighand->siglock)
{
	struct utrace *utrace;
	struct k_sigaction *ka;
	INIT_REPORT(report);
	struct utrace_attached_engine *engine, *next;
	const struct utrace_engine_ops *ops;
	unsigned long event, want;
	u32 ret;
	int signr;

	/*
	 * We could have been considered quiescent while we were in
	 * TASK_STOPPED, and detached asynchronously.  If we woke up
	 * and checked task->utrace_flags before that was finished,
	 * we might be here with utrace already removed or in the
	 * middle of being removed.
	 */
	rcu_read_lock();
	utrace = rcu_dereference(task->utrace);
	if (unlikely(utrace == NULL)) {
		rcu_read_unlock();
		return 0;
	}

	if (utrace->interrupt || utrace->report || utrace->signal_handler) {
		/*
		 * We've been asked for an explicit report before we
		 * even check for pending signals.
		 */

		spin_unlock_irq(&task->sighand->siglock);

		/*
		 * RCU makes it safe to get the utrace->lock even if
		 * it's being freed.  Once we have that lock, either an
		 * external detach has finished and this struct has been
		 * freed, or else we know we are excluding any other
		 * detach attempt.
		 */
		spin_lock(&utrace->lock);
		rcu_read_unlock();

		if (unlikely(task->utrace != utrace)) {
			spin_unlock(&utrace->lock);
			cond_resched();
			return -1;
		}

		splice_attaching(utrace);

		if (unlikely(!utrace->interrupt) && unlikely(!utrace->report))
			report.result = UTRACE_SIGNAL_IGN;
		else if (utrace->signal_handler)
			report.result = UTRACE_SIGNAL_HANDLER;
		else
			report.result = UTRACE_SIGNAL_REPORT;

		/*
		 * We are now making the report and it's on the
		 * interrupt path, so clear the flags asking for those.
		 */
		utrace->interrupt = utrace->report = utrace->signal_handler = 0;

		/*
		 * Make sure signal_pending() only returns true
		 * if there are real signals pending.
		 */
		if (signal_pending(task)) {
			spin_lock_irq(&task->sighand->siglock);
			recalc_sigpending();
			spin_unlock_irq(&task->sighand->siglock);
		}

		spin_unlock(&utrace->lock);

		if (unlikely(report.result == UTRACE_SIGNAL_IGN))
			/*
			 * We only got here to clear utrace->signal_handler.
			 */
			return -1;

		/*
		 * Do a reporting pass for no signal, just for EVENT(QUIESCE).
		 * The engine callbacks can fill in *info and *return_ka.
		 * We'll pass NULL for the @orig_ka argument to indicate
		 * that there was no original signal.
		 */
		event = 0;
		ka = NULL;
		memset(return_ka, 0, sizeof *return_ka);
	} else if ((task->utrace_flags & UTRACE_EVENT_SIGNAL_ALL) == 0) {
		/*
		 * If noone is interested in intercepting signals,
		 * let the caller just dequeue them normally.
		 */
		rcu_read_unlock();
		return 0;
	} else {
		if (unlikely(utrace->stopped)) {
			/*
			 * We were just in TASK_STOPPED, so we have to
			 * check for the race mentioned above.
			 *
			 * RCU makes it safe to get the utrace->lock even
			 * if it's being freed.  Once we have that lock,
			 * either an external detach has finished and this
			 * struct has been freed, or else we know we are
			 * excluding any other detach attempt.  Since we
			 * are no longer in TASK_STOPPED now, all we needed
			 * the lock for was to order any utrace_do_stop()
			 * call after us.
			 */
			spin_unlock_irq(&task->sighand->siglock);
			spin_lock(&utrace->lock);
			rcu_read_unlock();
			if (unlikely(task->utrace != utrace)) {
				spin_unlock(&utrace->lock);
				cond_resched();
				return -1;
			}
			utrace->stopped = 0;
			spin_unlock(&utrace->lock);
			spin_lock_irq(&task->sighand->siglock);
		} else {
			rcu_read_unlock();
		}

		/*
		 * Steal the next signal so we can let tracing engines
		 * examine it.  From the signal number and sigaction,
		 * determine what normal delivery would do.  If no
		 * engine perturbs it, we'll do that by returning the
		 * signal number after setting *return_ka.
		 */
		signr = dequeue_signal(task, &task->blocked, info);
		if (signr == 0)
			return signr;
		BUG_ON(signr != info->si_signo);

		ka = &task->sighand->action[signr - 1];
		*return_ka = *ka;

		/*
		 * We are never allowed to interfere with SIGKILL.
		 * Just punt after filling in *return_ka for our caller.
		 */
		if (signr == SIGKILL)
			return signr;

		if (ka->sa.sa_handler == SIG_IGN) {
			event = UTRACE_EVENT(SIGNAL_IGN);
			report.result = UTRACE_SIGNAL_IGN;
		} else if (ka->sa.sa_handler != SIG_DFL) {
			event = UTRACE_EVENT(SIGNAL);
			report.result = UTRACE_SIGNAL_DELIVER;
		} else if (sig_kernel_coredump(signr)) {
			event = UTRACE_EVENT(SIGNAL_CORE);
			report.result = UTRACE_SIGNAL_CORE;
		} else if (sig_kernel_ignore(signr)) {
			event = UTRACE_EVENT(SIGNAL_IGN);
			report.result = UTRACE_SIGNAL_IGN;
		} else if (signr == SIGSTOP) {
			event = UTRACE_EVENT(SIGNAL_STOP);
			report.result = UTRACE_SIGNAL_STOP;
		} else if (sig_kernel_stop(signr)) {
			event = UTRACE_EVENT(SIGNAL_STOP);
			report.result = UTRACE_SIGNAL_TSTP;
		} else {
			event = UTRACE_EVENT(SIGNAL_TERM);
			report.result = UTRACE_SIGNAL_TERM;
		}

		/*
		 * Now that we know what event type this signal is,
		 * we can short-circuit if noone cares about those.
		 */
		if ((task->utrace_flags & (event | UTRACE_EVENT(QUIESCE))) == 0)
			return signr;

		/*
		 * We have some interested engines, so tell them about
		 * the signal and let them change its disposition.
		 */
		spin_unlock_irq(&task->sighand->siglock);
	}

	/*
	 * This reporting pass chooses what signal disposition we'll act on.
	 */
	list_for_each_entry_safe(engine, next, &utrace->attached, entry) {
		utrace->reporting = engine;
		smp_mb();

		/*
		 * This pairs with the barrier in mark_engine_detached(),
		 * see start_callback() comments.
		 */
		want = engine->flags;
		smp_rmb();
		ops = engine->ops;

		if ((want & (event | UTRACE_EVENT(QUIESCE))) == 0) {
			utrace->reporting = NULL;
			continue;
		}

		if (ops->report_signal)
			ret = (*ops->report_signal)(
				report.result | report.action, engine, task,
				regs, info, ka, return_ka);
		else
			ret = (report.result | (*ops->report_quiesce)(
				       report.action, engine, task, event));

		/*
		 * Avoid a tight loop reporting again and again if some
		 * engine is too stupid.
		 */
		switch (utrace_resume_action(ret)) {
		default:
			break;
		case UTRACE_INTERRUPT:
		case UTRACE_REPORT:
			ret = (ret & ~UTRACE_RESUME_MASK) | UTRACE_RESUME;
			break;
		}

		finish_callback(utrace, &report, engine, ret);
	}

	/*
	 * We express the chosen action to the signals code in terms
	 * of a representative signal whose default action does it.
	 * Our caller uses our return value (signr) to decide what to
	 * do, but uses info->si_signo as the signal number to report.
	 */
	switch (utrace_signal_action(report.result)) {
	case UTRACE_SIGNAL_TERM:
		signr = SIGTERM;
		break;

	case UTRACE_SIGNAL_CORE:
		signr = SIGQUIT;
		break;

	case UTRACE_SIGNAL_STOP:
		signr = SIGSTOP;
		break;

	case UTRACE_SIGNAL_TSTP:
		signr = SIGTSTP;
		break;

	case UTRACE_SIGNAL_DELIVER:
		signr = info->si_signo;

		if (return_ka->sa.sa_handler == SIG_DFL) {
			/*
			 * We'll do signr's normal default action.
			 * For ignore, we'll fall through below.
			 * For stop/death, break locks and returns it.
			 */
			if (likely(signr) && !sig_kernel_ignore(signr))
				break;
		} else if (return_ka->sa.sa_handler != SIG_IGN &&
			   likely(signr)) {
			/*
			 * The handler will run.  If an engine wanted to
			 * stop or step, then make sure we do another
			 * report after signal handler setup.
			 */
			if (report.action != UTRACE_RESUME) {
				spin_lock(&utrace->lock);
				utrace->interrupt = 1;
				spin_unlock(&utrace->lock);
				set_tsk_thread_flag(task, TIF_SIGPENDING);
			}

			if (unlikely(report.result & UTRACE_SIGNAL_HOLD))
				push_back_signal(task, info);
			else
				spin_lock_irq(&task->sighand->siglock);

			/*
			 * We do the SA_ONESHOT work here since the
			 * normal path will only touch *return_ka now.
			 */
			if (unlikely(return_ka->sa.sa_flags & SA_ONESHOT)) {
				return_ka->sa.sa_flags &= ~SA_ONESHOT;
				if (likely(valid_signal(signr))) {
					ka = &task->sighand->action[signr - 1];
					ka->sa.sa_handler = SIG_DFL;
				}
			}

			return signr;
		}

		/* Fall through for an ignored signal.  */

	case UTRACE_SIGNAL_IGN:
	case UTRACE_SIGNAL_REPORT:
	default:
		/*
		 * If the signal is being ignored, then we are on the way
		 * directly back to user mode.  We can stop here, or step,
		 * as in utrace_resume(), above.  After we've dealt with that,
		 * our caller will relock and come back through here.
		 */
		finish_resume_report(&report, task, utrace);

		if (unlikely(report.killed)) {
			/*
			 * The only reason we woke up now was because of a
			 * SIGKILL.  Don't do normal dequeuing in case it
			 * might get a signal other than SIGKILL.  That would
			 * perturb the death state so it might differ from
			 * what the debugger would have allowed to happen.
			 * Instead, pluck out just the SIGKILL to be sure
			 * we'll die immediately with nothing else different
			 * from the quiescent state the debugger wanted us in.
			 */
			sigset_t sigkill_only;
			siginitsetinv(&sigkill_only, sigmask(SIGKILL));
			spin_lock_irq(&task->sighand->siglock);
			signr = dequeue_signal(task, &sigkill_only, info);
			BUG_ON(signr != SIGKILL);
			*return_ka = task->sighand->action[SIGKILL - 1];
			return signr;
		}

		if (unlikely(report.result & UTRACE_SIGNAL_HOLD)) {
			push_back_signal(task, info);
			spin_unlock_irq(&task->sighand->siglock);
		}

		return -1;
	}

	return_ka->sa.sa_handler = SIG_DFL;

	if (unlikely(report.result & UTRACE_SIGNAL_HOLD))
		push_back_signal(task, info);
	else
		spin_lock_irq(&task->sighand->siglock);

	if (sig_kernel_stop(signr))
		task->signal->flags |= SIGNAL_STOP_DEQUEUED;

	return signr;
}

/*
 * This gets called after a signal handler has been set up.
 * We set a flag so the next report knows it happened.
 * If we're already stepping, make sure we do a report_signal.
 * If not, make sure we get into utrace_resume() where we can
 * clear the signal_handler flag before resuming.
 */
void utrace_signal_handler(struct task_struct *task, int stepping)
{
	struct utrace *utrace = task->utrace;

	spin_lock(&utrace->lock);

	utrace->signal_handler = 1;
	if (stepping) {
		utrace->interrupt = 1;
		set_tsk_thread_flag(task, TIF_SIGPENDING);
	} else {
		set_notify_resume(task);
	}

	spin_unlock(&utrace->lock);
}

/**
 * utrace_prepare_examine - prepare to examine thread state
 * @target:		thread of interest, a &struct task_struct pointer
 * @engine:		engine pointer returned by utrace_attach_task()
 * @exam:		temporary state, a &struct utrace_examiner pointer
 *
 * This call prepares to safely examine the thread @target using
 * &struct user_regset calls, or direct access to thread-synchronous fields.
 *
 * When @target is current, this call is superfluous.  When @target is
 * another thread, it must held stopped via %UTRACE_STOP by @engine.
 *
 * This call may block the caller until @target stays stopped, so it must
 * be called only after the caller is sure @target is about to unschedule.
 * This means a zero return from a utrace_control() call on @engine giving
 * %UTRACE_STOP, or a report_quiesce() or report_signal() callback to
 * @engine that used %UTRACE_STOP in its return value.
 *
 * Returns -%ESRCH if @target is dead or -%EINVAL if %UTRACE_STOP was
 * not used.  If @target has started running again despite %UTRACE_STOP
 * (for %SIGKILL or a spurious wakeup), this call returns -%EAGAIN.
 *
 * When this call returns zero, it's safe to use &struct user_regset
 * calls and task_user_regset_view() on @target and to examine some of
 * its fields directly.  When the examination is complete, a
 * utrace_finish_examine() call must follow to check whether it was
 * completed safely.
 */
int utrace_prepare_examine(struct task_struct *target,
			   struct utrace_attached_engine *engine,
			   struct utrace_examiner *exam)
{
	int ret = 0;

	if (unlikely(target == current))
		return 0;

	rcu_read_lock();
	if (unlikely(!engine_wants_stop(engine)))
		ret = -EINVAL;
	else if (unlikely(target->exit_state))
		ret = -ESRCH;
	else {
		exam->state = target->state;
		if (unlikely(exam->state == TASK_RUNNING))
			ret = -EAGAIN;
		else
			get_task_struct(target);
	}
	rcu_read_unlock();

	if (likely(!ret)) {
		exam->ncsw = wait_task_inactive(target, exam->state);
		put_task_struct(target);
		if (unlikely(!exam->ncsw))
			ret = -EAGAIN;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(utrace_prepare_examine);

/**
 * utrace_finish_examine - complete an examination of thread state
 * @target:		thread of interest, a &struct task_struct pointer
 * @engine:		engine pointer returned by utrace_attach_task()
 * @exam:		pointer passed to utrace_prepare_examine() call
 *
 * This call completes an examination on the thread @target begun by a
 * paired utrace_prepare_examine() call with the same arguments that
 * returned success (zero).
 *
 * When @target is current, this call is superfluous.  When @target is
 * another thread, this returns zero if @target has remained unscheduled
 * since the paired utrace_prepare_examine() call returned zero.
 *
 * When this returns an error, any examination done since the paired
 * utrace_prepare_examine() call is unreliable and the data extracted
 * should be discarded.  The error is -%EINVAL if @engine is not
 * keeping @target stopped, or -%EAGAIN if @target woke up unexpectedly.
 */
int utrace_finish_examine(struct task_struct *target,
			  struct utrace_attached_engine *engine,
			  struct utrace_examiner *exam)
{
	int ret = 0;

	if (unlikely(target == current))
		return 0;

	rcu_read_lock();
	if (unlikely(!engine_wants_stop(engine)))
		ret = -EINVAL;
	else if (unlikely(target->state != exam->state))
		ret = -EAGAIN;
	else
		get_task_struct(target);
	rcu_read_unlock();

	if (likely(!ret)) {
		unsigned long ncsw = wait_task_inactive(target, exam->state);
		if (unlikely(ncsw != exam->ncsw))
			ret = -EAGAIN;
		put_task_struct(target);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(utrace_finish_examine);

/*
 * This is declared in linux/regset.h and defined in machine-dependent
 * code.  We put the export here to ensure no machine forgets it.
 */
EXPORT_SYMBOL_GPL(task_user_regset_view);

/*
 * Return the &struct task_struct for the task using ptrace on this one,
 * or %NULL.  Must be called with rcu_read_lock() held to keep the returned
 * struct alive.
 *
 * At exec time, this may be called with task_lock() still held from when
 * tracehook_unsafe_exec() was just called.  In that case it must give
 * results consistent with those unsafe_exec() results, i.e. non-%NULL if
 * any %LSM_UNSAFE_PTRACE_* bits were set.
 *
 * The value is also used to display after "TracerPid:" in /proc/PID/status,
 * where it is called with only rcu_read_lock() held.
 */
struct task_struct *utrace_tracer_task(struct task_struct *target)
{
	struct utrace *utrace;
	struct task_struct *tracer = NULL;

	utrace = rcu_dereference(target->utrace);
	if (utrace != NULL) {
		struct list_head *pos, *next;
		struct utrace_attached_engine *engine;
		const struct utrace_engine_ops *ops;
		list_for_each_safe(pos, next, &utrace->attached) {
			engine = list_entry(pos, struct utrace_attached_engine,
					    entry);
			ops = rcu_dereference(engine->ops);
			if (ops->tracer_task) {
				tracer = (*ops->tracer_task)(engine, target);
				if (tracer != NULL)
					break;
			}
		}
	}

	return tracer;
}

/*
 * Called on the current task to return LSM_UNSAFE_* bits implied by tracing.
 * Called with task_lock() held.
 */
int utrace_unsafe_exec(struct task_struct *task)
{
	struct utrace *utrace = task->utrace;
	struct utrace_attached_engine *engine, *next;
	const struct utrace_engine_ops *ops;
	int unsafe = 0;

	list_for_each_entry_safe(engine, next, &utrace->attached, entry) {
		ops = rcu_dereference(engine->ops);
		if (ops->unsafe_exec)
			unsafe |= (*ops->unsafe_exec)(engine, task);
	}

	return unsafe;
}

/*
 * Called with rcu_read_lock() held.
 */
void task_utrace_proc_status(struct seq_file *m, struct task_struct *p)
{
	struct utrace *utrace = rcu_dereference(p->utrace);
	if (unlikely(utrace))
		seq_printf(m, "Utrace: %lx%s%s%s\n",
			   p->utrace_flags,
			   utrace->stopped ? " (stopped)" : "",
			   utrace->report ? " (report)" : "",
			   utrace->interrupt ? " (interrupt)" : "");
}
