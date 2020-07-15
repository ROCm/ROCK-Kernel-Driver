/* SPDX-License-Identifier: MIT */
#ifndef AMDKCL_DRM_TASK_BARRIER_H
#define AMDKCL_DRM_TASK_BARRIER_H

#ifdef HAVE_DRM_TASK_BARRIER_H
#include <drm/task_barrier.h>
#else
/*
 * Reusable 2 PHASE task barrier (randevouz point) implementation for N tasks.
 * Based on the Little book of sempahores - https://greenteapress.com/wp/semaphores/
 */
#include <linux/semaphore.h>
#include <linux/atomic.h>

/*
 * Represents an instance of a task barrier.
 */
struct task_barrier {
	unsigned int n;
	atomic_t count;
	struct semaphore enter_turnstile;
	struct semaphore exit_turnstile;
};

static inline void task_barrier_signal_turnstile(struct semaphore *turnstile,
						 unsigned int n)
{
	int i;

	for (i = 0 ; i < n; i++)
		up(turnstile);
}

static inline void task_barrier_init(struct task_barrier *tb)
{
	tb->n = 0;
	atomic_set(&tb->count, 0);
	sema_init(&tb->enter_turnstile, 0);
	sema_init(&tb->exit_turnstile, 0);
}

static inline void task_barrier_add_task(struct task_barrier *tb)
{
	tb->n++;
}

static inline void task_barrier_rem_task(struct task_barrier *tb)
{
	tb->n--;
}

/*
 * Lines up all the threads BEFORE the critical point.
 *
 * When all thread passed this code the entry barrier is back to locked state.
 */
static inline void task_barrier_enter(struct task_barrier *tb)
{
	if (atomic_inc_return(&tb->count) == tb->n)
		task_barrier_signal_turnstile(&tb->enter_turnstile, tb->n);

	down(&tb->enter_turnstile);
}

/*
 * Lines up all the threads AFTER the critical point.
 *
 * This function is used to avoid any one thread running ahead if the barrier is
 *  used repeatedly .
 */
static inline void task_barrier_exit(struct task_barrier *tb)
{
	if (atomic_dec_return(&tb->count) == 0)
		task_barrier_signal_turnstile(&tb->exit_turnstile, tb->n);

	down(&tb->exit_turnstile);
}

/* Convinieince function when nothing to be done in between entry and exit */
static inline void task_barrier_full(struct task_barrier *tb)
{
	task_barrier_enter(tb);
	task_barrier_exit(tb);
}
#endif
#endif
