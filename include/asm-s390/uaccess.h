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
#if 0
#include <asm/segment.h>
#endif
#define VERIFY_READ     0
#define VERIFY_WRITE    1


/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

#define MAKE_MM_SEG(s,a)  ((mm_segment_t) { (s),(a) })


#define KERNEL_DS       MAKE_MM_SEG(0x7FFFFFFF,0)
#define USER_DS         MAKE_MM_SEG(PAGE_OFFSET,1)

#define get_ds()        (KERNEL_DS)
#define get_fs()        (current->thread.fs)
#define set_fs(x)       ({asm volatile("sar   4,%0"::"a" (x.acc4)); \
                         current->thread.fs = (x);})

#define segment_eq(a,b) ((a).acc4 == (b).acc4)


#define __access_ok(addr,size) ((((long) addr + size)&0x7FFFFFFFL) < current->addr_limit.seg)

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

/* Returns 0 if exception not found and fixup otherwise.  */
extern unsigned long search_exception_table(unsigned long);


/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 */

extern inline int __put_user_asm_4(__u32 x, void *ptr)
{
        int err;

        __asm__ __volatile__ (  "   iac   1\n"
				"   sr    %1,%1\n"
				"   la    4,%0\n"
                                "   sacf  512\n"
                                "0: st    %2,0(4)\n"
                                "   sacf  0(1)\n"
				"1:\n"
				".section .fixup,\"ax\"\n"
				"2: sacf  0(1)\n"
				"   lhi   %1,%h3\n"
				"   bras  4,3f\n"
				"   .long 1b\n"
				"3: l     4,0(4)\n"
				"   br    4\n"
				".previous\n"
				".section __ex_table,\"a\"\n"
				"   .align 4\n"
				"   .long  0b,2b\n"
				".previous"
                                : "=m" (*((__u32*) ptr)) , "=&d" (err)
                                : "d" (x), "K" (-EFAULT)
                                : "1", "4" );
        return err;
}

extern inline int __put_user_asm_2(__u16 x, void *ptr)
{
        int err;

        __asm__ __volatile__ (  "   iac   1\n"
				"   sr    %1,%1\n"
				"   la    4,%0\n"
                                "   sacf  512\n"
                                "0: sth   %2,0(4)\n"
                                "   sacf  0(1)\n"
				"1:\n"
				".section .fixup,\"ax\"\n"
				"2: sacf  0(1)\n"
				"   lhi   %1,%h3\n"
				"   bras  4,3f\n"
				"   .long 1b\n"
				"3: l     4,0(4)\n"
				"   br    4\n"
				".previous\n"
				".section __ex_table,\"a\"\n"
				"   .align 4\n"
				"   .long  0b,2b\n"
				".previous"
                                : "=m" (*((__u16*) ptr)) , "=&d" (err)
                                : "d" (x), "K" (-EFAULT)
                                : "1", "4" );
        return err;
}

extern inline int __put_user_asm_1(__u8 x, void *ptr)
{
        int err;

        __asm__ __volatile__ (  "   iac   1\n"
				"   sr    %1,%1\n"
				"   la    4,%0\n"
                                "   sacf  512\n"
                                "0: stc   %2,0(4)\n"
                                "   sacf  0(1)\n"
				"1:\n"
				".section .fixup,\"ax\"\n"
				"2: sacf  0(1)\n"
				"   lhi   %1,%h3\n"
				"   bras  4,3f\n"
				"   .long 1b\n"
				"3: l     4,0(4)\n"
				"   br    4\n"
				".previous\n"
				".section __ex_table,\"a\"\n"
				"   .align 4\n"
				"   .long  0b,2b\n"
				".previous"
                                : "=m" (*((__u8*) ptr)) , "=&d" (err)
                                : "d" (x), "K" (-EFAULT)
                                : "1", "4" );
        return err;
}

/*
 * (u8)(u32) ... autsch, but that the only way we can suppress the
 * warnings when compiling binfmt_elf.c
 */
#define __put_user(x, ptr)                                      \
({                                                              \
        int __pu_err;                                           \
        switch (sizeof (*(ptr))) {                              \
                case 1:                                         \
                        __pu_err = __put_user_asm_1((__u8)(__u32)x,ptr);\
                        break;                                  \
                case 2:                                         \
                        __pu_err = __put_user_asm_2((__u16)(__u32)x,ptr);\
                        break;                                  \
                case 4:                                         \
                        __pu_err = __put_user_asm_4((__u32) x,ptr);\
                        break;                                  \
                default:                                        \
                __pu_err = __put_user_bad();                    \
                break;                                          \
         }                                                      \
        __pu_err;                                               \
})

#define put_user(x, ptr)                                        \
({                                                              \
        long __pu_err = -EFAULT;                                \
        __typeof__(*(ptr)) *__pu_addr = (ptr);                  \
        __typeof__(x) __x = (x);                                \
        if (__access_ok((long)__pu_addr,sizeof(*(ptr)))) {      \
                __pu_err = 0;                                   \
                __put_user((__x), (__pu_addr));                 \
        }                                                       \
        __pu_err;                                               \
})

extern int __put_user_bad(void);


#define __get_user_asm_4(x, ptr, err)                                   \
({                                                                      \
        __asm__ __volatile__ (  "   iac   1\n"                          \
                                "   sr    %1,%1\n"                      \
                                "   la    4,%2\n"                       \
                                "   sacf  512\n"                        \
                                "0: l     %0,0(4)\n"                    \
                                "   sacf  0(1)\n"                       \
                                "1:\n"                                  \
                                ".section .fixup,\"ax\"\n"              \
                                "2: sacf  0(1)\n"                       \
                                "   lhi   %1,%h3\n"                     \
                                "   bras  4,3f\n"                       \
                                "   .long 1b\n"                         \
                                "3: l     4,0(4)\n"                     \
                                "   br    4\n"                          \
                                ".previous\n"                           \
                                ".section __ex_table,\"a\"\n"           \
                                "   .align 4\n"                         \
                                "   .long 0b,2b\n"                      \
                                ".previous"                             \
                                : "=d" (x) , "=&d" (err)                \
                                : "m" (*(__u32*) ptr), "K" (-EFAULT)    \
                                : "1", "4" );                           \
})

#define __get_user_asm_2(x, ptr, err)                                   \
({                                                                      \
        __asm__ __volatile__ (  "   iac   1\n"                          \
                                "   sr    %1,%1\n"                      \
                                "   la    4,%2\n"                       \
                                "   sacf  512\n"                        \
                                "0: lh    %0,0(4)\n"                    \
                                "   sacf  0(1)\n"                       \
                                "1:\n"                                  \
                                ".section .fixup,\"ax\"\n"              \
                                "2: sacf  0(1)\n"                       \
                                "   lhi   %1,%h3\n"                     \
                                "   bras  4,3f\n"                       \
                                "   .long 1b\n"                         \
                                "3: l     4,0(4)\n"                     \
                                "   br    4\n"                          \
                                ".previous\n"                           \
                                ".section __ex_table,\"a\"\n"           \
                                "   .align 4\n"                         \
                                "   .long 0b,2b\n"                      \
                                ".previous"                             \
                                : "=d" (x) , "=&d" (err)                \
                                : "m" (*(__u16*) ptr), "K" (-EFAULT)    \
                                : "1", "4" );                           \
})

#define __get_user_asm_1(x, ptr, err)                                   \
({                                                                      \
        __asm__ __volatile__ (  "   iac   1\n"                          \
                                "   sr    %1,%1\n"                      \
                                "   la    4,%2\n"                       \
                                "   sr    %0,%0\n"                      \
                                "   sacf  512\n"                        \
                                "0: ic    %0,0(4)\n"                    \
                                "   sacf  0(1)\n"                       \
                                "1:\n"                                  \
                                ".section .fixup,\"ax\"\n"              \
                                "2: sacf  0(1)\n"                       \
                                "   lhi   %1,%h3\n"                     \
                                "   bras  4,3f\n"                       \
                                "   .long 1b\n"                         \
                                "3: l     4,0(4)\n"                     \
                                "   br    4\n"                          \
                                ".previous\n"                           \
                                ".section __ex_table,\"a\"\n"           \
                                "   .align 4\n"                         \
                                "   .long 0b,2b\n"                      \
                                ".previous"                             \
                                : "=d" (x) , "=&d" (err)                \
                                : "m" (*(__u8*) ptr), "K" (-EFAULT)     \
                                : "1", "4" );                           \
})

#define __get_user(x, ptr)                                      \
({                                                              \
        int __gu_err;                                           \
        switch (sizeof(*(ptr))) {                               \
                case 1:                                         \
                        __get_user_asm_1(x,ptr,__gu_err);       \
                        break;                                  \
                case 2:                                         \
                        __get_user_asm_2(x,ptr,__gu_err);       \
                        break;                                  \
                case 4:                                         \
                        __get_user_asm_4(x,ptr,__gu_err);       \
                        break;                                  \
                default:                                        \
                        (x) = 0;                                \
                        __gu_err = __get_user_bad();            \
                break;                                          \
        }                                                       \
        __gu_err;                                               \
})

#define get_user(x, ptr)                                        \
({                                                              \
        long __gu_err = -EFAULT;                                \
        __typeof__(ptr) __gu_addr = (ptr);                      \
        __typeof__(x) __x;                                      \
        if (__access_ok((long)__gu_addr,sizeof(*(ptr)))) {      \
                __gu_err = 0;                                   \
                __get_user((__x), (__gu_addr));                 \
                (x) = __x;                                      \
        }                                                       \
        else                                                    \
                (x) = 0;                                        \
        __gu_err;                                               \
})

extern int __get_user_bad(void);

/*
 * access register are set up, that 4 points to secondary (user) , 2 to primary (kernel)
 */

extern inline unsigned long
__copy_to_user_asm(void* to, const void* from,  long n)
{

        __asm__ __volatile__ (  "   iac   1\n"
                                "   lr    2,%2\n"
                                "   lr    4,%1\n"
                                "   lr    3,%0\n"
                                "   lr    5,3\n"
                                "   sacf  512\n"
                                "0: mvcle 4,2,0\n"
                                "   jo    0b\n"
                                "1: sacf  0(1)\n"
                                "   lr    %0,3\n"
				".section __ex_table,\"a\"\n"
				"   .align 4\n"
				"   .long  0b,1b\n"
				".previous"
                                : "+&d" (n) : "d" (to), "d" (from)
                                : "1", "2", "3", "4", "5" );
        return n;
}

#define __copy_to_user(to, from, n)                             \
({                                                              \
        __copy_to_user_asm(to,from,n);                          \
})

#define copy_to_user(to, from, n)                               \
({                                                              \
        long err = 0;                                           \
        __typeof__(n) __n = (n);                                \
        if (__access_ok(to,__n)) {                              \
                err = __copy_to_user_asm(to,from,__n);          \
        }                                                       \
        else                                                    \
                err = __n;                                      \
        err;                                                    \
})

extern inline unsigned long
__copy_from_user_asm(void* to, const void* from,  long n)
{
        __asm__ __volatile__ (  "   iac   1\n"
				"   lr    2,%1\n"
                                "   lr    4,%2\n"
                                "   lr    3,%0\n"
                                "   lr    5,3\n"
                                "   sacf  512\n"
                                "0: mvcle 2,4,0\n"
                                "   jo    0b\n"
                                "1: sacf  0(1)\n"
                                "   lr    %0,3\n"
				".section __ex_table,\"a\"\n"
				"   .align 4\n"
				"   .long  0b,1b\n"
				".previous"
                                : "+&d" (n) : "d" (to), "d" (from)
                                : "1", "2", "3", "4", "5" );
        return n;
}


#define __copy_from_user(to, from, n)                           \
({                                                              \
        __copy_from_user_asm(to,from,n);                        \
})

#define copy_from_user(to, from, n)                             \
({                                                              \
        long err = 0;                                           \
        __typeof__(n) __n = (n);                                \
        if (__access_ok(from,__n)) {                            \
                err = __copy_from_user_asm(to,from,__n);        \
        }                                                       \
        else                                                    \
                err = __n;                                      \
        err;                                                    \
})

/*
 * Copy a null terminated string from userspace.
 */

static inline long
strncpy_from_user(char *dst, const char *src, long count)
{
        int len;
        __asm__ __volatile__ (  "   iac   1\n"
				"   slr   %0,%0\n"
				"   lr    2,%1\n"
                                "   lr    4,%2\n"
                                "   slr   3,3\n"
                                "   sacf  512\n"
                                "0: ic    3,0(%0,4)\n"
                                "1: stc   3,0(%0,2)\n"
                                "   ltr   3,3\n"
                                "   jz    2f\n"
                                "   ahi   %0,1\n"
                                "   clr   %0,%3\n"
                                "   jl    0b\n"
                                "2: sacf  0(1)\n"
				".section .fixup,\"ax\"\n"
                                "3: lhi   %0,%h4\n"
				"   basr  3,0\n"
                                "   l     3,4f-.(3)\n"
                                "   br    3\n"
				"4: .long 2b\n"
				".previous\n"
				".section __ex_table,\"a\"\n"
				"   .align 4\n"
				"   .long  0b,3b\n"
                                "   .long  1b,3b\n"
				".previous"
                                : "=&a" (len)
                                : "a" (dst), "d" (src), "d" (count),
                                  "K" (-EFAULT)
                                : "1", "2", "3", "4", "memory" );
        return len;
}

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 for error
 */
static inline unsigned long
strnlen_user(const char * src, unsigned long n)
{
	__asm__ __volatile__ ("   iac   1\n"
                              "   alr   %0,%1\n"
			      "   slr   0,0\n"
			      "   lr    4,%1\n"
			      "   sacf  512\n"
			      "0: srst  %0,4\n"
			      "   jo    0b\n"
			      "   slr   %0,%1\n"
			      "   ahi   %0,1\n"
			      "   sacf  0(1)\n"
                              "1:\n"
                              ".section .fixup,\"ax\"\n"
                              "2: sacf  0(1)\n"
                              "   slr   %0,%0\n"
                              "   bras  4,3f\n"
                              "   .long 1b\n"
                              "3: l     4,0(4)\n"
                              "   br    4\n"
                              ".previous\n"
			      ".section __ex_table,\"a\"\n"
			      "   .align 4\n"
			      "   .long  0b,2b\n"
			      ".previous"
			      : "+&a" (n) : "d" (src)
			      : "cc", "0", "1", "4" );
        return n;
}
#define strlen_user(str) strnlen_user(str, ~0UL)

/*
 * Zero Userspace
 */

static inline unsigned long
clear_user(void *to, unsigned long n)
{
        __asm__ __volatile__ (  "   iac   1\n"
                                "   sacf  512\n"
                                "   lr    4,%1\n"
                                "   lr    5,%0\n"
                                "   sr    2,2\n"
                                "   sr    3,3\n"
                                "0: mvcle 4,2,0\n"
                                "   jo    0b\n"
                                "1: sacf  0(1)\n"
                                "   lr    %0,3\n"
				".section __ex_table,\"a\"\n"
				"   .align 4\n"
				"   .long  0b,1b\n"
				".previous"
                                : "+&a" (n)
                                : "a"   (to)
                                : "cc", "1", "2", "3", "4", "5" );
        return n;
}

#endif                                 /* _S390_UACCESS_H                  */
