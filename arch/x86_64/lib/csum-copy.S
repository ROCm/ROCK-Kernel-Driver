/*
 * Copyright 2002 Andi Kleen
 *	
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details. No warranty for anything given at all.
 */
 	#include <linux/linkage.h>
	#include <asm/errno.h>

//	#define FIX_ALIGNMENT 1
/*
 * Checksum copy with exception handling.
 * On exceptions src_err_ptr or dst_err_ptr is set to -EFAULT and the 
 * destination is zeroed.
 * 
 * Input
 * rdi  source
 * rsi  destination
 * edx  len (32bit)
 * ecx  sum (32bit) 
 * r8   src_err_ptr (int)
 * r9   dst_err_ptr (int)
 *
 * Output
 * eax  64bit sum. undefined in case of exception.
 * 
 * Wrappers need to take care of valid exception sum and zeroing.		 
 */

/* for now - should vary this based on direction */
 #define prefetch prefetcht2
 #define movnti   movq

	.macro source
10:
	.section __ex_table,"a"
	.align 8
	.quad 10b,bad_source
	.previous
	.endm
		
	.macro dest
20:
	.section __ex_table,"a"
	.align 8
	.quad 20b,bad_dest
	.previous
	.endm
			
	.globl csum_partial_copy_generic
	.p2align
csum_partial_copy_generic:
	prefetchnta (%rdi)
	
	pushq %rbx
	pushq %r12
	pushq %r14
	pushq %r15
	movq %r8,%r14
	movq %r9,%r15
	movl  %ecx,%eax
	movl  %edx,%ecx

#ifdef FIX_ALIGNMENT
	/* align source to 8 bytes */	
	movl %edi,%r8d
	andl $7,%r8d
	jnz  bad_alignment	
after_bad_alignment:
#endif

	movl  $64,%r10d
	xorl  %r9d,%r9d
	movq  %rcx,%r12

	shrq  $6,%r12
	/* loopcounter is maintained as one less to test efficiently for the
	   previous to last iteration. This is needed to stop the prefetching. */
	decq  %r12
	js    handle_tail       /* < 64 */
	jz    loop_no_prefetch  /* = 64 + X */ 
	
	/* main loop. clear in 64 byte blocks */
	/* tries hard not to prefetch over the boundary */
	/* r10:	64, r9: zero, r8: temp2, rbx: temp1, rax: sum, rcx: saved length */
	/* r11:	temp3, rdx: temp4, r12 loopcnt */
	.p2align
loop:
	/* Could prefetch more than one loop, but then it would be even
	   trickier to avoid prefetching over the boundary. The hardware prefetch
	   should take care of this anyways. The reason for this prefetch is
	   just the non temporal hint to avoid cache pollution. Hopefully this
	   will be handled properly by the hardware. */
	prefetchnta 64(%rdi) 
	
loop_no_prefetch:
	source
	movq  (%rdi),%rbx
	source
	movq  8(%rdi),%r8
	source
	movq  16(%rdi),%r11
	source
	movq  24(%rdi),%rdx

	dest
	movnti %rbx,(%rsi)
	dest
	movnti %r8,8(%rsi)
	dest
	movnti %r11,16(%rsi)
	dest
	movnti %rdx,24(%rsi)
		
	addq  %rbx,%rax
	adcq  %r8,%rax
	adcq  %r11,%rax
	adcq  %rdx,%rax

	source
	movq  32(%rdi),%rbx
	source
	movq  40(%rdi),%r8
	source
	movq  48(%rdi),%r11
	source
	movq  56(%rdi),%rdx
	
	dest
	movnti %rbx,32(%rsi)
	dest
	movnti %r8,40(%rsi)
	dest
	movnti %r11,48(%rsi)
	dest
	movnti %rdx,56(%rsi)

	adcq %rbx,%rax
	adcq %r8,%rax
	adcq %r11,%rax
	adcq %rdx,%rax
	
	adcq %r9,%rax	/* add in carry */
	
	addq %r10,%rdi		
	addq %r10,%rsi

	decq %r12
	jz   loop_no_prefetch	/* previous to last iteration? */
	jns  loop

	/* do last upto 56 bytes */
handle_tail:
	/* ecx:	count */
	movl %ecx,%r10d
	andl $63,%ecx
	shrl $3,%ecx
	jz 	 fold
	clc
	movl $8,%edx
loop_8:	
	source
	movq (%rdi),%rbx
	adcq %rbx,%rax
	dest
	movnti %rbx,(%rsi)
	leaq (%rsi,%rdx),%rsi /* preserve carry */
	leaq (%rdi,%rdx),%rdi
	decl %ecx
	jnz	loop_8
	adcq %r9,%rax	/* add in carry */

fold:
	movl %eax,%ebx
	shrq $32,%rax
	addq %rbx,%rax

	/* do last upto 6 bytes */	
handle_7:
	movl %r10d,%ecx
	andl $7,%ecx
	shrl $1,%ecx
	jz   handle_1
	movl $2,%edx
	xorl %ebx,%ebx
	clc  
loop_1:	
	source
	movw (%rdi),%bx
	adcq %rbx,%rax
	dest
	movw %bx,(%rsi)
	addq %rdx,%rdi
	addq %rdx,%rsi
	decl %ecx
	jnz loop_1
	adcw %r9w,%ax	/* add in carry */
	
	/* handle last odd byte */
handle_1:	
	testl $1,%r10d
	jz    ende
	xorl  %ebx,%ebx
	source
	movb (%rdi),%bl
	dest
	movb %bl,(%rsi)
	addw %bx,%ax
	adcw %r9w,%ax		/* carry */
			
ende:		
	sfence
	popq %r15
	popq %r14
	popq %r12
	popq %rbx
	ret

#ifdef FIX_ALIGNMENT
	/* align source to 8 bytes. */
	/* r8d:	unalignedness, ecx len */
bad_alignment:
	testl $1,%edi
	jnz   odd_source

	/* compute distance to next aligned position */
	movl $8,%r8d
	xchgl %r8d,%ecx
	subl %r8d,%ecx

	/* handle unaligned part */
	shrl $1,%ecx
	xorl %ebx,%ebx	
	movl $2,%r10d
align_loop:
	source
	movw (%rdi),%bx
	addq %rbx,%rax	/* carry cannot happen */
	dest
	movw %bx,(%rsi)
	addq %r10,%rdi
	addq %r10,%rsi
	decl %ecx
	jnz align_loop
	jmp after_bad_alignment

	/* weird case. need to swap the sum at the end because the spec requires
	   16 bit words of the sum to be always paired. 
	   handle it recursively because it should be rather rare. */
odd_source:
	/* copy odd byte */
	xorl %ebx,%ebx
	source
	movb (%rdi),%bl
	addl %ebx,%eax       /* add to old checksum */
	adcl $0,%ecx
	dest
	movb %al,(%rsi)
	
	/* fix arguments */
	movl %eax,%ecx
	incq %rsi
	incq %rdi
	decq %rdx 
	call csum_partial_copy_generic
	bswap %eax        /* this should work, but check */
	jmp ende
#endif

	/* Exception handlers. Very simple, zeroing is done in the wrappers */
bad_source:
	movl $-EFAULT,(%r14)
	jmp  ende
	
bad_dest:
	movl $-EFAULT,(%r15)
	jmp ende
