/* 
 * User address space access functions.
 * The non inlined parts of asm-i386/uaccess.h are here.
 *
 * Copyright 1997 Andi Kleen <ak@muc.de>
 * Copyright 1997 Linus Torvalds
 */
#include <linux/config.h>
#include <asm/uaccess.h>
#include <asm/mmx.h>

#ifdef CONFIG_X86_USE_3DNOW_AND_WORKS

unsigned long
__generic_copy_to_user(void *to, const void *from, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
	{
		if(n<512)
			__copy_user(to,from,n);
		else
			mmx_copy_user(to,from,n);
	}
	return n;
}

unsigned long
__generic_copy_from_user(void *to, const void *from, unsigned long n)
{
	if (access_ok(VERIFY_READ, from, n))
	{
		if(n<512)
			__copy_user_zeroing(to,from,n);
		else
			mmx_copy_user_zeroing(to, from, n);
	}
	else
		memset(to, 0, n);
	return n;
}

#else

unsigned long
__generic_copy_to_user(void *to, const void *from, unsigned long n)
{
	prefetch(from);
	if (access_ok(VERIFY_WRITE, to, n)) {
		if (movsl_is_ok(to, from, n))
			__copy_user(to, from, n);
		else
			n = __copy_user_int(to, from, n);
	}
	return n;
}

unsigned long
__generic_copy_from_user(void *to, const void *from, unsigned long n)
{
	prefetchw(to);
	if (access_ok(VERIFY_READ, from, n)) {
		if (movsl_is_ok(to, from, n))
			__copy_user_zeroing(to,from,n);
		else
			n = __copy_user_zeroing_int(to, from, n);
	} else {
		memset(to, 0, n);
	}
	return n;
}

#endif

/*
 * Copy a null terminated string from userspace.
 */

#define __do_strncpy_from_user(dst,src,count,res)			   \
do {									   \
	int __d0, __d1, __d2;						   \
	__asm__ __volatile__(						   \
		"	testl %1,%1\n"					   \
		"	jz 2f\n"					   \
		"0:	lodsb\n"					   \
		"	stosb\n"					   \
		"	testb %%al,%%al\n"				   \
		"	jz 1f\n"					   \
		"	decl %1\n"					   \
		"	jnz 0b\n"					   \
		"1:	subl %1,%0\n"					   \
		"2:\n"							   \
		".section .fixup,\"ax\"\n"				   \
		"3:	movl %5,%0\n"					   \
		"	jmp 2b\n"					   \
		".previous\n"						   \
		".section __ex_table,\"a\"\n"				   \
		"	.align 4\n"					   \
		"	.long 0b,3b\n"					   \
		".previous"						   \
		: "=d"(res), "=c"(count), "=&a" (__d0), "=&S" (__d1),	   \
		  "=&D" (__d2)						   \
		: "i"(-EFAULT), "0"(count), "1"(count), "3"(src), "4"(dst) \
		: "memory");						   \
} while (0)

long
__strncpy_from_user(char *dst, const char *src, long count)
{
	long res;
	__do_strncpy_from_user(dst, src, count, res);
	return res;
}

long
strncpy_from_user(char *dst, const char *src, long count)
{
	long res = -EFAULT;
	if (access_ok(VERIFY_READ, src, 1))
		__do_strncpy_from_user(dst, src, count, res);
	return res;
}


/*
 * Zero Userspace
 */

#define __do_clear_user(addr,size)					\
do {									\
	int __d0;							\
  	__asm__ __volatile__(						\
		"0:	rep; stosl\n"					\
		"	movl %2,%0\n"					\
		"1:	rep; stosb\n"					\
		"2:\n"							\
		".section .fixup,\"ax\"\n"				\
		"3:	lea 0(%2,%0,4),%0\n"				\
		"	jmp 2b\n"					\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.align 4\n"					\
		"	.long 0b,3b\n"					\
		"	.long 1b,2b\n"					\
		".previous"						\
		: "=&c"(size), "=&D" (__d0)				\
		: "r"(size & 3), "0"(size / 4), "1"(addr), "a"(0));	\
} while (0)

unsigned long
clear_user(void *to, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		__do_clear_user(to, n);
	return n;
}

unsigned long
__clear_user(void *to, unsigned long n)
{
	__do_clear_user(to, n);
	return n;
}

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 on exception, a value greater than N if too long
 */

long strnlen_user(const char *s, long n)
{
	unsigned long mask = -__addr_ok(s);
	unsigned long res, tmp;

	__asm__ __volatile__(
		"	testl %0, %0\n"
		"	jz 3f\n"
		"	andl %0,%%ecx\n"
		"0:	repne; scasb\n"
		"	setne %%al\n"
		"	subl %%ecx,%0\n"
		"	addl %0,%%eax\n"
		"1:\n"
		".section .fixup,\"ax\"\n"
		"2:	xorl %%eax,%%eax\n"
		"	jmp 1b\n"
		"3:	movb $1,%%al\n"
		"	jmp 1b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.align 4\n"
		"	.long 0b,2b\n"
		".previous"
		:"=r" (n), "=D" (s), "=a" (res), "=c" (tmp)
		:"0" (n), "1" (s), "2" (0), "3" (mask)
		:"cc");
	return res & mask;
}

#ifdef INTEL_MOVSL
/*
 * Copy To/From Userspace
 */

/* Generic arbitrary sized copy.  */
unsigned long __copy_user_int(void *to, const void *from,unsigned long size)
{
	int d0, d1;
	__asm__ __volatile__(
		       "       .align 2,0x90\n" 
		       "0:     movl 32(%4), %%eax\n"
		       "       cmpl $67, %0\n"     
		       "       jbe 1f\n"            
		       "       movl 64(%4), %%eax\n"
		       "       .align 2,0x90\n"     
		       "1:     movl 0(%4), %%eax\n" 
		       "       movl 4(%4), %%edx\n" 
		       "2:     movl %%eax, 0(%3)\n" 
		       "21:    movl %%edx, 4(%3)\n" 
		       "       movl 8(%4), %%eax\n" 
		       "       movl 12(%4),%%edx\n" 
		       "3:     movl %%eax, 8(%3)\n" 
		       "31:    movl %%edx, 12(%3)\n"
		       "       movl 16(%4), %%eax\n"
		       "       movl 20(%4), %%edx\n"
		       "4:     movl %%eax, 16(%3)\n"
		       "41:    movl %%edx, 20(%3)\n"
		       "       movl 24(%4), %%eax\n"
		       "       movl 28(%4), %%edx\n"
		       "10:    movl %%eax, 24(%3)\n"
		       "51:    movl %%edx, 28(%3)\n"
		       "       movl 32(%4), %%eax\n"
		       "       movl 36(%4), %%edx\n"
		       "11:    movl %%eax, 32(%3)\n"
		       "61:    movl %%edx, 36(%3)\n"
		       "       movl 40(%4), %%eax\n"
		       "       movl 44(%4), %%edx\n"
		       "12:    movl %%eax, 40(%3)\n"
		       "71:    movl %%edx, 44(%3)\n"
		       "       movl 48(%4), %%eax\n"
		       "       movl 52(%4), %%edx\n"
		       "13:    movl %%eax, 48(%3)\n"
		       "81:    movl %%edx, 52(%3)\n"
		       "       movl 56(%4), %%eax\n"
		       "       movl 60(%4), %%edx\n"
		       "14:    movl %%eax, 56(%3)\n"
		       "91:    movl %%edx, 60(%3)\n"
		       "       addl $-64, %0\n"     
		       "       addl $64, %4\n"      
		       "       addl $64, %3\n"      
		       "       cmpl $63, %0\n"      
		       "       ja  0b\n"            
		       "5:     movl  %0, %%eax\n"   
		       "       shrl  $2, %0\n"      
		       "       andl  $3, %%eax\n"   
		       "       cld\n"               
		       "6:     rep; movsl\n"        
		       "       movl %%eax, %0\n"    
		       "7:     rep; movsb\n"		
		       "8:\n"				
		       ".section .fixup,\"ax\"\n"	
		       "9:     lea 0(%%eax,%0,4),%0\n"	
		       "       jmp 8b\n"               
		       ".previous\n"			
		       ".section __ex_table,\"a\"\n"	
		       "       .align 4\n"		
		       "       .long 2b,8b\n"		
		       "       .long 21b,8b\n"	
		       "       .long 3b,8b\n"		
		       "       .long 31b,8b\n"	
		       "       .long 4b,8b\n"		
		       "       .long 41b,8b\n"	
		       "       .long 10b,8b\n"	
		       "       .long 51b,8b\n"	
		       "       .long 11b,8b\n"	
		       "       .long 61b,8b\n"	
		       "       .long 12b,8b\n"	
		       "       .long 71b,8b\n"	
		       "       .long 13b,8b\n"	
		       "       .long 81b,8b\n"	
		       "       .long 14b,8b\n"	
		       "       .long 91b,8b\n"	
		       "       .long 6b,9b\n"		
		       "       .long 7b,8b\n"          
		       ".previous"			
		       : "=&c"(size), "=&D" (d0), "=&S" (d1)
		       :  "1"(to), "2"(from), "0"(size)
		       : "eax", "edx", "memory");			
	return size;
}

unsigned long
__copy_user_zeroing_int(void *to, const void *from, unsigned long size)
{
	int d0, d1;
	__asm__ __volatile__(
		       "        .align 2,0x90\n"
		       "0:      movl 32(%4), %%eax\n"
		       "        cmpl $67, %0\n"      
		       "        jbe 2f\n"            
		       "1:      movl 64(%4), %%eax\n"
		       "        .align 2,0x90\n"     
		       "2:      movl 0(%4), %%eax\n" 
		       "21:     movl 4(%4), %%edx\n" 
		       "        movl %%eax, 0(%3)\n" 
		       "        movl %%edx, 4(%3)\n" 
		       "3:      movl 8(%4), %%eax\n" 
		       "31:     movl 12(%4),%%edx\n" 
		       "        movl %%eax, 8(%3)\n" 
		       "        movl %%edx, 12(%3)\n"
		       "4:      movl 16(%4), %%eax\n"
		       "41:     movl 20(%4), %%edx\n"
		       "        movl %%eax, 16(%3)\n"
		       "        movl %%edx, 20(%3)\n"
		       "10:     movl 24(%4), %%eax\n"
		       "51:     movl 28(%4), %%edx\n"
		       "        movl %%eax, 24(%3)\n"
		       "        movl %%edx, 28(%3)\n"
		       "11:     movl 32(%4), %%eax\n"
		       "61:     movl 36(%4), %%edx\n"
		       "        movl %%eax, 32(%3)\n"
		       "        movl %%edx, 36(%3)\n"
		       "12:     movl 40(%4), %%eax\n"
		       "71:     movl 44(%4), %%edx\n"
		       "        movl %%eax, 40(%3)\n"
		       "        movl %%edx, 44(%3)\n"
		       "13:     movl 48(%4), %%eax\n"
		       "81:     movl 52(%4), %%edx\n"
		       "        movl %%eax, 48(%3)\n"
		       "        movl %%edx, 52(%3)\n"
		       "14:     movl 56(%4), %%eax\n"
		       "91:     movl 60(%4), %%edx\n"
		       "        movl %%eax, 56(%3)\n"
		       "        movl %%edx, 60(%3)\n"
		       "        addl $-64, %0\n"     
		       "        addl $64, %4\n"      
		       "        addl $64, %3\n"      
		       "        cmpl $63, %0\n"      
		       "        ja  0b\n"            
		       "5:      movl  %0, %%eax\n"   
		       "        shrl  $2, %0\n"      
		       "        andl $3, %%eax\n"    
		       "        cld\n"               
		       "6:      rep; movsl\n"   
		       "        movl %%eax,%0\n"
		       "7:      rep; movsb\n"	
		       "8:\n"			
		       ".section .fixup,\"ax\"\n"
		       "9:      lea 0(%%eax,%0,4),%0\n"	
		       "16:     pushl %0\n"	
		       "        pushl %%eax\n"	
		       "        xorl %%eax,%%eax\n"
		       "        rep; stosb\n"	
		       "        popl %%eax\n"	
		       "        popl %0\n"	
		       "        jmp 8b\n"	
		       ".previous\n"		
		       ".section __ex_table,\"a\"\n"
		       "	.align 4\n"	   
		       "	.long 0b,16b\n"	 
		       "	.long 1b,16b\n"
		       "	.long 2b,16b\n"
		       "	.long 21b,16b\n"
		       "	.long 3b,16b\n"	
		       "	.long 31b,16b\n"
		       "	.long 4b,16b\n"	
		       "	.long 41b,16b\n"
		       "	.long 10b,16b\n"
		       "	.long 51b,16b\n"
		       "	.long 11b,16b\n"
		       "	.long 61b,16b\n"
		       "	.long 12b,16b\n"
		       "	.long 71b,16b\n"
		       "	.long 13b,16b\n"
		       "	.long 81b,16b\n"
		       "	.long 14b,16b\n"
		       "	.long 91b,16b\n"
		       "	.long 6b,9b\n"	
		       "        .long 7b,16b\n" 
		       ".previous"		
		       : "=&c"(size), "=&D" (d0), "=&S" (d1)
		       :  "1"(to), "2"(from), "0"(size)
		       : "eax", "edx", "memory");
	return size;
}
#endif	/* INTEL_MOVSL */
