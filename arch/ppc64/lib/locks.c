/*
 * Spin and read/write lock operations.
 *
 * Copyright (C) 2001-2004 Paul Mackerras <paulus@au.ibm.com>, IBM
 * Copyright (C) 2001 Anton Blanchard <anton@au.ibm.com>, IBM
 * Copyright (C) 2002 Dave Engebretsen <engebret@us.ibm.com>, IBM
 *   Rework to support virtual processors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/stringify.h>
#include <asm/hvcall.h>
#include <asm/iSeries/HvCall.h>

#ifndef CONFIG_SPINLINE

/*
 * On a system with shared processors (that is, where a physical
 * processor is multiplexed between several virtual processors),
 * there is no point spinning on a lock if the holder of the lock
 * isn't currently scheduled on a physical processor.  Instead
 * we detect this situation and ask the hypervisor to give the
 * rest of our timeslice to the lock holder.
 *
 * So that we can tell which virtual processor is holding a lock,
 * we put 0x80000000 | smp_processor_id() in the lock when it is
 * held.  Conveniently, we have a word in the paca that holds this
 * value.
 */

/* waiting for a spinlock... */
#if defined(CONFIG_PPC_SPLPAR) || defined(CONFIG_PPC_ISERIES)

/* We only yield to the hypervisor if we are in shared processor mode */
#define SHARED_PROCESSOR (get_paca()->lppaca.xSharedProc)

void __spin_yield(spinlock_t *lock)
{
	unsigned int lock_value, holder_cpu, yield_count;
	struct paca_struct *holder_paca;

	lock_value = lock->lock;
	if (lock_value == 0)
		return;
	holder_cpu = lock_value & 0xffff;
	BUG_ON(holder_cpu >= NR_CPUS);
	holder_paca = &paca[holder_cpu];
	yield_count = holder_paca->lppaca.xYieldCount;
	if ((yield_count & 1) == 0)
		return;		/* virtual cpu is currently running */
	rmb();
	if (lock->lock != lock_value)
		return;		/* something has changed */
#ifdef CONFIG_PPC_ISERIES
	HvCall2(HvCallBaseYieldProcessor, HvCall_YieldToProc,
		((u64)holder_cpu << 32) | yield_count);
#else
	plpar_hcall_norets(H_CONFER, holder_cpu, yield_count);
#endif
}

#else /* SPLPAR || ISERIES */
#define __spin_yield(x)	barrier()
#define SHARED_PROCESSOR	0
#endif

/*
 * This returns the old value in the lock, so we succeeded
 * in getting the lock if the return value is 0.
 */
static __inline__ unsigned long __spin_trylock(spinlock_t *lock)
{
	unsigned long tmp, tmp2;

	__asm__ __volatile__(
"	lwz		%1,%3(13)		# __spin_trylock\n\
1:	lwarx		%0,0,%2\n\
	cmpwi		0,%0,0\n\
	bne-		2f\n\
	stwcx.		%1,0,%2\n\
	bne-		1b\n\
	isync\n\
2:"	: "=&r" (tmp), "=&r" (tmp2)
	: "r" (&lock->lock), "i" (offsetof(struct paca_struct, lock_token))
	: "cr0", "memory");

	return tmp;
}

int _raw_spin_trylock(spinlock_t *lock)
{
	return __spin_trylock(lock) == 0;
}

EXPORT_SYMBOL(_raw_spin_trylock);

void _raw_spin_lock(spinlock_t *lock)
{
	while (1) {
		if (likely(__spin_trylock(lock) == 0))
			break;
		do {
			HMT_low();
			if (SHARED_PROCESSOR)
				__spin_yield(lock);
		} while (likely(lock->lock != 0));
		HMT_medium();
	}
}

EXPORT_SYMBOL(_raw_spin_lock);

void _raw_spin_lock_flags(spinlock_t *lock, unsigned long flags)
{
	unsigned long flags_dis;

	while (1) {
		if (likely(__spin_trylock(lock) == 0))
			break;
		local_save_flags(flags_dis);
		local_irq_restore(flags);
		do {
			HMT_low();
			if (SHARED_PROCESSOR)
				__spin_yield(lock);
		} while (likely(lock->lock != 0));
		HMT_medium();
		local_irq_restore(flags_dis);
	}
}

EXPORT_SYMBOL(_raw_spin_lock_flags);

void spin_unlock_wait(spinlock_t *lock)
{
	while (lock->lock) {
		HMT_low();
		if (SHARED_PROCESSOR)
			__spin_yield(lock);
	}
	HMT_medium();
}

EXPORT_SYMBOL(spin_unlock_wait);

/*
 * Waiting for a read lock or a write lock on a rwlock...
 * This turns out to be the same for read and write locks, since
 * we only know the holder if it is write-locked.
 */
#if defined(CONFIG_PPC_SPLPAR) || defined(CONFIG_PPC_ISERIES)
void __rw_yield(rwlock_t *rw)
{
	int lock_value;
	unsigned int holder_cpu, yield_count;
	struct paca_struct *holder_paca;

	lock_value = rw->lock;
	if (lock_value >= 0)
		return;		/* no write lock at present */
	holder_cpu = lock_value & 0xffff;
	BUG_ON(holder_cpu >= NR_CPUS);
	holder_paca = &paca[holder_cpu];
	yield_count = holder_paca->lppaca.xYieldCount;
	if ((yield_count & 1) == 0)
		return;		/* virtual cpu is currently running */
	rmb();
	if (rw->lock != lock_value)
		return;		/* something has changed */
#ifdef CONFIG_PPC_ISERIES
	HvCall2(HvCallBaseYieldProcessor, HvCall_YieldToProc,
		((u64)holder_cpu << 32) | yield_count);
#else
	plpar_hcall_norets(H_CONFER, holder_cpu, yield_count);
#endif
}

#else /* SPLPAR || ISERIES */
#define __rw_yield(x)	barrier()
#endif

/*
 * This returns the old value in the lock + 1,
 * so we got a read lock if the return value is > 0.
 */
static __inline__ long __read_trylock(rwlock_t *rw)
{
	long tmp;

	__asm__ __volatile__(
"1:	lwarx		%0,0,%1		# read_trylock\n\
	extsw		%0,%0\n\
	addic.		%0,%0,1\n\
	ble-		2f\n\
	stwcx.		%0,0,%1\n\
	bne-		1b\n\
	isync\n\
2:"	: "=&r" (tmp)
	: "r" (&rw->lock)
	: "cr0", "xer", "memory");

	return tmp;
}

int _raw_read_trylock(rwlock_t *rw)
{
	return __read_trylock(rw) > 0;
}

EXPORT_SYMBOL(_raw_read_trylock);

void _raw_read_lock(rwlock_t *rw)
{
	while (1) {
		if (likely(__read_trylock(rw) > 0))
			break;
		do {
			HMT_low();
			if (SHARED_PROCESSOR)
				__rw_yield(rw);
		} while (likely(rw->lock < 0));
		HMT_medium();
	}
}

EXPORT_SYMBOL(_raw_read_lock);

void _raw_read_unlock(rwlock_t *rw)
{
	long tmp;

	__asm__ __volatile__(
	"eieio				# read_unlock\n\
1:	lwarx		%0,0,%1\n\
	addic		%0,%0,-1\n\
	stwcx.		%0,0,%1\n\
	bne-		1b"
	: "=&r"(tmp)
	: "r"(&rw->lock)
	: "cr0", "memory");
}

EXPORT_SYMBOL(_raw_read_unlock);

/*
 * This returns the old value in the lock,
 * so we got the write lock if the return value is 0.
 */
static __inline__ long __write_trylock(rwlock_t *rw)
{
	long tmp, tmp2;

	__asm__ __volatile__(
"	lwz		%1,%3(13)	# write_trylock\n\
1:	lwarx		%0,0,%2\n\
	cmpwi		0,%0,0\n\
	bne-		2f\n\
	stwcx.		%1,0,%2\n\
	bne-		1b\n\
	isync\n\
2:"	: "=&r" (tmp), "=&r" (tmp2)
	: "r" (&rw->lock), "i" (offsetof(struct paca_struct, lock_token))
	: "cr0", "memory");

	return tmp;
}

int _raw_write_trylock(rwlock_t *rw)
{
	return __write_trylock(rw) == 0;
}

EXPORT_SYMBOL(_raw_write_trylock);

void _raw_write_lock(rwlock_t *rw)
{
	while (1) {
		if (likely(__write_trylock(rw) == 0))
			break;
		do {
			HMT_low();
			if (SHARED_PROCESSOR)
				__rw_yield(rw);
		} while (likely(rw->lock != 0));
		HMT_medium();
	}
}

EXPORT_SYMBOL(_raw_write_lock);

#endif /* CONFIG_SPINLINE */
