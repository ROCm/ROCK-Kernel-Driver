/* Copyright 2002 Andi Kleen, SuSE Labs */

	// #define FIX_ALIGNMENT 1

/*
 * ISO C memset - set a memory block to a byte value.
 *	
 * rdi   destination
 * rsi   value (char) 
 * rdx   count (bytes) 
 * 
 * rax   original destination
 */	
	.globl ____memset
	.p2align
____memset:
	movq %rdi,%r10		/* save destination for return address */
	movq %rdx,%r11		/* save count */ 

	/* expand byte value  */
	movzbl %sil,%ecx	/* zero extend char value */
	movabs $0x0101010101010101,%rax		/* expansion pattern */
	mul    %rcx		/* expand with rax, clobbers rdx */

#ifdef FIX_ALIGNMENT
	/* align dst */
	movl  %edi,%r9d		
	andl  $7,%r9d		/* test unaligned bits */
	jnz  bad_alignment
after_bad_alignment:
#endif
	
	movq %r11,%rcx		/* restore count */
	shrq $6,%rcx		/* divide by 64 */
	jz	 handle_tail	/* block smaller than 64 bytes? */
	movl $64,%r8d		/* CSE loop block size */

loop_64:	
	movnti  %rax,0*8(%rdi) 
	movnti  %rax,1*8(%rdi) 
	movnti  %rax,2*8(%rdi) 
	movnti  %rax,3*8(%rdi) 
	movnti  %rax,4*8(%rdi) 
	movnti  %rax,5*8(%rdi) 
	movnti  %rax,6*8(%rdi) 
	movnti  %rax,7*8(%rdi) 	/* clear 64 byte blocks */
	addq    %r8,%rdi	/* increase pointer by 64 bytes */
	loop	loop_64		/* decrement rcx and if not zero loop */

	/* Handle tail in loops. The loops should be faster than hard
	   to predict jump tables. */ 
handle_tail:
	movl	%r11d,%ecx
	andl    $63,%ecx
	shrl	$3,%ecx
	jz 	handle_7
loop_8:
	movnti  %rax,(%rdi) 	/* long words */
	addq    $8,%rdi
	loop 	loop_8   

handle_7:
	movl	%r11d,%ecx
	andl	$7,%ecx
	jz      ende
loop_1:
	movb 	%al,(%rdi)	/* bytes */
	incq	%rdi
	loop	loop_1
	
ende:	
	movq	%r10,%rax
	ret

#ifdef FIX_ALIGNMENT
bad_alignment:
	andq $-8,%r11		/* shorter than 8 bytes */
	jz      handle_7	/* if yes handle it in the tail code */
	movnti %rax,(%rdi)	/* unaligned store of 8 bytes */
	movq $8,%r8			
	subq %r9,%r8 		/* compute alignment (8-misalignment) */
	addq %r8,%rdi		/* fix destination */
	subq %r8,%r11		/* fix count */
	jmp after_bad_alignment
#endif
