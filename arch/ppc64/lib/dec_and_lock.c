/*
 * ppc64 version of atomic_dec_and_lock() using cmpxchg
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/spinlock.h>
#include <asm/system.h>
#include <asm/atomic.h>

int atomic_dec_and_lock(atomic_t *atomic, spinlock_t *lock)
{
	int counter;
	int newcount;

repeat:
	counter = atomic_read(atomic);
	newcount = counter-1;

	if (!newcount)
		goto slow_path;

	newcount = cmpxchg(&atomic->counter, counter, newcount);

	if (newcount != counter)
		goto repeat;
	return 0;

slow_path:
	spin_lock(lock);
	if (atomic_dec_and_test(atomic))
		return 1;
	spin_unlock(lock);
	return 0;
}
