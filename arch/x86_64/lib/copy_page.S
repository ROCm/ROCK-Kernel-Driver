/*
 * Copyright 2002 Andi Kleen, SuSE Labs.
 */	 	
	
	#include <linux/linkage.h>
	#include <linux/config.h>
	#ifdef CONFIG_PREEMPT
	#warning "check your fpu context saving!"
	#endif

/*
 * Copy a page. 
 *		
 * rdi destination page
 * rsi source page
 *		
 * src/dst must be aligned to 16 bytes.
 * 
 * Warning: in case of super lazy FP save this needs to be preempt_stop	
 */
	
	.globl copy_page
	.p2align
copy_page:
	prefetchnta (%rsi) 
	prefetchnta 64(%rsi)
	    
	movq %rsp,%rax
	subq $16*4,%rsp
	andq $~15,%rsp		
	movdqa %xmm0,(%rsp) 
	movdqa %xmm1,16(%rsp) 
	movdqa %xmm2,32(%rsp) 
	movdqa %xmm3,48(%rsp)
	
	movl   $(4096/128)-2,%ecx
	movl   $128,%edx
loop:
	prefetchnta (%rsi) 
	prefetchnta 64(%rsi) 
loop_no_prefetch:	
	movdqa (%rsi),%xmm0 	 			 
	movdqa 16(%rsi),%xmm1
	movdqa 32(%rsi),%xmm2 	 			 
	movdqa 48(%rsi),%xmm3 	 			 
	movntdq %xmm0,(%rdi)	 
	movntdq %xmm1,16(%rdi)	 
	movntdq %xmm2,32(%rdi)	 
	movntdq %xmm3,48(%rdi)
	
	movdqa 64(%rsi),%xmm0 	 			 
	movdqa 80(%rsi),%xmm1
	movdqa 96(%rsi),%xmm2 	 			 
	movdqa 112(%rsi),%xmm3 	 			 
	movntdq %xmm0,64(%rdi)	 
	movntdq %xmm1,80(%rdi)	 
	movntdq %xmm2,96(%rdi)	 
	movntdq %xmm3,112(%rdi)

	addq   %rdx,%rdi	
	addq   %rdx,%rsi
	decl   %ecx
	jns    loop
	cmpl   $-1,%ecx
	je     loop_no_prefetch
	
	sfence

	movdqa (%rsp),%xmm0
	movdqa 16(%rsp),%xmm1
	movdqa 32(%rsp),%xmm2
	movdqa 48(%rsp),%xmm3
	movq   %rax,%rsp
	ret
