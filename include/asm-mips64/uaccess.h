/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_UACCESS_H
#define _ASM_UACCESS_H

#include <linux/errno.h>
#include <linux/sched.h>

#define STR(x)  __STR(x)
#define __STR(x)  #x

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */
#define KERNEL_DS	((mm_segment_t) { (unsigned long) 0L })
#define USER_DS		((mm_segment_t) { (unsigned long) -1L })

#define VERIFY_READ    0
#define VERIFY_WRITE   1

#define get_fs()        (current->thread.current_ds)
#define get_ds()	(KERNEL_DS)
#define set_fs(x)       (current->thread.current_ds=(x))

#define segment_eq(a,b)	((a).seg == (b).seg)


/*
 * Is a address valid? This does a straighforward calculation rather
 * than tests.
 *
 * Address valid if:
 *  - "addr" doesn't have any high-bits set
 *  - AND "size" doesn't have any high-bits set
 *  - AND "addr+size" doesn't have any high-bits set
 *  - OR we are in kernel mode.
 */
#define __access_ok(addr,size,mask) \
        (((__signed__ long)((mask)&(addr | size | (addr+size)))) >= 0)
#define __access_mask ((long)(get_fs().seg))

#define access_ok(type,addr,size) \
__access_ok(((unsigned long)(addr)),(size),__access_mask)

extern inline int verify_area(int type, const void * addr, unsigned long size)
{
	return access_ok(type,addr,size) ? 0 : -EFAULT;
}

/*
 * Uh, these should become the main single-value transfer routines ...
 * They automatically use the right size if we just have the right
 * pointer type ...
 *
 * As MIPS uses the same address space for kernel and user data, we
 * can just do these as direct assignments.
 *
 * Careful to not
 * (a) re-use the arguments for side effects (sizeof is ok)
 * (b) require any knowledge of processes at this stage
 */
#define put_user(x,ptr)	\
	__put_user_check((__typeof__(*(ptr)))(x),(ptr),sizeof(*(ptr)))
#define get_user(x,ptr) \
	__get_user_check((__typeof__(*(ptr)))(x),(ptr),sizeof(*(ptr)))

/*
 * The "__xxx" versions do not do address space checking, useful when
 * doing multiple accesses to the same area (the user has to do the
 * checks by hand with "access_ok()")
 */
#define __put_user(x,ptr) \
	__put_user_nocheck((__typeof__(*(ptr)))(x),(ptr),sizeof(*(ptr)))
#define __get_user(x,ptr) \
	__get_user_nocheck((__typeof__(*(ptr)))(x),(ptr),sizeof(*(ptr)))

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct *)(x))

#define __get_user_nocheck(x,ptr,size) ({ \
long __gu_err; \
__typeof(*(ptr)) __gu_val; \
long __gu_addr; \
__asm__("":"=r" (__gu_val)); \
__gu_addr = (long) (ptr); \
__asm__("":"=r" (__gu_err)); \
switch (size) { \
case 1: __get_user_asm("lb"); break; \
case 2: __get_user_asm("lh"); break; \
case 4: __get_user_asm("lw"); break; \
case 8:  __get_user_asm("ld"); break; \
default: __get_user_unknown(); break; \
} x = (__typeof__(*(ptr))) __gu_val; __gu_err; })

#define __get_user_check(x,ptr,size) ({ \
long __gu_err; \
__typeof__(*(ptr)) __gu_val; \
long __gu_addr; \
__asm__("":"=r" (__gu_val)); \
__gu_addr = (long) (ptr); \
__asm__("":"=r" (__gu_err)); \
if (__access_ok(__gu_addr,size,__access_mask)) { \
switch (size) { \
case 1: __get_user_asm("lb"); break; \
case 2: __get_user_asm("lh"); break; \
case 4: __get_user_asm("lw"); break; \
case 8: __get_user_asm("ld"); break; \
default: __get_user_unknown(); break; \
} } x = (__typeof__(*(ptr))) __gu_val; __gu_err; })

#define __get_user_asm(insn) \
({ \
__asm__ __volatile__( \
	"1:\t" insn "\t%1,%2\n\t" \
	"move\t%0,$0\n" \
	"2:\n\t" \
	".section\t.fixup,\"ax\"\n" \
	"3:\tli\t%0,%3\n\t" \
	"move\t%1,$0\n\t" \
	"j\t2b\n\t" \
	".previous\n\t" \
	".section\t__ex_table,\"a\"\n\t" \
	".dword\t1b,3b\n\t" \
	".previous" \
	:"=r" (__gu_err), "=r" (__gu_val) \
	:"o" (__m(__gu_addr)), "i" (-EFAULT)); })

extern void __get_user_unknown(void);

#define __put_user_nocheck(x,ptr,size) ({ \
long __pu_err; \
__typeof__(*(ptr)) __pu_val; \
long __pu_addr; \
__pu_val = (x); \
__pu_addr = (long) (ptr); \
__asm__("":"=r" (__pu_err)); \
switch (size) { \
case 1: __put_user_asm("sb"); break; \
case 2: __put_user_asm("sh"); break; \
case 4: __put_user_asm("sw"); break; \
case 8: __put_user_asm("sd"); break; \
default: __put_user_unknown(); break; \
} __pu_err; })

#define __put_user_check(x,ptr,size) ({ \
long __pu_err; \
__typeof__(*(ptr)) __pu_val; \
long __pu_addr; \
__pu_val = (x); \
__pu_addr = (long) (ptr); \
__asm__("":"=r" (__pu_err)); \
if (__access_ok(__pu_addr,size,__access_mask)) { \
switch (size) { \
case 1: __put_user_asm("sb"); break; \
case 2: __put_user_asm("sh"); break; \
case 4: __put_user_asm("sw"); break; \
case 8: __put_user_asm("sd"); break; \
default: __put_user_unknown(); break; \
} } __pu_err; })

#define __put_user_asm(insn) \
({ \
__asm__ __volatile__( \
	"1:\t" insn "\t%1,%2\n\t" \
	"move\t%0,$0\n" \
	"2:\n\t" \
	".section\t.fixup,\"ax\"\n" \
	"3:\tli\t%0,%3\n\t" \
	"j\t2b\n\t" \
	".previous\n\t" \
	".section\t__ex_table,\"a\"\n\t" \
	".dword\t1b,3b\n\t" \
	".previous" \
	:"=r" (__pu_err) \
	:"r" (__pu_val), "o" (__m(__pu_addr)), "i" (-EFAULT)); })

extern void __put_user_unknown(void);

/*
 * We're generating jump to subroutines which will be outside the range of
 * jump instructions
 */
#ifdef MODULE
#define __MODULE_JAL(destination) \
	".set\tnoat\n\t" \
	"la\t$1, " #destination "\n\t" \
	"jalr\t$1\n\t" \
	".set\tat\n\t"
#else
#define __MODULE_JAL(destination) \
	"jal\t" #destination "\n\t"
#endif

extern size_t __copy_user(void *__to, const void *__from, size_t __n);

#define __copy_to_user(to,from,n) ({ \
	void *__cu_to; \
	const void *__cu_from; \
	long __cu_len; \
	\
	__cu_to = (to); \
	__cu_from = (from); \
	__cu_len = (n); \
	__asm__ __volatile__( \
		"move\t$4, %1\n\t" \
		"move\t$5, %2\n\t" \
		"move\t$6, %3\n\t" \
		__MODULE_JAL(__copy_user) \
		"move\t%0, $6" \
		: "=r" (__cu_len) \
		: "r" (__cu_to), "r" (__cu_from), "r" (__cu_len) \
		: "$4", "$5", "$6", "$8", "$9", "$10", "$11", "$12", "$15", \
		  "$24", "$31","memory"); \
	__cu_len; \
})

#define __copy_from_user(to,from,n) ({ \
	void *__cu_to; \
	const void *__cu_from; \
	long __cu_len; \
	\
	__cu_to = (to); \
	__cu_from = (from); \
	__cu_len = (n); \
	__asm__ __volatile__( \
		"move\t$4, %1\n\t" \
		"move\t$5, %2\n\t" \
		"move\t$6, %3\n\t" \
		".set\tnoreorder\n\t" \
		__MODULE_JAL(__copy_user) \
		".set\tnoat\n\t" \
		"daddu\t$1, %2, %3\n\t" \
		".set\tat\n\t" \
		".set\treorder\n\t" \
		"move\t%0, $6" \
		: "=r" (__cu_len) \
		: "r" (__cu_to), "r" (__cu_from), "r" (__cu_len) \
		: "$4", "$5", "$6", "$8", "$9", "$10", "$11", "$12", "$15", \
		  "$24", "$31","memory"); \
	__cu_len; \
})

#define copy_to_user(to,from,n) ({ \
	void *__cu_to; \
	const void *__cu_from; \
	long __cu_len; \
	\
	__cu_to = (to); \
	__cu_from = (from); \
	__cu_len = (n); \
	if (access_ok(VERIFY_WRITE, __cu_to, __cu_len)) \
		__asm__ __volatile__( \
			"move\t$4, %1\n\t" \
			"move\t$5, %2\n\t" \
			"move\t$6, %3\n\t" \
			__MODULE_JAL(__copy_user) \
			"move\t%0, $6" \
			: "=r" (__cu_len) \
			: "r" (__cu_to), "r" (__cu_from), "r" (__cu_len) \
			: "$4", "$5", "$6", "$8", "$9", "$10", "$11", "$12", \
			  "$15", "$24", "$31","memory"); \
	__cu_len; \
})

#define copy_from_user(to,from,n) ({ \
	void *__cu_to; \
	const void *__cu_from; \
	long __cu_len; \
	\
	__cu_to = (to); \
	__cu_from = (from); \
	__cu_len = (n); \
	if (access_ok(VERIFY_READ, __cu_from, __cu_len)) \
		__asm__ __volatile__( \
			"move\t$4, %1\n\t" \
			"move\t$5, %2\n\t" \
			"move\t$6, %3\n\t" \
			".set\tnoreorder\n\t" \
			__MODULE_JAL(__copy_user) \
			".set\tnoat\n\t" \
			"daddu\t$1, %2, %3\n\t" \
			".set\tat\n\t" \
			".set\treorder\n\t" \
			"move\t%0, $6" \
			: "=r" (__cu_len) \
			: "r" (__cu_to), "r" (__cu_from), "r" (__cu_len) \
			: "$4", "$5", "$6", "$8", "$9", "$10", "$11", "$12", \
			  "$15", "$24", "$31","memory"); \
	__cu_len; \
})

extern inline __kernel_size_t
__clear_user(void *addr, __kernel_size_t size)
{
	__kernel_size_t res;

	__asm__ __volatile__(
		"move\t$4, %1\n\t"
		"move\t$5, $0\n\t"
		"move\t$6, %2\n\t"
		__MODULE_JAL(__bzero)
		"move\t%0, $6"
		: "=r" (res)
		: "r" (addr), "r" (size)
		: "$4", "$5", "$6", "$8", "$9", "$31");

	return res;
}

#define clear_user(addr,n) ({ \
void * __cl_addr = (addr); \
unsigned long __cl_size = (n); \
if (__cl_size && __access_ok(VERIFY_WRITE, ((unsigned long)(__cl_addr)), __cl_size)) \
__cl_size = __clear_user(__cl_addr, __cl_size); \
__cl_size; })

/*
 * Returns: -EFAULT if exception before terminator, N if the entire
 * buffer filled, else strlen.
 */
extern inline long
__strncpy_from_user(char *__to, const char *__from, long __len)
{
	long res;

	__asm__ __volatile__(
		"move\t$4, %1\n\t"
		"move\t$5, %2\n\t"
		"move\t$6, %3\n\t"
		__MODULE_JAL(__strncpy_from_user_nocheck_asm)
		"move\t%0, $2"
		: "=r" (res)
		: "r" (__to), "r" (__from), "r" (__len)
		: "$2", "$3", "$4", "$5", "$6", "$8", "$31", "memory");

	return res;
}

extern inline long
strncpy_from_user(char *__to, const char *__from, long __len)
{
	long res;

	__asm__ __volatile__(
		"move\t$4, %1\n\t"
		"move\t$5, %2\n\t"
		"move\t$6, %3\n\t"
		__MODULE_JAL(__strncpy_from_user_asm)
		"move\t%0, $2"
		: "=r" (res)
		: "r" (__to), "r" (__from), "r" (__len)
		: "$2", "$3", "$4", "$5", "$6", "$8", "$31", "memory");

	return res;
}

/* Returns: 0 if bad, string length+1 (memory size) of string if ok */
extern inline long __strlen_user(const char *s)
{
	long res;

	__asm__ __volatile__(
		"move\t$4, %1\n\t"
		__MODULE_JAL(__strlen_user_nocheck_asm)
		"move\t%0, $2"
		: "=r" (res)
		: "r" (s)
		: "$2", "$4", "$8", "$31");

	return res;
}

extern inline long strlen_user(const char *s)
{
	long res;

	__asm__ __volatile__(
		"move\t$4, %1\n\t"
		__MODULE_JAL(__strlen_user_asm)
		"move\t%0, $2"
		: "=r" (res)
		: "r" (s)
		: "$2", "$4", "$8", "$31");

	return res;
}

/* Returns: 0 if bad, string length+1 (memory size) of string if ok */
extern inline long __strnlen_user(const char *s, long n)
{
	long res;

	__asm__ __volatile__(
		"move\t$4, %1\n\t"
		"move\t$5, %2\n\t"
		__MODULE_JAL(__strlen_user_nocheck_asm)
		"move\t%0, $2"
		: "=r" (res)
		: "r" (s), "r" (n)
		: "$2", "$4", "$5", "$8", "$31");

	return res;
}

extern inline long strnlen_user(const char *s, long n)
{
	long res;

	__asm__ __volatile__(
		"move\t$4, %1\n\t"
		"move\t$5, %2\n\t"
		__MODULE_JAL(__strlen_user_asm)
		"move\t%0, $2"
		: "=r" (res)
		: "r" (s), "r" (n)
		: "$2", "$4", "$5", "$8", "$31");

	return res;
}

struct exception_table_entry
{
	unsigned long insn;
	unsigned long nextinsn;
};

/* Returns 0 if exception not found and fixup.unit otherwise.  */
extern unsigned long search_exception_table(unsigned long addr);

/* Returns the new pc */
#define fixup_exception(map_reg, fixup_unit, pc)                \
({                                                              \
	fixup_unit;                                             \
})

#endif /* _ASM_UACCESS_H */
