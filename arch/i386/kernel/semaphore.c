/*
 * i386 semaphore implementation.
 *
 * (C) Copyright 1999 Linus Torvalds
 *
 * Portions Copyright 1999 Red Hat, Inc.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * rw semaphores implemented November 1999 by Benjamin LaHaise <bcrl@redhat.com>
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <asm/semaphore.h>

/*
 * Semaphores are implemented using a two-way counter:
 * The "count" variable is decremented for each process
 * that tries to acquire the semaphore, while the "sleeping"
 * variable is a count of such acquires.
 *
 * Notably, the inline "up()" and "down()" functions can
 * efficiently test if they need to do any extra work (up
 * needs to do something only if count was negative before
 * the increment operation.
 *
 * "sleeping" and the contention routine ordering is
 * protected by the semaphore spinlock.
 *
 * Note that these functions are only called when there is
 * contention on the lock, and as such all this is the
 * "non-critical" part of the whole semaphore business. The
 * critical part is the inline stuff in <asm/semaphore.h>
 * where we want to avoid any extra jumps and calls.
 */

/*
 * Logic:
 *  - only on a boundary condition do we need to care. When we go
 *    from a negative count to a non-negative, we wake people up.
 *  - when we go from a non-negative count to a negative do we
 *    (a) synchronize with the "sleeper" count and (b) make sure
 *    that we're on the wakeup list before we synchronize so that
 *    we cannot lose wakeup events.
 */

void __up(struct semaphore *sem)
{
	wake_up(&sem->wait);
}

static spinlock_t semaphore_lock = SPIN_LOCK_UNLOCKED;

void __down(struct semaphore * sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);
	tsk->state = TASK_UNINTERRUPTIBLE;
	add_wait_queue_exclusive(&sem->wait, &wait);

	spin_lock_irq(&semaphore_lock);
	sem->sleepers++;
	for (;;) {
		int sleepers = sem->sleepers;

		/*
		 * Add "everybody else" into it. They aren't
		 * playing, because we own the spinlock.
		 */
		if (!atomic_add_negative(sleepers - 1, &sem->count)) {
			sem->sleepers = 0;
			break;
		}
		sem->sleepers = 1;	/* us - see -1 above */
		spin_unlock_irq(&semaphore_lock);

		schedule();
		tsk->state = TASK_UNINTERRUPTIBLE;
		spin_lock_irq(&semaphore_lock);
	}
	spin_unlock_irq(&semaphore_lock);
	remove_wait_queue(&sem->wait, &wait);
	tsk->state = TASK_RUNNING;
	wake_up(&sem->wait);
}

int __down_interruptible(struct semaphore * sem)
{
	int retval = 0;
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);
	tsk->state = TASK_INTERRUPTIBLE;
	add_wait_queue_exclusive(&sem->wait, &wait);

	spin_lock_irq(&semaphore_lock);
	sem->sleepers ++;
	for (;;) {
		int sleepers = sem->sleepers;

		/*
		 * With signals pending, this turns into
		 * the trylock failure case - we won't be
		 * sleeping, and we* can't get the lock as
		 * it has contention. Just correct the count
		 * and exit.
		 */
		if (signal_pending(current)) {
			retval = -EINTR;
			sem->sleepers = 0;
			atomic_add(sleepers, &sem->count);
			break;
		}

		/*
		 * Add "everybody else" into it. They aren't
		 * playing, because we own the spinlock. The
		 * "-1" is because we're still hoping to get
		 * the lock.
		 */
		if (!atomic_add_negative(sleepers - 1, &sem->count)) {
			sem->sleepers = 0;
			break;
		}
		sem->sleepers = 1;	/* us - see -1 above */
		spin_unlock_irq(&semaphore_lock);

		schedule();
		tsk->state = TASK_INTERRUPTIBLE;
		spin_lock_irq(&semaphore_lock);
	}
	spin_unlock_irq(&semaphore_lock);
	tsk->state = TASK_RUNNING;
	remove_wait_queue(&sem->wait, &wait);
	wake_up(&sem->wait);
	return retval;
}

/*
 * Trylock failed - make sure we correct for
 * having decremented the count.
 *
 * We could have done the trylock with a
 * single "cmpxchg" without failure cases,
 * but then it wouldn't work on a 386.
 */
int __down_trylock(struct semaphore * sem)
{
	int sleepers;
	unsigned long flags;

	spin_lock_irqsave(&semaphore_lock, flags);
	sleepers = sem->sleepers + 1;
	sem->sleepers = 0;

	/*
	 * Add "everybody else" and us into it. They aren't
	 * playing, because we own the spinlock.
	 */
	if (!atomic_add_negative(sleepers, &sem->count))
		wake_up(&sem->wait);

	spin_unlock_irqrestore(&semaphore_lock, flags);
	return 1;
}


/*
 * The semaphore operations have a special calling sequence that
 * allow us to do a simpler in-line version of them. These routines
 * need to convert that sequence back into the C sequence when
 * there is contention on the semaphore.
 *
 * %ecx contains the semaphore pointer on entry. Save the C-clobbered
 * registers (%eax, %edx and %ecx) except %eax when used as a return
 * value..
 */
asm(
".text\n"
".align 4\n"
".globl __down_failed\n"
"__down_failed:\n\t"
	"pushl %eax\n\t"
	"pushl %edx\n\t"
	"pushl %ecx\n\t"
	"call __down\n\t"
	"popl %ecx\n\t"
	"popl %edx\n\t"
	"popl %eax\n\t"
	"ret"
);

asm(
".text\n"
".align 4\n"
".globl __down_failed_interruptible\n"
"__down_failed_interruptible:\n\t"
	"pushl %edx\n\t"
	"pushl %ecx\n\t"
	"call __down_interruptible\n\t"
	"popl %ecx\n\t"
	"popl %edx\n\t"
	"ret"
);

asm(
".text\n"
".align 4\n"
".globl __down_failed_trylock\n"
"__down_failed_trylock:\n\t"
	"pushl %edx\n\t"
	"pushl %ecx\n\t"
	"call __down_trylock\n\t"
	"popl %ecx\n\t"
	"popl %edx\n\t"
	"ret"
);

asm(
".text\n"
".align 4\n"
".globl __up_wakeup\n"
"__up_wakeup:\n\t"
	"pushl %eax\n\t"
	"pushl %edx\n\t"
	"pushl %ecx\n\t"
	"call __up\n\t"
	"popl %ecx\n\t"
	"popl %edx\n\t"
	"popl %eax\n\t"
	"ret"
);

asm(
"
.text
.align 4
.globl __down_read_failed
__down_read_failed:
	pushl	%edx
	pushl	%ecx
	call	down_read_failed
	popl	%ecx
	popl	%edx
	ret
"
);

asm(
"
.text
.align 4
.globl __down_write_failed
__down_write_failed:
	pushl	%edx
	pushl	%ecx
	call	down_write_failed
	popl	%ecx
	popl	%edx
	ret
"
);

asm(
"
.text
.align 4
.globl __rwsem_wake
__rwsem_wake:
	pushl	%edx
	pushl	%ecx
	call	rwsem_wake
	popl	%ecx
	popl	%edx
	ret
"
);

struct rw_semaphore *FASTCALL(rwsem_wake(struct rw_semaphore *sem));
struct rw_semaphore *FASTCALL(down_read_failed(struct rw_semaphore *sem));
struct rw_semaphore *FASTCALL(down_write_failed(struct rw_semaphore *sem));

/*
 * implement exchange and add functionality
 */
static inline int rwsem_atomic_update(int delta, struct rw_semaphore *sem)
{
	int tmp = delta;

#ifndef CONFIG_USING_SPINLOCK_BASED_RWSEM
	__asm__ __volatile__(
		LOCK_PREFIX "xadd %0,(%1)"
		: "+r"(tmp)
		: "r"(sem)
		: "memory");

#else

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

#endif
	return tmp+delta;
}

/*
 * implement compare and exchange functionality on the rw-semaphore count LSW
 */
static inline __u16 rwsem_cmpxchgw(struct rw_semaphore *sem, __u16 old, __u16 new)
{
#ifndef CONFIG_USING_SPINLOCK_BASED_RWSEM
	return cmpxchg((__u16*)&sem->count.counter,0,RWSEM_ACTIVE_BIAS);

#else
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
#endif
}

/*
 * wait for the read lock to be granted
 * - need to repeal the increment made inline by the caller
 * - need to throw a write-lock style spanner into the works (sub 0x00010000 from count)
 */
struct rw_semaphore *down_read_failed(struct rw_semaphore *sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait,tsk);
	int count;

	rwsemdebug("[%d] Entering down_read_failed(%08x)\n",current->pid,atomic_read(&sem->count));

	/* this waitqueue context flag will be cleared when we are granted the lock */
	__set_bit(RWSEM_WAITING_FOR_READ,&wait.flags);
	set_task_state(tsk, TASK_UNINTERRUPTIBLE);

	add_wait_queue_exclusive(&sem->wait, &wait); /* FIFO */

	/* note that we're now waiting on the lock, but no longer actively read-locking */
	count = rwsem_atomic_update(RWSEM_WAITING_BIAS-RWSEM_ACTIVE_BIAS,sem);
	rwsemdebug("X(%08x)\n",count);

	/* if there are no longer active locks, wake the front queued process(es) up
	 * - it might even be this process, since the waker takes a more active part
	 */
	if (!(count & RWSEM_ACTIVE_MASK))
		rwsem_wake(sem);

	/* wait to be given the lock */
	for (;;) {
		if (!test_bit(RWSEM_WAITING_FOR_READ,&wait.flags))
			break;
		schedule();
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	}

	remove_wait_queue(&sem->wait,&wait);
	tsk->state = TASK_RUNNING;

	rwsemdebug("[%d] Leaving down_read_failed(%08x)\n",current->pid,atomic_read(&sem->count));

	return sem;
}

/*
 * wait for the write lock to be granted
 */
struct rw_semaphore *down_write_failed(struct rw_semaphore *sem)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait,tsk);
	int count;

	rwsemdebug("[%d] Entering down_write_failed(%08x)\n",
		   current->pid,atomic_read(&sem->count));

	/* this waitqueue context flag will be cleared when we are granted the lock */
	__set_bit(RWSEM_WAITING_FOR_WRITE,&wait.flags);
	set_task_state(tsk, TASK_UNINTERRUPTIBLE);

	add_wait_queue_exclusive(&sem->wait, &wait); /* FIFO */

	/* note that we're waiting on the lock, but no longer actively locking */
	count = rwsem_atomic_update(-RWSEM_ACTIVE_BIAS,sem);
	rwsemdebug("[%d] updated(%08x)\n",current->pid,count);

	/* if there are no longer active locks, wake the front queued process(es) up
	 * - it might even be this process, since the waker takes a more active part
	 */
	if (!(count & RWSEM_ACTIVE_MASK))
		rwsem_wake(sem);

	/* wait to be given the lock */
	for (;;) {
		if (!test_bit(RWSEM_WAITING_FOR_WRITE,&wait.flags))
			break;
		schedule();
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	}

	remove_wait_queue(&sem->wait,&wait);
	tsk->state = TASK_RUNNING;

	rwsemdebug("[%d] Leaving down_write_failed(%08x)\n",current->pid,atomic_read(&sem->count));

	return sem;
}

/*
 * handle the lock being released whilst there are processes blocked on it that can now run
 * - if we come here, then:
 *   - the 'active part' of the count (&0x0000ffff) reached zero (but may no longer be zero)
 *   - the 'waiting part' of the count (&0xffff0000) is negative (and will still be so)
 */
struct rw_semaphore *rwsem_wake(struct rw_semaphore *sem)
{
	int woken, count;

	rwsemdebug("[%d] Entering rwsem_wake(%08x)\n",current->pid,atomic_read(&sem->count));

 try_again:
	/* try to grab an 'activity' marker
	 * - need to make sure two copies of rwsem_wake() don't do this for two separate processes
	 *   simultaneously
	 * - be horribly naughty, and only deal with the LSW of the atomic counter
	 */
	if (rwsem_cmpxchgw(sem,0,RWSEM_ACTIVE_BIAS)!=0) {
		rwsemdebug("[%d] rwsem_wake: abort wakeup due to renewed activity\n",current->pid);
		goto out;
	}

	/* try to grant a single write lock if there's a writer at the front of the queue
	 * - note we leave the 'active part' of the count incremented by 1 and the waiting part
	 *   incremented by 0x00010000
	 */
	if (wake_up_ctx(&sem->wait,1,-RWSEM_WAITING_FOR_WRITE)==1)
		goto out;

	/* grant an infinite number of read locks to the readers at the front of the queue
	 * - note we increment the 'active part' of the count by the number of readers just woken,
	 *   less one for the activity decrement we've already done
	 */
	woken = wake_up_ctx(&sem->wait,65535,-RWSEM_WAITING_FOR_READ);
	if (woken<=0)
		goto counter_correction;

	woken *= RWSEM_ACTIVE_BIAS-RWSEM_WAITING_BIAS;
	woken -= RWSEM_ACTIVE_BIAS;
	rwsem_atomic_update(woken,sem);

 out:
	rwsemdebug("[%d] Leaving rwsem_wake(%08x)\n",current->pid,atomic_read(&sem->count));
	return sem;

	/* come here if we need to correct the counter for odd SMP-isms */
 counter_correction:
	count = rwsem_atomic_update(-RWSEM_ACTIVE_BIAS,sem);
	rwsemdebug("[%d] corrected(%08x)\n",current->pid,count);
	if (!(count & RWSEM_ACTIVE_MASK))
		goto try_again;
	goto out;
}

/*
 * rw spinlock fallbacks
 */
#if defined(CONFIG_SMP)
asm(
"
.align	4
.globl	__write_lock_failed
__write_lock_failed:
	" LOCK "addl	$" RW_LOCK_BIAS_STR ",(%eax)
1:	cmpl	$" RW_LOCK_BIAS_STR ",(%eax)
	jne	1b

	" LOCK "subl	$" RW_LOCK_BIAS_STR ",(%eax)
	jnz	__write_lock_failed
	ret


.align	4
.globl	__read_lock_failed
__read_lock_failed:
	lock ; incl	(%eax)
1:	cmpl	$1,(%eax)
	js	1b

	lock ; decl	(%eax)
	js	__read_lock_failed
	ret
"
);
#endif
