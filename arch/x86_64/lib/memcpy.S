/* Copyright 2002 Andi Kleen */
	
/*
 * memcpy - Copy a memory block.
 *
 * Input:	
 * rdi destination
 * rsi source
 * rdx count
 * 
 * Output:
 * rax original destination
 */	
 	.globl __memcpy
	.globl memcpy
	.p2align
__memcpy:
memcpy:		
	pushq %rbx
	prefetcht0 (%rsi)	/*for more hopefully the hw prefetch will kick in*/
	movq %rdi,%rax

	movl %edi,%ecx
	andl $7,%ecx
	jnz  bad_alignment	
after_bad_alignment:

	movq %rdx,%rcx
	movl $64,%ebx
	shrq $6,%rcx
	jz handle_tail
	
loop_64:
	/* no prefetch because we assume the hw prefetcher does it already 
           and we have no specific temporal hint to give. XXX or give a nta
	   hint for the source? */
	movq (%rsi),%r11
	movq 8(%rsi),%r8
	movq 2*8(%rsi),%r9
	movq 3*8(%rsi),%r10
	movnti %r11,(%rdi)
	movnti %r8,1*8(%rdi)
	movnti %r9,2*8(%rdi)
	movnti %r10,3*8(%rdi)
		
	movq 4*8(%rsi),%r11
	movq 5*8(%rsi),%r8
	movq 6*8(%rsi),%r9
	movq 7*8(%rsi),%r10
	movnti %r11,4*8(%rdi)
	movnti %r8,5*8(%rdi)
	movnti %r9,6*8(%rdi)
	movnti %r10,7*8(%rdi)

	addq %rbx,%rsi	
	addq %rbx,%rdi
	loop loop_64

handle_tail:
	movl %edx,%ecx
	andl $63,%ecx
	shrl $3,%ecx
	jz   handle_7
	movl $8,%ebx
loop_8: 
	movq (%rsi),%r8
	movnti %r8,(%rdi) 
	addq %rbx,%rdi
	addq %rbx,%rsi
	loop loop_8

handle_7:
	movl %edx,%ecx
	andl $7,%ecx
	jz ende
loop_1:
	movb (%rsi),%r8b
	movb %r8b,(%rdi) 
	incq %rdi
	incq %rsi
	loop loop_1
	
ende: 	
	sfence
	popq %rbx
	ret
	
	/* align destination */
	/* This is simpleminded. For bigger blocks it may make sense to align
	   src and dst to their aligned subset and handle the rest separately */
bad_alignment:
	movl $8,%r9d
	subl %ecx,%r9d
	movl %r9d,%ecx
	subq %r9,%rdx
	js   small_alignment
	jz   small_alignment
align_1:
	movb (%rsi),%r8b
	movb %r8b,(%rdi) 
	incq %rdi
	incq %rsi
	loop align_1
	jmp after_bad_alignment
small_alignment:
	addq %r9,%rdx
	jmp handle_7
