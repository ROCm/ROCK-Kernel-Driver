/*
 * Copyright (2004) Linus Torvalds
 *
 * Author: Zwane Mwaikambo <zwane@fsmlabs.com>
 */

#include <linux/config.h>
#include <linux/linkage.h>
#include <linux/preempt.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/module.h>

int __lockfunc _spin_trylock(spinlock_t *lock)
{
	preempt_disable();
	if (_raw_spin_trylock(lock))
		return 1;
	
	preempt_enable();
	return 0;
}
EXPORT_SYMBOL(_spin_trylock);

int __lockfunc _write_trylock(rwlock_t *lock)
{
	preempt_disable();
	if (_raw_write_trylock(lock))
		return 1;

	preempt_enable();
	return 0;
}
EXPORT_SYMBOL(_write_trylock);

#ifdef CONFIG_PREEMPT
/*
 * This could be a long-held lock.  If another CPU holds it for a long time,
 * and that CPU is not asked to reschedule then *this* CPU will spin on the
 * lock for a long time, even if *this* CPU is asked to reschedule.
 *
 * So what we do here, in the slow (contended) path is to spin on the lock by
 * hand while permitting preemption.
 *
 * Called inside preempt_disable().
 */
static inline void __preempt_spin_lock(spinlock_t *lock)
{
	if (preempt_count() > 1) {
		_raw_spin_lock(lock);
		return;
	}

	do {
		preempt_enable();
		while (spin_is_locked(lock))
			cpu_relax();
		preempt_disable();
	} while (!_raw_spin_trylock(lock));
}

void __lockfunc _spin_lock(spinlock_t *lock)
{
	preempt_disable();
	if (unlikely(!_raw_spin_trylock(lock)))
		__preempt_spin_lock(lock);
}

static inline void __preempt_write_lock(rwlock_t *lock)
{
	if (preempt_count() > 1) {
		_raw_write_lock(lock);
		return;
	}

	do {
		preempt_enable();
		while (rwlock_is_locked(lock))
			cpu_relax();
		preempt_disable();
	} while (!_raw_write_trylock(lock));
}

void __lockfunc _write_lock(rwlock_t *lock)
{
	preempt_disable();
	if (unlikely(!_raw_write_trylock(lock)))
		__preempt_write_lock(lock);
}
#else
void __lockfunc _spin_lock(spinlock_t *lock)
{
	preempt_disable();
	_raw_spin_lock(lock);
}

void __lockfunc _write_lock(rwlock_t *lock)
{
	preempt_disable();
	_raw_write_lock(lock);
}
#endif
EXPORT_SYMBOL(_spin_lock);
EXPORT_SYMBOL(_write_lock);

void __lockfunc _read_lock(rwlock_t *lock)
{
	preempt_disable();
	_raw_read_lock(lock);
}
EXPORT_SYMBOL(_read_lock);

void __lockfunc _spin_unlock(spinlock_t *lock)
{
	_raw_spin_unlock(lock);
	preempt_enable();
}
EXPORT_SYMBOL(_spin_unlock);

void __lockfunc _write_unlock(rwlock_t *lock)
{
	_raw_write_unlock(lock);
	preempt_enable();
}
EXPORT_SYMBOL(_write_unlock);

void __lockfunc _read_unlock(rwlock_t *lock)
{
	_raw_read_unlock(lock);
	preempt_enable();
}
EXPORT_SYMBOL(_read_unlock);

unsigned long __lockfunc _spin_lock_irqsave(spinlock_t *lock)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	_raw_spin_lock_flags(lock, flags);
	return flags;
}
EXPORT_SYMBOL(_spin_lock_irqsave);

void __lockfunc _spin_lock_irq(spinlock_t *lock)
{
	local_irq_disable();
	preempt_disable();
	_raw_spin_lock(lock);
}
EXPORT_SYMBOL(_spin_lock_irq);

void __lockfunc _spin_lock_bh(spinlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	_raw_spin_lock(lock);
}
EXPORT_SYMBOL(_spin_lock_bh);

unsigned long __lockfunc _read_lock_irqsave(rwlock_t *lock)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	_raw_read_lock(lock);
	return flags;
}
EXPORT_SYMBOL(_read_lock_irqsave);

void __lockfunc _read_lock_irq(rwlock_t *lock)
{
	local_irq_disable();
	preempt_disable();
	_raw_read_lock(lock);
}
EXPORT_SYMBOL(_read_lock_irq);

void __lockfunc _read_lock_bh(rwlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	_raw_read_lock(lock);
}
EXPORT_SYMBOL(_read_lock_bh);

unsigned long __lockfunc _write_lock_irqsave(rwlock_t *lock)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	_raw_write_lock(lock);
	return flags;
}
EXPORT_SYMBOL(_write_lock_irqsave);

void __lockfunc _write_lock_irq(rwlock_t *lock)
{
	local_irq_disable();
	preempt_disable();
	_raw_write_lock(lock);
}
EXPORT_SYMBOL(_write_lock_irq);

void __lockfunc _write_lock_bh(rwlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	_raw_write_lock(lock);
}
EXPORT_SYMBOL(_write_lock_bh);

void __lockfunc _spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{
	_raw_spin_unlock(lock);
	local_irq_restore(flags);
	preempt_enable();
}
EXPORT_SYMBOL(_spin_unlock_irqrestore);

void __lockfunc _spin_unlock_irq(spinlock_t *lock)
{
	_raw_spin_unlock(lock);
	local_irq_enable();
	preempt_enable();
}
EXPORT_SYMBOL(_spin_unlock_irq);

void __lockfunc _spin_unlock_bh(spinlock_t *lock)
{
	_raw_spin_unlock(lock);
	preempt_enable();
	local_bh_enable();
}
EXPORT_SYMBOL(_spin_unlock_bh);

void __lockfunc _read_unlock_irqrestore(rwlock_t *lock, unsigned long flags)
{
	_raw_read_unlock(lock);
	local_irq_restore(flags);
	preempt_enable();
}
EXPORT_SYMBOL(_read_unlock_irqrestore);

void __lockfunc _read_unlock_irq(rwlock_t *lock)
{
	_raw_read_unlock(lock);
	local_irq_enable();
	preempt_enable();
}
EXPORT_SYMBOL(_read_unlock_irq);

void __lockfunc _read_unlock_bh(rwlock_t *lock)
{
	_raw_read_unlock(lock);
	preempt_enable();
	local_bh_enable();
}
EXPORT_SYMBOL(_read_unlock_bh);

void __lockfunc _write_unlock_irqrestore(rwlock_t *lock, unsigned long flags)
{
	_raw_write_unlock(lock);
	local_irq_restore(flags);
	preempt_enable();
}
EXPORT_SYMBOL(_write_unlock_irqrestore);

void __lockfunc _write_unlock_irq(rwlock_t *lock)
{
	_raw_write_unlock(lock);
	local_irq_enable();
	preempt_enable();
}
EXPORT_SYMBOL(_write_unlock_irq);

void __lockfunc _write_unlock_bh(rwlock_t *lock)
{
	_raw_write_unlock(lock);
	preempt_enable();
	local_bh_enable();
}
EXPORT_SYMBOL(_write_unlock_bh);

int __lockfunc _spin_trylock_bh(spinlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	if (_raw_spin_trylock(lock))
		return 1;

	preempt_enable();
	local_bh_enable();
	return 0;
}
EXPORT_SYMBOL(_spin_trylock_bh);

int in_lock_functions(unsigned long addr)
{
	/* Linker adds these: start and end of __lockfunc functions */
	extern char __lock_text_start[], __lock_text_end[];

	return addr >= (unsigned long)__lock_text_start
	&& addr < (unsigned long)__lock_text_end;
}
EXPORT_SYMBOL(in_lock_functions);
