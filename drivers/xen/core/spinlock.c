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

extern irqreturn_t smp_reschedule_interrupt(int, void *);

static DEFINE_PER_CPU(int, spinlock_irq) = -1;
static char spinlock_name[NR_CPUS][15];

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
	int rc;

	sprintf(spinlock_name[cpu], "spinlock%u", cpu);
	rc = bind_ipi_to_irqhandler(SPIN_UNLOCK_VECTOR,
				    cpu,
				    smp_reschedule_interrupt,
				    IRQF_DISABLED|IRQF_NOBALANCING,
				    spinlock_name[cpu],
				    NULL);
 	if (rc < 0)
 		return rc;

	disable_irq(rc); /* make sure it's never delivered */
	per_cpu(spinlock_irq, cpu) = rc;

	return 0;
}

void __cpuinit xen_spinlock_cleanup(unsigned int cpu)
{
	if (per_cpu(spinlock_irq, cpu) >= 0)
		unbind_from_irqhandler(per_cpu(spinlock_irq, cpu), NULL);
	per_cpu(spinlock_irq, cpu) = -1;
}

int xen_spin_wait(raw_spinlock_t *lock, unsigned int token)
{
	int rc = 0, irq = __get_cpu_var(spinlock_irq);
	raw_rwlock_t *rm_lock;
	unsigned long flags;
	struct spinning spinning;

	/* If kicker interrupt not initialized yet, just spin. */
	if (unlikely(irq < 0))
		return 0;

	token >>= TICKET_SHIFT;

	/* announce we're spinning */
	spinning.ticket = token;
	spinning.lock = lock;
	spinning.prev = __get_cpu_var(spinning);
	smp_wmb();
	__get_cpu_var(spinning) = &spinning;

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
	kstat_this_cpu.irqs[irq] += !rc;

	/* announce we're done */
	__get_cpu_var(spinning) = spinning.prev;
	rm_lock = &__get_cpu_var(spinning_rm_lock);
	raw_local_irq_save(flags);
	__raw_write_lock(rm_lock);
	__raw_write_unlock(rm_lock);
	raw_local_irq_restore(flags);

	return rc;
}
EXPORT_SYMBOL(xen_spin_wait);

unsigned int xen_spin_adjust(raw_spinlock_t *lock, unsigned int token)
{
	return token;//todo
}
EXPORT_SYMBOL(xen_spin_adjust);

int xen_spin_wait_flags(raw_spinlock_t *lock, unsigned int *token,
			  unsigned int flags)
{
	return xen_spin_wait(lock, *token);//todo
}
EXPORT_SYMBOL(xen_spin_wait_flags);

void xen_spin_kick(raw_spinlock_t *lock, unsigned int token)
{
	unsigned int cpu;

	token &= (1U << TICKET_SHIFT) - 1;
	for_each_online_cpu(cpu) {
		raw_rwlock_t *rm_lock;
		unsigned long flags;
		struct spinning *spinning;

		if (cpu == raw_smp_processor_id())
			continue;

		rm_lock = &per_cpu(spinning_rm_lock, cpu);
		raw_local_irq_save(flags);
		__raw_read_lock(rm_lock);

		spinning = per_cpu(spinning, cpu);
		smp_rmb();
		while (spinning) {
			if (spinning->lock == lock
			    && spinning->ticket == token)
				break;
			spinning = spinning->prev;
		}

		__raw_read_unlock(rm_lock);
		raw_local_irq_restore(flags);

		if (spinning) {
			notify_remote_via_irq(per_cpu(spinlock_irq, cpu));
			return;
		}
	}
}
EXPORT_SYMBOL(xen_spin_kick);
