#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <asm/system.h>

/* Note that PA-RISC has to use `1' to mean unlocked and `0' to mean locked
 * since it only has load-and-zero.
 */

#undef SPIN_LOCK_UNLOCKED
#define SPIN_LOCK_UNLOCKED (spinlock_t) { 1 }

#define spin_lock_init(x)	do { (x)->lock = 1; } while(0)

#define spin_is_locked(x) ((x)->lock == 0)

#define spin_unlock_wait(x)	do { barrier(); } while(((volatile spinlock_t *)(x))->lock == 0)
#define _raw_spin_lock_flags(lock, flags) _raw_spin_lock(lock)

#if 1
#define _raw_spin_lock(x) do { \
	while (__ldcw (&(x)->lock) == 0) \
		while (((x)->lock) == 0) ; } while (0)

#else
#define _raw_spin_lock(x) \
	do { while(__ldcw(&(x)->lock) == 0); } while(0)
#endif
	
#define _raw_spin_unlock(x) \
	do { (x)->lock = 1; } while(0)

#define _raw_spin_trylock(x) (__ldcw(&(x)->lock) != 0)



/*
 * Read-write spinlocks, allowing multiple readers
 * but only one writer.
 */
typedef struct {
	spinlock_t lock;
	volatile int counter;
} rwlock_t;

#define RW_LOCK_UNLOCKED (rwlock_t) { {1}, 0 }

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
