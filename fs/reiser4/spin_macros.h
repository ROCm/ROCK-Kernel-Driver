/* Copyright 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

/* Wrapper functions/macros for spin locks. */

/*
 * This file implements wrapper functions and macros to work with spin locks
 * and read write locks embedded into kernel objects. Wrapper functions
 * provide following functionality:
 *
 *    (1) encapsulation of locks: in stead of writing spin_lock(&obj->lock),
 *    where obj is object of type foo, one writes spin_lock_foo(obj).
 *
 *    (2) optional keeping (in per-thread reiser4_context->locks) information
 *    about number of locks of particular type currently held by thread. This
 *    is done if REISER4_DEBUG is on.
 *
 *    (3) optional checking of lock ordering. For object type foo, it is
 *    possible to provide "lock ordering predicate" (possibly using
 *    information stored in reiser4_context->locks) checking that locks are
 *    acquired in the proper order. This is done if REISER4_DEBUG is on.
 *
 *    (4) optional collection of spin lock contention statistics. In this mode
 *    two sysfs objects (located in /sys/profregion) are associated with each
 *    spin lock type. One object (foo_t) shows how much time was spent trying
 *    to acquire spin locks of foo type. Another (foo_h) shows how much time
 *    spin locks of the type foo were held locked. See spinprof.h for more
 *    details on this.
 *
 */

#ifndef __SPIN_MACROS_H__
#define __SPIN_MACROS_H__

#include <linux/spinlock.h>
#include <linux/profile.h>

#include "debug.h"

/* Checks that read write lock @s is locked (or not) by the -current-
 * thread. not yet implemented */
#define check_is_write_locked(s)     ((void)(s), 1)
#define check_is_read_locked(s)      ((void)(s), 1)
#define check_is_not_read_locked(s)  ((void)(s), 1)
#define check_is_not_write_locked(s) ((void)(s), 1)

/* Checks that spin lock @s is locked (or not) by the -current- thread. */
#define check_spin_is_not_locked(s) ((void)(s), 1)
#define spin_is_not_locked(s)       ((void)(s), 1)
#if defined(CONFIG_SMP)
#    define check_spin_is_locked(s)     spin_is_locked(s)
#else
#    define check_spin_is_locked(s)     ((void)(s), 1)
#endif

/*
 * Data structure embedded into kernel objects together with spin lock.
 */
typedef struct reiser4_spin_data {
	/* spin lock proper */
	spinlock_t lock;
} reiser4_spin_data;

/*
 * Data structure embedded into kernel objects together with read write lock.
 */
typedef struct reiser4_rw_data {
	/* read write lock proper */
	rwlock_t lock;
} reiser4_rw_data;

#if REISER4_DEBUG
#define __ODCA(l, e) ON_DEBUG_CONTEXT(assert(l, e))
#else
#define __ODCA(l, e) noop
#endif

/* Define several inline functions for each type of spinlock. This is long
 * monster macro definition. */
#define SPIN_LOCK_FUNCTIONS(NAME,TYPE,FIELD)					\
										\
/* Initialize spin lock embedded in @x			*/			\
static inline void spin_ ## NAME ## _init(TYPE *x)				\
{										\
	__ODCA("nikita-2987", x != NULL);					\
	memset(& x->FIELD, 0, sizeof x->FIELD);					\
	spin_lock_init(& x->FIELD.lock);					\
}										\
										\
/* Increment per-thread lock counter for this lock type and total counter */	\
/* of acquired spin locks. This is helper function used by spin lock      */	\
/* acquiring functions below                                              */	\
static inline void spin_ ## NAME ## _inc(void)					\
{										\
	LOCK_CNT_INC(spin_locked_ ## NAME);					\
	LOCK_CNT_INC(spin_locked);						\
}										\
										\
/* Decrement per-thread lock counter and total counter of acquired spin   */	\
/* locks. This is helper function used by spin lock releasing functions   */	\
/* below.                                                                 */	\
static inline void spin_ ## NAME ## _dec(void)					\
{										\
	LOCK_CNT_DEC(spin_locked_ ## NAME);					\
	LOCK_CNT_DEC(spin_locked);						\
}										\
										\
/* Return true of spin lock embedded in @x is acquired by -current-       */	\
/* thread                                                                 */	\
static inline int  spin_ ## NAME ## _is_locked (const TYPE *x)			\
{										\
	return check_spin_is_locked (& x->FIELD.lock) &&			\
	       LOCK_CNT_GTZ(spin_locked_ ## NAME);				\
}										\
										\
/* Return true of spin lock embedded in @x is not acquired by -current-   */	\
/* thread                                                                 */	\
static inline int  spin_ ## NAME ## _is_not_locked (TYPE *x)			\
{										\
	return check_spin_is_not_locked (& x->FIELD.lock);			\
}										\
										\
/* Acquire spin lock embedded in @x without checking lock ordering.       */	\
/* This is useful when, for example, locking just created object.         */	\
static inline void spin_lock_ ## NAME ## _no_ord (TYPE *x) 			\
{										\
	__ODCA("nikita-2703", spin_ ## NAME ## _is_not_locked(x));		\
	spin_lock(&x->FIELD.lock);						\
	spin_ ## NAME ## _inc();						\
}										\
										\
/* Account for spin lock acquired by some other means. For example        */	\
/* through atomic_dec_and_lock() or similar.                              */	\
static inline void spin_lock_ ## NAME ## _acc (TYPE *x)				\
{										\
	spin_ ## NAME ## _inc();						\
}										\
										\
/* Lock @x with explicit indication of spin lock profiling "sites".       */	\
/* Locksite is used by spin lock profiling code (spinprof.[ch]) to        */	\
/* identify fragment of code that locks @x.                               */	\
/*                                                                        */	\
/* If clock interrupt finds that current thread is spinning waiting for   */	\
/* the lock on @x, counters in @t will be incremented.                    */	\
/*                                                                        */	\
/* If clock interrupt finds that current thread holds the lock on @x,     */	\
/* counters in @h will be incremented.                                    */	\
/*                                                                        */	\
static inline void spin_lock_ ## NAME ## _at (TYPE *x) 				\
{										\
	__ODCA("nikita-1383", spin_ordering_pred_ ## NAME(x));			\
	spin_lock_ ## NAME ## _no_ord(x);					\
}										\
										\
/* Lock @x.                                                               */	\
static inline void spin_lock_ ## NAME (TYPE *x)					\
{										\
	__ODCA("nikita-1383", spin_ordering_pred_ ## NAME(x));			\
	spin_lock_ ## NAME ## _no_ord(x);					\
}										\
										\
/* Try to obtain lock @x. On success, returns 1 with @x locked.           */	\
/* If @x is already locked, return 0 immediately.                         */	\
static inline int  spin_trylock_ ## NAME (TYPE *x)				\
{										\
	if (spin_trylock (& x->FIELD.lock)) {					\
		spin_ ## NAME ## _inc();					\
		return 1;							\
	}									\
	return 0;								\
}										\
										\
/* Unlock @x.                                                             */	\
static inline void spin_unlock_ ## NAME (TYPE *x)				\
{										\
	__ODCA("nikita-1375", LOCK_CNT_GTZ(spin_locked_ ## NAME));		\
	__ODCA("nikita-1376", LOCK_CNT_GTZ(spin_locked > 0));			\
	__ODCA("nikita-2703", spin_ ## NAME ## _is_locked(x));			\
										\
	spin_ ## NAME ## _dec();						\
	spin_unlock (& x->FIELD.lock);						\
}										\
										\
typedef struct { int foo; } NAME ## _spin_dummy

/*
 * Helper macro to perform a simple operation that requires taking of spin
 * lock.
 *
 * 1. Acquire spin lock on object @obj of type @obj_type.
 *
 * 2. Execute @exp under spin lock, and store result.
 *
 * 3. Release spin lock.
 *
 * 4. Return result of @exp.
 *
 * Example:
 *
 * right_delimiting_key = UNDER_SPIN(dk, current_tree, *znode_get_rd_key(node));
 *
 */
#define UNDER_SPIN(obj_type, obj, exp)						\
({										\
	typeof (obj) __obj;							\
	typeof (exp) __result;							\
										\
	__obj = (obj);								\
	__ODCA("nikita-2492", __obj != NULL);					\
	spin_lock_ ## obj_type ## _at (__obj);					\
	__result = exp;								\
	spin_unlock_ ## obj_type (__obj);					\
	__result;								\
})

/*
 * The same as UNDER_SPIN, but without storing and returning @exp's result.
 */
#define UNDER_SPIN_VOID(obj_type, obj, exp)					\
({										\
	typeof (obj) __obj;							\
										\
	__obj = (obj);								\
	__ODCA("nikita-2492", __obj != NULL);					\
	spin_lock_ ## obj_type ## _at (__obj);					\
	exp;									\
	spin_unlock_ ## obj_type (__obj);					\
})


/* Define several inline functions for each type of read write lock. This is
 * insanely long macro definition. */
#define RW_LOCK_FUNCTIONS(NAME,TYPE,FIELD)					\
										\
										\
/* Initialize read write lock embedded into @x.                           */	\
static inline void rw_ ## NAME ## _init(TYPE *x)				\
{										\
	__ODCA("nikita-2988", x != NULL);					\
	memset(& x->FIELD, 0, sizeof x->FIELD);					\
	rwlock_init(& x->FIELD.lock);						\
}										\
										\
/* True, if @x is read locked by the -current- thread.                    */	\
static inline int  rw_ ## NAME ## _is_read_locked (const TYPE *x)		\
{										\
	return check_is_read_locked (& x->FIELD.lock);				\
}										\
										\
/* True, if @x is write locked by the -current- thread.                   */	\
static inline int  rw_ ## NAME ## _is_write_locked (const TYPE *x)		\
{										\
	return check_is_write_locked (& x->FIELD.lock);				\
}										\
										\
/* True, if @x is not read locked by the -current- thread.                */	\
static inline int  rw_ ## NAME ## _is_not_read_locked (TYPE *x)			\
{										\
	return check_is_not_read_locked (& x->FIELD.lock);			\
}										\
										\
/* True, if @x is not write locked by the -current- thread.               */	\
static inline int  rw_ ## NAME ## _is_not_write_locked (TYPE *x)		\
{										\
	return check_is_not_write_locked (& x->FIELD.lock);			\
}										\
										\
/* True, if @x is either read or write locked by the -current- thread.    */	\
static inline int  rw_ ## NAME ## _is_locked (const TYPE *x)			\
{										\
	return check_is_read_locked (& x->FIELD.lock) ||			\
	       check_is_write_locked (& x->FIELD.lock);				\
}										\
										\
/* True, if @x is neither read nor write locked by the -current- thread.  */	\
static inline int  rw_ ## NAME ## _is_not_locked (const TYPE *x)		\
{										\
	return check_is_not_read_locked (& x->FIELD.lock) &&			\
	       check_is_not_write_locked (& x->FIELD.lock);			\
}										\
										\
/* This is helper function used by lock acquiring functions below         */	\
static inline void read_ ## NAME ## _inc(void)					\
{										\
	LOCK_CNT_INC(read_locked_ ## NAME);					\
	LOCK_CNT_INC(rw_locked_ ## NAME);					\
	LOCK_CNT_INC(spin_locked);						\
}										\
										\
/* This is helper function used by lock acquiring functions below         */	\
static inline void read_ ## NAME ## _dec(void)					\
{										\
	LOCK_CNT_DEC(read_locked_ ## NAME);					\
	LOCK_CNT_DEC(rw_locked_ ## NAME);					\
	LOCK_CNT_DEC(spin_locked);						\
}										\
										\
/* This is helper function used by lock acquiring functions below         */	\
static inline void write_ ## NAME ## _inc(void)					\
{										\
	LOCK_CNT_INC(write_locked_ ## NAME);					\
	LOCK_CNT_INC(rw_locked_ ## NAME);					\
	LOCK_CNT_INC(spin_locked);						\
}										\
										\
/* This is helper function used by lock acquiring functions below         */	\
static inline void write_ ## NAME ## _dec(void)					\
{										\
	LOCK_CNT_DEC(write_locked_ ## NAME);					\
	LOCK_CNT_DEC(rw_locked_ ## NAME);					\
	LOCK_CNT_DEC(spin_locked);						\
}										\
										\
/* Acquire read lock on @x without checking lock ordering predicates.     */	\
/* This is useful when, for example, locking just created object.         */	\
static inline void read_lock_ ## NAME ## _no_ord (TYPE *x)			\
{										\
	__ODCA("nikita-2976", rw_ ## NAME ## _is_not_read_locked(x));		\
	read_lock(&x->FIELD.lock);						\
	read_ ## NAME ## _inc();						\
}										\
										\
/* Acquire write lock on @x without checking lock ordering predicates.    */	\
/* This is useful when, for example, locking just created object.         */	\
static inline void write_lock_ ## NAME ## _no_ord (TYPE *x)			\
{										\
	__ODCA("nikita-2977", rw_ ## NAME ## _is_not_write_locked(x));		\
	write_lock(&x->FIELD.lock);						\
	write_ ## NAME ## _inc();						\
}										\
										\
/* Read lock @x with explicit indication of spin lock profiling "sites".  */	\
/* See spin_lock_foo_at() above for more information.                     */	\
static inline void read_lock_ ## NAME ## _at (TYPE *x) 				\
{										\
	__ODCA("nikita-2975", rw_ordering_pred_ ## NAME(x));			\
	read_lock_ ## NAME ## _no_ord(x);					\
}										\
										\
/* Write lock @x with explicit indication of spin lock profiling "sites". */	\
/* See spin_lock_foo_at() above for more information.                     */	\
static inline void write_lock_ ## NAME ## _at (TYPE *x)				\
{										\
	__ODCA("nikita-2978", rw_ordering_pred_ ## NAME(x));			\
	write_lock_ ## NAME ## _no_ord(x);					\
}										\
										\
/* Read lock @x.                                                          */	\
static inline void read_lock_ ## NAME (TYPE *x)					\
{										\
	__ODCA("nikita-2975", rw_ordering_pred_ ## NAME(x));			\
	read_lock_ ## NAME ## _no_ord(x);					\
}										\
										\
/* Write lock @x.                                                         */	\
static inline void write_lock_ ## NAME (TYPE *x)				\
{										\
	__ODCA("nikita-2978", rw_ordering_pred_ ## NAME(x));			\
	write_lock_ ## NAME ## _no_ord(x);					\
}										\
										\
/* Release read lock on @x.                                               */	\
static inline void read_unlock_ ## NAME (TYPE *x)				\
{										\
	__ODCA("nikita-2979", LOCK_CNT_GTZ(read_locked_ ## NAME));		\
	__ODCA("nikita-2980", LOCK_CNT_GTZ(rw_locked_ ## NAME));		\
	__ODCA("nikita-2980", LOCK_CNT_GTZ(spin_locked));			\
	read_ ## NAME ## _dec();						\
	__ODCA("nikita-2703", rw_ ## NAME ## _is_read_locked(x));		\
	read_unlock (& x->FIELD.lock);						\
}										\
										\
/* Release write lock on @x.                                              */	\
static inline void write_unlock_ ## NAME (TYPE *x)				\
{										\
	__ODCA("nikita-2979", LOCK_CNT_GTZ(write_locked_ ## NAME));		\
	__ODCA("nikita-2980", LOCK_CNT_GTZ(rw_locked_ ## NAME));		\
	__ODCA("nikita-2980", LOCK_CNT_GTZ(spin_locked));			\
	write_ ## NAME ## _dec();						\
	__ODCA("nikita-2703", rw_ ## NAME ## _is_write_locked(x));		\
	write_unlock (& x->FIELD.lock);						\
}										\
										\
/* Try to obtain write lock on @x. On success, returns 1 with @x locked.  */	\
/* If @x is already locked, return 0 immediately.                         */	\
static inline int  write_trylock_ ## NAME (TYPE *x)				\
{										\
	if (write_trylock (& x->FIELD.lock)) {					\
		write_ ## NAME ## _inc();					\
		return 1;							\
	}									\
	return 0;								\
}										\
										\
										\
typedef struct { int foo; } NAME ## _rw_dummy

/*
 * Helper macro to perform a simple operation that requires taking of read
 * write lock.
 *
 * 1. Acquire read or write (depending on @rw parameter) lock on object @obj
 * of type @obj_type.
 *
 * 2. Execute @exp under lock, and store result.
 *
 * 3. Release lock.
 *
 * 4. Return result of @exp.
 *
 * Example:
 *
 * tree_height = UNDER_RW(tree, current_tree, read, current_tree->height);
 */
#define UNDER_RW(obj_type, obj, rw, exp)				\
({									\
	typeof (obj) __obj;						\
	typeof (exp) __result;						\
									\
	__obj = (obj);							\
	__ODCA("nikita-2981", __obj != NULL);				\
	rw ## _lock_ ## obj_type ## _at (__obj);			\
	__result = exp;							\
	rw ## _unlock_ ## obj_type (__obj);				\
	__result;							\
})

/*
 * The same as UNDER_RW, but without storing and returning @exp's result.
 */
#define UNDER_RW_VOID(obj_type, obj, rw, exp)				\
({									\
	typeof (obj) __obj;						\
									\
	__obj = (obj);							\
	__ODCA("nikita-2982", __obj != NULL);				\
	rw ## _lock_ ## obj_type ## _at (__obj);			\
	exp;								\
	rw ## _unlock_ ## obj_type (__obj);				\
})

#define LOCK_JNODE(node) spin_lock_jnode(node)
#define LOCK_JLOAD(node) spin_lock_jload(node)
#define LOCK_ATOM(atom) spin_lock_atom(atom)
#define LOCK_TXNH(txnh) spin_lock_txnh(txnh)
#define LOCK_INODE(inode) spin_lock_inode_object(inode)
#define RLOCK_TREE(tree) read_lock_tree(tree)
#define WLOCK_TREE(tree) write_lock_tree(tree)
#define RLOCK_DK(tree) read_lock_dk(tree)
#define WLOCK_DK(tree) write_lock_dk(tree)
#define RLOCK_ZLOCK(lock) read_lock_zlock(lock)
#define WLOCK_ZLOCK(lock) write_lock_zlock(lock)

#define UNLOCK_JNODE(node) spin_unlock_jnode(node)
#define UNLOCK_JLOAD(node) spin_unlock_jload(node)
#define UNLOCK_ATOM(atom) spin_unlock_atom(atom)
#define UNLOCK_TXNH(txnh) spin_unlock_txnh(txnh)
#define UNLOCK_INODE(inode) spin_unlock_inode_object(inode)
#define RUNLOCK_TREE(tree) read_unlock_tree(tree)
#define WUNLOCK_TREE(tree) write_unlock_tree(tree)
#define RUNLOCK_DK(tree) read_unlock_dk(tree)
#define WUNLOCK_DK(tree) write_unlock_dk(tree)
#define RUNLOCK_ZLOCK(lock) read_unlock_zlock(lock)
#define WUNLOCK_ZLOCK(lock) write_unlock_zlock(lock)

/* __SPIN_MACROS_H__ */
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
