/* rwsem.h: R/W semaphores based on spinlocks
 *
 * Written by David Howells (dhowells@redhat.com).
 *
 * Derived from asm-i386/semaphore.h and asm-i386/spinlock.h
 */

#ifndef _I386_RWSEM_SPIN_H
#define _I386_RWSEM_SPIN_H

#include <linux/config.h>

#ifndef _LINUX_RWSEM_H
#error please dont include asm/rwsem-spin.h directly, use linux/rwsem.h instead
#endif

#include <linux/spinlock.h>

#ifdef __KERNEL__

/*
 * the semaphore definition
 */
struct rw_semaphore {
	signed long		count;
#define RWSEM_UNLOCKED_VALUE		0x00000000
#define RWSEM_ACTIVE_BIAS		0x00000001
#define RWSEM_ACTIVE_MASK		0x0000ffff
#define RWSEM_WAITING_BIAS		(-0x00010000)
#define RWSEM_ACTIVE_READ_BIAS		RWSEM_ACTIVE_BIAS
#define RWSEM_ACTIVE_WRITE_BIAS		(RWSEM_WAITING_BIAS + RWSEM_ACTIVE_BIAS)
	spinlock_t		lock;
#define RWSEM_SPINLOCK_OFFSET_STR	"4" /* byte offset of spinlock */
	wait_queue_head_t	wait;
#define RWSEM_WAITING_FOR_READ	WQ_FLAG_CONTEXT_0	/* bits to use in wait_queue_t.flags */
#define RWSEM_WAITING_FOR_WRITE	WQ_FLAG_CONTEXT_1
#if RWSEM_DEBUG
	int			debug;
#endif
#if RWSEM_DEBUG_MAGIC
	long			__magic;
	atomic_t		readers;
	atomic_t		writers;
#endif
};

/*
 * initialisation
 */
#if RWSEM_DEBUG
#define __RWSEM_DEBUG_INIT      , 0
#else
#define __RWSEM_DEBUG_INIT	/* */
#endif
#if RWSEM_DEBUG_MAGIC
#define __RWSEM_DEBUG_MINIT(name)	, (int)&(name).__magic, ATOMIC_INIT(0), ATOMIC_INIT(0)
#else
#define __RWSEM_DEBUG_MINIT(name)	/* */
#endif

#define __RWSEM_INITIALIZER(name,count) \
{ RWSEM_UNLOCKED_VALUE, SPIN_LOCK_UNLOCKED, \
	__WAIT_QUEUE_HEAD_INITIALIZER((name).wait) \
	__RWSEM_DEBUG_INIT __RWSEM_DEBUG_MINIT(name) }

#define __DECLARE_RWSEM_GENERIC(name,count) \
	struct rw_semaphore name = __RWSEM_INITIALIZER(name,count)

#define DECLARE_RWSEM(name) __DECLARE_RWSEM_GENERIC(name,RW_LOCK_BIAS)
#define DECLARE_RWSEM_READ_LOCKED(name) __DECLARE_RWSEM_GENERIC(name,RW_LOCK_BIAS-1)
#define DECLARE_RWSEM_WRITE_LOCKED(name) __DECLARE_RWSEM_GENERIC(name,0)

static inline void init_rwsem(struct rw_semaphore *sem)
{
	sem->count = RWSEM_UNLOCKED_VALUE;
	spin_lock_init(&sem->lock);
	init_waitqueue_head(&sem->wait);
#if RWSEM_DEBUG
	sem->debug = 0;
#endif
#if RWSEM_DEBUG_MAGIC
	sem->__magic = (long)&sem->__magic;
	atomic_set(&sem->readers, 0);
	atomic_set(&sem->writers, 0);
#endif
}

/*
 * lock for reading
 */
static inline void __down_read(struct rw_semaphore *sem)
{
	__asm__ __volatile__(
		"# beginning down_read\n\t"
#ifdef CONFIG_SMP
LOCK_PREFIX	"  decb      "RWSEM_SPINLOCK_OFFSET_STR"(%%eax)\n\t" /* try to grab the spinlock */
		"  js        3f\n" /* jump if failed */
		"1:\n\t"
#endif
		"  incl      (%%eax)\n\t" /* adds 0x00000001, returns the old value */
#ifdef CONFIG_SMP
		"  movb      $1,"RWSEM_SPINLOCK_OFFSET_STR"(%%eax)\n\t" /* release the spinlock */
#endif
		"  js        4f\n\t" /* jump if we weren't granted the lock */
		"2:\n"
		".section .text.lock,\"ax\"\n"
#ifdef CONFIG_SMP
		"3:\n\t" /* spin on the spinlock till we get it */
		"  cmpb      $0,"RWSEM_SPINLOCK_OFFSET_STR"(%%eax)\n\t"
		"  rep;nop   \n\t"
		"  jle       3b\n\t"
		"  jmp       1b\n"
#endif
		"4:\n\t"
		"  call      __rwsem_down_read_failed\n\t"
		"  jmp       2b\n"
		".previous"
		"# ending __down_read\n\t"
		: "=m"(sem->count), "=m"(sem->lock)
		: "a"(sem), "m"(sem->count), "m"(sem->lock)
		: "memory");
}

/*
 * lock for writing
 */
static inline void __down_write(struct rw_semaphore *sem)
{
	int tmp;

	tmp = RWSEM_ACTIVE_WRITE_BIAS;
	__asm__ __volatile__(
		"# beginning down_write\n\t"
#ifdef CONFIG_SMP
LOCK_PREFIX	"  decb      "RWSEM_SPINLOCK_OFFSET_STR"(%%eax)\n\t" /* try to grab the spinlock */
		"  js        3f\n" /* jump if failed */
		"1:\n\t"
#endif
		"  xchg      %0,(%%eax)\n\t" /* retrieve the old value */
		"  add       %0,(%%eax)\n\t" /* add 0xffff0001, result in memory */
#ifdef CONFIG_SMP
		"  movb      $1,"RWSEM_SPINLOCK_OFFSET_STR"(%%eax)\n\t" /* release the spinlock */
#endif
		"  testl     %0,%0\n\t" /* was the count 0 before? */
		"  jnz       4f\n\t" /* jump if we weren't granted the lock */
		"2:\n\t"
		".section .text.lock,\"ax\"\n"
#ifdef CONFIG_SMP
		"3:\n\t" /* spin on the spinlock till we get it */
		"  cmpb      $0,"RWSEM_SPINLOCK_OFFSET_STR"(%%eax)\n\t"
		"  rep;nop   \n\t"
		"  jle       3b\n\t"
		"  jmp       1b\n"
#endif
		"4:\n\t"
		"  call     __rwsem_down_write_failed\n\t"
		"  jmp      2b\n"
		".previous\n"
		"# ending down_write"
		: "+r"(tmp), "=m"(sem->count), "=m"(sem->lock)
		: "a"(sem), "m"(sem->count), "m"(sem->lock)
		: "memory");
}

/*
 * unlock after reading
 */
static inline void __up_read(struct rw_semaphore *sem)
{
	int tmp;

	tmp = -RWSEM_ACTIVE_READ_BIAS;
	__asm__ __volatile__(
		"# beginning __up_read\n\t"
#ifdef CONFIG_SMP
LOCK_PREFIX	"  decb      "RWSEM_SPINLOCK_OFFSET_STR"(%%eax)\n\t" /* try to grab the spinlock */
		"  js        3f\n" /* jump if failed */
		"1:\n\t"
#endif
		"  xchg      %0,(%%eax)\n\t" /* retrieve the old value */
		"  addl      %0,(%%eax)\n\t" /* subtract 1, result in memory */
#ifdef CONFIG_SMP
		"  movb      $1,"RWSEM_SPINLOCK_OFFSET_STR"(%%eax)\n\t" /* release the spinlock */
#endif
		"  js        4f\n\t" /* jump if the lock is being waited upon */
		"2:\n\t"
		".section .text.lock,\"ax\"\n"
#ifdef CONFIG_SMP
		"3:\n\t" /* spin on the spinlock till we get it */
		"  cmpb      $0,"RWSEM_SPINLOCK_OFFSET_STR"(%%eax)\n\t"
		"  rep;nop   \n\t"
		"  jle       3b\n\t"
		"  jmp       1b\n"
#endif
		"4:\n\t"
		"  decl      %0\n\t" /* xchg gave us the old count */
		"  testl     %4,%0\n\t" /* do nothing if still outstanding active readers */
		"  jnz       2b\n\t"
		"  call      __rwsem_wake\n\t"
		"  jmp       2b\n"
		".previous\n"
		"# ending __up_read\n"
		: "+r"(tmp), "=m"(sem->count), "=m"(sem->lock)
		: "a"(sem), "i"(RWSEM_ACTIVE_MASK), "m"(sem->count), "m"(sem->lock)
		: "memory");
}

/*
 * unlock after writing
 */
static inline void __up_write(struct rw_semaphore *sem)
{
	__asm__ __volatile__(
		"# beginning __up_write\n\t"
#ifdef CONFIG_SMP
LOCK_PREFIX	"  decb      "RWSEM_SPINLOCK_OFFSET_STR"(%%eax)\n\t" /* try to grab the spinlock */
		"  js        3f\n" /* jump if failed */
		"1:\n\t"
#endif
		"  addl      %3,(%%eax)\n\t" /* adds 0x00010001 */
#ifdef CONFIG_SMP
		"  movb      $1,"RWSEM_SPINLOCK_OFFSET_STR"(%%eax)\n\t" /* release the spinlock */
#endif
		"  js        4f\n\t" /* jump if the lock is being waited upon */
		"2:\n\t"
		".section .text.lock,\"ax\"\n"
#ifdef CONFIG_SMP
		"3:\n\t" /* spin on the spinlock till we get it */
		"  cmpb      $0,"RWSEM_SPINLOCK_OFFSET_STR"(%%eax)\n\t"
		"  rep;nop   \n\t"
		"  jle       3b\n\t"
		"  jmp       1b\n"
#endif
		"4:\n\t"
		"  call     __rwsem_wake\n\t"
		"  jmp      2b\n"
		".previous\n"
		"# ending __up_write\n"
		: "=m"(sem->count), "=m"(sem->lock)
		: "a"(sem), "i"(-RWSEM_ACTIVE_WRITE_BIAS), "m"(sem->count), "m"(sem->lock)
		: "memory");
}

/*
 * implement exchange and add functionality
 */
static inline int rwsem_atomic_update(int delta, struct rw_semaphore *sem)
{
	int tmp = delta;

	__asm__ __volatile__(
		"# beginning rwsem_atomic_update\n\t"
#ifdef CONFIG_SMP
LOCK_PREFIX	"  decb      "RWSEM_SPINLOCK_OFFSET_STR"(%1)\n\t" /* try to grab the spinlock */
		"  js        3f\n" /* jump if failed */
		"1:\n\t"
#endif
		"  xchgl     %0,(%1)\n\t" /* retrieve the old value */
		"  addl      %0,(%1)\n\t" /* add 0xffff0001, result in memory */
#ifdef CONFIG_SMP
		"  movb      $1,"RWSEM_SPINLOCK_OFFSET_STR"(%1)\n\t" /* release the spinlock */
#endif
		".section .text.lock,\"ax\"\n"
#ifdef CONFIG_SMP
		"3:\n\t" /* spin on the spinlock till we get it */
		"  cmpb      $0,"RWSEM_SPINLOCK_OFFSET_STR"(%1)\n\t"
		"  rep;nop   \n\t"
		"  jle       3b\n\t"
		"  jmp       1b\n"
#endif
		".previous\n"
		"# ending rwsem_atomic_update\n\t"
		: "+r"(tmp)
		: "r"(sem)
		: "memory");

	return tmp+delta;
}

/*
 * implement compare and exchange functionality on the rw-semaphore count LSW
 */
static inline __u16 rwsem_cmpxchgw(struct rw_semaphore *sem, __u16 old, __u16 new)
{
	__u16 prev;

	__asm__ __volatile__(
		"# beginning rwsem_cmpxchgw\n\t"
#ifdef CONFIG_SMP
LOCK_PREFIX	"  decb      "RWSEM_SPINLOCK_OFFSET_STR"(%3)\n\t" /* try to grab the spinlock */
		"  js        3f\n" /* jump if failed */
		"1:\n\t"
#endif
		"  cmpw      %w1,(%3)\n\t"
		"  jne       4f\n\t" /* jump if old doesn't match sem->count LSW */
		"  movw      %w2,(%3)\n\t" /* replace sem->count LSW with the new value */
		"2:\n\t"
#ifdef CONFIG_SMP
		"  movb      $1,"RWSEM_SPINLOCK_OFFSET_STR"(%3)\n\t" /* release the spinlock */
#endif
		".section .text.lock,\"ax\"\n"
#ifdef CONFIG_SMP
		"3:\n\t" /* spin on the spinlock till we get it */
		"  cmpb      $0,"RWSEM_SPINLOCK_OFFSET_STR"(%3)\n\t"
		"  rep;nop   \n\t"
		"  jle       3b\n\t"
		"  jmp       1b\n"
#endif
		"4:\n\t"
		"  movw      (%3),%w0\n" /* we'll want to return the current value */
		"  jmp       2b\n"
		".previous\n"
		"# ending rwsem_cmpxchgw\n\t"
		: "=r"(prev)
		: "r0"(old), "r"(new), "r"(sem)
		: "memory");

	return prev;
}

#endif /* __KERNEL__ */
#endif /* _I386_RWSEM_SPIN_H */
