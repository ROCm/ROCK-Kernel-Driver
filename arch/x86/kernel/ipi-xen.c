#include <linux/cpumask.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>
#include <linux/cache.h>
#include <linux/interrupt.h>
#include <linux/cpu.h>
#include <linux/module.h>

#include <asm/smp.h>
#include <asm/mtrr.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/apic.h>
#include <asm/proto.h>

#ifdef CONFIG_X86_32
#ifndef CONFIG_XEN
#include <mach_apic.h>
/*
 * the following functions deal with sending IPIs between CPUs.
 *
 * We use 'broadcast', CPU->CPU IPIs and self-IPIs too.
 */

static inline int __prepare_ICR(unsigned int shortcut, int vector)
{
	unsigned int icr = shortcut | APIC_DEST_LOGICAL;

	switch (vector) {
	default:
		icr |= APIC_DM_FIXED | vector;
		break;
	case NMI_VECTOR:
		icr |= APIC_DM_NMI;
		break;
	}
	return icr;
}

static inline int __prepare_ICR2(unsigned int mask)
{
	return SET_APIC_DEST_FIELD(mask);
}
#else
#include <xen/evtchn.h>

DECLARE_PER_CPU(int, ipi_to_irq[NR_IPIS]);

static inline void __send_IPI_one(unsigned int cpu, int vector)
{
	int irq = per_cpu(ipi_to_irq, cpu)[vector];
	BUG_ON(irq < 0);
	notify_remote_via_irq(irq);
}
#endif

void __send_IPI_shortcut(unsigned int shortcut, int vector)
{
#ifndef CONFIG_XEN
	/*
	 * Subtle. In the case of the 'never do double writes' workaround
	 * we have to lock out interrupts to be safe.  As we don't care
	 * of the value read we use an atomic rmw access to avoid costly
	 * cli/sti.  Otherwise we use an even cheaper single atomic write
	 * to the APIC.
	 */
	unsigned int cfg;

	/*
	 * Wait for idle.
	 */
	apic_wait_icr_idle();

	/*
	 * No need to touch the target chip field
	 */
	cfg = __prepare_ICR(shortcut, vector);

	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	apic_write_around(APIC_ICR, cfg);
#else
	int cpu;

	switch (shortcut) {
	case APIC_DEST_SELF:
		__send_IPI_one(smp_processor_id(), vector);
		break;
	case APIC_DEST_ALLBUT:
		for (cpu = 0; cpu < NR_CPUS; ++cpu) {
			if (cpu == smp_processor_id())
				continue;
			if (cpu_isset(cpu, cpu_online_map)) {
				__send_IPI_one(cpu, vector);
			}
		}
		break;
	default:
		printk("XXXXXX __send_IPI_shortcut %08x vector %d\n", shortcut,
		       vector);
		break;
	}
#endif
}

void send_IPI_self(int vector)
{
	__send_IPI_shortcut(APIC_DEST_SELF, vector);
}

#ifndef CONFIG_XEN
/*
 * This is used to send an IPI with no shorthand notation (the destination is
 * specified in bits 56 to 63 of the ICR).
 */
static inline void __send_IPI_dest_field(unsigned long mask, int vector)
{
	unsigned long cfg;

	/*
	 * Wait for idle.
	 */
	if (unlikely(vector == NMI_VECTOR))
		safe_apic_wait_icr_idle();
	else
		apic_wait_icr_idle();

	/*
	 * prepare target chip field
	 */
	cfg = __prepare_ICR2(mask);
	apic_write_around(APIC_ICR2, cfg);

	/*
	 * program the ICR
	 */
	cfg = __prepare_ICR(0, vector);

	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	apic_write_around(APIC_ICR, cfg);
}
#endif

/*
 * This is only used on smaller machines.
 */
void send_IPI_mask_bitmask(cpumask_t cpumask, int vector)
{
	unsigned long mask = cpus_addr(cpumask)[0];
	unsigned long flags;
#ifdef CONFIG_XEN
	unsigned int cpu;
#endif

	local_irq_save(flags);
	WARN_ON(mask & ~cpus_addr(cpu_online_map)[0]);
#ifndef CONFIG_XEN
	__send_IPI_dest_field(mask, vector);
#else
	for (cpu = 0; cpu < NR_CPUS; ++cpu)
		if (cpu_isset(cpu, cpumask))
			__send_IPI_one(cpu, vector);
#endif
	local_irq_restore(flags);
}

void send_IPI_mask_sequence(cpumask_t mask, int vector)
{
#ifndef CONFIG_XEN
	unsigned long flags;
	unsigned int query_cpu;

	/*
	 * Hack. The clustered APIC addressing mode doesn't allow us to send
	 * to an arbitrary mask, so I do a unicasts to each CPU instead. This
	 * should be modified to do 1 message per cluster ID - mbligh
	 */

	local_irq_save(flags);
	for_each_possible_cpu(query_cpu) {
		if (cpu_isset(query_cpu, mask)) {
			__send_IPI_dest_field(cpu_to_logical_apicid(query_cpu),
					      vector);
		}
	}
	local_irq_restore(flags);
#else
	send_IPI_mask_bitmask(mask, vector);
#endif
}

/* must come after the send_IPI functions above for inlining */
#include <mach_ipi.h>

#ifndef CONFIG_XEN
static int convert_apicid_to_cpu(int apic_id)
{
	int i;

	for_each_possible_cpu(i) {
		if (per_cpu(x86_cpu_to_apicid, i) == apic_id)
			return i;
	}
	return -1;
}

int safe_smp_processor_id(void)
{
	int apicid, cpuid;

	if (!boot_cpu_has(X86_FEATURE_APIC))
		return 0;

	apicid = hard_smp_processor_id();
	if (apicid == BAD_APICID)
		return 0;

	cpuid = convert_apicid_to_cpu(apicid);

	return cpuid >= 0 ? cpuid : 0;
}
#endif
#endif
