
#include <linux/percpu_counter.h>

void percpu_counter_mod(struct percpu_counter *fbc, long amount)
{
	int cpu = get_cpu();
	long count = fbc->counters[cpu].count;

	count += amount;
	if (count >= FBC_BATCH || count <= -FBC_BATCH) {
		spin_lock(&fbc->lock);
		fbc->count += count;
		spin_unlock(&fbc->lock);
		count = 0;
	}
	fbc->counters[cpu].count = count;
	put_cpu();
}
