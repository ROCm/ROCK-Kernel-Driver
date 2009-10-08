/*
 *	Xen spinlock functions
 *
 *	See arch/x86/xen/smp.c for copyright and credits for derived
 *	portions of this file.
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <xen/evtchn.h>

#ifdef TICKET_SHIFT

static int __read_mostly spinlock_irq = -1;

struct spinning {
	raw_spinlock_t *lock;
	unsigned int ticket;
	struct spinning *prev;
};
static DEFINE_PER_CPU(struct spinning *, spinning);
/*
 * Protect removal of objects: Addition can be done lockless, and even
 * removal itself doesn't need protection - what needs to be prevented is
 * removed objects going out of scope (as they're allocated on the stack.
 */
static DEFINE_PER_CPU(raw_rwlock_t, spinning_rm_lock) = __RAW_RW_LOCK_UNLOCKED;

int __cpuinit xen_spinlock_init(unsigned int cpu)
{
	static struct irqaction spinlock_action = {
		.handler = smp_reschedule_interrupt,
		.flags   = IRQF_DISABLED,
		.name    = "spinlock"
	};
	int rc;

	rc = bind_ipi_to_irqaction(SPIN_UNLOCK_VECTOR,
				   cpu,
				   &spinlock_action);
 	if (rc < 0)
 		return rc;

	if (spinlock_irq < 0) {
		disable_irq(rc); /* make sure it's never delivered */
		spinlock_irq = rc;
	} else
		BUG_ON(spinlock_irq != rc);

	return 0;
}

void __cpuinit xen_spinlock_cleanup(unsigned int cpu)
{
	unbind_from_per_cpu_irq(spinlock_irq, cpu, NULL);
}

int xen_spin_wait(raw_spinlock_t *lock, unsigned int token)
{
	int rc = 0, irq = spinlock_irq;
	raw_rwlock_t *rm_lock;
	unsigned long flags;
	struct spinning spinning;

	/* If kicker interrupt not initialized yet, just spin. */
	if (unlikely(irq < 0) || unlikely(!cpu_online(raw_smp_processor_id())))
		return 0;

	token >>= TICKET_SHIFT;

	/* announce we're spinning */
	spinning.ticket = token;
	spinning.lock = lock;
	spinning.prev = percpu_read(spinning);
	smp_wmb();
	percpu_write(spinning, &spinning);

	/* clear pending */
	xen_clear_irq_pending(irq);

	do {
		/* Check again to make sure it didn't become free while
		 * we weren't looking. */
		if ((lock->slock & ((1U << TICKET_SHIFT) - 1)) == token) {
			/* If we interrupted another spinlock while it was
			 * blocking, make sure it doesn't block (again)
			 * without rechecking the lock. */
			if (spinning.prev)
				xen_set_irq_pending(irq);
			rc = 1;
			break;
		}

		/* block until irq becomes pending */
		xen_poll_irq(irq);
	} while (!xen_test_irq_pending(irq));

	/* Leave the irq pending so that any interrupted blocker will
	 * re-check. */
	if (!rc)
		kstat_incr_irqs_this_cpu(irq, irq_to_desc(irq));

	/* announce we're done */
	percpu_write(spinning, spinning.prev);
	rm_lock = &__get_cpu_var(spinning_rm_lock);
	raw_local_irq_save(flags);
	__raw_write_lock(rm_lock);
	__raw_write_unlock(rm_lock);
	raw_local_irq_restore(flags);

	return rc;
}

unsigned int xen_spin_adjust(raw_spinlock_t *lock, unsigned int token)
{
	return token;//todo
}

int xen_spin_wait_flags(raw_spinlock_t *lock, unsigned int *token,
			  unsigned int flags)
{
	return xen_spin_wait(lock, *token);//todo
}

void xen_spin_kick(raw_spinlock_t *lock, unsigned int token)
{
	unsigned int cpu;

	token &= (1U << TICKET_SHIFT) - 1;
	for_each_online_cpu(cpu) {
		raw_rwlock_t *rm_lock;
		unsigned long flags;
		struct spinning *spinning;

		if (cpu == raw_smp_processor_id() || !per_cpu(spinning, cpu))
			continue;

		rm_lock = &per_cpu(spinning_rm_lock, cpu);
		raw_local_irq_save(flags);
		__raw_read_lock(rm_lock);

		spinning = per_cpu(spinning, cpu);
		smp_rmb();
		if (spinning
		    && (spinning->lock != lock || spinning->ticket != token))
			spinning = NULL;

		__raw_read_unlock(rm_lock);
		raw_local_irq_restore(flags);

		if (unlikely(spinning)) {
			notify_remote_via_ipi(SPIN_UNLOCK_VECTOR, cpu);
			return;
		}
	}
}
EXPORT_SYMBOL(xen_spin_kick);

#endif /* TICKET_SHIFT */
