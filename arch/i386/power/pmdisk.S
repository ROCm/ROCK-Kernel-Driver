/* Originally gcc generated, modified by hand */

#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page.h>

	.text

ENTRY(pmdisk_arch_suspend)
	cmpl $0,4(%esp)
	jne .L1450

	movl %esp, saved_context_esp
	movl %ebx, saved_context_ebx
	movl %ebp, saved_context_ebp
	movl %esi, saved_context_esi
	movl %edi, saved_context_edi
	pushfl ; popl saved_context_eflags

	call pmdisk_suspend
	jmp .L1449
	.p2align 4,,7
.L1450:
	movl $swsusp_pg_dir-__PAGE_OFFSET,%ecx
	movl %ecx,%cr3

	movl	pm_pagedir_nosave,%ebx
	xorl	%eax, %eax
	xorl	%edx, %edx
	.p2align 4,,7
.L1455:
	movl	4(%ebx,%edx),%edi
	movl	(%ebx,%edx),%esi

	movl	$1024, %ecx
	rep
	movsl

	movl	%cr3, %ecx;
	movl	%ecx, %cr3;  # flush TLB 

	incl	%eax
	addl	$16, %edx
	cmpl	pmdisk_pages,%eax
	jb .L1455
	.p2align 4,,7
.L1453:
	movl saved_context_esp, %esp
	movl saved_context_ebp, %ebp
	movl saved_context_ebx, %ebx
	movl saved_context_esi, %esi
	movl saved_context_edi, %edi
	pushl saved_context_eflags ; popfl
	call pmdisk_resume
.L1449:
	ret
