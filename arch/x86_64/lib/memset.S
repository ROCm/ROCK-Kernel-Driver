/* Copyright 2002 Andi Kleen, SuSE Labs */

/*
 * ISO C memset - set a memory block to a byte value.
 *	
 * rdi   destination
 * rsi   value (char) 
 * rdx   count (bytes) 
 * 
 * rax   original destination
 */	
 	.globl __memset
	.globl memset
	.p2align
memset:	
__memset:
	movq %rdi,%r10
	movq %rdx,%r11

	/* expand byte value  */
	movzbl %sil,%ecx
	movabs $0x0101010101010101,%rax
	mul    %rcx		/* with rax, clobbers rdx */

	/* align dst */
	movl  %edi,%r9d		
	andl  $7,%r9d	
	jnz  bad_alignment
after_bad_alignment:
	
	movq %r11,%rcx
	movl $64,%r8d
	shrq $6,%rcx
	jz	 handle_tail

loop_64:	
	movq  %rax,(%rdi) 
	movq  %rax,8(%rdi) 
	movq  %rax,16(%rdi) 
	movq  %rax,24(%rdi) 
	movq  %rax,32(%rdi) 
	movq  %rax,40(%rdi) 
	movq  %rax,48(%rdi) 
	movq  %rax,56(%rdi) 
	addq    %r8,%rdi
	decl   %ecx
	jnz    loop_64

	/* Handle tail in loops. The loops should be faster than hard
	   to predict jump tables. */ 
handle_tail:
	movl	%r11d,%ecx
	andl    $63&(~7),%ecx
	jz 	handle_7
	shrl	$3,%ecx
loop_8:
	movq  %rax,(%rdi) 
	addq    $8,%rdi
	decl   %ecx
	jnz    loop_8

handle_7:
	movl	%r11d,%ecx
	andl	$7,%ecx
	jz      ende
loop_1:
	movb 	%al,(%rdi)
	addq	$1,%rdi
	decl    %ecx
	jnz     loop_1
	
ende:	
	movq	%r10,%rax
	ret

bad_alignment:
	cmpq $7,%r11
	jbe	handle_7
	movq %rax,(%rdi)	/* unaligned store */
	movq $8,%r8			
	subq %r9,%r8 
	addq %r8,%rdi
	subq %r8,%r11
	jmp after_bad_alignment
