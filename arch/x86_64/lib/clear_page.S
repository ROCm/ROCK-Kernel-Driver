/*
 * Copyright 2002 Andi Kleen, SuSE Labs.
 */		 
	
	#include <linux/linkage.h>

/*
 * Zero a page. 	
 * rdi	page
 */			
ENTRY(clear_page)
	xorl   %eax,%eax
	movl   $4096/128,%ecx
	movl   $128,%edx
loop:
#define PUT(x) movnti %rax,x*8(%rdi) 
	PUT(0)
	PUT(1)
	PUT(2)
	PUT(3)
	PUT(4)
	PUT(5)
	PUT(6)
	PUT(7)
	PUT(8)
	PUT(9)
	PUT(10)
	PUT(11)
	PUT(12)
	PUT(13)
	PUT(14)
	PUT(15)
	addq    %rdx,%rdi
	decl	%ecx
	jnz	loop
	sfence	
	ret
	    	
