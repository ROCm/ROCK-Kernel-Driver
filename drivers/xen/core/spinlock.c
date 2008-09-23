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
};
static DEFINE_PER_CPU(struct spinning, spinning);
static DEFINE_PER_CPU(struct spinning, spinning_bh);
static DEFINE_PER_CPU(struct spinning, spinning_irq);

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
	struct spinning *spinning;
	int rc = 0, irq = __get_cpu_var(spinlock_irq);

	/* If kicker interrupt not initialized yet, just spin. */
	if (irq < 0)
		return 0;

	token >>= TICKET_SHIFT;

	/* announce we're spinning */
	spinning = &__get_cpu_var(spinning);
	if (spinning->lock) {
		BUG_ON(spinning->lock == lock);
		if(raw_irqs_disabled()) {
			BUG_ON(__get_cpu_var(spinning_bh).lock == lock);
			spinning = &__get_cpu_var(spinning_irq);
		} else {
			BUG_ON(!in_softirq());
			spinning = &__get_cpu_var(spinning_bh);
		}
		BUG_ON(spinning->lock);
	}
	spinning->ticket = token;
	smp_wmb();
	spinning->lock = lock;

	/* clear pending */
	xen_clear_irq_pending(irq);

	do {
		/* Check again to make sure it didn't become free while
		 * we weren't looking. */
		if ((lock->slock & ((1U << TICKET_SHIFT) - 1)) == token) {
			/* If we interrupted another spinlock while it was
			 * blocking, make sure it doesn't block (again)
			 * without rechecking the lock. */
			if (spinning != &__get_cpu_var(spinning))
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
	spinning->lock = NULL;

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

static inline int spinning(const struct spinning *spinning, unsigned int cpu,
			   raw_spinlock_t *lock, unsigned int ticket)
{
	if (spinning->lock != lock)
		return 0;
	smp_rmb();
	if (spinning->ticket != ticket)
		return 0;
	notify_remote_via_irq(per_cpu(spinlock_irq, cpu));
	return 1;
}

void xen_spin_kick(raw_spinlock_t *lock, unsigned int token)
{
	unsigned int cpu;

	token &= (1U << TICKET_SHIFT) - 1;
	for_each_online_cpu(cpu) {
		if (cpu == raw_smp_processor_id())
			continue;
		if (spinning(&per_cpu(spinning, cpu), cpu, lock, token))
			return;
		if (in_interrupt()
		    && spinning(&per_cpu(spinning_bh, cpu), cpu, lock, token))
			return;
		if (raw_irqs_disabled()
		    && spinning(&per_cpu(spinning_irq, cpu), cpu, lock, token))
			return;
	}
}
EXPORT_SYMBOL(xen_spin_kick);
