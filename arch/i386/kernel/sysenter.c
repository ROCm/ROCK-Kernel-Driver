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

static int __init sysenter_setup(void)
{
	static const char __initdata int80[] = {
		0xcd, 0x80,		/* int $0x80 */
		0xc3			/* ret */
	};
	/* Unwind information for the int80 code.  Keep track of
	   where the return address is stored.  */
	static const char __initdata int80_eh_frame[] = {
	/* First the Common Information Entry (CIE):  */
		0x14, 0x00, 0x00, 0x00,	/* Length of the CIE */
		0x00, 0x00, 0x00, 0x00,	/* CIE Identifier Tag */
		0x01,			/* CIE Version */
		'z', 'R', 0x00,		/* CIE Augmentation */
		0x01,			/* CIE Code Alignment Factor */
		0x7c,			/* CIE Data Alignment Factor */
		0x08,			/* CIE RA Column */
		0x01,			/* Augmentation size */
		0x1b,			/* FDE Encoding (pcrel sdata4) */
		0x0c,			/* DW_CFA_def_cfa */
		0x04,
		0x04,
		0x88,			/* DW_CFA_offset, column 0x8 */
		0x01,
		0x00,			/* padding */
		0x00,
	/* Now the FDE which contains the instructions for the frame.  */
		0x0a, 0x00, 0x00, 0x00,	/* FDE Length */
		0x1c, 0x00, 0x00, 0x00,	/* FDE CIE offset */
	/* The PC-relative offset to the beginning of the code this
	   FDE covers.  The computation below assumes that the offset
	   can be represented in one byte.  Change if this is not true
	   anymore.  The offset from the beginning of the .eh_frame
	   is represented by EH_FRAME_OFFSET.  The word with the offset
	   starts at byte 0x20 of the .eh_frame.  */
		0x100 - (EH_FRAME_OFFSET + 0x20),
		0xff, 0xff, 0xff,	/* FDE initial location */
		3,			/* FDE address range */
		0x00			/* Augmentation size */
	/* The code does not change the stack pointer.  We need not
	   record any operations.  */
	};
	static const char __initdata sysent[] = {
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
	/* Unwind information for the sysenter code.  Keep track of
	   where the return address is stored.  */
	static const char __initdata sysent_eh_frame[] = {
	/* First the Common Information Entry (CIE):  */
		0x14, 0x00, 0x00, 0x00,	/* Length of the CIE */
		0x00, 0x00, 0x00, 0x00,	/* CIE Identifier Tag */
		0x01,			/* CIE Version */
		'z', 'R', 0x00,		/* CIE Augmentation */
		0x01,			/* CIE Code Alignment Factor */
		0x7c,			/* CIE Data Alignment Factor */
		0x08,			/* CIE RA Column */
		0x01,			/* Augmentation size */
		0x1b,			/* FDE Encoding (pcrel sdata4) */
		0x0c,			/* DW_CFA_def_cfa */
		0x04,
		0x04,
		0x88,			/* DW_CFA_offset, column 0x8 */
		0x01,
		0x00,			/* padding */
		0x00,
	/* Now the FDE which contains the instructions for the frame.  */
		0x22, 0x00, 0x00, 0x00,	/* FDE Length */
		0x1c, 0x00, 0x00, 0x00,	/* FDE CIE offset */
	/* The PC-relative offset to the beginning of the code this
	   FDE covers.  The computation below assumes that the offset
	   can be represented in one byte.  Change if this is not true
	   anymore.  The offset from the beginning of the .eh_frame
	   is represented by EH_FRAME_OFFSET.  The word with the offset
	   starts at byte 0x20 of the .eh_frame.  */
		0x100 - (EH_FRAME_OFFSET + 0x20),
		0xff, 0xff, 0xff,	/* FDE initial location */
		0x14, 0x00, 0x00, 0x00,	/* FDE address range */
		0x00,			/* Augmentation size */
	/* What follows are the instructions for the table generation.
	   We have to record all changes of the stack pointer and
	   callee-saved registers.  */
		0x41,			/* DW_CFA_advance_loc+1, push %ecx */
		0x0e,			/* DW_CFA_def_cfa_offset */
		0x08,			/* RA at offset 8 now */
		0x41,			/* DW_CFA_advance_loc+1, push %edx */
		0x0e,			/* DW_CFA_def_cfa_offset */
		0x0c,			/* RA at offset 12 now */
		0x41,			/* DW_CFA_advance_loc+1, push %ebp */
		0x0e,			/* DW_CFA_def_cfa_offset */
		0x10,			/* RA at offset 16 now */
		0x85, 0x04,		/* DW_CFA_offset %ebp -16 */
	/* Finally the epilogue.  */
		0x4e,			/* DW_CFA_advance_loc+14, pop %ebx */
		0x0e,			/* DW_CFA_def_cfa_offset */
		0x12,			/* RA at offset 12 now */
		0xc5,			/* DW_CFA_restore %ebp */
		0x41,			/* DW_CFA_advance_loc+1, pop %edx */
		0x0e,			/* DW_CFA_def_cfa_offset */
		0x08,			/* RA at offset 8 now */
		0x41,			/* DW_CFA_advance_loc+1, pop %ecx */
		0x0e,			/* DW_CFA_def_cfa_offset */
		0x04			/* RA at offset 4 now */
	};
	static const char __initdata sigreturn[] = {
	/* 32: sigreturn point */
		0x58,				/* popl %eax */
		0xb8, __NR_sigreturn, 0, 0, 0,	/* movl $__NR_sigreturn, %eax */
		0xcd, 0x80,			/* int $0x80 */
	};
	static const char __initdata rt_sigreturn[] = {
	/* 64: rt_sigreturn point */
		0xb8, __NR_rt_sigreturn, 0, 0, 0,	/* movl $__NR_rt_sigreturn, %eax */
		0xcd, 0x80,			/* int $0x80 */
	};
	unsigned long page = get_zeroed_page(GFP_ATOMIC);

	__set_fixmap(FIX_VSYSCALL, __pa(page), PAGE_READONLY);
	memcpy((void *) page, int80, sizeof(int80));
	memcpy((void *)(page + 32), sigreturn, sizeof(sigreturn));
	memcpy((void *)(page + 64), rt_sigreturn, sizeof(rt_sigreturn));
	memcpy((void *)(page + EH_FRAME_OFFSET), int80_eh_frame,
	       sizeof(int80_eh_frame));
	if (!boot_cpu_has(X86_FEATURE_SEP))
		return 0;

	memcpy((void *) page, sysent, sizeof(sysent));
	memcpy((void *)(page + EH_FRAME_OFFSET), sysent_eh_frame,
	       sizeof(sysent_eh_frame));
	on_each_cpu(enable_sep_cpu, NULL, 1, 1);
	return 0;
}

__initcall(sysenter_setup);
