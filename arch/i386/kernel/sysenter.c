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

	wrmsr(MSR_IA32_SYSENTER_CS, __KERNEL_CS, 0);
	wrmsr(MSR_IA32_SYSENTER_ESP, tss->esp0, 0);
	wrmsr(MSR_IA32_SYSENTER_EIP, (unsigned long) sysenter_entry, 0);

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
		0x51,			/* push %ecx */
		0x52,			/* push %edx */
		0x55,			/* push %ebp */
	/* 3: backjump target */
		0x89, 0xe5,		/* movl %esp,%ebp */
		0x0f, 0x34,		/* sysenter */

	/* 7: align return point with nop's to make disassembly easier */
		0x90, 0x90, 0x90, 0x90,
		0x90, 0x90, 0x90,

	/* 14: System call restart point is here! (SYSENTER_RETURN - 2) */
		0xeb, 0xf3,		/* jmp to "movl %esp,%ebp" */
	/* 16: System call normal return point is here! (SYSENTER_RETURN in entry.S) */
		0x5d,			/* pop %ebp */
		0x5a,			/* pop %edx */
		0x59,			/* pop %ecx */
		0xc3			/* ret */
	};
	static const char sigreturn[] = {
	/* 32: sigreturn point */
		0x58,				/* popl %eax */
		0xb8, __NR_sigreturn, 0, 0, 0,	/* movl $__NR_sigreturn, %eax */
		0xcd, 0x80,			/* int $0x80 */
	};
	static const char rt_sigreturn[] = {
	/* 64: rt_sigreturn point */
		0xb8, __NR_rt_sigreturn, 0, 0, 0,	/* movl $__NR_rt_sigreturn, %eax */
		0xcd, 0x80,			/* int $0x80 */
	};
	unsigned long page = get_zeroed_page(GFP_ATOMIC);

	__set_fixmap(FIX_VSYSCALL, __pa(page), PAGE_READONLY);
	memcpy((void *) page, int80, sizeof(int80));
	memcpy((void *)(page + 32), sigreturn, sizeof(sigreturn));
	memcpy((void *)(page + 64), rt_sigreturn, sizeof(rt_sigreturn));
	if (!boot_cpu_has(X86_FEATURE_SEP))
		return 0;

	memcpy((void *) page, sysent, sizeof(sysent));
	on_each_cpu(enable_sep_cpu, NULL, 1, 1);
	return 0;
}

__initcall(sysenter_setup);
