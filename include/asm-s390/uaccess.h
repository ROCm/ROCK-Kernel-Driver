/*
 *  include/asm-s390/uaccess.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/uaccess.h"
 */
#ifndef __S390_UACCESS_H
#define __S390_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/sched.h>
#include <linux/errno.h>

#define VERIFY_READ     0
#define VERIFY_WRITE    1


/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

#define MAKE_MM_SEG(a)  ((mm_segment_t) { (a) })


#define KERNEL_DS       MAKE_MM_SEG(0)
#define USER_DS         MAKE_MM_SEG(1)

#define get_ds()        (KERNEL_DS)
#define get_fs()        ({ mm_segment_t __x; \
			   asm volatile("ear   %0,4":"=a" (__x)); \
			   __x;})
#define set_fs(x)       ({asm volatile("sar   4,%0"::"a" ((x).ar4));})

#define segment_eq(a,b) ((a).ar4 == (b).ar4)


#define __access_ok(addr,size) (1)

#define access_ok(type,addr,size) __access_ok(addr,size)

extern inline int verify_area(int type, const void * addr, unsigned long size)
{
        return access_ok(type,addr,size)?0:-EFAULT;
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

/*
 * Standard fixup section for uaccess inline functions.
 * local label 0: is the fault point
 * local label 1: is the return point
 * %0 is the error variable
 * %3 is the error value -EFAULT
 */
#ifndef __s390x__
#define __uaccess_fixup \
	".section .fixup,\"ax\"\n"	\
	"8: sacf  0\n"			\
	"   lhi	  %0,%h3\n"		\
	"   bras  4,9f\n"		\
	"   .long 1b\n"			\
	"9: l	  4,0(4)\n"		\
	"   br	  4\n"			\
	".previous\n"			\
	".section __ex_table,\"a\"\n"	\
	"   .align 4\n"			\
	"   .long  0b,8b\n"		\
	".previous"
#else /* __s390x__ */
#define __uaccess_fixup \
	".section .fixup,\"ax\"\n"	\
	"9: sacf  0\n"			\
	"   lhi	  %0,%h3\n"		\
	"   jg	  1b\n"			\
	".previous\n"			\
	".section __ex_table,\"a\"\n"	\
	"   .align 8\n"			\
	"   .quad  0b,9b\n"		\
	".previous"
#endif /* __s390x__ */

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 */
#ifndef __s390x__

#define __put_user_asm_8(x, ptr, err) \
({								\
	register __typeof__(x) const * __from asm("2");		\
	register __typeof__(*(ptr)) * __to asm("4");		\
	__from = &(x);						\
	__to = (ptr);						\
	__asm__ __volatile__ (					\
		"   sacf  512\n"				\
		"0: mvc	  0(8,%1),0(%2)\n"			\
		"   sacf  0\n"					\
		"1:\n"						\
		__uaccess_fixup					\
		: "=&d" (err)					\
		: "a" (__to),"a" (__from),"K" (-EFAULT),"0" (0)	\
		: "cc" );					\
})

#else /* __s390x__ */

#define __put_user_asm_8(x, ptr, err) \
({								\
	register __typeof__(*(ptr)) * __ptr asm("4");		\
	__ptr = (ptr);						\
	__asm__ __volatile__ (					\
		"   sacf  512\n"				\
		"0: stg	  %2,0(%1)\n"				\
		"   sacf  0\n"					\
		"1:\n"						\
		__uaccess_fixup					\
		: "=&d" (err)					\
		: "a" (__ptr), "d" (x), "K" (-EFAULT), "0" (0)	\
		: "cc" );					\
})

#endif /* __s390x__ */

#define __put_user_asm_4(x, ptr, err) \
({								\
	register __typeof__(*(ptr)) * __ptr asm("4");		\
	__ptr = (ptr);						\
	__asm__ __volatile__ (					\
		"   sacf  512\n"				\
		"0: st	  %2,0(%1)\n"				\
		"   sacf  0\n"					\
		"1:\n"						\
		__uaccess_fixup					\
		: "=&d" (err)					\
		: "a" (__ptr), "d" (x), "K" (-EFAULT), "0" (0)	\
		: "cc" );					\
})

#define __put_user_asm_2(x, ptr, err) \
({								\
	register __typeof__(*(ptr)) * __ptr asm("4");		\
	__ptr = (ptr);						\
	__asm__ __volatile__ (					\
		"   sacf  512\n"				\
		"0: sth	  %2,0(%1)\n"				\
		"   sacf  0\n"					\
		"1:\n"						\
		__uaccess_fixup					\
		: "=&d" (err)					\
		: "a" (__ptr), "d" (x), "K" (-EFAULT), "0" (0)	\
		: "cc" );					\
})

#define __put_user_asm_1(x, ptr, err) \
({								\
	register __typeof__(*(ptr)) * __ptr asm("4");		\
	__ptr = (ptr);						\
	__asm__ __volatile__ (					\
		"   sacf  512\n"				\
		"0: stc	  %2,0(%1)\n"				\
		"   sacf  0\n"					\
		"1:\n"						\
		__uaccess_fixup					\
		: "=&d" (err)					\
		: "a" (__ptr), "d" (x),	"K" (-EFAULT), "0" (0)	\
		: "cc" );					\
})

#define __put_user(x, ptr) \
({								\
	__typeof__(*(ptr)) __x = (x);				\
	int __pu_err;						\
	switch (sizeof (*(ptr))) {				\
	case 1:							\
		__put_user_asm_1(__x, ptr, __pu_err);		\
		break;						\
	case 2:							\
		__put_user_asm_2(__x, ptr, __pu_err);		\
		break;						\
	case 4:							\
		__put_user_asm_4(__x, ptr, __pu_err);		\
		break;						\
	case 8:							\
		__put_user_asm_8(__x, ptr, __pu_err);		\
		break;						\
	default:						\
		__pu_err = __put_user_bad();			\
		break;						\
	 }							\
	__pu_err;						\
})

#define put_user(x, ptr)					\
({								\
	might_sleep();						\
	__put_user(x, ptr);					\
})


extern int __put_user_bad(void);

#ifndef __s390x__

#define __get_user_asm_8(x, ptr, err) \
({								\
	register __typeof__(*(ptr)) const * __from asm("4");	\
	register __typeof__(x) * __to asm("2");			\
	__from = (ptr);						\
	__to = &(x);						\
	__asm__ __volatile__ (					\
		"   sacf  512\n"				\
		"0: mvc	  0(8,%2),0(%4)\n"			\
		"   sacf  0\n"					\
		"1:\n"						\
		__uaccess_fixup					\
		: "=&d" (err), "=m" (x)				\
		: "a" (__to),"K" (-EFAULT),"a" (__from),"0" (0)	\
		: "cc" );					\
})

#else /* __s390x__ */

#define __get_user_asm_8(x, ptr, err) \
({								\
	register __typeof__(*(ptr)) const * __ptr asm("4");	\
	__ptr = (ptr);						\
	__asm__ __volatile__ (					\
		"   sacf  512\n"				\
		"0: lg	  %1,0(%2)\n"				\
		"   sacf  0\n"					\
		"1:\n"						\
		__uaccess_fixup					\
		: "=&d" (err), "=d" (x)				\
		: "a" (__ptr), "K" (-EFAULT), "0" (0)		\
		: "cc" );					\
})

#endif /* __s390x__ */


#define __get_user_asm_4(x, ptr, err) \
({								\
	register __typeof__(*(ptr)) const * __ptr asm("4");	\
	__ptr = (ptr);						\
	__asm__ __volatile__ (					\
		"   sacf  512\n"				\
		"0: l	  %1,0(%2)\n"				\
		"   sacf  0\n"					\
		"1:\n"						\
		__uaccess_fixup					\
		: "=&d" (err), "=d" (x)				\
		: "a" (__ptr), "K" (-EFAULT), "0" (0)		\
		: "cc" );					\
})

#define __get_user_asm_2(x, ptr, err) \
({								\
	register __typeof__(*(ptr)) const * __ptr asm("4");	\
	__ptr = (ptr);						\
	__asm__ __volatile__ (					\
		"   sacf  512\n"				\
		"0: lh	  %1,0(%2)\n"				\
		"   sacf  0\n"					\
		"1:\n"						\
		__uaccess_fixup					\
		: "=&d" (err), "=d" (x)				\
		: "a" (__ptr), "K" (-EFAULT), "0" (0)		\
		: "cc" );					\
})

#define __get_user_asm_1(x, ptr, err) \
({								\
	register __typeof__(*(ptr)) const * __ptr asm("4");	\
	__ptr = (ptr);						\
	__asm__ __volatile__ (					\
		"   sr	  %1,%1\n"				\
		"   sacf  512\n"				\
		"0: ic	  %1,0(%2)\n"				\
		"   sacf  0\n"					\
		"1:\n"						\
		__uaccess_fixup					\
		: "=&d" (err), "=&d" (x)			\
		: "a" (__ptr), "K" (-EFAULT), "0" (0)		\
		: "cc" );					\
})

#define __get_user(x, ptr)					\
({								\
	__typeof__(*(ptr)) __x;					\
	int __gu_err;						\
	switch (sizeof(*(ptr))) {				\
	case 1:							\
		__get_user_asm_1(__x, ptr, __gu_err);		\
		break;						\
	case 2:							\
		__get_user_asm_2(__x, ptr, __gu_err);		\
		break;						\
	case 4:							\
		__get_user_asm_4(__x, ptr, __gu_err);		\
		break;						\
	case 8:							\
		__get_user_asm_8(__x, ptr, __gu_err);		\
		break;						\
	default:						\
		__x = 0;					\
		__gu_err = __get_user_bad();			\
		break;						\
	}							\
	(x) = __x;						\
	__gu_err;						\
})

#define get_user(x, ptr)					\
({								\
	might_sleep();						\
	__get_user(x, ptr);					\
})

extern int __get_user_bad(void);

/*
 * access register are set up, that 4 points to secondary (user),
 * 2 to primary (kernel)
 */

extern long __copy_to_user_asm(const void *from, long n, const void *to);

#define __copy_to_user(to, from, n)                             \
({                                                              \
        __copy_to_user_asm(from, n, to);                        \
})

#define copy_to_user(to, from, n)                               \
({                                                              \
        long err = 0;                                           \
        __typeof__(n) __n = (n);                                \
        might_sleep();						\
        if (__access_ok(to,__n)) {                              \
                err = __copy_to_user_asm(from, __n, to);        \
        }                                                       \
        else                                                    \
                err = __n;                                      \
        err;                                                    \
})

extern long __copy_from_user_asm(void *to, long n, const void *from);

#define __copy_from_user(to, from, n)                           \
({                                                              \
        __copy_from_user_asm(to, n, from);                      \
})

#define copy_from_user(to, from, n)                             \
({                                                              \
        long err = 0;                                           \
        __typeof__(n) __n = (n);                                \
        might_sleep();						\
        if (__access_ok(from,__n)) {                            \
                err = __copy_from_user_asm(to, __n, from);      \
        }                                                       \
        else                                                    \
                err = __n;                                      \
        err;                                                    \
})

/*
 * Copy a null terminated string from userspace.
 */

#ifndef __s390x__

static inline long
__strncpy_from_user(char *dst, const char *src, long count)
{
        int len;
        __asm__ __volatile__ (
		"   slr   %0,%0\n"
		"   lr    2,%1\n"
                "   lr    4,%2\n"
                "   slr   3,3\n"
                "   sacf  512\n"
		"0: ic	  3,0(%0,4)\n"
		"1: stc	  3,0(%0,2)\n"
		"   ltr	  3,3\n"
		"   jz	  2f\n"
		"   ahi	  %0,1\n"
		"   clr	  %0,%3\n"
		"   jl	  0b\n"
		"2: sacf  0\n"
		".section .fixup,\"ax\"\n"
		"3: lhi	  %0,%h4\n"
		"   basr  3,0\n"
		"   l	  3,4f-.(3)\n"
		"   br	  3\n"
		"4: .long 2b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"   .align 4\n"
		"   .long  0b,3b\n"
		"   .long  1b,3b\n"
		".previous"
		: "=&a" (len)
		: "a" (dst), "d" (src), "d" (count), "K" (-EFAULT)
		: "2", "3", "4", "memory", "cc" );
	return len;
}

#else /* __s390x__ */

static inline long
__strncpy_from_user(char *dst, const char *src, long count)
{
	long len;
	__asm__ __volatile__ (
		"   slgr  %0,%0\n"
		"   lgr	  2,%1\n"
		"   lgr	  4,%2\n"
		"   slr	  3,3\n"
		"   sacf  512\n"
		"0: ic	  3,0(%0,4)\n"
		"1: stc	  3,0(%0,2)\n"
		"   ltr	  3,3\n"
		"   jz	  2f\n"
		"   aghi  %0,1\n"
		"   cgr	  %0,%3\n"
		"   jl	  0b\n"
		"2: sacf  0\n"
		".section .fixup,\"ax\"\n"
		"3: lghi  %0,%h4\n"
		"   jg	  2b\n"	 
		".previous\n"
		".section __ex_table,\"a\"\n"
		"   .align 8\n"
		"   .quad  0b,3b\n"
		"   .quad  1b,3b\n"
		".previous"
		: "=&a" (len)
		: "a"  (dst), "d" (src), "d" (count), "K" (-EFAULT)
		: "cc", "2" ,"3", "4" );
	return len;
}

#endif /* __s390x__ */

static inline long
strncpy_from_user(char *dst, const char *src, long count)
{
        long res = -EFAULT;
        might_sleep();
        if (access_ok(VERIFY_READ, src, 1))
                res = __strncpy_from_user(dst, src, count);
        return res;
}


/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 for error
 */
#ifndef __s390x__

static inline unsigned long
strnlen_user(const char * src, unsigned long n)
{
	might_sleep();
	__asm__ __volatile__ (
		"   alr   %0,%1\n"
		"   slr   0,0\n"
		"   lr    4,%1\n"
		"   sacf  512\n"
		"0: srst  %0,4\n"
		"   jo    0b\n"
		"   slr   %0,%1\n"
		"   ahi   %0,1\n"
		"   sacf  0\n"
		"1:\n"
		".section .fixup,\"ax\"\n"
		"2: sacf  0\n"
		"   slr   %0,%0\n"
		"   bras  4,3f\n"
		"   .long 1b\n"
		"3: l     4,0(4)\n"
		"   br    4\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"  .align 4\n"
		"  .long  0b,2b\n"
		".previous"
		: "+&a" (n) : "d" (src)
		: "cc", "0", "4" );
	return n;
}

#else /* __s390x__ */

static inline unsigned long
strnlen_user(const char * src, unsigned long n)
{
	might_sleep();
#if 0
	__asm__ __volatile__ (
		"   algr  %0,%1\n"
		"   slgr  0,0\n"
		"   lgr	  4,%1\n"
		"   sacf  512\n"
		"0: srst  %0,4\n"
		"   jo	0b\n"
		"   slgr  %0,%1\n"
		"   aghi  %0,1\n"
		"1: sacf  0\n"
		".section .fixup,\"ax\"\n"
		"2: slgr  %0,%0\n"
		"   jg	  1b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"  .align 8\n"
		"  .quad  0b,2b\n"
		".previous"
		: "+&a" (n) : "d" (src)
		: "cc", "0", "4" );
#else
	__asm__ __volatile__ (
		"   lgr	  4,%1\n"
		"   sacf  512\n"
		"0: cli   0(4),0x00\n"
		"   la    4,1(4)\n"
		"   je    1f\n"
		"   brctg %0,0b\n"
		"1: lgr	  %0,4\n"
		"   slgr  %0,%1\n"
		"2: sacf  0\n"
		".section .fixup,\"ax\"\n"
		"3: slgr  %0,%0\n"
		"   jg    2b\n"  
		".previous\n"
		".section __ex_table,\"a\"\n"
		"  .align 8\n"
		"  .quad  0b,3b\n"
		".previous"
		: "+&a" (n) : "d" (src)
		: "cc", "4" );
#endif
	return n;
}

#endif /* __s390x__ */

#define strlen_user(str) strnlen_user(str, ~0UL)

/*
 * Zero Userspace
 */

extern long __clear_user_asm(void *to, long n);

#define __clear_user(to, n)					\
({								\
	__clear_user_asm(to, n);				\
})

static inline unsigned long
clear_user(void *to, unsigned long n)
{
	might_sleep();
	if (access_ok(VERIFY_WRITE, to, n))
		n = __clear_user(to, n);
	return n;
}


#endif				       /* _S390_UACCESS_H		   */
