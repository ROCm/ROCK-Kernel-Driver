#include <linux/config.h>
#include <linux/sysdev.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <asm/current.h>
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/hvcall.h>

/* PMC stuff */

/* XXX convert to rusty's on_one_cpu */
static unsigned long run_on_cpu(unsigned long cpu,
			        unsigned long (*func)(unsigned long),
				unsigned long arg)
{
	cpumask_t old_affinity = current->cpus_allowed;
	unsigned long ret;

	/* should return -EINVAL to userspace */
	if (set_cpus_allowed(current, cpumask_of_cpu(cpu)))
		return 0;

	ret = func(arg);

	set_cpus_allowed(current, old_affinity);

	return ret;
}

#define SYSFS_PMCSETUP(NAME, ADDRESS) \
static unsigned long read_##NAME(unsigned long junk) \
{ \
	return mfspr(ADDRESS); \
} \
static unsigned long write_##NAME(unsigned long val) \
{ \
	mtspr(ADDRESS, val); \
	return 0; \
} \
static ssize_t show_##NAME(struct sys_device *dev, char *buf) \
{ \
	struct cpu *cpu = container_of(dev, struct cpu, sysdev); \
	unsigned long val = run_on_cpu(cpu->sysdev.id, read_##NAME, 0); \
	return sprintf(buf, "%lx\n", val); \
} \
static ssize_t store_##NAME(struct sys_device *dev, const char *buf, \
			    size_t count) \
{ \
	struct cpu *cpu = container_of(dev, struct cpu, sysdev); \
	unsigned long val; \
	int ret = sscanf(buf, "%lx", &val); \
	if (ret != 1) \
		return -EINVAL; \
	run_on_cpu(cpu->sysdev.id, write_##NAME, val); \
	return count; \
}

SYSFS_PMCSETUP(mmcr0, SPRN_MMCR0);
SYSFS_PMCSETUP(mmcr1, SPRN_MMCR1);
SYSFS_PMCSETUP(mmcra, SPRN_MMCRA);
SYSFS_PMCSETUP(pmc1, SPRN_PMC1);
SYSFS_PMCSETUP(pmc2, SPRN_PMC2);
SYSFS_PMCSETUP(pmc3, SPRN_PMC3);
SYSFS_PMCSETUP(pmc4, SPRN_PMC4);
SYSFS_PMCSETUP(pmc5, SPRN_PMC5);
SYSFS_PMCSETUP(pmc6, SPRN_PMC6);
SYSFS_PMCSETUP(pmc7, SPRN_PMC7);
SYSFS_PMCSETUP(pmc8, SPRN_PMC8);
SYSFS_PMCSETUP(purr, SPRN_PURR);

static SYSDEV_ATTR(mmcr0, 0600, show_mmcr0, store_mmcr0);
static SYSDEV_ATTR(mmcr1, 0600, show_mmcr1, store_mmcr1);
static SYSDEV_ATTR(mmcra, 0600, show_mmcra, store_mmcra);
static SYSDEV_ATTR(pmc1, 0600, show_pmc1, store_pmc1);
static SYSDEV_ATTR(pmc2, 0600, show_pmc2, store_pmc2);
static SYSDEV_ATTR(pmc3, 0600, show_pmc3, store_pmc3);
static SYSDEV_ATTR(pmc4, 0600, show_pmc4, store_pmc4);
static SYSDEV_ATTR(pmc5, 0600, show_pmc5, store_pmc5);
static SYSDEV_ATTR(pmc6, 0600, show_pmc6, store_pmc6);
static SYSDEV_ATTR(pmc7, 0600, show_pmc7, store_pmc7);
static SYSDEV_ATTR(pmc8, 0600, show_pmc8, store_pmc8);
static SYSDEV_ATTR(purr, 0600, show_purr, NULL);

static void __init register_cpu_pmc(struct sys_device *s)
{
	sysdev_create_file(s, &attr_mmcr0);
	sysdev_create_file(s, &attr_mmcr1);

	if (cur_cpu_spec->cpu_features & CPU_FTR_MMCRA)
		sysdev_create_file(s, &attr_mmcra);

	sysdev_create_file(s, &attr_pmc1);
	sysdev_create_file(s, &attr_pmc2);
	sysdev_create_file(s, &attr_pmc3);
	sysdev_create_file(s, &attr_pmc4);
	sysdev_create_file(s, &attr_pmc5);
	sysdev_create_file(s, &attr_pmc6);

	if (cur_cpu_spec->cpu_features & CPU_FTR_PMC8) {
		sysdev_create_file(s, &attr_pmc7);
		sysdev_create_file(s, &attr_pmc8);
	}

	if (cur_cpu_spec->cpu_features & CPU_FTR_SMT)
		sysdev_create_file(s, &attr_purr);
}


/* NUMA stuff */

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

		register_cpu_pmc(&c->sysdev);

		sysdev_create_file(&c->sysdev, &attr_physical_id);
	}

	return 0;
}
__initcall(topology_init);
