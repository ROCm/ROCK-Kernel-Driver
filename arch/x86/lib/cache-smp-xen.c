#include <linux/smp.h>
#include <linux/module.h>
#include <asm/hypervisor.h>

static void __wbinvd(void *dummy)
{
	wbinvd();
}

#ifndef CONFIG_XEN
void wbinvd_on_cpu(int cpu)
{
	smp_call_function_single(cpu, __wbinvd, NULL, 1);
}
EXPORT_SYMBOL(wbinvd_on_cpu);
#endif

int wbinvd_on_all_cpus(void)
{
	struct mmuext_op op = { .cmd = MMUEXT_FLUSH_CACHE_GLOBAL };

	if (HYPERVISOR_mmuext_op(&op, 1, NULL, DOMID_SELF) == 0)
		return 0;
	/* Best effort as fallback. */
	return on_each_cpu(__wbinvd, NULL, 1);
}
EXPORT_SYMBOL(wbinvd_on_all_cpus);
