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

#include <asm/processor.h>	/* for cpu relax */
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

#else

#if !defined(CONFIG_PREEMPT) && !defined(CONFIG_DEBUG_SPINLOCK)
# define atomic_dec_and_lock(atomic,lock) atomic_dec_and_test(atomic)
# define ATOMIC_DEC_AND_LOCK
#endif

#ifdef CONFIG_DEBUG_SPINLOCK
 
#define SPINLOCK_MAGIC	0x1D244B3C
typedef struct {
	unsigned long magic;
	volatile unsigned long lock;
	volatile unsigned int babble;
	const char *module;
	char *owner;
	int oline;
} spinlock_t;
#define SPIN_LOCK_UNLOCKED (spinlock_t) { SPINLOCK_MAGIC, 0, 10, __FILE__ , NULL, 0}

#define spin_lock_init(x) \
	do { \
		(x)->magic = SPINLOCK_MAGIC; \
		(x)->lock = 0; \
		(x)->babble = 5; \
		(x)->module = __FILE__; \
		(x)->owner = NULL; \
		(x)->oline = 0; \
	} while (0)

#define CHECK_LOCK(x) \
	do { \
	 	if ((x)->magic != SPINLOCK_MAGIC) { \
			printk(KERN_ERR "%s:%d: spin_is_locked on uninitialized spinlock %p.\n", \
					__FILE__, __LINE__, (x)); \
		} \
	} while(0)

#define _raw_spin_lock(x)		\
	do { \
	 	CHECK_LOCK(x); \
		if ((x)->lock&&(x)->babble) { \
			(x)->babble--; \
			printk("%s:%d: spin_lock(%s:%p) already locked by %s/%d\n", \
					__FILE__,__LINE__, (x)->module, \
					(x), (x)->owner, (x)->oline); \
		} \
		(x)->lock = 1; \
		(x)->owner = __FILE__; \
		(x)->oline = __LINE__; \
	} while (0)

/* without debugging, spin_is_locked on UP always says
 * FALSE. --> printk if already locked. */
#define spin_is_locked(x) \
	({ \
	 	CHECK_LOCK(x); \
		if ((x)->lock&&(x)->babble) { \
			(x)->babble--; \
			printk("%s:%d: spin_is_locked(%s:%p) already locked by %s/%d\n", \
					__FILE__,__LINE__, (x)->module, \
					(x), (x)->owner, (x)->oline); \
		} \
		0; \
	})

/* without debugging, spin_trylock on UP always says
 * TRUE. --> printk if already locked. */
#define _raw_spin_trylock(x) \
	({ \
	 	CHECK_LOCK(x); \
		if ((x)->lock&&(x)->babble) { \
			(x)->babble--; \
			printk("%s:%d: spin_trylock(%s:%p) already locked by %s/%d\n", \
					__FILE__,__LINE__, (x)->module, \
					(x), (x)->owner, (x)->oline); \
		} \
		(x)->lock = 1; \
		(x)->owner = __FILE__; \
		(x)->oline = __LINE__; \
		1; \
	})

#define spin_unlock_wait(x)	\
	do { \
	 	CHECK_LOCK(x); \
		if ((x)->lock&&(x)->babble) { \
			(x)->babble--; \
			printk("%s:%d: spin_unlock_wait(%s:%p) owned by %s/%d\n", \
					__FILE__,__LINE__, (x)->module, (x), \
					(x)->owner, (x)->oline); \
		}\
	} while (0)

#define _raw_spin_unlock(x) \
	do { \
	 	CHECK_LOCK(x); \
		if (!(x)->lock&&(x)->babble) { \
			(x)->babble--; \
			printk("%s:%d: spin_unlock(%s:%p) not locked\n", \
					__FILE__,__LINE__, (x)->module, (x));\
		} \
		(x)->lock = 0; \
	} while (0)
#else
/*
 * gcc versions before ~2.95 have a nasty bug with empty initializers.
 */
#if (__GNUC__ > 2)
  typedef struct { } spinlock_t;
  #define SPIN_LOCK_UNLOCKED (spinlock_t) { }
#else
  typedef struct { int gcc_is_buggy; } spinlock_t;
  #define SPIN_LOCK_UNLOCKED (spinlock_t) { 0 }
#endif

/*
 * If CONFIG_SMP is unset, declare the _raw_* definitions as nops
 */
#define spin_lock_init(lock)	do { (void)(lock); } while(0)
#define _raw_spin_lock(lock)	do { (void)(lock); } while(0)
#define spin_is_locked(lock)	((void)(lock), 0)
#define _raw_spin_trylock(lock)	((void)(lock), 1)
#define spin_unlock_wait(lock)	do { (void)(lock); } while(0)
#define _raw_spin_unlock(lock)	do { (void)(lock); } while(0)
#endif /* CONFIG_DEBUG_SPINLOCK */

/* RW spinlocks: No debug version */

#if (__GNUC__ > 2)
  typedef struct { } rwlock_t;
  #define RW_LOCK_UNLOCKED (rwlock_t) { }
#else
  typedef struct { int gcc_is_buggy; } rwlock_t;
  #define RW_LOCK_UNLOCKED (rwlock_t) { 0 }
#endif

#define rwlock_init(lock)	do { (void)(lock); } while(0)
#define _raw_read_lock(lock)	do { (void)(lock); } while(0)
#define _raw_read_unlock(lock)	do { (void)(lock); } while(0)
#define _raw_write_lock(lock)	do { (void)(lock); } while(0)
#define _raw_write_unlock(lock)	do { (void)(lock); } while(0)
#define _raw_write_trylock(lock) ({ (void)(lock); (1); })

#endif /* !SMP */

/*
 * Define the various spin_lock and rw_lock methods.  Note we define these
 * regardless of whether CONFIG_SMP or CONFIG_PREEMPT are set. The various
 * methods are defined as nops in the case they are not required.
 */
#define spin_trylock(lock)	({preempt_disable(); _raw_spin_trylock(lock) ? \
				1 : ({preempt_enable(); 0;});})

#define write_trylock(lock)	({preempt_disable();_raw_write_trylock(lock) ? \
				1 : ({preempt_enable(); 0;});})

/* Where's read_trylock? */

#if defined(CONFIG_SMP) && defined(CONFIG_PREEMPT)
void __preempt_spin_lock(spinlock_t *lock);
void __preempt_write_lock(rwlock_t *lock);

#define spin_lock(lock) \
do { \
	preempt_disable(); \
	if (unlikely(!_raw_spin_trylock(lock))) \
		__preempt_spin_lock(lock); \
} while (0)

#define write_lock(lock) \
do { \
	preempt_disable(); \
	if (unlikely(!_raw_write_trylock(lock))) \
		__preempt_write_lock(lock); \
} while (0)

#else
#define spin_lock(lock)	\
do { \
	preempt_disable(); \
	_raw_spin_lock(lock); \
} while(0)

#define write_lock(lock) \
do { \
	preempt_disable(); \
	_raw_write_lock(lock); \
} while(0)
#endif

#define read_lock(lock)	\
do { \
	preempt_disable(); \
	_raw_read_lock(lock); \
} while(0)

#define spin_unlock(lock) \
do { \
	_raw_spin_unlock(lock); \
	preempt_enable(); \
} while (0)

#define write_unlock(lock) \
do { \
	_raw_write_unlock(lock); \
	preempt_enable(); \
} while(0)

#define read_unlock(lock) \
do { \
	_raw_read_unlock(lock); \
	preempt_enable(); \
} while(0)

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

/*
 *  bit-based spin_lock()
 *
 * Don't use this unless you really need to: spin_lock() and spin_unlock()
 * are significantly faster.
 */
static inline void bit_spin_lock(int bitnum, unsigned long *addr)
{
	/*
	 * Assuming the lock is uncontended, this never enters
	 * the body of the outer loop. If it is contended, then
	 * within the inner loop a non-atomic test is used to
	 * busywait with less bus contention for a good time to
	 * attempt to acquire the lock bit.
	 */
	preempt_disable();
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	while (test_and_set_bit(bitnum, addr)) {
		while (test_bit(bitnum, addr))
			cpu_relax();
	}
#endif
}

/*
 * Return true if it was acquired
 */
static inline int bit_spin_trylock(int bitnum, unsigned long *addr)
{
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	int ret;

	preempt_disable();
	ret = !test_and_set_bit(bitnum, addr);
	if (!ret)
		preempt_enable();
	return ret;
#else
	preempt_disable();
	return 1;
#endif
}

/*
 *  bit-based spin_unlock()
 */
static inline void bit_spin_unlock(int bitnum, unsigned long *addr)
{
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	BUG_ON(!test_bit(bitnum, addr));
	smp_mb__before_clear_bit();
	clear_bit(bitnum, addr);
#endif
	preempt_enable();
}

/*
 * Return true if the lock is held.
 */
static inline int bit_spin_is_locked(int bitnum, unsigned long *addr)
{
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
	return test_bit(bitnum, addr);
#elif defined CONFIG_PREEMPT
	return preempt_count();
#else
	return 1;
#endif
}

#endif /* __LINUX_SPINLOCK_H */
