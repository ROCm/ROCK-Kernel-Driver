/* 
 * User address space access functions.
 * The non inlined parts of asm-i386/uaccess.h are here.
 *
 * Copyright 1997 Andi Kleen <ak@muc.de>
 * Copyright 1997 Linus Torvalds
 */
#include <linux/config.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/blkdev.h>
#include <asm/uaccess.h>
#include <asm/mmx.h>

static inline int movsl_is_ok(const void *a1, const void *a2, unsigned long n)
{
#ifdef CONFIG_X86_INTEL_USERCOPY
	if (n >= 64 && (((const long)a1 ^ (const long)a2) & movsl_mask.mask))
		return 0;
#endif
	return 1;
}

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

/**
 * __strncpy_from_user: - Copy a NUL terminated string from userspace, with less checking.
 * @dst:   Destination address, in kernel space.  This buffer must be at
 *         least @count bytes long.
 * @src:   Source address, in user space.
 * @count: Maximum number of bytes to copy, including the trailing NUL.
 * 
 * Copies a NUL-terminated string from userspace to kernel space.
 * Caller must check the specified block with access_ok() before calling
 * this function.
 *
 * On success, returns the length of the string (not including the trailing
 * NUL).
 *
 * If access to userspace fails, returns -EFAULT (some data may have been
 * copied).
 *
 * If @count is smaller than the length of the string, copies @count bytes
 * and returns @count.
 */
long
__strncpy_from_user(char *dst, const char *src, long count)
{
	long res;
	__do_strncpy_from_user(dst, src, count, res);
	return res;
}

/**
 * strncpy_from_user: - Copy a NUL terminated string from userspace.
 * @dst:   Destination address, in kernel space.  This buffer must be at
 *         least @count bytes long.
 * @src:   Source address, in user space.
 * @count: Maximum number of bytes to copy, including the trailing NUL.
 * 
 * Copies a NUL-terminated string from userspace to kernel space.
 *
 * On success, returns the length of the string (not including the trailing
 * NUL).
 *
 * If access to userspace fails, returns -EFAULT (some data may have been
 * copied).
 *
 * If @count is smaller than the length of the string, copies @count bytes
 * and returns @count.
 */
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

/**
 * clear_user: - Zero a block of memory in user space.
 * @to:   Destination address, in user space.
 * @n:    Number of bytes to zero.
 *
 * Zero a block of memory in user space.
 *
 * Returns number of bytes that could not be cleared.
 * On success, this will be zero.
 */
unsigned long
clear_user(void *to, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		__do_clear_user(to, n);
	return n;
}

/**
 * __clear_user: - Zero a block of memory in user space, with less checking.
 * @to:   Destination address, in user space.
 * @n:    Number of bytes to zero.
 *
 * Zero a block of memory in user space.  Caller must check
 * the specified block with access_ok() before calling this function.
 *
 * Returns number of bytes that could not be cleared.
 * On success, this will be zero.
 */
unsigned long
__clear_user(void *to, unsigned long n)
{
	__do_clear_user(to, n);
	return n;
}

/**
 * strlen_user: - Get the size of a string in user space.
 * @s: The string to measure.
 * @n: The maximum valid length
 *
 * Get the size of a NUL-terminated string in user space.
 *
 * Returns the size of the string INCLUDING the terminating NUL.
 * On exception, returns 0.
 * If the string is too long, returns a value greater than @n.
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

#ifdef CONFIG_X86_INTEL_USERCOPY
static unsigned long
__copy_user_intel(void *to, const void *from,unsigned long size)
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

static unsigned long
__copy_user_zeroing_intel(void *to, const void *from, unsigned long size)
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
#else
/*
 * Leave these declared but undefined.  They should not be any references to
 * them
 */
unsigned long
__copy_user_zeroing_intel(void *to, const void *from, unsigned long size);
unsigned long
__copy_user_intel(void *to, const void *from,unsigned long size);
#endif /* CONFIG_X86_INTEL_USERCOPY */

/* Generic arbitrary sized copy.  */
#define __copy_user(to,from,size)					\
do {									\
	int __d0, __d1, __d2;						\
	__asm__ __volatile__(						\
		"	cmp  $7,%0\n"					\
		"	jbe  1f\n"					\
		"	movl %1,%0\n"					\
		"	negl %0\n"					\
		"	andl $7,%0\n"					\
		"	subl %0,%3\n"					\
		"4:	rep; movsb\n"					\
		"	movl %3,%0\n"					\
		"	shrl $2,%0\n"					\
		"	andl $3,%3\n"					\
		"	.align 2,0x90\n"				\
		"0:	rep; movsl\n"					\
		"	movl %3,%0\n"					\
		"1:	rep; movsb\n"					\
		"2:\n"							\
		".section .fixup,\"ax\"\n"				\
		"5:	addl %3,%0\n"					\
		"	jmp 2b\n"					\
		"3:	lea 0(%3,%0,4),%0\n"				\
		"	jmp 2b\n"					\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.align 4\n"					\
		"	.long 4b,5b\n"					\
		"	.long 0b,3b\n"					\
		"	.long 1b,2b\n"					\
		".previous"						\
		: "=&c"(size), "=&D" (__d0), "=&S" (__d1), "=r"(__d2)	\
		: "3"(size), "0"(size), "1"(to), "2"(from)		\
		: "memory");						\
} while (0)

#define __copy_user_zeroing(to,from,size)				\
do {									\
	int __d0, __d1, __d2;						\
	__asm__ __volatile__(						\
		"	cmp  $7,%0\n"					\
		"	jbe  1f\n"					\
		"	movl %1,%0\n"					\
		"	negl %0\n"					\
		"	andl $7,%0\n"					\
		"	subl %0,%3\n"					\
		"4:	rep; movsb\n"					\
		"	movl %3,%0\n"					\
		"	shrl $2,%0\n"					\
		"	andl $3,%3\n"					\
		"	.align 2,0x90\n"				\
		"0:	rep; movsl\n"					\
		"	movl %3,%0\n"					\
		"1:	rep; movsb\n"					\
		"2:\n"							\
		".section .fixup,\"ax\"\n"				\
		"5:	addl %3,%0\n"					\
		"	jmp 6f\n"					\
		"3:	lea 0(%3,%0,4),%0\n"				\
		"6:	pushl %0\n"					\
		"	pushl %%eax\n"					\
		"	xorl %%eax,%%eax\n"				\
		"	rep; stosb\n"					\
		"	popl %%eax\n"					\
		"	popl %0\n"					\
		"	jmp 2b\n"					\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.align 4\n"					\
		"	.long 4b,5b\n"					\
		"	.long 0b,3b\n"					\
		"	.long 1b,6b\n"					\
		".previous"						\
		: "=&c"(size), "=&D" (__d0), "=&S" (__d1), "=r"(__d2)	\
		: "3"(size), "0"(size), "1"(to), "2"(from)		\
		: "memory");						\
} while (0)


unsigned long __copy_to_user_ll(void *to, const void *from, unsigned long n)
{
#ifndef CONFIG_X86_WP_WORKS_OK
	if (unlikely(boot_cpu_data.wp_works_ok == 0) &&
			((unsigned long )to) < TASK_SIZE) {
		/* 
		 * CPU does not honor the WP bit when writing
		 * from supervisory mode, and due to preemption or SMP,
		 * the page tables can change at any time.
		 * Do it manually.	Manfred <manfred@colorfullife.com>
		 */
		while (n) {
		      	unsigned long offset = ((unsigned long)to)%PAGE_SIZE;
			unsigned long len = PAGE_SIZE - offset;
			int retval;
			struct page *pg;
			void *maddr;
			
			if (len > n)
				len = n;

survive:
			down_read(&current->mm->mmap_sem);
			retval = get_user_pages(current, current->mm,
					(unsigned long )to, 1, 1, 0, &pg, NULL);

			if (retval == -ENOMEM && current->pid == 1) {
				up_read(&current->mm->mmap_sem);
				blk_congestion_wait(WRITE, HZ/50);
				goto survive;
			}

			if (retval != 1)
		       		break;

			maddr = kmap_atomic(pg, KM_USER0);
			memcpy(maddr + offset, from, len);
			kunmap_atomic(maddr, KM_USER0);
			set_page_dirty_lock(pg);
			put_page(pg);
			up_read(&current->mm->mmap_sem);

			from += len;
			to += len;
			n -= len;
		}
		return n;
	}
#endif
	if (movsl_is_ok(to, from, n))
		__copy_user(to, from, n);
	else
		n = __copy_user_intel(to, from, n);
	return n;
}

unsigned long __copy_from_user_ll(void *to, const void *from, unsigned long n)
{
	if (movsl_is_ok(to, from, n))
		__copy_user_zeroing(to, from, n);
	else
		n = __copy_user_zeroing_intel(to, from, n);
	return n;
}
