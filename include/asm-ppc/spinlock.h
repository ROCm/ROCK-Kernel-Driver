#ifdef __KERNEL__
#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

/*
 * Simple spin lock operations.
 */

typedef struct {
	volatile unsigned long lock;
	volatile unsigned long owner_pc;
	volatile unsigned long owner_cpu;
} spinlock_t;

#define SPIN_LOCK_UNLOCKED	(spinlock_t) { 0, 0, 0 }
#define spin_lock_init(lp) 	do { (lp)->lock = 0; } while(0)
#define spin_unlock_wait(lp)	do { barrier(); } while((lp)->lock)
#define spin_is_locked(x)	((x)->lock != 0)

extern void _spin_lock(spinlock_t *lock);
extern void _spin_unlock(spinlock_t *lock);
extern int spin_trylock(spinlock_t *lock);

#define spin_lock(lp)			_spin_lock(lp)
#define spin_unlock(lp)			_spin_unlock(lp)

extern unsigned long __spin_trylock(volatile unsigned long *lock);

/*
 * Read-write spinlocks, allowing multiple readers
 * but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts
 * but no interrupt writers. For those circumstances we
 * can "mix" irq-safe locks - any writer needs to get a
 * irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 */
typedef struct {
	volatile unsigned long lock;
	volatile unsigned long owner_pc;
} rwlock_t;

#define RW_LOCK_UNLOCKED (rwlock_t) { 0, 0 }
#define rwlock_init(lp) do { *(lp) = RW_LOCK_UNLOCKED; } while(0)

extern void _read_lock(rwlock_t *rw);
extern void _read_unlock(rwlock_t *rw);
extern void _write_lock(rwlock_t *rw);
extern void _write_unlock(rwlock_t *rw);

#define read_lock(rw)		_read_lock(rw)
#define write_lock(rw)		_write_lock(rw)
#define write_unlock(rw)	_write_unlock(rw)
#define read_unlock(rw)		_read_unlock(rw)

#endif /* __ASM_SPINLOCK_H */
#endif /* __KERNEL__ */
