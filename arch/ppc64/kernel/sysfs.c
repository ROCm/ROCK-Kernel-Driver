#include <linux/config.h>
#include <linux/sysdev.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <asm/processor.h>

#ifdef CONFIG_NUMA
static struct node node_devices[MAX_NUMNODES];

static void register_nodes(void)
{
	int i;

	for (i = 0; i < MAX_NUMNODES; i++) {
		if (node_online(i)) {
			int p_node = parent_node(i);
			struct node *parent = NULL;

			if (p_node != i)
				parent = &node_devices[p_node];

			register_node(&node_devices[i], i, parent);
		}
	}
}
#else
static void register_nodes(void)
{
	return;
}
#endif


/* Only valid if CPU is online. */
static ssize_t show_physical_id(struct sys_device *dev, char *buf)
{
	struct cpu *cpu = container_of(dev, struct cpu, sysdev);

	return sprintf(buf, "%u\n", get_hard_smp_processor_id(cpu->sysdev.id));
}
static SYSDEV_ATTR(physical_id, 0444, show_physical_id, NULL);


static DEFINE_PER_CPU(struct cpu, cpu_devices);

static int __init topology_init(void)
{
	int cpu;
	struct node *parent = NULL;

	register_nodes();

	for_each_cpu(cpu) {
		struct cpu *c = &per_cpu(cpu_devices, cpu);

#ifdef CONFIG_NUMA
		parent = &node_devices[cpu_to_node(cpu)];
#endif
		register_cpu(c, cpu, parent);

		sysdev_create_file(&c->sysdev, &attr_physical_id);
	}

	return 0;
}
__initcall(topology_init);
