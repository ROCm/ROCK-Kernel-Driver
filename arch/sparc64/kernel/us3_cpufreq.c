/* us3_cpufreq.c: UltraSPARC-III cpu frequency support
 *
 * Copyright (C) 2003 David S. Miller (davem@redhat.com)
 *
 * Many thanks to Dominik Brodowski for fixing up the cpufreq
 * infrastructure in order to make this driver easier to implement.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/cpufreq.h>
#include <linux/threads.h>
#include <linux/slab.h>
#include <linux/init.h>

static struct cpufreq_driver *cpufreq_us3_driver;

/* Index by [(CPU * 4) + INDEX] (first three indices are
 * actual us3 divisor entries, last is for CPUFREQ_TABLE_END)
 */
static struct cpufreq_frequency_table *us3_freq_table;

/* UltraSPARC-III has three dividers: 1, 2, and 32.  These are controlled
 * in the Safari config register.
 */
#define SAFARI_CFG_DIV_1	0x0000000000000000UL
#define SAFARI_CFG_DIV_2	0x0000000040000000UL
#define SAFARI_CFG_DIV_32	0x0000000080000000UL
#define SAFARI_CFG_DIV_MASK	0x00000000C0000000UL

static unsigned long read_safari_cfg(void)
{
	unsigned long ret;

	__asm__ __volatile__("ldxa	[%%g0] %1, %0"
			     : "=&r" (ret)
			     : "i" (ASI_SAFARI_CONFIG));
	return ret;
}

static void write_safari_cfg(unsigned long val)
{
	__asm__ __volatile__("stxa	%0, [%%g0] %1\n\t"
			     "membar	#Sync"
			     : /* no outputs */
			     : "r" (val), "i" (ASI_SAFARI_CONFIG)
			     : "memory");
}

#ifndef CONFIG_SMP
extern unsigned long up_clock_tick;
#endif

static __inline__ unsigned long get_clock_tick(unsigned int cpu)
{
#ifdef CONFIG_SMP
	return cpu_data[cpu].clock_tick;
#else
	return up_clock_tick;
#endif
}

static unsigned long get_current_freq(unsigned int cpu, unsigned long safari_cfg)
{
	unsigned long clock_tick = get_clock_tick(cpu);
	unsigned long ret;

	switch (safari_cfg & SAFARI_CFG_DIV_MASK) {
	case SAFARI_CFG_DIV_1:
		ret = clock_tick / 1;
		break;
	case SAFARI_CFG_DIV_2:
		ret = clock_tick / 2;
		break;
	case SAFARI_CFG_DIV_32:
		ret = clock_tick / 32;
		break;
	default:
		BUG();
	};

	return ret;
}

static void us3_set_cpu_divider_index(unsigned int cpu, unsigned int index)
{
	unsigned long new_bits, new_freq, reg, cpus_allowed;
	struct cpufreq_freqs freqs;

	if (!cpu_online(cpu))
		return;

	cpus_allowed = current->cpus_allowed;
	set_cpus_allowed(current, (1UL << cpu));

	new_freq = get_clock_tick(cpu);
	switch (index) {
	case 0:
		new_bits = SAFARI_CFG_DIV_1;
		new_freq /= 1;
		break;
	case 1:
		new_bits = SAFARI_CFG_DIV_2;
		new_freq /= 2;
		break;
	case 2:
		new_bits = SAFARI_CFG_DIV_32;
		new_freq /= 32;
		break;

	default:
		BUG();
	};

	reg = read_safari_cfg();

	freqs.old = get_current_freq(cpu, reg);
	freqs.new = new_freq;
	freqs.cpu = cpu;
	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	reg &= ~SAFARI_CFG_DIV_MASK;
	reg |= new_bits;
	write_safari_cfg(reg);

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	set_cpus_allowed(current, cpus_allowed);
}

static int us3_setpolicy(struct cpufreq_policy *policy)
{
	unsigned int new_index = 0;

	if (cpufreq_frequency_table_setpolicy(policy,
					      &us3_freq_table[(policy->cpu * 4) + 0],
					      &new_index))
		return -EINVAL;

	us3_set_cpu_divider_index(policy->cpu, new_index);

	return 0;
}

static int us3_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy,
					      &us3_freq_table[(policy->cpu * 4) + 0]);
}

#ifndef CONFIG_SMP
extern unsigned long up_clock_tick;
#endif

static void __init us3_init_freq_table(unsigned int cpu)
{
	unsigned long clock_tick = get_clock_tick(cpu);

	us3_freq_table[(cpu * 4) + 0].index = 0;
	us3_freq_table[(cpu * 4) + 0].frequency = clock_tick / 1;
	us3_freq_table[(cpu * 4) + 1].index = 1;
	us3_freq_table[(cpu * 4) + 1].frequency = clock_tick / 2;
	us3_freq_table[(cpu * 4) + 2].index = 2;
	us3_freq_table[(cpu * 4) + 2].frequency = clock_tick / 32;
	us3_freq_table[(cpu * 4) + 3].index = 0;
	us3_freq_table[(cpu * 4) + 3].frequency = CPUFREQ_TABLE_END;
}

static int __init us3freq_init(void)
{
	struct cpufreq_driver *driver;
	unsigned long manuf, impl, ver;
	int i, ret;

	__asm__("rdpr %%ver, %0" : "=r" (ver));
	manuf = ((ver >> 48) & 0xffff);
	impl  = ((ver >> 32) & 0xffff);

	/* XXX Maybe accept cheetah+ too? */
	if (manuf == 0x3e && impl == 0x14) {
		driver = kmalloc(sizeof(struct cpufreq_driver) +
				 (NR_CPUS * sizeof(struct cpufreq_policy)),
				 GFP_KERNEL);
		if (!driver)
			return -ENOMEM;
		us3_freq_table = kmalloc(
			(NR_CPUS * 4 * sizeof(struct cpufreq_frequency_table)),
			GFP_KERNEL);
		if (!us3_freq_table) {
			kfree(driver);
			return -ENOMEM;
		}

		driver->policy = (struct cpufreq_policy *) (driver + 1);
		driver->verify = us3_verify;
		driver->setpolicy = us3_setpolicy;

		for (i = 0; i < NR_CPUS; i++) {
			driver->policy[i].cpu = i;
			us3_init_freq_table(i);
			ret = cpufreq_frequency_table_cpuinfo(&driver->policy[i],
				&us3_freq_table[(i * 4) + 0]);
			if (ret) {
				kfree(driver);
				kfree(us3_freq_table);
				us3_freq_table = NULL;
				return ret;
			}
			driver->policy[i].policy = CPUFREQ_POLICY_PERFORMANCE;
			us3_set_cpu_divider_index(i, 0);
		}
		cpufreq_us3_driver = driver;
		ret = cpufreq_register_driver(driver);
		if (ret) {
			kfree(driver);
			cpufreq_us3_driver = NULL;
			kfree(us3_freq_table);
			us3_freq_table = NULL;
			return ret;
		}
		return 0;
	}

	return -ENODEV;
}

static void __exit us3freq_exit(void)
{
	int i;

	if (cpufreq_us3_driver) {
		for (i = 0; i < NR_CPUS; i++)
			us3_set_cpu_divider_index(i, 0);
		cpufreq_unregister_driver(cpufreq_us3_driver);

		kfree(cpufreq_us3_driver);
		cpufreq_us3_driver = NULL;
		kfree(us3_freq_table);
		us3_freq_table = NULL;
	}
}

MODULE_AUTHOR("David S. Miller <davem@redhat.com>");
MODULE_DESCRIPTION("cpufreq driver for UltraSPARC-III");
MODULE_LICENSE("GPL");

module_init(us3freq_init);
module_exit(us3freq_exit);
