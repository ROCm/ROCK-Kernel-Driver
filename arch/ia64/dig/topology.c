/*
 * arch/ia64/dig/topology.c
 *	Popuate driverfs with topology information.
 *	Derived entirely from i386/mach-default.c
 *  Intel Corporation - Ashok Raj
 */
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <asm/cpu.h>

static DEFINE_PER_CPU(struct ia64_cpu, cpu_devices);

/*
 * First Pass: simply borrowed code for now. Later should hook into
 * hotplug notification for node/cpu/memory as applicable
 */

static int arch_register_cpu(int num)
{
	struct node *parent = NULL;

#ifdef CONFIG_NUMA
	//parent = &node_devices[cpu_to_node(num)].node;
#endif

	return register_cpu(&per_cpu(cpu_devices,num).cpu, num, parent);
}

static int __init topology_init(void)
{
    int i;

    for_each_cpu(i) {
        arch_register_cpu(i);
	}
    return 0;
}

subsys_initcall(topology_init);
