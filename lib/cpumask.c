#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/cpumask.h>
#include <linux/module.h>

int __first_cpu(const cpumask_t *srcp)
{
	return min_t(int, NR_CPUS, find_first_bit(srcp->bits, NR_CPUS));
}
EXPORT_SYMBOL(__first_cpu);

int __next_cpu(int n, const cpumask_t *srcp)
{
	return min_t(int, NR_CPUS, find_next_bit(srcp->bits, NR_CPUS, n+1));
}
EXPORT_SYMBOL(__next_cpu);

int cpumask_next_and(int n, const cpumask_t *srcp, const cpumask_t *andp)
{
	while ((n = next_cpu_nr(n, *srcp)) < nr_cpu_ids)
		if (cpu_isset(n, *andp))
			break;
	return n;
}
EXPORT_SYMBOL(cpumask_next_and);

#if NR_CPUS > 64
int __next_cpu_nr(int n, const cpumask_t *srcp)
{
	return min_t(int, nr_cpu_ids,
				find_next_bit(srcp->bits, nr_cpu_ids, n+1));
}
EXPORT_SYMBOL(__next_cpu_nr);
#endif

int __any_online_cpu(const cpumask_t *mask)
{
	int cpu;

	for_each_cpu_mask(cpu, *mask) {
		if (cpu_online(cpu))
			break;
	}
	return cpu;
}
EXPORT_SYMBOL(__any_online_cpu);
