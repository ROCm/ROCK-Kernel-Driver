#ifndef __LINUX_SPINLOCK_H
#define __LINUX_SPINLOCK_H

/*
 * include/linux/spinlock.h - generic locking declarations
 */

#include <linux/config.h>
#include <linux/preempt.h>
#include <linux/linkage.h>
#include <linux/compiler.h>
#include <linux/thread_info.h>
#include <linux/kernel.h>
#include <linux/stringify.h>

#include <asm/system.h>

/*
 * Must define these before including other files, inline functions need them
 */
#define LOCK_SECTION_NAME			\
	".text.lock." __stringify(KBUILD_BASENAME)

#define LOCK_SECTION_START(extra)		\
	".subsection 1\n\t"			\
	extra					\
	".ifndef " LOCK_SECTION_NAME "\n\t"	\
	LOCK_SECTION_NAME ":\n\t"		\
	".endif\n\t"

#define LOCK_SECTION_END			\
	".previous\n\t"

/*
 * If CONFIG_SMP is set, pull in the _raw_* definitions
 */
#ifdef CONFIG_SMP
#include <asm/spinlock.h>

/*
 * !CONFIG_SMP and spin_lock_init not previously defined
 * (e.g. by including include/asm/spinlock.h)
 */
#elif !defined(spin_lock_init)

#ifndef CONFIG_PREEMPT
# define atomic_dec_and_lock(atomic,lock) atomic_dec_and_test(atomic)
# define ATOMIC_DEC_AND_LOCK
#endif

/*
 * gcc versions before ~2.95 have a nasty bug with empty initializers.
 */
#if (__GNUC__ > 2)
  typedef struct { } spinlock_t;
  typedef struct { } rwlock_t;
  #define SPIN_LOCK_UNLOCKED (spinlock_t) { }
  #define RW_LOCK_UNLOCKED (rwlock_t) { }
#else
  typedef struct { int gcc_is_buggy; } spinlock_t;
  typedef struct { int gcc_is_buggy; } rwlock_t;
  #define SPIN_LOCK_UNLOCKED (spinlock_t) { 0 }
  #define RW_LOCK_UNLOCKED (rwlock_t) { 0 }
#endif

/*
 * If CONFIG_SMP is unset, declare the _raw_* definitions as nops
 */
#define spin_lock_init(lock)	do { (void)(lock); } while(0)
#define _raw_spin_lock(lock)	(void)(lock)
#define spin_is_locked(lock)	((void)(lock), 0)
#define _raw_spin_trylock(lock)	((void)(lock), 1)
#define spin_unlock_wait(lock)	do { (void)(lock); } while(0)
#define _raw_spin_unlock(lock)	do { (void)(lock); } while(0)
#define rwlock_init(lock)	do { } while(0)
#define _raw_read_lock(lock)	(void)(lock)
#define _raw_read_unlock(lock)	do { } while(0)
#define _raw_write_lock(lock)	(void)(lock)
#define _raw_write_unlock(lock)	do { } while(0)

#endif /* !SMP */

/*
 * Define the various spin_lock and rw_lock methods.  Note we define these
 * regardless of whether CONFIG_SMP or CONFIG_PREEMPT are set. The various
 * methods are defined as nops in the case they are not required.
 */
#define spin_lock(lock)	\
do { \
	preempt_disable(); \
	_raw_spin_lock(lock); \
} while(0)

#define spin_trylock(lock)	({preempt_disable(); _raw_spin_trylock(lock) ? \
				1 : ({preempt_enable(); 0;});})

#define spin_unlock(lock) \
do { \
	_raw_spin_unlock(lock); \
	preempt_enable(); \
} while (0)

#define read_lock(lock)	\
do { \
	preempt_disable(); \
	_raw_read_lock(lock); \
} while(0)

#define read_unlock(lock) \
do { \
	_raw_read_unlock(lock); \
	preempt_enable(); \
} while(0)

#define write_lock(lock) \
do { \
	preempt_disable(); \
	_raw_write_lock(lock); \
} while(0)

#define write_unlock(lock) \
do { \
	_raw_write_unlock(lock); \
	preempt_enable(); \
} while(0)

#define write_trylock(lock)	({preempt_disable();_raw_write_trylock(lock) ? \
				1 : ({preempt_enable(); 0;});})

#define spin_lock_irqsave(lock, flags) \
do { \
	local_irq_save(flags); \
	preempt_disable(); \
	_raw_spin_lock(lock); \
} while (0)

#define spin_lock_irq(lock) \
do { \
	local_irq_disable(); \
	preempt_disable(); \
	_raw_spin_lock(lock); \
} while (0)

#define spin_lock_bh(lock) \
do { \
	local_bh_disable(); \
	preempt_disable(); \
	_raw_spin_lock(lock); \
} while (0)

#define read_lock_irqsave(lock, flags) \
do { \
	local_irq_save(flags); \
	preempt_disable(); \
	_raw_read_lock(lock); \
} while (0)

#define read_lock_irq(lock) \
do { \
	local_irq_disable(); \
	preempt_disable(); \
	_raw_read_lock(lock); \
} while (0)

#define read_lock_bh(lock) \
do { \
	local_bh_disable(); \
	preempt_disable(); \
	_raw_read_lock(lock); \
} while (0)

#define write_lock_irqsave(lock, flags) \
do { \
	local_irq_save(flags); \
	preempt_disable(); \
	_raw_write_lock(lock); \
} while (0)

#define write_lock_irq(lock) \
do { \
	local_irq_disable(); \
	preempt_disable(); \
	_raw_write_lock(lock); \
} while (0)

#define write_lock_bh(lock) \
do { \
	local_bh_disable(); \
	preempt_disable(); \
	_raw_write_lock(lock); \
} while (0)

#define spin_unlock_irqrestore(lock, flags) \
do { \
	_raw_spin_unlock(lock); \
	local_irq_restore(flags); \
	preempt_enable(); \
} while (0)

#define _raw_spin_unlock_irqrestore(lock, flags) \
do { \
	_raw_spin_unlock(lock); \
	local_irq_restore(flags); \
} while (0)

#define spin_unlock_irq(lock) \
do { \
	_raw_spin_unlock(lock); \
	local_irq_enable(); \
	preempt_enable(); \
} while (0)

#define spin_unlock_bh(lock) \
do { \
	_raw_spin_unlock(lock); \
	preempt_enable(); \
	local_bh_enable(); \
} while (0)

#define read_unlock_irqrestore(lock, flags) \
do { \
	_raw_read_unlock(lock); \
	local_irq_restore(flags); \
	preempt_enable(); \
} while (0)

#define read_unlock_irq(lock) \
do { \
	_raw_read_unlock(lock); \
	local_irq_enable(); \
	preempt_enable(); \
} while (0)

#define read_unlock_bh(lock) \
do { \
	_raw_read_unlock(lock); \
	preempt_enable(); \
	local_bh_enable(); \
} while (0)

#define write_unlock_irqrestore(lock, flags) \
do { \
	_raw_write_unlock(lock); \
	local_irq_restore(flags); \
	preempt_enable(); \
} while (0)

#define write_unlock_irq(lock) \
do { \
	_raw_write_unlock(lock); \
	local_irq_enable(); \
	preempt_enable(); \
} while (0)

#define write_unlock_bh(lock) \
do { \
	_raw_write_unlock(lock); \
	preempt_enable(); \
	local_bh_enable(); \
} while (0)

#define spin_trylock_bh(lock)	({ local_bh_disable(); preempt_disable(); \
				_raw_spin_trylock(lock) ? 1 : \
				({preempt_enable(); local_bh_enable(); 0;});})

/* "lock on reference count zero" */
#ifndef ATOMIC_DEC_AND_LOCK
#include <asm/atomic.h>
extern int atomic_dec_and_lock(atomic_t *atomic, spinlock_t *lock);
#endif

#endif /* __LINUX_SPINLOCK_H */
