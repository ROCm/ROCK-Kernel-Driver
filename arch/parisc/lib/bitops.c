/* atomic.c: atomic operations which got too long to be inlined all over
 * the place.
 * 
 * Copyright 1999 Philipp Rumpf (prumpf@tux.org */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <asm/system.h>
#include <asm/atomic.h>

#ifdef CONFIG_SMP
spinlock_t __atomic_hash[ATOMIC_HASH_SIZE] = {
	[0 ... (ATOMIC_HASH_SIZE-1)]  = SPIN_LOCK_UNLOCKED
};
#endif

spinlock_t __atomic_lock = SPIN_LOCK_UNLOCKED;

#ifndef __LP64__
unsigned long __xchg(unsigned long x, unsigned long *ptr, int size)
{
	unsigned long temp, flags;

	if (size != sizeof x) {
		printk("__xchg called with bad pointer\n");
	}
	spin_lock_irqsave(&__atomic_lock, flags);
	temp = *ptr;
	*ptr = x;
	spin_unlock_irqrestore(&__atomic_lock, flags);
	return temp;
}
#else
unsigned long __xchg(unsigned long x, unsigned long *ptr, int size)
{
	unsigned long temp, flags;
	unsigned int *ptr32;

	if (size == 8) {
try_long:
		spin_lock_irqsave(&__atomic_lock, flags);
		temp = *ptr;
		*ptr = x;
		spin_unlock_irqrestore(&__atomic_lock, flags);
		return temp;
	}
	if (size == 4) {
		ptr32 = (unsigned int *)ptr;
		spin_lock_irqsave(&__atomic_lock, flags);
		temp = (unsigned long)*ptr32;
		*ptr32 = (unsigned int)x;
		spin_unlock_irqrestore(&__atomic_lock, flags);
		return temp;
	}

	printk("__xchg called with bad pointer\n");
	goto try_long;
}
#endif
