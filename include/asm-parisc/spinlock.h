#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <asm/system.h>

/* Note that PA-RISC has to use `1' to mean unlocked and `0' to mean locked
 * since it only has load-and-zero. Moreover, at least on some PA processors,
 * the semaphore address has to be 16-byte aligned.
 */

#undef SPIN_LOCK_UNLOCKED
#define SPIN_LOCK_UNLOCKED (spinlock_t) { { 1, 1, 1, 1 } }

#define spin_lock_init(x)	do { *(x) = SPIN_LOCK_UNLOCKED; } while(0)

static inline int spin_is_locked(spinlock_t *x)
{
	volatile unsigned int *a = __ldcw_align(x);
	return *a == 0;
}

#define spin_unlock_wait(x)	do { barrier(); } while(spin_is_locked(x))
#define _raw_spin_lock_flags(lock, flags) _raw_spin_lock(lock)

static inline void _raw_spin_lock(spinlock_t *x)
{
	volatile unsigned int *a = __ldcw_align(x);
	while (__ldcw(a) == 0)
		while (*a == 0);
}

static inline void _raw_spin_unlock(spinlock_t *x)
{
	volatile unsigned int *a = __ldcw_align(x);
	*a = 1;
}

static inline int _raw_spin_trylock(spinlock_t *x)
{
	volatile unsigned int *a = __ldcw_align(x);
	return __ldcw(a) != 0;
}
	
/*
 * Read-write spinlocks, allowing multiple readers
 * but only one writer.
 */
typedef struct {
	spinlock_t lock;
	volatile int counter;
} rwlock_t;

#define RW_LOCK_UNLOCKED (rwlock_t) { { { 1, 1, 1, 1 } }, 0 }

#define rwlock_init(lp)	do { *(lp) = RW_LOCK_UNLOCKED; } while (0)

#define rwlock_is_locked(lp) ((lp)->counter != 0)

/* read_lock, read_unlock are pretty straightforward.  Of course it somehow
 * sucks we end up saving/restoring flags twice for read_lock_irqsave aso. */

static  __inline__ void _raw_read_lock(rwlock_t *rw)
{
	unsigned long flags;
	local_irq_save(flags);
	_raw_spin_lock(&rw->lock); 

	rw->counter++;

	_raw_spin_unlock(&rw->lock);
	local_irq_restore(flags);
}

static  __inline__ void _raw_read_unlock(rwlock_t *rw)
{
	unsigned long flags;
	local_irq_save(flags);
	_raw_spin_lock(&rw->lock); 

	rw->counter--;

	_raw_spin_unlock(&rw->lock);
	local_irq_restore(flags);
}

/* write_lock is less trivial.  We optimistically grab the lock and check
 * if we surprised any readers.  If so we release the lock and wait till
 * they're all gone before trying again
 *
 * Also note that we don't use the _irqsave / _irqrestore suffixes here.
 * If we're called with interrupts enabled and we've got readers (or other
 * writers) in interrupt handlers someone fucked up and we'd dead-lock
 * sooner or later anyway.   prumpf */

static  __inline__ void _raw_write_lock(rwlock_t *rw)
{
retry:
	_raw_spin_lock(&rw->lock);

	if(rw->counter != 0) {
		/* this basically never happens */
		_raw_spin_unlock(&rw->lock);

		while(rw->counter != 0);

		goto retry;
	}

	/* got it.  now leave without unlocking */
	rw->counter = -1; /* remember we are locked */
}

/* write_unlock is absolutely trivial - we don't have to wait for anything */

static  __inline__ void _raw_write_unlock(rwlock_t *rw)
{
	rw->counter = 0;
	_raw_spin_unlock(&rw->lock);
}

static __inline__ int is_read_locked(rwlock_t *rw)
{
	return rw->counter > 0;
}

static __inline__ int is_write_locked(rwlock_t *rw)
{
	return rw->counter < 0;
}

#endif /* __ASM_SPINLOCK_H */
