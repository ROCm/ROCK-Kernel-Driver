/*
 *	Xen spinlock functions
 *
 *	See arch/x86/xen/smp.c for copyright and credits for derived
 *	portions of this file.
 */
#define XEN_SPINLOCK_SOURCE
#include <linux/spinlock_types.h>

#ifdef TICKET_SHIFT

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/hardirq.h>
#include <xen/clock.h>
#include <xen/evtchn.h>

struct spinning {
	arch_spinlock_t *lock;
	unsigned int ticket;
	struct spinning *prev;
};
static DEFINE_PER_CPU(struct spinning *, _spinning);
static DEFINE_PER_CPU_READ_MOSTLY(evtchn_port_t, poll_evtchn);
/*
 * Protect removal of objects: Addition can be done lockless, and even
 * removal itself doesn't need protection - what needs to be prevented is
 * removed objects going out of scope (as they're allocated on the stack).
 */
static DEFINE_PER_CPU(arch_rwlock_t, spinning_rm_lock) = __ARCH_RW_LOCK_UNLOCKED;

int __cpuinit xen_spinlock_init(unsigned int cpu)
{
	struct evtchn_bind_ipi bind_ipi;
	int rc;

	setup_runstate_area(cpu);

 	WARN_ON(per_cpu(poll_evtchn, cpu));
	bind_ipi.vcpu = cpu;
	rc = HYPERVISOR_event_channel_op(EVTCHNOP_bind_ipi, &bind_ipi);
	if (!rc)
	 	per_cpu(poll_evtchn, cpu) = bind_ipi.port;
	else
		pr_warning("No spinlock poll event channel for CPU#%u (%d)\n",
			   cpu, rc);

	return rc;
}

void __cpuinit xen_spinlock_cleanup(unsigned int cpu)
{
	struct evtchn_close close;

	close.port = per_cpu(poll_evtchn, cpu);
 	per_cpu(poll_evtchn, cpu) = 0;
	WARN_ON(HYPERVISOR_event_channel_op(EVTCHNOP_close, &close));
}

#ifdef CONFIG_PM_SLEEP
#include <linux/sysdev.h>

static int __cpuinit spinlock_resume(struct sys_device *dev)
{
	unsigned int cpu;

	for_each_online_cpu(cpu) {
		per_cpu(poll_evtchn, cpu) = 0;
		xen_spinlock_init(cpu);
	}

	return 0;
}

static struct sysdev_class __cpuinitdata spinlock_sysclass = {
	.name	= "spinlock",
	.resume	= spinlock_resume
};

static struct sys_device __cpuinitdata device_spinlock = {
	.id	= 0,
	.cls	= &spinlock_sysclass
};

static int __init spinlock_register(void)
{
	int rc;

	if (is_initial_xendomain())
		return 0;

	rc = sysdev_class_register(&spinlock_sysclass);
	if (!rc)
		rc = sysdev_register(&device_spinlock);
	return rc;
}
core_initcall(spinlock_register);
#endif

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
	return spin_adjust(percpu_read(_spinning), lock, token);
}

unsigned int xen_spin_wait(arch_spinlock_t *lock, unsigned int *ptok,
			   unsigned int flags)
{
	unsigned int cpu = raw_smp_processor_id();
	bool rc;
	typeof(vcpu_info(0)->evtchn_upcall_mask) upcall_mask;
	arch_rwlock_t *rm_lock;
	struct spinning spinning, *other;

	/* If kicker interrupt not initialized yet, just spin. */
	if (unlikely(!cpu_online(cpu)) || unlikely(!percpu_read(poll_evtchn)))
		return UINT_MAX;

	/* announce we're spinning */
	spinning.ticket = *ptok >> TICKET_SHIFT;
	spinning.lock = lock;
	spinning.prev = percpu_read(_spinning);
	smp_wmb();
	percpu_write(_spinning, &spinning);
	upcall_mask = vcpu_info_read(evtchn_upcall_mask);

	do {
		bool nested = false;

		clear_evtchn(percpu_read(poll_evtchn));

		/*
		 * Check again to make sure it didn't become free while
		 * we weren't looking.
		 */
		if (lock->cur == spinning.ticket) {
			lock->owner = cpu;
			/*
			 * If we interrupted another spinlock while it was
			 * blocking, make sure it doesn't block (again)
			 * without rechecking the lock.
			 */
			if (spinning.prev)
				set_evtchn(percpu_read(poll_evtchn));
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

				arch_local_irq_disable();
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
		 * No need to use arch_local_irq_restore() here, as the
		 * intended event processing will happen with the poll
		 * call.
		 */
		vcpu_info_write(evtchn_upcall_mask,
				nested ? upcall_mask : flags);

		if (HYPERVISOR_poll_no_timeout(&__get_cpu_var(poll_evtchn), 1))
			BUG();

		vcpu_info_write(evtchn_upcall_mask, upcall_mask);

		rc = !test_evtchn(percpu_read(poll_evtchn));
		if (!rc)
			inc_irq_stat(irq_lock_count);
	} while (spinning.prev || rc);

	/*
	 * Leave the irq pending so that any interrupted blocker will
	 * re-check.
	 */

	/* announce we're done */
	other = spinning.prev;
	percpu_write(_spinning, other);
	rm_lock = &__get_cpu_var(spinning_rm_lock);
	arch_local_irq_disable();
	arch_write_lock(rm_lock);
	arch_write_unlock(rm_lock);

	/*
	 * Obtain new tickets for (or acquire) all those locks where
	 * above we avoided acquiring them.
	 */
	if (other) {
		do {
			unsigned int token;
			bool free;

			if (other->ticket + 1)
				continue;
			lock = other->lock;
			__ticket_spin_lock_preamble;
			if (!free)
				token = spin_adjust(other->prev, lock, token);
			other->ticket = token >> TICKET_SHIFT;
			if (lock->cur == other->ticket)
				lock->owner = cpu;
		} while ((other = other->prev) != NULL);
		lock = spinning.lock;
	}

	arch_local_irq_restore(upcall_mask);
	*ptok = lock->cur | (spinning.ticket << TICKET_SHIFT);

	return rc ? 0 : __ticket_spin_count(lock);
}

void xen_spin_kick(arch_spinlock_t *lock, unsigned int token)
{
	unsigned int cpu;

	token &= (1U << TICKET_SHIFT) - 1;
	for_each_online_cpu(cpu) {
		arch_rwlock_t *rm_lock;
		unsigned int flags;
		struct spinning *spinning;

		if (cpu == raw_smp_processor_id())
			continue;

		rm_lock = &per_cpu(spinning_rm_lock, cpu);
		flags = arch_local_irq_save();
		arch_read_lock(rm_lock);

		spinning = per_cpu(_spinning, cpu);
		smp_rmb();
		while (spinning) {
			if (spinning->lock == lock && spinning->ticket == token)
				break;
			spinning = spinning->prev;
		}

		arch_read_unlock(rm_lock);
		arch_local_irq_restore(flags);

		if (unlikely(spinning)) {
			notify_remote_via_evtchn(per_cpu(poll_evtchn, cpu));
			return;
		}
	}
}
EXPORT_SYMBOL(xen_spin_kick);

#endif /* TICKET_SHIFT */
