/*
 *  include/asm-x86_64/bugs.h
 *
 *  Copyright (C) 1994  Linus Torvalds
 *  Copyright (C) 2000  SuSE
 *
 * This is included by init/main.c to check for architecture-dependent bugs.
 *
 * Needs:
 *	void check_bugs(void);
 */

#include <linux/config.h>
#include <asm/processor.h>
#include <asm/i387.h>

static inline void check_fpu(void)
{
	extern void __bad_fxsave_alignment(void);
	if (offsetof(struct task_struct, thread.i387.fxsave) & 15)
		__bad_fxsave_alignment();
	printk(KERN_INFO "Enabling fast FPU save and restore... ");
	set_in_cr4(X86_CR4_OSFXSR);
	printk("done.\n");
	printk(KERN_INFO "Enabling unmasked SIMD FPU exception support... ");
	set_in_cr4(X86_CR4_OSXMMEXCPT);
	printk("done.\n");
}

/*
 * If we configured ourselves for FXSR, we'd better have it.
 */

static void __init check_bugs(void)
{
	identify_cpu(&boot_cpu_data);
	check_fpu();
#if !defined(CONFIG_SMP)
	printk("CPU: ");
	print_cpu_info(&boot_cpu_data);
#endif
}
