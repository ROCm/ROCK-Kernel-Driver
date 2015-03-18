/*
 *	Xen spinlock functions
 *
 *	See arch/x86/xen/smp.c for copyright and credits for derived
 *	portions of this file.
 */
#define XEN_SPINLOCK_SOURCE
#include <linux/spinlock_types.h>

#ifdef TICKET_SHIFT

#include <linux/export.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <asm/hardirq.h>
#include <xen/clock.h>
#include <xen/evtchn.h>

struct spinning {
	arch_spinlock_t *lock;
	unsigned int ticket;
#if CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING
	unsigned int irq_count;
#endif
	struct spinning *prev;
};
static DEFINE_PER_CPU(struct spinning *, _spinning);
static DEFINE_PER_CPU_READ_MOSTLY(evtchn_port_t, poll_evtchn);
/*
 * Protect removal of objects: Addition can be done lockless, and even
 * removal itself doesn't need protection - what needs to be prevented is
 * removed objects going out of scope (as they're allocated on the stack).
 */
struct rm_seq {
	unsigned int idx;
	atomic_t ctr[2];
};
static DEFINE_PER_CPU(struct rm_seq, rm_seq);

static bool __read_mostly nopoll;
module_param(nopoll, bool, 0);

int xen_spinlock_init(unsigned int cpu)
{
	struct evtchn_bind_ipi bind_ipi;
	int rc;

	setup_runstate_area(cpu);

	if (nopoll)
		return 0;

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

void xen_spinlock_cleanup(unsigned int cpu)
{
	struct evtchn_close close;

	close.port = per_cpu(poll_evtchn, cpu);
	if (!close.port)
		return;
	per_cpu(poll_evtchn, cpu) = 0;
	WARN_ON(HYPERVISOR_event_channel_op(EVTCHNOP_close, &close));
}

#ifdef CONFIG_PM_SLEEP
#include <linux/syscore_ops.h>

static void spinlock_resume(void)
{
	unsigned int cpu;

	for_each_online_cpu(cpu) {
		per_cpu(poll_evtchn, cpu) = 0;
		xen_spinlock_init(cpu);
	}
}

static struct syscore_ops spinlock_syscore_ops = {
	.resume	= spinlock_resume
};

static int __init spinlock_register(void)
{
	if (!is_initial_xendomain())
		register_syscore_ops(&spinlock_syscore_ops);
	return 0;
}
core_initcall(spinlock_register);
#endif

static inline void sequence(void)
{
	unsigned int rm_idx;

	smp_wmb();
	rm_idx = (__this_cpu_inc_return(rm_seq.idx) - 1) & 1;
	smp_mb();
	while (__this_cpu_read(rm_seq.ctr[rm_idx].counter))
		cpu_relax();
}

#if CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING
static DEFINE_PER_CPU(unsigned int, _irq_count);

static __ticket_t spin_adjust(struct spinning *spinning,
			      const arch_spinlock_t *lock,
			      __ticket_t ticket)
{
	for (; spinning; spinning = spinning->prev) {
		unsigned int old = spinning->ticket;

		if (spinning->lock != lock)
			continue;
		while (likely(old + 1)) {
			unsigned int cur;

#if CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING > 1
			ticket = spin_adjust(spinning->prev, lock, ticket);
#endif
			cur = cmpxchg(&spinning->ticket, old, ticket);
			if (cur == old)
				return cur;
			old = cur;
		}
#if CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING == 1
		break;
#endif
	}
	return ticket;
}

struct __raw_tickets xen_spin_adjust(const arch_spinlock_t *lock,
				     struct __raw_tickets token)
{
	token.tail = spin_adjust(__this_cpu_read(_spinning), lock, token.tail);
	token.head = lock->tickets.head;
	return token;
}

static unsigned int ticket_drop(struct spinning *spinning,
				unsigned int ticket, unsigned int cpu)
{
	arch_spinlock_t *lock = spinning->lock;

	if (cmpxchg(&spinning->ticket, ticket, -1) != ticket)
		return -1;
	lock->owner = cpu;
	__add(&lock->tickets.head, TICKET_LOCK_INC, UNLOCK_LOCK_PREFIX);
	ticket = (__ticket_t)(ticket + 1);
	return ticket != lock->tickets.tail ? ticket : -1;
}

static unsigned int ticket_get(arch_spinlock_t *lock, struct spinning *prev)
{
	struct __raw_tickets token = xadd(&lock->tickets,
					  (struct __raw_tickets)
					  { .tail = TICKET_LOCK_INC });

	return __tickets_equal(token.head, token.tail)
	       ? token.tail : spin_adjust(prev, lock, token.tail);
}

void xen_spin_irq_enter(void)
{
	struct spinning *spinning = __this_cpu_read(_spinning);
	unsigned int cpu = raw_smp_processor_id();

	__this_cpu_inc(_irq_count);
	smp_mb();
	for (; spinning; spinning = spinning->prev) {
		arch_spinlock_t *lock = spinning->lock;

		/*
		 * Return the ticket if we now own the lock. While just being
		 * desirable generally (to reduce latency on spinning CPUs),
		 * this is essential in the case where interrupts get
		 * re-enabled in xen_spin_wait().
		 * Try to get a new ticket right away (to reduce latency after
		 * the current lock was released), but don't acquire the lock.
		 */
		while (lock->tickets.head == spinning->ticket) {
			unsigned int ticket = ticket_drop(spinning,
							  lock->tickets.head,
							  cpu);

			if (!(ticket + 1))
				break;
			xen_spin_kick(lock, ticket);
			spinning->ticket = ticket_get(lock, spinning->prev);
			smp_mb();
		}
	}
}

void xen_spin_irq_exit(void)
{
	struct spinning *spinning = __this_cpu_read(_spinning);
	/*
	 * Despite its counterpart being first in xen_spin_irq_enter() (to make
	 * xen_spin_kick() properly handle locks that get owned after their
	 * tickets were obtained there), it can validly be done first here:
	 * We're guaranteed to see another invocation of xen_spin_irq_enter()
	 * if any of the tickets need to be dropped again.
	 */
	unsigned int irq_count = __this_cpu_dec_return(_irq_count);

	/*
	 * Make sure all xen_spin_kick() instances which may still have seen
	 * our old IRQ count exit their critical region (so that we won't fail
	 * to re-obtain a ticket if ticket_drop() completes only after our
	 * ticket check below).
	 */
	sequence();

	/*
	 * Obtain new tickets for (or acquire) all those locks at the IRQ
	 * nesting level we are about to return to where above we avoided
	 * acquiring them.
	 */
	for (; spinning; spinning = spinning->prev) {
		arch_spinlock_t *lock = spinning->lock;

		if (spinning->irq_count < irq_count)
			break;
		if (spinning->ticket + 1)
			continue;
		spinning->ticket = ticket_get(lock, spinning->prev);
		if (lock->tickets.head == spinning->ticket) {
			lock->owner = raw_smp_processor_id();
			set_evtchn(__this_cpu_read(poll_evtchn));
		}
	}
}
#endif

unsigned int xen_spin_wait(arch_spinlock_t *lock, struct __raw_tickets *ptok,
			   unsigned int flags)
{
	unsigned int cpu = raw_smp_processor_id();
	typeof(vcpu_info(0)->evtchn_upcall_mask) upcall_mask
		= arch_local_save_flags();
	struct spinning spinning;

	/* If kicker interrupt not initialized yet, just spin. */
	if (unlikely(!cpu_online(cpu))
	    || unlikely(!__this_cpu_read(poll_evtchn)))
		return UINT_MAX;

	/* announce we're spinning */
	spinning.ticket = ptok->tail;
	spinning.lock = lock;
	spinning.prev = __this_cpu_read(_spinning);
#if CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING
	spinning.irq_count = UINT_MAX;
	if (upcall_mask > flags) {
		const struct spinning *other;
		int nesting = CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING;

		for (other = spinning.prev; other; other = other->prev)
			if (other->lock == lock && !--nesting) {
				flags = upcall_mask;
				break;
			}
	}
	arch_local_irq_disable();
#endif
	smp_wmb();
	__this_cpu_write(_spinning, &spinning);

	for (;;) {
		clear_evtchn(__this_cpu_read(poll_evtchn));

		/*
		 * Check again to make sure it didn't become free while
		 * we weren't looking.
		 */
		if (lock->tickets.head == spinning.ticket) {
			/*
			 * If we interrupted another spinlock while it was
			 * blocking, make sure it doesn't block (again)
			 * without rechecking the lock.
			 */
			if (spinning.prev)
				set_evtchn(__this_cpu_read(poll_evtchn));
			break;
		}

#if CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING
		spinning.irq_count = __this_cpu_read(_irq_count);
		smp_wmb();
		arch_local_irq_restore(flags);
#endif

		if (!test_evtchn(__this_cpu_read(poll_evtchn)) &&
		    HYPERVISOR_poll_no_timeout(this_cpu_ptr(&poll_evtchn), 1))
			BUG();

#if CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING
		arch_local_irq_disable();
		smp_wmb();
		spinning.irq_count = UINT_MAX;
#endif

		if (test_evtchn(__this_cpu_read(poll_evtchn))) {
			inc_irq_stat(irq_lock_count);
			break;
		}
	}

	/*
	 * Leave the event pending so that any interrupted blocker will
	 * re-check.
	 */

	/* announce we're done */
	__this_cpu_write(_spinning, spinning.prev);
	if (!CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING)
		arch_local_irq_disable();
	sequence();
	arch_local_irq_restore(upcall_mask);
	smp_rmb();
	if (lock->tickets.head == spinning.ticket) {
		lock->owner = cpu;
		return 0;
	}
	BUG_ON(CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING && !(spinning.ticket + 1));
	ptok->head = lock->tickets.head;
	ptok->tail = spinning.ticket;

	return SPIN_THRESHOLD >> 2;
}

static inline unsigned int cpumask_cycle(int n, const struct cpumask *srcp)
{
	unsigned int nxt = cpumask_next(n, srcp);

	if (nxt >= nr_cpu_ids)
		nxt = cpumask_first(srcp);
	return nxt;
}

void xen_spin_kick(const arch_spinlock_t *lock, unsigned int ticket)
{
	unsigned int local = raw_smp_processor_id(), start, stop = local, cpu;

	if (nopoll)
		return;

	if (unlikely(!cpu_online(local)))
		stop = start = cpumask_cycle(local, cpu_online_mask);
#if CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING
	else if (__this_cpu_read(_spinning))
		start = local;
#endif
	else {
		start = cpumask_cycle(local, cpu_online_mask);
		if (start == stop)
			return;
	}

	for (cpu = start; ; ) {
		unsigned int flags;
		atomic_t *rm_ctr = NULL;
		struct spinning *spinning;

		flags = arch_local_irq_save();
		if (cpu == local)
			spinning = __this_cpu_read(_spinning);
		else for (;;) {
			unsigned int rm_idx = per_cpu(rm_seq.idx, cpu);

			rm_ctr = per_cpu(rm_seq.ctr, cpu) + (rm_idx & 1);
			atomic_inc(rm_ctr);
#ifdef CONFIG_X86 /* atomic ops are full CPU barriers */
			barrier();
#else
			smp_mb();
#endif
			spinning = per_cpu(_spinning, cpu);
			smp_rmb();
			if (rm_idx == per_cpu(rm_seq.idx, cpu))
				break;
			atomic_dec(rm_ctr);
			if (!vcpu_running(cpu))
				HYPERVISOR_yield();
		}

		for (; spinning; spinning = spinning->prev)
			if (spinning->lock == lock &&
			    spinning->ticket == ticket) {
#if CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING
				ticket = spinning->irq_count
					 < per_cpu(_irq_count, cpu)
					 ? ticket_drop(spinning, ticket, cpu) : -2;
#endif
				break;
			}

		if (rm_ctr)
			atomic_dec(rm_ctr);
		arch_local_irq_restore(flags);

		if (unlikely(spinning)) {
#if CONFIG_XEN_SPINLOCK_ACQUIRE_NESTING
			if (!(ticket + 1))
				return;
			if (ticket + 2) {
				cpu = start;
				continue;
			}
#endif
			notify_remote_via_evtchn(per_cpu(poll_evtchn, cpu));
			return;
		}
		cpu = cpumask_cycle(cpu, cpu_online_mask);
		if (cpu == stop)
			break;
	}
}
EXPORT_SYMBOL(xen_spin_kick);

#endif /* TICKET_SHIFT */
