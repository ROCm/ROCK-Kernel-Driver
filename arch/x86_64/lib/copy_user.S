/* Copyright 2002 Andi Kleen, SuSE Labs.
 * Subject to the GNU Public License v2.
 * 
 * Functions to copy from and to user space.		
 */		 

#define FIX_ALIGNMENT 1
		
#define movnti movq  /* write to cache for now */
#define prefetch prefetcht2
		
	#include <asm/current.h>
	#include <asm/offset.h>
	#include <asm/thread_info.h>

/* Standard copy_to_user with segment limit checking */		
	.globl copy_to_user
	.p2align 	
copy_to_user:
	GET_THREAD_INFO(%rax)
	movq %rdi,%rcx
	addq %rdx,%rcx
	jc  bad_to_user
	cmpq threadinfo_addr_limit(%rax),%rcx
	jae bad_to_user
	jmp copy_user_generic

/* Standard copy_from_user with segment limit checking */	
	.globl copy_from_user
	.p2align 	
copy_from_user:
	GET_THREAD_INFO(%rax)
	movq %rsi,%rcx
	addq %rdx,%rcx
	jc  bad_from_user
	cmpq threadinfo_addr_limit(%rax),%rcx
	jae  bad_from_user
	/* FALL THROUGH to copy_user_generic */
	
	.section .fixup,"ax"
	/* must zero dest */
bad_from_user:
	movl %edx,%ecx
	xorl %eax,%eax
	rep
	stosb
bad_to_user:
	movl	%edx,%eax
	ret
	.previous
	
/*
 * copy_user_generic - memory copy with exception handling.
 * 	
 * Input:	
 * rdi destination
 * rsi source
 * rdx count
 *
 * Output:		
 * eax uncopied bytes or 0 if successful. 
 */
	.globl copy_user_generic	
copy_user_generic:	
	/* Put the first cacheline into cache. This should handle
	   the small movements in ioctls etc., but not penalize the bigger
	   filesystem data copies too much. */
	pushq %rbx
	prefetch (%rsi)
	xorl %eax,%eax		/*zero for the exception handler */

#ifdef FIX_ALIGNMENT
	/* check for bad alignment of destination */
	movl %edi,%ecx
	andl $7,%ecx
	jnz  bad_alignment
after_bad_alignment:
#endif

	movq %rdx,%rcx

	movl $64,%ebx	
	shrq $6,%rdx
	decq %rdx
	js   handle_tail
	jz   loop_no_prefetch
	
loop:
	prefetch 64(%rsi)
	
loop_no_prefetch:	
s1:	movq (%rsi),%r11
s2:	movq 1*8(%rsi),%r8
s3:	movq 2*8(%rsi),%r9
s4:	movq 3*8(%rsi),%r10
d1:	movnti %r11,(%rdi)
d2:	movnti %r8,1*8(%rdi)
d3:	movnti %r9,2*8(%rdi)
d4:	movnti %r10,3*8(%rdi)
		
s5:	movq 4*8(%rsi),%r11
s6:	movq 5*8(%rsi),%r8
s7:	movq 6*8(%rsi),%r9
s8:	movq 7*8(%rsi),%r10
d5:	movnti %r11,4*8(%rdi)
d6:	movnti %r8,5*8(%rdi)
d7:	movnti %r9,6*8(%rdi)
d8:	movnti %r10,7*8(%rdi)

	addq %rbx,%rsi	
	addq %rbx,%rdi
	
	decq %rdx
	jz   loop_no_prefetch
	jns  loop

handle_tail:
	movl %ecx,%edx
	andl $63,%ecx
	shrl $3,%ecx
	jz   handle_7
	movl $8,%ebx
loop_8:
s9:	movq (%rsi),%r8
d9:	movq %r8,(%rdi)
	addq %rbx,%rdi
	addq %rbx,%rsi
	decl %ecx
	jnz loop_8
	
handle_7:		
	movl %edx,%ecx	
	andl $7,%ecx
	jz   ende
loop_1:
s10:	movb (%rsi),%bl
d10:	movb %bl,(%rdi)
	incq %rdi
	incq %rsi
	decl %ecx
	jnz loop_1
			
ende:
	sfence
	popq %rbx
	ret	

#ifdef FIX_ALIGNMENT		  		
	/* align destination */
bad_alignment:
	movl $8,%r9d
	subl %ecx,%r9d
	movl %r9d,%ecx
	subq %r9,%rdx
	jz   small_align
	js   small_align
align_1:		
s11:	movb (%rsi),%bl
d11:	movb %bl,(%rdi)
	incq %rsi
	incq %rdi
	decl %ecx
	jnz align_1
	jmp after_bad_alignment
small_align:
	addq %r9,%rdx
	jmp handle_7
#endif
	
	/* table sorted by exception address */	
	.section __ex_table,"a"
	.align 8
	.quad s1,s1e
	.quad s2,s2e
	.quad s3,s3e
	.quad s4,s4e	
	.quad d1,s1e
	.quad d2,s2e
	.quad d3,s3e
	.quad d4,s4e
	.quad s5,s5e
	.quad s6,s6e
	.quad s7,s7e
	.quad s8,s8e	
	.quad d5,s5e
	.quad d6,s6e
	.quad d7,s7e
	.quad d8,s8e
	.quad s9,e_quad
	.quad d9,e_quad
	.quad s10,e_byte
	.quad d10,e_byte
#ifdef FIX_ALIGNMENT	
	.quad s11,e_byte
	.quad d11,e_byte
#endif
	.quad e5,e_zero
	.previous

	/* compute 64-offset for main loop. 8 bytes accuracy with error on the 
	   pessimistic side. this is gross. it would be better to fix the 
	   interface. */	
	/* eax: zero, ebx: 64 */
s1e: 	addl $8,%eax
s2e: 	addl $8,%eax
s3e: 	addl $8,%eax
s4e: 	addl $8,%eax
s5e: 	addl $8,%eax
s6e: 	addl $8,%eax
s7e: 	addl $8,%eax
s8e: 	addl $8,%eax
	addq %rbx,%rdi	/* +64 */
	subq %rax,%rdi  /* correct destination with computed offset */

	shlq $6,%rdx	/* loop counter * 64 (stride length) */
	addq %rax,%rdx	/* add offset to loopcnt */
	andl $63,%ecx	/* remaining bytes */
	addq %rcx,%rdx	/* add them */
	jmp zero_rest

	/* exception on quad word loop in tail handling */
	/* ecx:	loopcnt/8, %edx: length, rdi: correct */
e_quad:
	shll $3,%ecx
	andl $7,%edx
	addl %ecx,%edx
	/* edx: bytes to zero, rdi: dest, eax:zero */
zero_rest:
	movq %rdx,%rcx
e_byte:
	xorl %eax,%eax
e5:	rep 
	stosb
	/* when there is another exception while zeroing the rest just return */
e_zero:		
	movq %rdx,%rax
	jmp ende
