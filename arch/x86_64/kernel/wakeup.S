/*
 * ACPI S3 entry/exit handling.
 *		
 * Notes:
 * Relies on kernel being loaded below 4GB.
 * Needs restore_low_mappings called before.
 *			
 * Copyright 2003 by Andi Kleen, SuSE Labs.
 *	
 * Long mode entry loosely based on example code in chapter 14 of the x86-64 system 
 * programmer's manual.
 * 
 * Notebook:	

 FIXME need to interface with suspend.c properly. do_magic. check i386. rename to suspend64.S
		
 Need to fix vgacon,mtrr,bluesmoke to do resume
 	
 Interrupts should be off until the io-apic code has reinited the APIC.
 Need support for that in the pm frame work or a special hack?

 SMP support is non existent. Need to somehow restart the other CPUs again.
 If CPU hotplug was working it could be used. Save/Restore needs to run on the same CPU.

 Should check magic like i386 code
	
 suspend code copies something. check what it is.		
 */
	
#include <linux/linkage.h>
	
#include <asm/msr.h>
#include <asm/segment.h>
#include <asm/page.h>
			
#define O(x) (x-acpi_wakeup)
		
	.text
	.code16
ENTRY(acpi_wakeup)
	/* 16bit real mode entered from ACPI BIOS */
	/* The machine is just through BIOS setup after power down and everything set up
	   by Linux needs to be restored. */
	/* The code here needs to be position independent or manually relocated,
	   because it is copied to a <1MB page for real mode execution */
	
	/* A20 enabled (according to ACPI spec) */
	/* cs = acpi_wakeup >> 4 ;  eip = acpi_wakeup & 0xF */
		
	movw %cs,%ax
	movw %ax,%ds	/* make %ds point to acpi_wakeup */
	movw %ax,%ss
	movw $O(wakeup_stack),%sp	/* setup stack */

	pushl $0
	popfl			/* clear EFLAGS */
	
	lgdt %ds:O(pGDT)	/* load kernel GDT */
	
	movl $0x1,%eax		/* enable protected mode */
	movl %eax,%cr0

	movl %ds:O(wakeup_page_table),%edi
	ljmpl  $__KERNEL16_CS,$0   /* -> s3_prot16 (filled in earlier by caller) */
	
	/* patched by s3_restore_state below */
pGDT:
	.short 0
	.quad  0

	.align 4
	.globl wakeup_page_table
wakeup_page_table:	
	.long 0	
	
	.align 8
wakeup_stack:
	.fill 128,1,0
	.globl acpi_wakeup_end
acpi_wakeup_end:
	/* end of real mode trampoline */
		
	/* pointed to by __KERNEL16_CS:0 */	
	.code16	
ENTRY(s3_prot16)
	/* Now in 16bit protected mode, still no paging, stack/data segments invalid */

	/* Prepare everything for 64bit paging, but still keep it turned off */
	movl %cr4,%eax
	bts  $5,%eax	/* set PAE bit */
	movl %eax,%cr4
	
	movl %edi,%cr3	/* load kernel page table */

	movl $0x80000001,%eax	
	cpuid		/* no execute supported ? */
	movl %edx,%esi	
	
	movl $MSR_EFER,%ecx
	rdmsr 
	bts $8,%eax	/* long mode */
	bt  $20,%esi	/* NX supported ? */
	jnc 1f
	bt  $_EFER_NX,%eax
1:	
	wrmsr		/* set temporary efer - real one is restored a bit later */
	
	movl %cr0,%eax
	bts  $31,%eax	/* paging */
	movl %eax,%cr0

	/* running in identity mapping now */

	/* go to 64bit code segment */
	ljmpl $__KERNEL_CS,$s3_restore_state-__START_KERNEL_map

	.code64			
	.macro	SAVEMSR msr,target
	movl	$\msr,%ecx
	rdmsr
	shlq	$32,%rdx
	orq	%rax,%rdx
	movq	%rdx,\target(%rip)
	.endm

	.macro RESTMSR msr,src
	movl	$\msr,%ecx
	movq	\src(%rip),%rax
	movq	%rax,%rdx
	shrq	$32,%rdx
	wrmsr
	.endm
	
	.macro	SAVECTL reg
	movq	%\reg,%rax
	movq	%rax,saved_\reg(%rip)
	.endm
	
	.macro	RESTCTL reg
	movq	saved_\reg(%rip),%rax
	movq	%rax,%\reg
	.endm

	/* Running in identity mapping, long mode */
s3_restore_state_low:
	movq	$s3_restore_state,%rax
	jmpq	*%rax	

	/* Running in real kernel mapping now */
s3_restore_state:
	xorl	%eax,%eax
	movl	%eax,%ds
	movq	saved_rsp(%rip),%rsp
	movw	saved_ss(%rip),%ss
	movw	saved_fs(%rip),%fs
	movw	saved_gs(%rip),%gs
	movw	saved_es(%rip),%es
	movw	saved_ds(%rip),%ds

	lidt	saved_idt
	ltr	saved_tr
	lldt	saved_ldt	
	/* gdt is already loaded */
	
	RESTCTL	cr0
	RESTCTL	cr4
	/* cr3 is already loaded */	
	
	RESTMSR MSR_EFER,saved_efer
	RESTMSR MSR_LSTAR,saved_lstar
	RESTMSR MSR_CSTAR,saved_cstar
	RESTMSR MSR_FS_BASE,saved_fs_base
	RESTMSR MSR_GS_BASE,saved_gs_base
	RESTMSR MSR_KERNEL_GS_BASE,saved_kernel_gs_base
	RESTMSR MSR_SYSCALL_MASK,saved_syscall_mask
				
	fxrstor	fpustate(%rip)

	RESTCTL	dr0
	RESTCTL	dr1
	RESTCTL	dr2
	RESTCTL	dr3
	RESTCTL	dr6
	RESTCTL	dr7

	movq	saved_rflags(%rip),%rax
	pushq	%rax
	popfq
								
	movq	saved_rbp(%rip),%rbp
	movq	saved_rbx(%rip),%rbx
	movq	saved_r12(%rip),%r12
	movq	saved_r13(%rip),%r13
	movq	saved_r14(%rip),%r14
	movq	saved_r15(%rip),%r15		
	ret
		
ENTRY(acpi_prepare_wakeup)
	sgdt	saved_gdt
	
	/* copy gdt descr and page table to low level wakeup code so that it can 
	   reload them early. */
	movq	acpi_wakeup_address(%rip),%rax	
	movw	saved_gdt+8(%rip),%cx
	movw	%cx,O(pGDT)+8(%rax)	
	movq	saved_gdt(%rip),%rcx
	movq	%rcx,O(pGDT)(%rax)
	
	movq	%cr3,%rdi
	movl	%edi,O(wakeup_page_table)(%rax)
	ret

	/* Save CPU state. */
	/* Everything saved here needs to be restored above. */
ENTRY(do_suspend_lowlevel)
	testl	%edi,%edi
	jnz	s3_restore_state
		
	SAVECTL	cr0
	SAVECTL	cr4
	SAVECTL	cr3
		
	str	saved_tr
	sidt	saved_idt		
	sgdt	saved_gdt
	sldt	saved_ldt
			
	SAVEMSR MSR_EFER,saved_efer
	SAVEMSR MSR_LSTAR,saved_lstar
	SAVEMSR MSR_CSTAR,saved_cstar
	SAVEMSR MSR_FS_BASE,saved_fs_base
	SAVEMSR MSR_GS_BASE,saved_gs_base
	SAVEMSR MSR_KERNEL_GS_BASE,saved_kernel_gs_base
	SAVEMSR MSR_SYSCALL_MASK,saved_syscall_mask

	movw	%ds,saved_ds(%rip)
	movw	%es,saved_es(%rip)
	movw	%fs,saved_fs(%rip)
	movw	%gs,saved_gs(%rip)
	movw	%ss,saved_ss(%rip)
	movq	%rsp,saved_rsp(%rip)
	
	pushfq
	popq	%rax
	movq	%rax,saved_rflags(%rip)

	SAVECTL  dr0
	SAVECTL  dr1
	SAVECTL  dr2
	SAVECTL  dr3
	SAVECTL  dr6
	SAVECTL  dr7
	
	fxsave	fpustate(%rip) 
	
	/* finally save callee saved registers */
	movq	%rbp,saved_rbp(%rip)
	movq	%rbx,saved_rbx(%rip)
	movq	%r12,saved_r12(%rip)
	movq	%r13,saved_r13(%rip)
	movq	%r14,saved_r14(%rip)
	movq	%r15,saved_r15(%rip)
	movq	$3,%rdi		
	call	acpi_enter_sleep_state
	ret	/* should not happen */

	.data
	.align 8
saved_efer:	.quad 0
saved_lstar:	.quad 0
saved_cstar:	.quad 0
saved_cr4:	.quad 0
saved_cr3:	.quad 0
saved_cr0:	.quad 0	
saved_rbp:	.quad 0
saved_rbx:	.quad 0
saved_rsp:	.quad 0
saved_r12:	.quad 0
saved_r13:	.quad 0
saved_r14:	.quad 0
saved_r15:	.quad 0
saved_rflags:	.quad 0
saved_gs_base:	.quad 0
saved_fs_base:	.quad 0
saved_kernel_gs_base:	.quad 0
saved_syscall_mask:	.quad 0
saved_dr0:	.quad 0	
saved_dr1:	.quad 0	
saved_dr2:	.quad 0	
saved_dr3:	.quad 0	
saved_dr6:	.quad 0	
saved_dr7:	.quad 0	
saved_ds:	.short 0
saved_fs:	.short 0
saved_gs:	.short 0
saved_es:	.short 0
saved_ss:	.short 0	
saved_idt:	.short 0
		.quad 0
saved_ldt:	.short 0
saved_gdt:	.short 0
		.quad 0		
saved_tr:	.short 0
			
		.align 16
fpustate:	.fill  512,1,0				
