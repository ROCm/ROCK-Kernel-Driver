/*
 * bitops.c: atomic operations which got too long to be inlined all over
 *      the place.
 * 
 * Copyright 1999 Philipp Rumpf (prumpf@tux.org)
 * Copyright 2000 Grant Grundler (grundler@cup.hp.com)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <asm/system.h>
#include <asm/atomic.h>

#ifdef CONFIG_SMP
atomic_lock_t __atomic_hash[ATOMIC_HASH_SIZE] __lock_aligned = {
	[0 ... (ATOMIC_HASH_SIZE-1)]  = (atomic_lock_t) { { 1, 1, 1, 1 } }
};
#endif

#ifdef __LP64__
unsigned long __xchg64(unsigned long x, unsigned long *ptr)
{
	unsigned long temp, flags;

	atomic_spin_lock_irqsave(ATOMIC_HASH(ptr), flags);
	temp = *ptr;
	*ptr = x;
	atomic_spin_unlock_irqrestore(ATOMIC_HASH(ptr), flags);
	return temp;
}
#endif

unsigned long __xchg32(int x, int *ptr)
{
	unsigned long flags;
	unsigned long temp;

	atomic_spin_lock_irqsave(ATOMIC_HASH(ptr), flags);
	(long) temp = (long) *ptr;	/* XXX - sign extension wanted? */
	*ptr = x;
	atomic_spin_unlock_irqrestore(ATOMIC_HASH(ptr), flags);
	return temp;
}


unsigned long __xchg8(char x, char *ptr)
{
	unsigned long flags;
	unsigned long temp;

	atomic_spin_lock_irqsave(ATOMIC_HASH(ptr), flags);
	(long) temp = (long) *ptr;	/* XXX - sign extension wanted? */
	*ptr = x;
	atomic_spin_unlock_irqrestore(ATOMIC_HASH(ptr), flags);
	return temp;
}


#ifdef __LP64__
unsigned long __cmpxchg_u64(volatile unsigned long *ptr, unsigned long old, unsigned long new)
{
	unsigned long flags;
	unsigned long prev;

	atomic_spin_lock_irqsave(ATOMIC_HASH(ptr), flags);
	if ((prev = *ptr) == old)
		*ptr = new;
	atomic_spin_unlock_irqrestore(ATOMIC_HASH(ptr), flags);
	return prev;
}
#endif

unsigned long __cmpxchg_u32(volatile unsigned int *ptr, unsigned int old, unsigned int new)
{
	unsigned long flags;
	unsigned int prev;

	atomic_spin_lock_irqsave(ATOMIC_HASH(ptr), flags);
	if ((prev = *ptr) == old)
		*ptr = new;
	atomic_spin_unlock_irqrestore(ATOMIC_HASH(ptr), flags);
	return (unsigned long)prev;
}
