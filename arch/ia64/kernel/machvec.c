#include <linux/config.h>

#ifdef CONFIG_IA64_GENERIC

#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/page.h>
#include <asm/machvec.h>

struct ia64_machine_vector ia64_mv;

/*
 * Most platforms use this routine for mapping page frame addresses into a memory map
 * index.
 *
 * Note: we can't use __pa() because map_nr_dense(X) MUST map to something >= max_mapnr if
 * X is outside the identity mapped kernel space.
 */
unsigned long
map_nr_dense (unsigned long addr)
{
	return (addr - PAGE_OFFSET) >> PAGE_SHIFT;
}

static struct ia64_machine_vector *
lookup_machvec (const char *name)
{
	extern struct ia64_machine_vector machvec_start[];
	extern struct ia64_machine_vector machvec_end[];
	struct ia64_machine_vector *mv;

	for (mv = machvec_start; mv < machvec_end; ++mv)
		if (strcmp (mv->name, name) == 0)
			return mv;

	return 0;
}

void
machvec_init (const char *name)
{
	struct ia64_machine_vector *mv;

	mv = lookup_machvec(name);
	if (!mv) {
		panic("generic kernel failed to find machine vector for platform %s!", name);
	}
	ia64_mv = *mv;
	printk("booting generic kernel on platform %s\n", name);
}

#endif /* CONFIG_IA64_GENERIC */

void
machvec_noop (void)
{
}
