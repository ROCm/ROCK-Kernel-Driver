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
	
ENTRY(copy_page)
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
	movdqa 1*16(%rsi),%xmm1
	movdqa 2*16(%rsi),%xmm2 	 			 
	movdqa 3*16(%rsi),%xmm3 	 			 
	movntdq %xmm0,(%rdi)	 
	movntdq %xmm1,16(%rdi)	 
	movntdq %xmm2,2*16(%rdi)	 
	movntdq %xmm3,3*16(%rdi)
	
	movdqa 4*16(%rsi),%xmm0 	 			 
	movdqa 5*16(%rsi),%xmm1
	movdqa 6*16(%rsi),%xmm2 	 			 
	movdqa 7*16(%rsi),%xmm3 	 			 
	movntdq %xmm0,4*16(%rdi)	 
	movntdq %xmm1,5*16(%rdi)	 
	movntdq %xmm2,6*16(%rdi)	 
	movntdq %xmm3,7*16(%rdi)

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
