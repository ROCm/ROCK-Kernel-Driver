#include <linux/init.h>
#include <linux/sched.h>

/* Don't do any NUMA setup on Opteron right now. They seem to be
   better off with flat scheduling. This is just for SMT. */

#ifdef CONFIG_SCHED_SMT

static struct sched_group sched_group_cpus[NR_CPUS];
static struct sched_group sched_group_phys[NR_CPUS];
static DEFINE_PER_CPU(struct sched_domain, cpu_domains);
static DEFINE_PER_CPU(struct sched_domain, phys_domains);
__init void arch_init_sched_domains(void)
{
	int i;
	struct sched_group *first = NULL, *last = NULL;

	/* Set up domains */
	for_each_cpu(i) {
		struct sched_domain *cpu_domain = &per_cpu(cpu_domains, i);
		struct sched_domain *phys_domain = &per_cpu(phys_domains, i);

		*cpu_domain = SD_SIBLING_INIT;
		cpu_domain->span = cpu_sibling_map[i];
		cpu_domain->parent = phys_domain;
		cpu_domain->groups = &sched_group_cpus[i];

		*phys_domain = SD_CPU_INIT;
		phys_domain->span = cpu_possible_map;
		phys_domain->groups = &sched_group_phys[first_cpu(cpu_domain->span)];
	}

	/* Set up CPU (sibling) groups */
	for_each_cpu(i) {
		struct sched_domain *cpu_domain = &per_cpu(cpu_domains, i);
		int j;
		first = last = NULL;

		if (i != first_cpu(cpu_domain->span))
			continue;

		for_each_cpu_mask(j, cpu_domain->span) {
			struct sched_group *cpu = &sched_group_cpus[j];

			cpus_clear(cpu->cpumask);
			cpu_set(j, cpu->cpumask);
			cpu->cpu_power = SCHED_LOAD_SCALE;

			if (!first)
				first = cpu;
			if (last)
				last->next = cpu;
			last = cpu;
		}
		last->next = first;
	}

	first = last = NULL;
	/* Set up physical groups */
	for_each_cpu(i) {
		struct sched_domain *cpu_domain = &per_cpu(cpu_domains, i);
		struct sched_group *cpu = &sched_group_phys[i];

		if (i != first_cpu(cpu_domain->span))
			continue;

		cpu->cpumask = cpu_domain->span;
		/*
		 * Make each extra sibling increase power by 10% of
		 * the basic CPU. This is very arbitrary.
		 */
		cpu->cpu_power = SCHED_LOAD_SCALE + SCHED_LOAD_SCALE*(cpus_weight(cpu->cpumask)-1) / 10;

		if (!first)
			first = cpu;
		if (last)
			last->next = cpu;
		last = cpu;
	}
	last->next = first;

	mb();
	for_each_cpu(i) {
		struct sched_domain *cpu_domain = &per_cpu(cpu_domains, i);
		cpu_attach_domain(cpu_domain, i);
	}
}

#endif
