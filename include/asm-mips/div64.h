/*
 * include/asm-mips/div64.h
 * 
 * Copyright (C) 2000  Maciej W. Rozycki
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef _ASM_DIV64_H
#define _ASM_DIV64_H

#include <asm/sgidefs.h>

/*
 * No traps on overflows for any of these...
 */

#if (_MIPS_ISA == _MIPS_ISA_MIPS1) || (_MIPS_ISA == _MIPS_ISA_MIPS2)

#define do_div64_32(res, high, low, base) ({ \
	unsigned long __quot, __mod; \
	unsigned long __cf, __tmp, __i; \
	\
	__asm__(".set	push\n\t" \
		".set	noat\n\t" \
		".set	noreorder\n\t" \
		"b	1f\n\t" \
		" li	%4,0x21\n" \
		"0:\n\t" \
		"sll	$1,%0,0x1\n\t" \
		"srl	%3,%0,0x1f\n\t" \
		"or	%0,$1,$2\n\t" \
		"sll	%1,%1,0x1\n\t" \
		"sll	%2,%2,0x1\n" \
		"1:\n\t" \
		"bnez	%3,2f\n\t" \
		"sltu	$2,%0,%z5\n\t" \
		"bnez	$2,3f\n\t" \
		"2:\n\t" \
		" addiu	%4,%4,-1\n\t" \
		"subu	%0,%0,%z5\n\t" \
		"addiu	%2,%2,1\n" \
		"3:\n\t" \
		"bnez	%4,0b\n\t" \
		" srl	$2,%1,0x1f\n\t" \
		".set	pop" \
		: "=&r" (__mod), "=&r" (__tmp), "=&r" (__quot), "=&r" (__cf), \
		  "=&r" (__i) \
		: "Jr" (base), "0" (high), "1" (low), "2" (0), "3" (0) \
		/* Aarrgh!  Ran out of gcc's limit on constraints... */ \
		: "$1", "$2"); \
	\
	(res) = __quot; \
	__mod; })

#define do_div(n, base) ({ \
	unsigned long long __quot; \
	unsigned long __upper, __low, __high, __mod; \
	\
	__quot = (n); \
	__high = __quot >> 32; \
	__low = __quot; \
	__upper = __high; \
	\
	if (__high) \
		__asm__("divu	$0,%z2,%z3" \
			: "=h" (__upper), "=l" (__high) \
			: "Jr" (__high), "Jr" (base)); \
	\
	__mod = do_div64_32(__low, __upper, __low, base); \
	\
	__quot = __high; \
	__quot = __quot << 32 | __low; \
	(n) = __quot; \
	__mod; })

#else

#define do_div64_32(res, high, low, base) ({ \
	unsigned long __quot, __mod, __r0; \
	\
	__asm__("dsll32	%2,%z3,0\n\t" \
		"or	%2,%2,%z4\n\t" \
		"ddivu	$0,%2,%z5" \
		: "=h" (__mod), "=l" (__quot), "=&r" (__r0) \
		: "Jr" (high), "Jr" (low), "Jr" (base)); \
	\
	(res) = __quot; \
	__mod; })

#define do_div(n, base) ({ \
	unsigned long long __quot; \
	unsigned long __mod, __r0; \
	\
	__quot = (n); \
	\
	__asm__("dsll32	%2,%M3,0\n\t" \
		"or	%2,%2,%L3\n\t" \
		"ddivu	$0,%2,%z4\n\t" \
		"mflo	%L1\n\t" \
		"dsra32	%M1,%L1,0\n\t" \
		"dsll32	%L1,%L1,0\n\t" \
		"dsra32	%L1,%L1,0" \
		: "=h" (__mod), "=r" (__quot), "=&r" (__r0) \
		: "r" (n), "Jr" (base)); \
	\
	(n) = __quot; \
	__mod; })

#endif

#endif /* _ASM_DIV64_H */
