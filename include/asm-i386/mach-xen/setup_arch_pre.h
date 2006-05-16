/* Hook to call BIOS initialisation function */

#define ARCH_SETUP machine_specific_arch_setup();

static void __init machine_specific_arch_setup(void);

static inline char * machine_specific_memory_setup(void)
{
	return "Xen";
}

