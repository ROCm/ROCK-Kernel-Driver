/* $Id: rwsem.h,v 1.2 2001/04/19 01:52:04 davem Exp $
 * rwsem.h: R/W semaphores implemented using CAS
 *
 * Written by David S. Miller (davem@redhat.com), 2001.
 * Derived from asm-i386/rwsem-xadd.h
 */
#ifndef _SPARC64_RWSEM_H
#define _SPARC64_RWSEM_H

#ifndef _LINUX_RWSEM_H
#error please dont include asm/rwsem.h directly, use linux/rwsem.h instead
#endif

#ifdef __KERNEL__

struct rwsem_waiter;

struct rw_semaphore {
	signed int count;
#define RWSEM_UNLOCKED_VALUE		0x00000000
#define RWSEM_ACTIVE_BIAS		0x00000001
#define RWSEM_ACTIVE_MASK		0x0000ffff
#define RWSEM_WAITING_BIAS		0xffff0000
#define RWSEM_ACTIVE_READ_BIAS		RWSEM_ACTIVE_BIAS
#define RWSEM_ACTIVE_WRITE_BIAS		(RWSEM_WAITING_BIAS + RWSEM_ACTIVE_BIAS)
	spinlock_t		wait_lock;
	struct rwsem_waiter	*wait_front;
	struct rwsem_waiter	**wait_back;
};

#define __RWSEM_INITIALIZER(name) \
{ RWSEM_UNLOCKED_VALUE, SPIN_LOCK_UNLOCKED, NULL, &(name).wait_front }

#define DECLARE_RWSEM(name) \
	struct rw_semaphore name = __RWSEM_INITIALIZER(name)

static inline void init_rwsem(struct rw_semaphore *sem)
{
	sem->count = RWSEM_UNLOCKED_VALUE;
	init_waitqueue_head(&sem->wait);
}

static inline void __down_read(struct rw_semaphore *sem)
{
	__asm__ __volatile__(
		"! beginning __down_read\n"
		"1:\tlduw	[%0], %%g5\n\t"
		"add		%%g5, 1, %%g7\n\t"
		"cas		[%0], %%g5, %%g7\n\t"
		"cmp		%%g5, %%g7\n\t"
		"bne,pn		%%icc, 1b\n\t"
		" add		%%g7, 1, %%g7\n\t"
		"cmp		%%g7, 0\n\t"
		"bl,pn		%%icc, 3f\n\t"
		" membar	#StoreStore\n"
		"2:\n\t"
		".subsection	2\n"
		"3:\tmov	%0, %%g5\n\t"
		"save		%%sp, -160, %%sp\n\t"
		"mov		%%g1, %%l1\n\t"
		"mov		%%g2, %%l2\n\t"
		"mov		%%g3, %%l3\n\t"
		"call		%1\n\t"
		" mov		%%g5, %%o0\n\t"
		"mov		%%l1, %%g1\n\t"
		"mov		%%l2, %%g2\n\t"
		"ba,pt		%%xcc, 2b\n\t"
		" restore	%%l3, %%g0, %%g3\n\t"
		".previous\n\t"
		"! ending __down_read"
		: : "r" (sem), "i" (rwsem_down_read_failed)
		: "g5", "g7", "memory", "cc");
}

static inline void __down_write(struct rw_semaphore *sem)
{
	__asm__ __volatile__(
		"! beginning __down_write\n\t"
		"sethi		%%hi(%2), %%g1\n\t"
		"or		%%g1, %%lo(%2), %%g1\n"
		"1:\tlduw	[%0], %%g5\n\t"
		"add		%%g5, %%g1, %%g7\n\t"
		"cas		[%0], %%g5, %%g7\n\t"
		"cmp		%%g5, %%g7\n\t"
		"bne,pn		%%icc, 1b\n\t"
		" cmp		%%g7, 0\n\t"
		"bne,pn		%%icc, 3f\n\t"
		" membar	#StoreStore\n"
		"2:\n\t"
		".subsection	2\n"
		"3:\tmov	%0, %%g5\n\t"
		"save		%%sp, -160, %%sp\n\t"
		"mov		%%g2, %%l2\n\t"
		"mov		%%g3, %%l3\n\t"
		"call		%1\n\t"
		" mov		%%g5, %%o0\n\t"
		"mov		%%l2, %%g2\n\t"
		"ba,pt		%%xcc, 2b\n\t"
		" restore	%%l3, %%g0, %%g3\n\t"
		".previous\n\t"
		"! ending __down_write"
		: : "r" (sem), "i" (rwsem_down_write_failed),
		    "i" (RWSEM_ACTIVE_WRITE_BIAS)
		: "g1", "g5", "g7", "memory", "cc");
}

static inline void __up_read(struct rw_semaphore *sem)
{
	__asm__ __volatile__(
		"! beginning __up_read\n\t"
		"1:\tlduw	[%0], %%g5\n\t"
		"sub		%%g5, 1, %%g7\n\t"
		"cas		[%0], %%g5, %%g7\n\t"
		"cmp		%%g5, %%g7\n\t"
		"bne,pn		%%icc, 1b\n\t"
		" cmp		%%g7, 0\n\t"
		"bl,pn		%%icc, 3f\n\t"
		" membar	#StoreStore\n"
		"2:\n\t"
		".subsection	2\n"
		"3:\tsethi	%%hi(%2), %%g1\n\t"
		"sub		%%g7, 1, %%g7\n\t"
		"or		%%g1, %%lo(%2), %%g1\n\t"
		"andcc		%%g7, %%g1, %%g0\n\t"
		"bne,pn		%%icc, 2b\n\t"
		" mov		%0, %%g5\n\t"
		"save		%%sp, -160, %%sp\n\t"
		"mov		%%g2, %%l2\n\t"
		"mov		%%g3, %%l3\n\t"
		"call		%1\n\t"
		" mov		%%g5, %%o0\n\t"
		"mov		%%l2, %%g2\n\t"
		"ba,pt		%%xcc, 2b\n\t"
		" restore	%%l3, %%g0, %%g3\n\t"
		".previous\n\t"
		"! ending __up_read"
		: : "r" (sem), "i" (rwsem_wake),
		    "i" (RWSEM_ACTIVE_MASK)
		: "g1", "g5", "g7", "memory", "cc");
}

static inline void __up_write(struct rw_semaphore *sem)
{
	__asm__ __volatile__(
		"! beginning __up_write\n\t"
		"sethi		%%hi(%2), %%g1\n\t"
		"or		%%g1, %%lo(%2), %%g1\n"
		"1:\tlduw	[%0], %%g5\n\t"
		"sub		%%g5, %%g1, %%g7\n\t"
		"cas		[%0], %%g5, %%g7\n\t"
		"cmp		%%g5, %%g7\n\t"
		"bne,pn		%%icc, 1b\n\t"
		" sub		%%g7, %%g1, %%g7\n\t"
		"cmp		%%g7, 0\n\t"
		"bl,pn		%%icc, 3f\n\t"
		" membar	#StoreStore\n"
		"2:\n\t"
		".subsection 2\n"
		"3:\tmov	%0, %%g5\n\t"
		"save		%%sp, -160, %%sp\n\t"
		"mov		%%g2, %%l2\n\t"
		"mov		%%g3, %%l3\n\t"
		"call		%1\n\t"
		" mov		%%g5, %%o0\n\t"
		"mov		%%l2, %%g2\n\t"
		"ba,pt		%%xcc, 2b\n\t"
		" restore	%%l3, %%g0, %%g3\n\t"
		".previous\n\t"
		"! ending __up_write"
		: : "r" (sem), "i" (rwsem_wake),
		    "i" (RWSEM_ACTIVE_WRITE_BIAS)
		: "g1", "g5", "g7", "memory", "cc");
}

static inline int rwsem_atomic_update(int delta, struct rw_semaphore *sem)
{
	int tmp = delta;

	__asm__ __volatile__(
		"1:\tlduw	[%2], %%g5\n\t"
		"add		%%g5, %1, %%g7\n\t"
		"cas		[%2], %%g5, %%g7\n\t"
		"cmp		%%g5, %%g7\n\t"
		"bne,pn		%%icc, 1b\n\t"
		" nop\n\t"
		"mov		%%g7, %0\n\t"
		: "=&r" (tmp)
		: "0" (tmp), "r" (sem)
		: "g5", "g7", "memory");

	return tmp + delta;
}

static inline __u16 rwsem_cmpxchgw(struct rw_semaphore *sem, __u16 __old, __u16 __new)
{
	u32 old = (sem->count & 0xffff0000) | (u32) __old;
	u32 new = (old & 0xffff0000) | (u32) __new;
	u32 prev;

again:
	__asm__ __volatile__("cas	[%2], %3, %0\n\t"
			     "membar	#StoreStore | #StoreLoad"
			     : "=&r" (prev)
			     : "0" (new), "r" (sem), "r" (old)
			     : "memory");

	/* To give the same semantics as x86 cmpxchgw, keep trying
	 * if only the upper 16-bits changed.
	 */
	if (prev != old &&
	    ((prev & 0xffff) == (old & 0xffff)))
		goto again;

	return prev & 0xffff;
}

#endif /* __KERNEL__ */

#endif /* _SPARC64_RWSEM_H */
