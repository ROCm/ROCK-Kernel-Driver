/**
 * @file init.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/oprofile.h>
#include <linux/init.h>
 
/* We support CPUs that have performance counters like the Pentium Pro
 * with NMI mode samples. Other x86 CPUs use a simple interrupt keyed
 * off the timer interrupt, which cannot profile interrupts-disabled
 * code unlike the NMI-based code.
 */
 
extern int nmi_init(struct oprofile_operations ** ops, enum oprofile_cpu * cpu);
extern void timer_init(struct oprofile_operations ** ops, enum oprofile_cpu * cpu);

int __init oprofile_arch_init(struct oprofile_operations ** ops, enum oprofile_cpu * cpu)
{
#ifdef CONFIG_X86_LOCAL_APIC
	if (!nmi_init(ops, cpu))
#endif
		timer_init(ops, cpu);
	return 0;
}
