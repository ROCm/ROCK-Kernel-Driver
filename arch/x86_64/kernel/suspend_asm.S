/* Originally gcc generated, modified by hand
 *
 * This may not use any stack, nor any variable that is not "NoSave":
 *
 * Its rewriting one kernel image with another. What is stack in "old"
 * image could very well be data page in "new" image, and overwriting
 * your own stack under you is bad idea.
 */
	
	.text
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page.h>

ENTRY(swsusp_arch_suspend)

	movq %rsp, saved_context_esp(%rip)
	movq %rax, saved_context_eax(%rip)
	movq %rbx, saved_context_ebx(%rip)
	movq %rcx, saved_context_ecx(%rip)
	movq %rdx, saved_context_edx(%rip)
	movq %rbp, saved_context_ebp(%rip)
	movq %rsi, saved_context_esi(%rip)
	movq %rdi, saved_context_edi(%rip)
	movq %r8,  saved_context_r08(%rip)
	movq %r9,  saved_context_r09(%rip)
	movq %r10, saved_context_r10(%rip)
	movq %r11, saved_context_r11(%rip)
	movq %r12, saved_context_r12(%rip)
	movq %r13, saved_context_r13(%rip)
	movq %r14, saved_context_r14(%rip)
	movq %r15, saved_context_r15(%rip)
	pushfq ; popq saved_context_eflags(%rip)

	call swsusp_save
	ret

ENTRY(swsusp_arch_resume)
	/* set up cr3 */	
	leaq	init_level4_pgt(%rip),%rax
	subq	$__START_KERNEL_map,%rax
	movq	%rax,%cr3

	movq	mmu_cr4_features(%rip), %rax
	movq	%rax, %rdx
	andq	$~(1<<7), %rdx	# PGE
	movq	%rdx, %cr4;  # turn off PGE
	movq	%cr3, %rcx;  # flush TLB
	movq	%rcx, %cr3;
	movq	%rax, %cr4;  # turn PGE back on

	movl	nr_copy_pages(%rip), %eax
	xorl	%ecx, %ecx
	movq	$0, %r10
	testl	%eax, %eax
	jz	done
.L105:
	xorl	%esi, %esi
	movq	$0, %r11
	jmp	.L104
	.p2align 4,,7
copy_one_page:
	movq	%r10, %rcx
.L104:
	movq	pagedir_nosave(%rip), %rdx
	movq	%rcx, %rax
	salq	$5, %rax
	movq	8(%rdx,%rax), %rcx
	movq	(%rdx,%rax), %rax
	movzbl	(%rsi,%rax), %eax
	movb	%al, (%rsi,%rcx)

	movq	%cr3, %rax;  # flush TLB
	movq	%rax, %cr3;

	movq	%r11, %rax
	incq	%rax
	cmpq	$4095, %rax
	movq	%rax, %rsi
	movq	%rax, %r11
	jbe	copy_one_page
	movq	%r10, %rax
	incq	%rax
	movq	%rax, %rcx
	movq	%rax, %r10
	mov	nr_copy_pages(%rip), %eax
	cmpq	%rax, %rcx
	jb	.L105
done:
	movl	$24, %eax
	movl	%eax, %ds

	movq saved_context_esp(%rip), %rsp
	movq saved_context_ebp(%rip), %rbp
	movq saved_context_eax(%rip), %rax
	movq saved_context_ebx(%rip), %rbx
	movq saved_context_ecx(%rip), %rcx
	movq saved_context_edx(%rip), %rdx
	movq saved_context_esi(%rip), %rsi
	movq saved_context_edi(%rip), %rdi
	movq saved_context_r08(%rip), %r8
	movq saved_context_r09(%rip), %r9
	movq saved_context_r10(%rip), %r10
	movq saved_context_r11(%rip), %r11
	movq saved_context_r12(%rip), %r12
	movq saved_context_r13(%rip), %r13
	movq saved_context_r14(%rip), %r14
	movq saved_context_r15(%rip), %r15
	pushq saved_context_eflags(%rip) ; popfq
	call	swsusp_restore
	ret
