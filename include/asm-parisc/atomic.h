#ifndef _ASM_PARISC_ATOMIC_H_
#define _ASM_PARISC_ATOMIC_H_

#include <linux/config.h>
#include <asm/system.h>

/* Copyright (C) 2000 Philipp Rumpf <prumpf@tux.org>.  */

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 *
 * And probably incredibly slow on parisc.  OTOH, we don't
 * have to write any serious assembly.   prumpf
 */

#ifdef CONFIG_SMP
/* we have an array of spinlocks for our atomic_ts, and a hash function
 * to get the right index */
#  define ATOMIC_HASH_SIZE 1
#  define ATOMIC_HASH(a) (&__atomic_hash[0])

extern spinlock_t __atomic_hash[ATOMIC_HASH_SIZE];
/* copied from <asm/spinlock.h> and modified */
#  define SPIN_LOCK(x) \
	do { while(__ldcw(&(x)->lock) == 0); } while(0)
	
#  define SPIN_UNLOCK(x) \
	do { (x)->lock = 1; } while(0)
#else
#  define ATOMIC_HASH_SIZE 1
#  define ATOMIC_HASH(a)	(0)

/* copied from <linux/spinlock.h> and modified */
#  define SPIN_LOCK(x) (void)(x)
	
#  define SPIN_UNLOCK(x) do { } while(0)
#endif

/* copied from <linux/spinlock.h> and modified */
#define SPIN_LOCK_IRQSAVE(lock, flags)		do { local_irq_save(flags);       SPIN_LOCK(lock); } while (0)
#define SPIN_UNLOCK_IRQRESTORE(lock, flags)	do { SPIN_UNLOCK(lock);  local_irq_restore(flags); } while (0)

/* Note that we need not lock read accesses - aligned word writes/reads
 * are atomic, so a reader never sees unconsistent values.
 *
 * Cache-line alignment would conflict with, for example, linux/module.h */

typedef struct {
	volatile int counter;
} atomic_t;

/* It's possible to reduce all atomic operations to either
 * __atomic_add_return, __atomic_set and __atomic_ret (the latter
 * is there only for consistency). */

static __inline__ int __atomic_add_return(int i, atomic_t *v)
{
	int ret;
	unsigned long flags;
	SPIN_LOCK_IRQSAVE(ATOMIC_HASH(v), flags);

	ret = (v->counter += i);

	SPIN_UNLOCK_IRQRESTORE(ATOMIC_HASH(v), flags);
	return ret;
}

static __inline__ void __atomic_set(atomic_t *v, int i) 
{
	unsigned long flags;
	SPIN_LOCK_IRQSAVE(ATOMIC_HASH(v), flags);

	v->counter = i;

	SPIN_UNLOCK_IRQRESTORE(ATOMIC_HASH(v), flags);
}
	
static __inline__ int __atomic_read(atomic_t *v)
{
	return v->counter;
}

/* exported interface */

#define atomic_add(i,v)		((void)(__atomic_add_return( (i),(v))))
#define atomic_sub(i,v)		((void)(__atomic_add_return(-(i),(v))))
#define atomic_inc(v)		((void)(__atomic_add_return(   1,(v))))
#define atomic_dec(v)		((void)(__atomic_add_return(  -1,(v))))

#define atomic_add_return(i,v)	(__atomic_add_return( (i),(v)))
#define atomic_sub_return(i,v)	(__atomic_add_return(-(i),(v)))
#define atomic_inc_return(v)	(__atomic_add_return(   1,(v)))
#define atomic_dec_return(v)	(__atomic_add_return(  -1,(v)))

#define atomic_dec_and_test(v)	(atomic_dec_return(v) == 0)

#define atomic_set(v,i)		(__atomic_set((v),i))
#define atomic_read(v)		(__atomic_read(v))

#define ATOMIC_INIT(i)	{ (i) }

#endif
