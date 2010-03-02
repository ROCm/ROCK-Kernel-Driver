/*
 *	Xen spinlock functions
 *
 *	See arch/x86/xen/smp.c for copyright and credits for derived
 *	portions of this file.
 */
#define XEN_SPINLOCK_SOURCE
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <xen/clock.h>
#include <xen/evtchn.h>

#ifdef TICKET_SHIFT

static int __read_mostly spinlock_irq = -1;

struct spinning {
	arch_spinlock_t *lock;
	unsigned int ticket;
	struct spinning *prev;
};
static DEFINE_PER_CPU(struct spinning *, spinning);
/*
 * Protect removal of objects: Addition can be done lockless, and even
 * removal itself doesn't need protection - what needs to be prevented is
 * removed objects going out of scope (as they're allocated on the stack.
 */
static DEFINE_PER_CPU(arch_rwlock_t, spinning_rm_lock) = __ARCH_RW_LOCK_UNLOCKED;

int __cpuinit xen_spinlock_init(unsigned int cpu)
{
	static struct irqaction spinlock_action = {
		.handler = smp_reschedule_interrupt,
		.flags   = IRQF_DISABLED,
		.name    = "spinlock"
	};
	int rc;

	setup_runstate_area(cpu);

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

static unsigned int spin_adjust(struct spinning *spinning,
				const arch_spinlock_t *lock,
				unsigned int token)
{
	for (; spinning; spinning = spinning->prev)
		if (spinning->lock == lock) {
			unsigned int ticket = spinning->ticket;

			if (unlikely(!(ticket + 1)))
				break;
			spinning->ticket = token >> TICKET_SHIFT;
			token = (token & ((1 << TICKET_SHIFT) - 1))
				| (ticket << TICKET_SHIFT);
			break;
		}

	return token;
}

unsigned int xen_spin_adjust(const arch_spinlock_t *lock, unsigned int token)
{
	return spin_adjust(percpu_read(spinning), lock, token);
}

bool xen_spin_wait(arch_spinlock_t *lock, unsigned int *ptok,
                   unsigned int flags)
{
	int irq = spinlock_irq;
	bool rc;
	typeof(vcpu_info(0)->evtchn_upcall_mask) upcall_mask;
	arch_rwlock_t *rm_lock;
	struct spinning spinning, *other;

	/* If kicker interrupt not initialized yet, just spin. */
	if (unlikely(irq < 0) || unlikely(!cpu_online(raw_smp_processor_id())))
		return false;

	/* announce we're spinning */
	spinning.ticket = *ptok >> TICKET_SHIFT;
	spinning.lock = lock;
	spinning.prev = percpu_read(spinning);
	smp_wmb();
	percpu_write(spinning, &spinning);
	upcall_mask = vcpu_info_read(evtchn_upcall_mask);

	do {
		bool nested = false;

		xen_clear_irq_pending(irq);

		/*
		 * Check again to make sure it didn't become free while
		 * we weren't looking.
		 */
		if (lock->cur == spinning.ticket) {
			/*
			 * If we interrupted another spinlock while it was
			 * blocking, make sure it doesn't block (again)
			 * without rechecking the lock.
			 */
			if (spinning.prev)
				xen_set_irq_pending(irq);
			rc = true;
			break;
		}

		for (other = spinning.prev; other; other = other->prev) {
			if (other->lock == lock)
				nested = true;
			else {
				/*
				 * Return the ticket if we now own the lock.
				 * While just being desirable generally (to
				 * reduce latency on other CPUs), this is
				 * essential in the case where interrupts
				 * get re-enabled below.
				 * Try to get a new ticket right away (to
				 * reduce latency after the current lock was
				 * released), but don't acquire the lock.
				 */
				arch_spinlock_t *lock = other->lock;

				raw_local_irq_disable();
				while (lock->cur == other->ticket) {
					unsigned int token;
					bool kick, free;

					other->ticket = -1;
					__ticket_spin_unlock_body;
					if (!kick)
						break;
					xen_spin_kick(lock, token);
					__ticket_spin_lock_preamble;
					if (!free)
						token = spin_adjust(
							other->prev, lock,
							token);
					other->ticket = token >> TICKET_SHIFT;
					smp_mb();
				}
			}
		}

		/*
		 * No need to use raw_local_irq_restore() here, as the
		 * intended event processing will happen with the poll
		 * call.
		 */
		vcpu_info_write(evtchn_upcall_mask,
				nested ? upcall_mask : flags);

		xen_poll_irq(irq);

		vcpu_info_write(evtchn_upcall_mask, upcall_mask);

		rc = !xen_test_irq_pending(irq);
		if (!rc)
			kstat_incr_irqs_this_cpu(irq, irq_to_desc(irq));
	} while (spinning.prev || rc);

	/*
	 * Leave the irq pending so that any interrupted blocker will
	 * re-check.
	 */

	/* announce we're done */
	other = spinning.prev;
	percpu_write(spinning, other);
	rm_lock = &__get_cpu_var(spinning_rm_lock);
	raw_local_irq_disable();
	arch_write_lock(rm_lock);
	arch_write_unlock(rm_lock);
	*ptok = lock->cur | (spinning.ticket << TICKET_SHIFT);

	/*
	 * Obtain new tickets for (or acquire) all those locks where
	 * above we avoided acquiring them.
	 */
	for (; other; other = other->prev)
		if (!(other->ticket + 1)) {
			unsigned int token;
			bool free;

			lock = other->lock;
			__ticket_spin_lock_preamble;
			if (!free)
				token = spin_adjust(other->prev, lock, token);
			other->ticket = token >> TICKET_SHIFT;
		}
	raw_local_irq_restore(upcall_mask);

	return rc;
}

void xen_spin_kick(arch_spinlock_t *lock, unsigned int token)
{
	unsigned int cpu;

	token &= (1U << TICKET_SHIFT) - 1;
	for_each_online_cpu(cpu) {
		arch_rwlock_t *rm_lock;
		unsigned long flags;
		struct spinning *spinning;

		if (cpu == raw_smp_processor_id() || !per_cpu(spinning, cpu))
			continue;

		rm_lock = &per_cpu(spinning_rm_lock, cpu);
		raw_local_irq_save(flags);
		arch_read_lock(rm_lock);

		spinning = per_cpu(spinning, cpu);
		smp_rmb();
		while (spinning) {
			if (spinning->lock == lock && spinning->ticket == token)
				break;
			spinning = spinning->prev;
		}

		arch_read_unlock(rm_lock);
		raw_local_irq_restore(flags);

		if (unlikely(spinning)) {
			notify_remote_via_ipi(SPIN_UNLOCK_VECTOR, cpu);
			return;
		}
	}
}
EXPORT_SYMBOL(xen_spin_kick);

#endif /* TICKET_SHIFT */
