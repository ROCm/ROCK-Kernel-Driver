#include <linux/config.h>

#include <asm/system.h>

#ifdef CONFIG_IA64_GENERIC

#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/machvec.h>
#include <asm/page.h>

struct ia64_machine_vector ia64_mv;

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
	printk(KERN_INFO "booting generic kernel on platform %s\n", name);
}

#endif /* CONFIG_IA64_GENERIC */

void
machvec_noop (void)
{
}

void
machvec_memory_fence (void)
{
	mb();
}
