/*
 *  linux/include/asm-arm/proc-armv/uaccess.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <asm/arch/memory.h>
#include <asm/proc/domain.h>

/*
 * Note that this is actually 0x1,0000,0000
 */
#define KERNEL_DS	0x00000000
#define USER_DS		TASK_SIZE

static inline void set_fs (mm_segment_t fs)
{
	current_thread_info()->addr_limit = fs;
	modify_domain(DOMAIN_KERNEL, fs ? DOMAIN_CLIENT : DOMAIN_MANAGER);
}

/* We use 33-bit arithmetic here... */
#define __range_ok(addr,size) ({ \
	unsigned long flag, sum; \
	__asm__("adds %1, %2, %3; sbcccs %1, %1, %0; movcc %0, #0" \
		: "=&r" (flag), "=&r" (sum) \
		: "r" (addr), "Ir" (size), "0" (current_thread_info()->addr_limit) \
		: "cc"); \
	flag; })

#define __addr_ok(addr) ({ \
	unsigned long flag; \
	__asm__("cmp %2, %0; movlo %0, #0" \
		: "=&r" (flag) \
		: "0" (current_thread_info()->addr_limit), "r" (addr) \
		: "cc"); \
	(flag == 0); })

#define __put_user_asm_byte(x,__pu_addr,err)			\
	__asm__ __volatile__(					\
	"1:	strbt	%1,[%2],#0\n"				\
	"2:\n"							\
	"	.section .fixup,\"ax\"\n"			\
	"	.align	2\n"					\
	"3:	mov	%0, %3\n"				\
	"	b	2b\n"					\
	"	.previous\n"					\
	"	.section __ex_table,\"a\"\n"			\
	"	.align	3\n"					\
	"	.long	1b, 3b\n"				\
	"	.previous"					\
	: "+r" (err)						\
	: "r" (x), "r" (__pu_addr), "i" (-EFAULT)		\
	: "cc")

#ifndef __ARMEB__
#define __put_user_asm_half(x,__pu_addr,err)			\
({								\
	unsigned long __temp = (unsigned long)(x);		\
	__put_user_asm_byte(__temp, __pu_addr, err);		\
	__put_user_asm_byte(__temp >> 8, __pu_addr + 1, err);	\
})
#else
#define __put_user_asm_half(x,__pu_addr,err)			\
({								\
	unsigned long __temp = (unsigned long)(x);		\
	__put_user_asm_byte(__temp >> 8, __pu_addr, err);	\
	__put_user_asm_byte(__temp, __pu_addr + 1, err);	\
})
#endif

#define __put_user_asm_word(x,__pu_addr,err)			\
	__asm__ __volatile__(					\
	"1:	strt	%1,[%2],#0\n"				\
	"2:\n"							\
	"	.section .fixup,\"ax\"\n"			\
	"	.align	2\n"					\
	"3:	mov	%0, %3\n"				\
	"	b	2b\n"					\
	"	.previous\n"					\
	"	.section __ex_table,\"a\"\n"			\
	"	.align	3\n"					\
	"	.long	1b, 3b\n"				\
	"	.previous"					\
	: "+r" (err)						\
	: "r" (x), "r" (__pu_addr), "i" (-EFAULT)		\
	: "cc")

#ifndef __ARMEB__
#define	__reg_oper0	"%R2"
#define	__reg_oper1	"%Q2"
#else
#define	__reg_oper0	"%Q2"
#define	__reg_oper1	"%R2"
#endif

#define __put_user_asm_dword(x,__pu_addr,err)			\
	__asm__ __volatile__(					\
	"1:	strt	" __reg_oper1 ", [%1], #4\n"		\
	"2:	strt	" __reg_oper0 ", [%1], #0\n"		\
	"3:\n"							\
	"	.section .fixup,\"ax\"\n"			\
	"	.align	2\n"					\
	"4:	mov	%0, %3\n"				\
	"	b	3b\n"					\
	"	.previous\n"					\
	"	.section __ex_table,\"a\"\n"			\
	"	.align	3\n"					\
	"	.long	1b, 4b\n"				\
	"	.long	2b, 4b\n"				\
	"	.previous"					\
	: "+r" (err), "+r" (__pu_addr)				\
	: "r" (x), "i" (-EFAULT)				\
	: "cc")

#define __get_user_asm_byte(x,addr,err)				\
	__asm__ __volatile__(					\
	"1:	ldrbt	%1,[%2],#0\n"				\
	"2:\n"							\
	"	.section .fixup,\"ax\"\n"			\
	"	.align	2\n"					\
	"3:	mov	%0, %3\n"				\
	"	mov	%1, #0\n"				\
	"	b	2b\n"					\
	"	.previous\n"					\
	"	.section __ex_table,\"a\"\n"			\
	"	.align	3\n"					\
	"	.long	1b, 3b\n"				\
	"	.previous"					\
	: "+r" (err), "=&r" (x)					\
	: "r" (addr), "i" (-EFAULT)				\
	: "cc")

#ifndef __ARMEB__
#define __get_user_asm_half(x,__gu_addr,err)			\
({								\
	unsigned long __b1, __b2;				\
	__get_user_asm_byte(__b1, __gu_addr, err);		\
	__get_user_asm_byte(__b2, __gu_addr + 1, err);		\
	(x) = __b1 | (__b2 << 8);				\
})
#else
#define __get_user_asm_half(x,__gu_addr,err)			\
({								\
	unsigned long __b1, __b2;				\
	__get_user_asm_byte(__b1, __gu_addr, err);		\
	__get_user_asm_byte(__b2, __gu_addr + 1, err);		\
	(x) = (__b1 << 8) | __b2;				\
})
#endif

#define __get_user_asm_word(x,addr,err)				\
	__asm__ __volatile__(					\
	"1:	ldrt	%1,[%2],#0\n"				\
	"2:\n"							\
	"	.section .fixup,\"ax\"\n"			\
	"	.align	2\n"					\
	"3:	mov	%0, %3\n"				\
	"	mov	%1, #0\n"				\
	"	b	2b\n"					\
	"	.previous\n"					\
	"	.section __ex_table,\"a\"\n"			\
	"	.align	3\n"					\
	"	.long	1b, 3b\n"				\
	"	.previous"					\
	: "+r" (err), "=&r" (x)					\
	: "r" (addr), "i" (-EFAULT)				\
	: "cc")

extern unsigned long __arch_copy_from_user(void *to, const void *from, unsigned long n);
#define __do_copy_from_user(to,from,n)				\
	(n) = __arch_copy_from_user(to,from,n)

extern unsigned long __arch_copy_to_user(void *to, const void *from, unsigned long n);
#define __do_copy_to_user(to,from,n)				\
	(n) = __arch_copy_to_user(to,from,n)

extern unsigned long __arch_clear_user(void *addr, unsigned long n);
#define __do_clear_user(addr,sz)				\
	(sz) = __arch_clear_user(addr,sz)

extern unsigned long __arch_strncpy_from_user(char *to, const char *from, unsigned long count);
#define __do_strncpy_from_user(dst,src,count,res)		\
	(res) = __arch_strncpy_from_user(dst,src,count)

extern unsigned long __arch_strnlen_user(const char *s, long n);
#define __do_strnlen_user(s,n,res)					\
	(res) = __arch_strnlen_user(s,n)
