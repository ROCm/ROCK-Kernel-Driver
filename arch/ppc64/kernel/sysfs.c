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
#include <asm/prom.h>


/* SMT stuff */

#ifndef CONFIG_PPC_ISERIES

/* default to snooze disabled */
DEFINE_PER_CPU(unsigned long, smt_snooze_delay);

static ssize_t store_smt_snooze_delay(struct sys_device *dev, const char *buf,
				      size_t count)
{
	struct cpu *cpu = container_of(dev, struct cpu, sysdev);
	ssize_t ret;
	unsigned long snooze;

	ret = sscanf(buf, "%lu", &snooze);
	if (ret != 1)
		return -EINVAL;

	per_cpu(smt_snooze_delay, cpu->sysdev.id) = snooze;

	return count;
}

static ssize_t show_smt_snooze_delay(struct sys_device *dev, char *buf)
{
	struct cpu *cpu = container_of(dev, struct cpu, sysdev);

	return sprintf(buf, "%lu\n", per_cpu(smt_snooze_delay, cpu->sysdev.id));
}

static SYSDEV_ATTR(smt_snooze_delay, 0644, show_smt_snooze_delay,
		   store_smt_snooze_delay);

/* Only parse OF options if the matching cmdline option was not specified */
static int smt_snooze_cmdline;

static int __init smt_setup(void)
{
	struct device_node *options;
	unsigned int *val;
	unsigned int cpu;

	if (!cur_cpu_spec->cpu_features & CPU_FTR_SMT)
		return 1;

	options = find_path_device("/options");
	if (!options)
		return 1;

	val = (unsigned int *)get_property(options, "ibm,smt-snooze-delay",
					   NULL);
	if (!smt_snooze_cmdline && val) {
		for_each_cpu(cpu)
			per_cpu(smt_snooze_delay, cpu) = *val;
	}

	return 1;
}
__initcall(smt_setup);

static int __init setup_smt_snooze_delay(char *str)
{
	unsigned int cpu;
	int snooze;

	if (!cur_cpu_spec->cpu_features & CPU_FTR_SMT)
		return 1;

	smt_snooze_cmdline = 1;

	if (get_option(&str, &snooze)) {
		for_each_cpu(cpu)
			per_cpu(smt_snooze_delay, cpu) = snooze;
	}

	return 1;
}
__setup("smt-snooze-delay=", setup_smt_snooze_delay);

#endif


/* PMC stuff */

/*
 * Enabling PMCs will slow partition context switch times so we only do
 * it the first time we write to the PMCs.
 */

static DEFINE_PER_CPU(char, pmcs_enabled);

#ifdef CONFIG_PPC_ISERIES
void ppc64_enable_pmcs(void)
{
	/* XXX Implement for iseries */
}
#else
void ppc64_enable_pmcs(void)
{
	unsigned long hid0;
	unsigned long set, reset;
	int ret;
	unsigned int ctrl;

	/* Only need to enable them once */
	if (__get_cpu_var(pmcs_enabled))
		return;

	__get_cpu_var(pmcs_enabled) = 1;

	switch (systemcfg->platform) {
		case PLATFORM_PSERIES:
			hid0 = mfspr(HID0);
			hid0 |= 1UL << (63 - 20);

			/* POWER4 requires the following sequence */
			asm volatile(
				"sync\n"
				"mtspr	%1, %0\n"
				"mfspr	%0, %1\n"
				"mfspr	%0, %1\n"
				"mfspr	%0, %1\n"
				"mfspr	%0, %1\n"
				"mfspr	%0, %1\n"
				"mfspr	%0, %1\n"
				"isync" : "=&r" (hid0) : "i" (HID0), "0" (hid0):
				"memory");
			break;

		case PLATFORM_PSERIES_LPAR:
			set = 1UL << 63;
			reset = 0;
			ret = plpar_hcall_norets(H_PERFMON, set, reset);
			if (ret)
				printk(KERN_ERR "H_PERFMON call returned %d",
				       ret);
			break;

		default:
			break;
	}

	/* instruct hypervisor to maintain PMCs */
	if (cur_cpu_spec->firmware_features & FW_FEATURE_SPLPAR) {
		char *ptr = (char *)&paca[smp_processor_id()].xLpPaca;
		ptr[0xBB] = 1;
	}

	/*
	 * On SMT machines we have to set the run latch in the ctrl register
	 * in order to make PMC6 spin.
	 */
	if (cur_cpu_spec->cpu_features & CPU_FTR_SMT) {
		ctrl = mfspr(CTRLF);
		ctrl |= RUNLATCH;
		mtspr(CTRLT, ctrl);
	}
}
#endif

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
	ppc64_enable_pmcs(); \
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

#ifndef CONFIG_PPC_ISERIES
		if (cur_cpu_spec->cpu_features & CPU_FTR_SMT)
			sysdev_create_file(&c->sysdev, &attr_smt_snooze_delay);
#endif
	}

	return 0;
}
__initcall(topology_init);
