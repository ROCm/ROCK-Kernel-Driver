/*
 * linux/arch/i386/kernel/sysenter.c
 *
 * (C) Copyright 2002 Linus Torvalds
 *
 * This file contains the needed initializations to support sysenter.
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/thread_info.h>
#include <linux/gfp.h>
#include <linux/string.h>

#include <asm/cpufeature.h>
#include <asm/msr.h>
#include <asm/pgtable.h>

extern asmlinkage void sysenter_entry(void);

static void __init enable_sep_cpu(void *info)
{
	unsigned long page = __get_free_page(GFP_ATOMIC);
	int cpu = get_cpu();
	unsigned long *esp0_ptr = &(init_tss + cpu)->esp0;
	unsigned long rel32;

	rel32 = (unsigned long) sysenter_entry - (page+11);

	
	*(short *) (page+0) = 0x258b;		/* movl xxxxx,%esp */
	*(long **) (page+2) = esp0_ptr;
	*(char *)  (page+6) = 0xe9;		/* jmp rl32 */
	*(long *)  (page+7) = rel32;

	wrmsr(0x174, __KERNEL_CS, 0);		/* SYSENTER_CS_MSR */
	wrmsr(0x175, page+PAGE_SIZE, 0);	/* SYSENTER_ESP_MSR */
	wrmsr(0x176, page, 0);			/* SYSENTER_EIP_MSR */

	printk("Enabling SEP on CPU %d\n", cpu);
	put_cpu();	
}

static int __init sysenter_setup(void)
{
	static const char int80[] = {
		0xcd, 0x80,		/* int $0x80 */
		0xc3			/* ret */
	};
	static const char sysent[] = {
		0x55,			/* push %ebp */
		0x51,			/* push %ecx */
		0x52,			/* push %edx */
		0x89, 0xe5,		/* movl %esp,%ebp */
		0x0f, 0x34,		/* sysenter */
		0x5a,			/* pop %edx */
		0x59,			/* pop %ecx */
		0x5d,			/* pop %ebp */
		0xc3			/* ret */
	};
	unsigned long page = get_zeroed_page(GFP_ATOMIC);

	__set_fixmap(FIX_VSYSCALL, __pa(page), PAGE_READONLY);
	memcpy((void *) page, int80, sizeof(int80));
	if (!boot_cpu_has(X86_FEATURE_SEP))
		return 0;

	memcpy((void *) page, sysent, sizeof(sysent));
	enable_sep_cpu(NULL);
	smp_call_function(enable_sep_cpu, NULL, 1, 1);
	return 0;
}

__initcall(sysenter_setup);
