#ifndef _ASM_M32R_UACCESS_H
#define _ASM_M32R_UACCESS_H

/* $Id$ */

#undef UACCESS_DEBUG

#ifdef UACCESS_DEBUG
#define UAPRINTK(args...) printk(args)
#else
#define UAPRINTK(args...)
#endif /* UACCESS_DEBUG */

/*
 * User space memory access functions
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/thread_info.h>
#include <asm/page.h>

#define VERIFY_READ 0
#define VERIFY_WRITE 1

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

#define MAKE_MM_SEG(s)	((mm_segment_t) { (s) })

#ifdef CONFIG_MMU
#define KERNEL_DS	MAKE_MM_SEG(0xFFFFFFFF)
#define USER_DS		MAKE_MM_SEG(PAGE_OFFSET)
#else
#define KERNEL_DS	MAKE_MM_SEG(0xFFFFFFFF)
#define USER_DS		MAKE_MM_SEG(0xFFFFFFFF)
#endif /* CONFIG_MMU */

#define get_ds()	(KERNEL_DS)
#ifdef CONFIG_MMU
#define get_fs()	(current_thread_info()->addr_limit)
#define set_fs(x)	(current_thread_info()->addr_limit = (x))
#else
static inline mm_segment_t get_fs(void)
{
  return USER_DS;
}

static inline void set_fs(mm_segment_t s)
{
}
#endif /* CONFIG_MMU */

#define segment_eq(a,b)	((a).seg == (b).seg)

#define __addr_ok(addr) \
	((unsigned long)(addr) < (current_thread_info()->addr_limit.seg))

/*
 * Uhhuh, this needs 33-bit arithmetic. We have a carry..
 */
#define __range_ok(addr,size) ({					\
	unsigned long flag, sum; 					\
	__chk_user_ptr(addr);						\
	asm ( 								\
		"	cmpu	%1, %1    ; clear cbit\n"		\
		"	addx	%1, %3    ; set cbit if overflow\n"	\
		"	subx	%0, %0\n"				\
		"	cmpu	%4, %1\n"				\
		"	subx	%0, %5\n"				\
		: "=&r"(flag), "=r"(sum)				\
		: "1"(addr), "r"((int)(size)), 				\
		  "r"(current_thread_info()->addr_limit.seg), "r"(0)	\
		: "cbit" );						\
	flag; })

#ifdef CONFIG_MMU
#define access_ok(type,addr,size) (__range_ok(addr,size) == 0)
#else
static inline int access_ok(int type, const void *addr, unsigned long size)
{
  extern unsigned long memory_start, memory_end;
  unsigned long val = (unsigned long)addr;

  return ((val >= memory_start) && ((val + size) < memory_end));
}
#endif /* CONFIG_MMU */

static __inline__ int verify_area(int type, const void __user *addr,
	unsigned long size)
{
	return access_ok(type, addr, size) ? 0 : -EFAULT;
}


/*
 * The exception table consists of pairs of addresses: the first is the
 * address of an instruction that is allowed to fault, and the second is
 * the address at which the program should continue.  No registers are
 * modified, so it is entirely up to the continuation code to figure out
 * what to do.
 *
 * All the routines below use bits of fixup code that are out of line
 * with the main instruction path.  This means when everything is well,
 * we don't even have to jump over them.  Further, they do not intrude
 * on our cache or tlb entries.
 */

struct exception_table_entry
{
	unsigned long insn, fixup;
};

extern int fixup_exception(struct pt_regs *regs);

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 *
 * This gets kind of ugly. We want to return _two_ values in "get_user()"
 * and yet we don't want to do any pointers, because that is too much
 * of a performance impact. Thus we have a few rather ugly macros here,
 * and hide all the uglyness from the user.
 *
 * The "__xxx" versions of the user access functions are versions that
 * do not verify the address space, that must have been done previously
 * with a separate "access_ok()" call (this is used when we do multiple
 * accesses to the same area of user memory).
 */

extern void __get_user_1(void);
extern void __get_user_2(void);
extern void __get_user_4(void);

#ifndef MODULE
#define __get_user_x(size,ret,x,ptr) 					\
	__asm__ __volatile__(						\
		"	mv	r0, %0\n"				\
		"	mv	r1, %1\n" 				\
		"	bl __get_user_" #size "\n"			\
		"	mv	%0, r0\n"				\
		"	mv	%1, r1\n" 				\
		: "=r"(ret), "=r"(x) 					\
		: "0"(ptr)						\
		: "r0", "r1", "r14" )
#else /* MODULE */
/*
 * Use "jl" instead of "bl" for MODULE
 */
#define __get_user_x(size,ret,x,ptr) 					\
	__asm__ __volatile__(						\
		"	mv	r0, %0\n"				\
		"	mv	r1, %1\n" 				\
		"	seth	lr, #high(__get_user_" #size ")\n"	\
		"	or3	lr, lr, #low(__get_user_" #size ")\n"	\
		"	jl 	lr\n"					\
		"	mv	%0, r0\n"				\
		"	mv	%1, r1\n" 				\
		: "=r"(ret), "=r"(x) 					\
		: "0"(ptr)						\
		: "r0", "r1", "r14" )
#endif

/* Careful: we have to cast the result to the type of the pointer for sign
   reasons */
#define get_user(x,ptr)							\
({	int __ret_gu,__val_gu;						\
	__chk_user_ptr(ptr);						\
	switch(sizeof (*(ptr))) {					\
	case 1:  __get_user_x(1,__ret_gu,__val_gu,ptr); break;		\
	case 2:  __get_user_x(2,__ret_gu,__val_gu,ptr); break;		\
	case 4:  __get_user_x(4,__ret_gu,__val_gu,ptr); break;		\
	default: __get_user_x(X,__ret_gu,__val_gu,ptr); break;		\
	}								\
	(x) = (__typeof__(*(ptr)))__val_gu;				\
	__ret_gu;							\
})

extern void __put_user_bad(void);

#define put_user(x,ptr)							\
  __put_user_check((__typeof__(*(ptr)))(x),(ptr),sizeof(*(ptr)))

#define __get_user(x,ptr) \
  __get_user_nocheck((x),(ptr),sizeof(*(ptr)))
#define __put_user(x,ptr) \
  __put_user_nocheck((__typeof__(*(ptr)))(x),(ptr),sizeof(*(ptr)))

#define __put_user_nocheck(x,ptr,size)					\
({									\
	long __pu_err;							\
	__put_user_size((x),(ptr),(size),__pu_err);			\
	__pu_err;							\
})


#define __put_user_check(x,ptr,size)					\
({									\
	long __pu_err = -EFAULT;					\
	__typeof__(*(ptr)) *__pu_addr = (ptr);				\
	might_sleep();							\
	if (access_ok(VERIFY_WRITE,__pu_addr,size))			\
		__put_user_size((x),__pu_addr,(size),__pu_err);		\
	__pu_err;							\
})

#define __put_user_size(x,ptr,size,retval)				\
do {									\
	retval = 0;							\
	__chk_user_ptr(ptr);						\
	switch (size) {							\
	  case 1: __put_user_asm(x,ptr,retval,"b"); break;		\
	  case 2: __put_user_asm(x,ptr,retval,"h"); break;		\
	  case 4: __put_user_asm(x,ptr,retval,""); break;		\
	  case 8: __put_user_u64((__typeof__(*ptr))(x),ptr,retval); break;\
	  default: __put_user_bad();					\
	}								\
} while (0)

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct *)(x))

/*
 * Tell gcc we read from memory instead of writing: this is because
 * we do not write to any memory gcc knows about, so there are no
 * aliasing issues.
 */
#define __put_user_asm(x, addr, err, itype)				\
	__asm__ __volatile__(						\
		"	.fillinsn\n"					\
		"1:	st"itype" %1,@%2\n"				\
		"	.fillinsn\n"					\
		"2:\n"							\
		".section .fixup,\"ax\"\n"				\
		"	.balign 4\n"					\
		"3:	ldi %0,%3\n"					\
		"	seth r14,#high(2b)\n"				\
		"	or3 r14,r14,#low(2b)\n"				\
		"	jmp r14\n"					\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.balign 4\n"					\
		"	.long 1b,3b\n"					\
		".previous"						\
		: "=r"(err)						\
		: "r"(x), "r"(addr), "i"(-EFAULT), "0"(err)		\
		: "r14", "memory")

#if defined(__LITTLE_ENDIAN__)
#define __put_user_u64(x, addr, err)                                    \
        __asm__ __volatile__(                                           \
                "       .fillinsn\n"                                    \
                "1:     st %2,@%3\n"                                    \
                "       .fillinsn\n"                                    \
                "2:     st %1,@(4,%3)\n"                                \
                "       .fillinsn\n"                                    \
                "3:\n"                                                  \
                ".section .fixup,\"ax\"\n"                              \
                "       .balign 4\n"                                    \
                "4:     ldi %0,%4\n"                                    \
                "       seth r14,#high(3b)\n"                           \
                "       or3 r14,r14,#low(3b)\n"                         \
                "       jmp r14\n"                                      \
                ".previous\n"                                           \
                ".section __ex_table,\"a\"\n"                           \
                "       .balign 4\n"                                    \
                "       .long 1b,4b\n"                                  \
                "       .long 2b,4b\n"                                  \
                ".previous"                                             \
                : "=r"(err)                                             \
                : "r"((unsigned long)((unsigned long long)x >> 32)),    \
                  "r"((unsigned long)x ),                               \
                  "r"(addr), "i"(-EFAULT), "0"(err)                     \
                : "r14", "memory")

#elif defined(__BIG_ENDIAN__)
#define __put_user_u64(x, addr, err)					\
	__asm__ __volatile__(						\
		"	.fillinsn\n"					\
		"1:	st %1,@%3\n"					\
		"	.fillinsn\n"					\
		"2:	st %2,@(4,%3)\n"				\
		"	.fillinsn\n"					\
		"3:\n"							\
		".section .fixup,\"ax\"\n"				\
		"	.balign 4\n"					\
		"4:	ldi %0,%4\n"					\
		"	seth r14,#high(3b)\n"				\
		"	or3 r14,r14,#low(3b)\n"				\
		"	jmp r14\n"					\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.balign 4\n"					\
		"	.long 1b,4b\n"					\
		"	.long 2b,4b\n"					\
		".previous"						\
		: "=r"(err)						\
		: "r"((unsigned long)((unsigned long long)x >> 32)),	\
		  "r"((unsigned long)x ),				\
		  "r"(addr), "i"(-EFAULT), "0"(err)			\
		: "r14", "memory")
#else
#error no endian defined
#endif

#define __get_user_nocheck(x,ptr,size)					\
({									\
	long __gu_err, __gu_val;					\
	__get_user_size(__gu_val,(ptr),(size),__gu_err);		\
	(x) = (__typeof__(*(ptr)))__gu_val;				\
	__gu_err;							\
})

extern long __get_user_bad(void);

#define __get_user_size(x,ptr,size,retval)				\
do {									\
	retval = 0;							\
	__chk_user_ptr(ptr);						\
	switch (size) {							\
	  case 1: __get_user_asm(x,ptr,retval,"ub"); break;		\
	  case 2: __get_user_asm(x,ptr,retval,"uh"); break;		\
	  case 4: __get_user_asm(x,ptr,retval,""); break;		\
	  default: (x) = __get_user_bad();				\
	}								\
} while (0)

#define __get_user_asm(x, addr, err, itype)				\
	__asm__ __volatile__(						\
		"	.fillinsn\n"					\
		"1:	ld"itype" %1,@%2\n"				\
		"	.fillinsn\n"					\
		"2:\n"							\
		".section .fixup,\"ax\"\n"				\
		"	.balign 4\n"					\
		"3:	ldi %0,%3\n"					\
		"	seth r14,#high(2b)\n"				\
		"	or3 r14,r14,#low(2b)\n"				\
		"	jmp r14\n"					\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.balign 4\n"					\
		"	.long 1b,3b\n"					\
		".previous"						\
		: "=r"(err), "=&r"(x)					\
		: "r"(addr), "i"(-EFAULT), "0"(err)			\
		: "r14", "memory")


/*
 * Copy To/From Userspace
 */

/* Generic arbitrary sized copy.  */
/* Return the number of bytes NOT copied.  */
#define __copy_user(to,from,size)					\
do {									\
	unsigned long __dst, __src, __c;				\
	__asm__ __volatile__ (						\
		"	mv	r14, %0\n"				\
		"	or	r14, %1\n"				\
		"	beq	%0, %1, 9f\n"				\
		"	beqz	%2, 9f\n"				\
		"	and3	r14, r14, #3\n"				\
		"	bnez	r14, 2f\n"				\
		"	and3	%2, %2, #3\n"				\
		"	beqz	%3, 2f\n"				\
		"	addi	%0, #-4		; word_copy \n"		\
		"	.fillinsn\n"					\
		"0:	ld	r14, @%1+\n"				\
		"	addi	%3, #-1\n"				\
		"	.fillinsn\n"					\
		"1:	st	r14, @+%0\n"				\
		"	bnez	%3, 0b\n"				\
		"	beqz	%2, 9f\n"				\
		"	addi	%0, #4\n"				\
		"	.fillinsn\n"					\
		"2:	ldb	r14, @%1	; byte_copy \n"		\
		"	.fillinsn\n"					\
		"3:	stb	r14, @%0\n"				\
		"	addi	%1, #1\n"				\
		"	addi	%2, #-1\n"				\
		"	addi	%0, #1\n"				\
		"	bnez	%2, 2b\n"				\
		"	.fillinsn\n"					\
		"9:\n"							\
		".section .fixup,\"ax\"\n"				\
		"	.balign 4\n"					\
		"5:	addi	%3, #1\n"				\
		"	addi	%1, #-4\n"				\
		"	.fillinsn\n"					\
		"6:	slli	%3, #2\n"				\
		"	add	%2, %3\n"				\
		"	addi	%0, #4\n"				\
		"	.fillinsn\n"					\
		"7:	seth	r14, #high(9b)\n"			\
		"	or3	r14, r14, #low(9b)\n"			\
		"	jmp	r14\n"					\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.balign 4\n"					\
		"	.long 0b,6b\n"					\
		"	.long 1b,5b\n"					\
		"	.long 2b,9b\n"					\
		"	.long 3b,9b\n"					\
		".previous\n"						\
		: "=&r"(__dst), "=&r"(__src), "=&r"(size), "=&r"(__c)	\
		: "0"(to), "1"(from), "2"(size), "3"(size / 4)		\
		: "r14", "memory");					\
} while (0)

#define __copy_user_zeroing(to,from,size)				\
do {									\
	unsigned long __dst, __src, __c;				\
	__asm__ __volatile__ (						\
		"	mv	r14, %0\n"				\
		"	or	r14, %1\n"				\
		"	beq	%0, %1, 9f\n"				\
		"	beqz	%2, 9f\n"				\
		"	and3	r14, r14, #3\n"				\
		"	bnez	r14, 2f\n"				\
		"	and3	%2, %2, #3\n"				\
		"	beqz	%3, 2f\n"				\
		"	addi	%0, #-4		; word_copy \n"		\
		"	.fillinsn\n"					\
		"0:	ld	r14, @%1+\n"				\
		"	addi	%3, #-1\n"				\
		"	.fillinsn\n"					\
		"1:	st	r14, @+%0\n"				\
		"	bnez	%3, 0b\n"				\
		"	beqz	%2, 9f\n"				\
		"	addi	%0, #4\n"				\
		"	.fillinsn\n"					\
		"2:	ldb	r14, @%1	; byte_copy \n"		\
		"	.fillinsn\n"					\
		"3:	stb	r14, @%0\n"				\
		"	addi	%1, #1\n"				\
		"	addi	%2, #-1\n"				\
		"	addi	%0, #1\n"				\
		"	bnez	%2, 2b\n"				\
		"	.fillinsn\n"					\
		"9:\n"							\
		".section .fixup,\"ax\"\n"				\
		"	.balign 4\n"					\
		"5:	addi	%3, #1\n"				\
		"	addi	%1, #-4\n"				\
		"	.fillinsn\n"					\
		"6:	slli	%3, #2\n"				\
		"	add	%2, %3\n"				\
		"	addi	%0, #4\n"				\
		"	.fillinsn\n"					\
		"7:	ldi	r14, #0		; store zero \n"	\
		"	.fillinsn\n"					\
		"8:	addi	%2, #-1\n"				\
		"	stb	r14, @%0	; ACE? \n"		\
		"	addi	%0, #1\n"				\
		"	bnez	%2, 8b\n"				\
		"	seth	r14, #high(9b)\n"			\
		"	or3	r14, r14, #low(9b)\n"			\
		"	jmp	r14\n"					\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.balign 4\n"					\
		"	.long 0b,6b\n"					\
		"	.long 1b,5b\n"					\
		"	.long 2b,7b\n"					\
		"	.long 3b,7b\n"					\
		".previous\n"						\
		: "=&r"(__dst), "=&r"(__src), "=&r"(size), "=&r"(__c)	\
		: "0"(to), "1"(from), "2"(size), "3"(size / 4)		\
		: "r14", "memory");					\
} while (0)


/* We let the __ versions of copy_from/to_user inline, because they're often
 * used in fast paths and have only a small space overhead.
 */
static __inline__ unsigned long __generic_copy_from_user_nocheck(void *to,
	const void __user *from, unsigned long n)
{
	__copy_user_zeroing(to,from,n);
	return n;
}

static __inline__ unsigned long __generic_copy_to_user_nocheck(void __user *to,
	const void *from, unsigned long n)
{
	__copy_user(to,from,n);
	return n;
}

unsigned long __generic_copy_to_user(void *, const void *, unsigned long);
unsigned long __generic_copy_from_user(void *, const void *, unsigned long);

#define copy_to_user(to,from,n)				\
({							\
	might_sleep();					\
	__generic_copy_to_user((to),(from),(n));	\
})

#define copy_from_user(to,from,n)			\
({							\
	might_sleep();					\
	__generic_copy_from_user((to),(from),(n));	\
})

#define __copy_to_user(to,from,n)			\
	__generic_copy_to_user_nocheck((to),(from),(n))

#define __copy_from_user(to,from,n)			\
	__generic_copy_from_user_nocheck((to),(from),(n))

long strncpy_from_user(char *dst, const char __user *src, long count);
long __strncpy_from_user(char *dst, const char __user *src, long count);
#define strlen_user(str) strnlen_user(str, ~0UL >> 1)
long strnlen_user(const char __user *str, long n);
unsigned long clear_user(void __user *mem, unsigned long len);
unsigned long __clear_user(void __user *mem, unsigned long len);

#endif /* _ASM_M32R_UACCESS_H */

