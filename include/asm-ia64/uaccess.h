#ifndef _ASM_IA64_UACCESS_H
#define _ASM_IA64_UACCESS_H

/*
 * This file defines various macros to transfer memory areas across
 * the user/kernel boundary.  This needs to be done carefully because
 * this code is executed in kernel mode and uses user-specified
 * addresses.  Thus, we need to be careful not to let the user to
 * trick us into accessing kernel memory that would normally be
 * inaccessible.  This code is also fairly performance sensitive,
 * so we want to spend as little time doing safety checks as
 * possible.
 *
 * To make matters a bit more interesting, these macros sometimes also
 * called from within the kernel itself, in which case the address
 * validity check must be skipped.  The get_fs() macro tells us what
 * to do: if get_fs()==USER_DS, checking is performed, if
 * get_fs()==KERNEL_DS, checking is bypassed.
 *
 * Note that even if the memory area specified by the user is in a
 * valid address range, it is still possible that we'll get a page
 * fault while accessing it.  This is handled by filling out an
 * exception handler fixup entry for each instruction that has the
 * potential to fault.  When such a fault occurs, the page fault
 * handler checks to see whether the faulting instruction has a fixup
 * associated and, if so, sets r8 to -EFAULT and clears r9 to 0 and
 * then resumes execution at the continuation point.
 *
 * Copyright (C) 1998, 1999, 2001-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/sched.h>

#include <asm/intrinsics.h>
#include <asm/pgtable.h>

/*
 * For historical reasons, the following macros are grossly misnamed:
 */
#define KERNEL_DS	((mm_segment_t) { ~0UL })		/* cf. access_ok() */
#define USER_DS		((mm_segment_t) { TASK_SIZE-1 })	/* cf. access_ok() */

#define VERIFY_READ	0
#define VERIFY_WRITE	1

#define get_ds()  (KERNEL_DS)
#define get_fs()  (current_thread_info()->addr_limit)
#define set_fs(x) (current_thread_info()->addr_limit = (x))

#define segment_eq(a,b)	((a).seg == (b).seg)

/*
 * When accessing user memory, we need to make sure the entire area really is in
 * user-level space.  In order to do this efficiently, we make sure that the page at
 * address TASK_SIZE is never valid.  We also need to make sure that the address doesn't
 * point inside the virtually mapped linear page table.
 */
#define __access_ok(addr,size,segment)						\
	likely(((unsigned long) (addr)) <= (segment).seg			\
	       && ((segment).seg == KERNEL_DS.seg				\
		   || REGION_OFFSET((unsigned long) (addr)) < RGN_MAP_LIMIT))
#define access_ok(type,addr,size)	__access_ok((addr),(size),get_fs())

static inline int
verify_area (int type, const void *addr, unsigned long size)
{
	return access_ok(type,addr,size) ? 0 : -EFAULT;
}

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 *
 * Careful to not
 * (a) re-use the arguments for side effects (sizeof/typeof is ok)
 * (b) require any knowledge of processes at this stage
 */
#define put_user(x,ptr)	__put_user_check((__typeof__(*(ptr)))(x),(ptr),sizeof(*(ptr)),get_fs())
#define get_user(x,ptr)	__get_user_check((x),(ptr),sizeof(*(ptr)),get_fs())

/*
 * The "__xxx" versions do not do address space checking, useful when
 * doing multiple accesses to the same area (the programmer has to do the
 * checks by hand with "access_ok()")
 */
#define __put_user(x,ptr)	__put_user_nocheck((__typeof__(*(ptr)))(x),(ptr),sizeof(*(ptr)))
#define __get_user(x,ptr)	__get_user_nocheck((x),(ptr),sizeof(*(ptr)))

#ifdef ASM_SUPPORTED

extern void __get_user_unknown (void);

#define __get_user_nocheck(x,ptr,size)		\
({						\
	register long __gu_err asm ("r8") = 0;	\
	register long __gu_val asm ("r9") = 0;	\
	switch (size) {				\
	  case 1: __get_user_8(ptr); break;	\
	  case 2: __get_user_16(ptr); break;	\
	  case 4: __get_user_32(ptr); break;	\
	  case 8: __get_user_64(ptr); break;	\
	  default: __get_user_unknown(); break;	\
	}					\
	(x) = (__typeof__(*(ptr))) __gu_val;	\
	__gu_err;				\
})

#define __get_user_check(x,ptr,size,segment)			\
({								\
	register long __gu_err asm ("r8") = -EFAULT;		\
	register long __gu_val asm ("r9") = 0;			\
	const __typeof__(*(ptr)) *__gu_addr = (ptr);		\
	if (__access_ok((long)__gu_addr,size,segment)) {	\
		__gu_err = 0;					\
		switch (size) {					\
		  case 1: __get_user_8(__gu_addr); break;	\
		  case 2: __get_user_16(__gu_addr); break;	\
		  case 4: __get_user_32(__gu_addr); break;	\
		  case 8: __get_user_64(__gu_addr); break;	\
		  default: __get_user_unknown(); break;		\
		}						\
	}							\
	(x) = (__typeof__(*(ptr))) __gu_val;			\
	__gu_err;						\
})

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct *)(x))

/* We need to declare the __ex_table section before we can use it in .xdata.  */
asm (".section \"__ex_table\", \"a\"\n\t.previous");

#define __get_user_64(addr)									\
	asm ("\n[1:]\tld8 %0=%2%P2\t// %0 and %1 get overwritten by exception handler\n"	\
	     "\t.xdata4 \"__ex_table\", 1b-., 1f-.+4\n"						\
	     "[1:]"										\
	     : "=r"(__gu_val), "=r"(__gu_err) : "m"(__m(addr)), "1"(__gu_err));

#define __get_user_32(addr)									\
	asm ("\n[1:]\tld4 %0=%2%P2\t// %0 and %1 get overwritten by exception handler\n"	\
	     "\t.xdata4 \"__ex_table\", 1b-., 1f-.+4\n"						\
	     "[1:]"										\
	     : "=r"(__gu_val), "=r"(__gu_err) : "m"(__m(addr)), "1"(__gu_err));

#define __get_user_16(addr)									\
	asm ("\n[1:]\tld2 %0=%2%P2\t// %0 and %1 get overwritten by exception handler\n"	\
	     "\t.xdata4 \"__ex_table\", 1b-., 1f-.+4\n"						\
	     "[1:]"										\
	     : "=r"(__gu_val), "=r"(__gu_err) : "m"(__m(addr)), "1"(__gu_err));

#define __get_user_8(addr)									\
	asm ("\n[1:]\tld1 %0=%2%P2\t// %0 and %1 get overwritten by exception handler\n"	\
	     "\t.xdata4 \"__ex_table\", 1b-., 1f-.+4\n"						\
	     "[1:]"										\
	     : "=r"(__gu_val), "=r"(__gu_err) : "m"(__m(addr)), "1"(__gu_err));

extern void __put_user_unknown (void);

#define __put_user_nocheck(x,ptr,size)		\
({						\
	register long __pu_err asm ("r8") = 0;	\
	switch (size) {				\
	  case 1: __put_user_8(x,ptr); break;	\
	  case 2: __put_user_16(x,ptr); break;	\
	  case 4: __put_user_32(x,ptr); break;	\
	  case 8: __put_user_64(x,ptr); break;	\
	  default: __put_user_unknown(); break;	\
	}					\
	__pu_err;				\
})

#define __put_user_check(x,ptr,size,segment)			\
({								\
	register long __pu_err asm ("r8") = -EFAULT;		\
	__typeof__(*(ptr)) *__pu_addr = (ptr);			\
	if (__access_ok((long)__pu_addr,size,segment)) {	\
		__pu_err = 0;					\
		switch (size) {					\
		  case 1: __put_user_8(x,__pu_addr); break;	\
		  case 2: __put_user_16(x,__pu_addr); break;	\
		  case 4: __put_user_32(x,__pu_addr); break;	\
		  case 8: __put_user_64(x,__pu_addr); break;	\
		  default: __put_user_unknown(); break;		\
		}						\
	}							\
	__pu_err;						\
})

/*
 * The "__put_user_xx()" macros tell gcc they read from memory
 * instead of writing: this is because they do not write to
 * any memory gcc knows about, so there are no aliasing issues
 */
#define __put_user_64(x,addr)								\
	asm volatile (									\
		"\n[1:]\tst8 %1=%r2%P1\t// %0 gets overwritten by exception handler\n"	\
		"\t.xdata4 \"__ex_table\", 1b-., 1f-.\n"				\
		"[1:]"									\
		: "=r"(__pu_err) : "m"(__m(addr)), "rO"(x), "0"(__pu_err))

#define __put_user_32(x,addr)								\
	asm volatile (									\
		"\n[1:]\tst4 %1=%r2%P1\t// %0 gets overwritten by exception handler\n"	\
		"\t.xdata4 \"__ex_table\", 1b-., 1f-.\n"				\
		"[1:]"									\
		: "=r"(__pu_err) : "m"(__m(addr)), "rO"(x), "0"(__pu_err))

#define __put_user_16(x,addr)								\
	asm volatile (									\
		"\n[1:]\tst2 %1=%r2%P1\t// %0 gets overwritten by exception handler\n"	\
		"\t.xdata4 \"__ex_table\", 1b-., 1f-.\n"				\
		"[1:]"									\
		: "=r"(__pu_err) : "m"(__m(addr)), "rO"(x), "0"(__pu_err))

#define __put_user_8(x,addr)								\
	asm volatile (									\
		"\n[1:]\tst1 %1=%r2%P1\t// %0 gets overwritten by exception handler\n"	\
		"\t.xdata4 \"__ex_table\", 1b-., 1f-.\n"				\
		"[1:]"									\
		: "=r"(__pu_err) : "m"(__m(addr)), "rO"(x), "0"(__pu_err))

#else /* !ASM_SUPPORTED */

#define RELOC_TYPE	2	/* ip-rel */

#define __put_user_xx(val, addr, size, err)							\
	__st_user("__ex_table", (unsigned long) addr, size, RELOC_TYPE, (unsigned long) (val));	\
	(err) = ia64_getreg(_IA64_REG_R8);

#define __get_user_xx(val, addr, size, err)					\
	__ld_user("__ex_table", (unsigned long) addr, size, RELOC_TYPE);	\
	(err) = ia64_getreg(_IA64_REG_R8);					\
	(val) = ia64_getreg(_IA64_REG_R9);

extern void __get_user_unknown (void);

#define __get_user_nocheck(x, ptr, size)				\
({									\
	register long __gu_err = 0;					\
	register long __gu_val = 0;					\
	const __typeof__(*(ptr)) *__gu_addr = (ptr);			\
	switch (size) {							\
	      case 1: case 2: case 4: case 8:				\
		__get_user_xx(__gu_val, __gu_addr, size, __gu_err);	\
		break;							\
	      default:							\
		__get_user_unknown();					\
		break;							\
        }								\
        (x) = (__typeof__(*(ptr))) __gu_val;				\
        __gu_err;							\
})

#define __get_user_check(x,ptr,size,segment)					\
({										\
	register long __gu_err = -EFAULT;					\
	register long __gu_val  = 0;						\
	const __typeof__(*(ptr)) *__gu_addr = (ptr);				\
	if (__access_ok((long) __gu_addr, size, segment)) {			\
		switch (size) {							\
		      case 1: case 2: case 4: case 8:				\
			__get_user_xx(__gu_val, __gu_addr, size, __gu_err);	\
			break;							\
		      default:							\
			__get_user_unknown(); break;				\
		}								\
	}									\
	(x) = (__typeof__(*(ptr))) __gu_val;					\
	__gu_err;								\
})

extern void __put_user_unknown (void);

#define __put_user_nocheck(x, ptr, size)			\
({								\
	int __pu_err = 0;					\
	__typeof__(*(ptr)) *__pu_addr = (ptr);			\
	switch (size) {						\
	      case 1: case 2: case 4: case 8:			\
	  	__put_user_xx(x, __pu_addr, size, __pu_err);	\
		break;						\
	      default:						\
	  	__put_user_unknown(); break;			\
	}							\
	__pu_err;						\
})

#define __put_user_check(x,ptr,size,segment)				\
({									\
	register long __pu_err = -EFAULT;				\
	__typeof__(*(ptr)) *__pu_addr = (ptr);				\
	if (__access_ok((long)__pu_addr,size,segment)) {		\
		switch (size) {						\
		      case 1: case 2: case 4: case 8:			\
			__put_user_xx(x,__pu_addr, size, __pu_err);	\
			break;						\
		      default:						\
			__put_user_unknown(); break;			\
		}							\
	}								\
	__pu_err;							\
})

#endif /* !ASM_SUPPORTED */

/*
 * Complex access routines
 */
extern unsigned long __copy_user (void *to, const void *from, unsigned long count);

#define __copy_to_user(to,from,n)	__copy_user((to), (from), (n))
#define __copy_from_user(to,from,n)	__copy_user((to), (from), (n))

#define copy_to_user(to,from,n)   __copy_tofrom_user((to), (from), (n), 1)
#define copy_from_user(to,from,n) __copy_tofrom_user((to), (from), (n), 0)

#define __copy_tofrom_user(to,from,n,check_to)							\
({												\
	void *__cu_to = (to);									\
	const void *__cu_from = (from);								\
	long __cu_len = (n);									\
												\
	if (__access_ok((long) ((check_to) ? __cu_to : __cu_from), __cu_len, get_fs())) {	\
		__cu_len = __copy_user(__cu_to, __cu_from, __cu_len);				\
	}											\
	__cu_len;										\
})

#define __copy_in_user(to, from, size)          \
        __copy_user((to), (from), (size))

static inline unsigned long
copy_in_user (void *to, const void *from, unsigned long n)
{
	if (likely(access_ok(VERIFY_READ, from, n) &&
	    access_ok(VERIFY_WRITE, to, n)))
		n = __copy_user(to, from, n);
	return n;
}

extern unsigned long __do_clear_user (void *, unsigned long);

#define __clear_user(to,n)			\
({						\
	__do_clear_user(to,n);			\
})

#define clear_user(to,n)					\
({								\
	unsigned long __cu_len = (n);				\
	if (__access_ok((long) to, __cu_len, get_fs())) {	\
		__cu_len = __do_clear_user(to, __cu_len);	\
	}							\
	__cu_len;						\
})


/* Returns: -EFAULT if exception before terminator, N if the entire
   buffer filled, else strlen.  */

extern long __strncpy_from_user (char *to, const char *from, long to_len);

#define strncpy_from_user(to,from,n)					\
({									\
	const char * __sfu_from = (from);				\
	long __sfu_ret = -EFAULT;					\
	if (__access_ok((long) __sfu_from, 0, get_fs()))		\
		__sfu_ret = __strncpy_from_user((to), __sfu_from, (n));	\
	__sfu_ret;							\
})

/* Returns: 0 if bad, string length+1 (memory size) of string if ok */
extern unsigned long __strlen_user (const char *);

#define strlen_user(str)				\
({							\
	const char *__su_str = (str);			\
	unsigned long __su_ret = 0;			\
	if (__access_ok((long) __su_str, 0, get_fs()))	\
		__su_ret = __strlen_user(__su_str);	\
	__su_ret;					\
})

/*
 * Returns: 0 if exception before NUL or reaching the supplied limit
 * (N), a value greater than N if the limit would be exceeded, else
 * strlen.
 */
extern unsigned long __strnlen_user (const char *, long);

#define strnlen_user(str, len)					\
({								\
	const char *__su_str = (str);				\
	unsigned long __su_ret = 0;				\
	if (__access_ok((long) __su_str, 0, get_fs()))		\
		__su_ret = __strnlen_user(__su_str, len);	\
	__su_ret;						\
})

struct exception_table_entry {
	int addr;	/* gp-relative address of insn this fixup is for */
	int cont;	/* gp-relative continuation address; if bit 2 is set, r9 is set to 0 */
};

extern void handle_exception (struct pt_regs *regs, const struct exception_table_entry *e);
extern const struct exception_table_entry *search_exception_tables (unsigned long addr);

# define SEARCH_EXCEPTION_TABLE(regs) search_exception_tables(regs->cr_iip + ia64_psr(regs)->ri)

static inline int
done_with_exception (struct pt_regs *regs)
{
	const struct exception_table_entry *e;
	e = SEARCH_EXCEPTION_TABLE(regs);
	if (e) {
		handle_exception(regs, e);
		return 1;
	}
	return 0;
}

#endif /* _ASM_IA64_UACCESS_H */
