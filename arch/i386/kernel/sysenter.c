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
#include <linux/sched.h>
#include <linux/gfp.h>
#include <linux/string.h>
#include <linux/elf.h>

#include <asm/cpufeature.h>
#include <asm/msr.h>
#include <asm/pgtable.h>
#include <asm/unistd.h>

extern asmlinkage void sysenter_entry(void);

/*
 * Create a per-cpu fake "SEP thread" stack, so that we can
 * enter the kernel without having to worry about things like
 * "current" etc not working (debug traps and NMI's can happen
 * before we can switch over to the "real" thread).
 *
 * Return the resulting fake stack pointer.
 */
struct fake_sep_struct {
	struct thread_info thread;
	struct task_struct task;
	unsigned char trampoline[32] __attribute__((aligned(1024)));
	unsigned char stack[0];
} __attribute__((aligned(8192)));
	
void enable_sep_cpu(void *info)
{
	int cpu = get_cpu();
	struct tss_struct *tss = init_tss + cpu;

	tss->ss1 = __KERNEL_CS;
	tss->esp1 = sizeof(struct tss_struct) + (unsigned long) tss;
	wrmsr(MSR_IA32_SYSENTER_CS, __KERNEL_CS, 0);
	wrmsr(MSR_IA32_SYSENTER_ESP, tss->esp1, 0);
	wrmsr(MSR_IA32_SYSENTER_EIP, (unsigned long) sysenter_entry, 0);

	printk("Enabling SEP on CPU %d\n", cpu);
	put_cpu();	
}

/*
 * These symbols are defined by vsyscall.o to mark the bounds
 * of the ELF DSO images included therein.
 */
extern const char vsyscall_int80_start, vsyscall_int80_end;
extern const char vsyscall_sysenter_start, vsyscall_sysenter_end;

static int __init sysenter_setup(void)
{
	unsigned long page = get_zeroed_page(GFP_ATOMIC);

	__set_fixmap(FIX_VSYSCALL, __pa(page), PAGE_READONLY);

	if (!boot_cpu_has(X86_FEATURE_SEP)) {
		memcpy((void *) page,
		       &vsyscall_int80_start,
		       &vsyscall_int80_end - &vsyscall_int80_start);
		return 0;
	}

	memcpy((void *) page,
	       &vsyscall_sysenter_start,
	       &vsyscall_sysenter_end - &vsyscall_sysenter_start);

	on_each_cpu(enable_sep_cpu, NULL, 1, 1);
	return 0;
}

__initcall(sysenter_setup);
