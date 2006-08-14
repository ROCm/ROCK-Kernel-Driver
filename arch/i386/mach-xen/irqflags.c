#include <linux/module.h>
#include <linux/smp.h>
#include <asm/irqflags.h>
#include <asm/hypervisor.h>

/* interrupt control.. */

/*
 * The use of 'barrier' in the following reflects their use as local-lock
 * operations. Reentrancy must be prevented (e.g., __cli()) /before/ following
 * critical operations are executed. All critical operations must complete
 * /before/ reentrancy is permitted (e.g., __sti()). Alpha architecture also
 * includes these barriers, for example.
 */

unsigned long __raw_local_save_flags(void)
{
	struct vcpu_info *_vcpu;
	unsigned long flags;

	preempt_disable();
	_vcpu = &HYPERVISOR_shared_info->vcpu_info[__vcpu_id];
	flags = _vcpu->evtchn_upcall_mask;
	preempt_enable();

	return flags;
}
EXPORT_SYMBOL(__raw_local_save_flags);

void raw_local_irq_restore(unsigned long flags)
{
	struct vcpu_info *_vcpu;

	preempt_disable();
	_vcpu = &HYPERVISOR_shared_info->vcpu_info[__vcpu_id];
	if ((_vcpu->evtchn_upcall_mask = flags) == 0) {
		barrier(); /* unmask then check (avoid races) */
		if (unlikely(_vcpu->evtchn_upcall_pending))
			force_evtchn_callback();
		preempt_enable();
	} else
		preempt_enable_no_resched();

}
EXPORT_SYMBOL(raw_local_irq_restore);

void raw_local_irq_disable(void)
{
	struct vcpu_info *_vcpu;

	preempt_disable();
	_vcpu = &HYPERVISOR_shared_info->vcpu_info[__vcpu_id];
	_vcpu->evtchn_upcall_mask = 1;
	preempt_enable_no_resched();
}
EXPORT_SYMBOL(raw_local_irq_disable);

void raw_local_irq_enable(void)
{
	struct vcpu_info *_vcpu;

	preempt_disable();
	_vcpu = &HYPERVISOR_shared_info->vcpu_info[__vcpu_id];
	_vcpu->evtchn_upcall_mask = 0;
	barrier(); /* unmask then check (avoid races) */
	if (unlikely(_vcpu->evtchn_upcall_pending))
		force_evtchn_callback();
	preempt_enable();
}
EXPORT_SYMBOL(raw_local_irq_enable);

/* Cannot use preempt_enable() here as we would recurse in preempt_sched(). */
int raw_irqs_disabled(void)
{
	struct vcpu_info *_vcpu;
	int disabled;

	preempt_disable();
	_vcpu = &HYPERVISOR_shared_info->vcpu_info[__vcpu_id];
	disabled = (_vcpu->evtchn_upcall_mask != 0);
	preempt_enable_no_resched();
	return disabled;
}
EXPORT_SYMBOL(raw_irqs_disabled);

unsigned long __raw_local_irq_save(void)
{
	struct vcpu_info *_vcpu;
	unsigned long flags;

	preempt_disable();
	_vcpu = &HYPERVISOR_shared_info->vcpu_info[__vcpu_id];
	flags = _vcpu->evtchn_upcall_mask;
	_vcpu->evtchn_upcall_mask = 1;
	preempt_enable_no_resched();

	return flags;
}
EXPORT_SYMBOL(__raw_local_irq_save);
