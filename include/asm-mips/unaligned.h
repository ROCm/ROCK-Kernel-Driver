/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1999 by Ralf Baechle
 */
#ifndef _ASM_UNALIGNED_H
#define _ASM_UNALIGNED_H

extern void __get_unaligned_bad_length(void);
extern void __put_unaligned_bad_length(void);

/*
 * Load quad unaligned.
 */
extern __inline__ unsigned long ldq_u(const unsigned long long * __addr)
{
	unsigned long long __res;

	__asm__("uld\t%0,(%1)"
		:"=&r" (__res)
		:"r" (__addr));

	return __res;
}

/*
 * Load long unaligned.
 */
extern __inline__ unsigned long ldl_u(const unsigned int * __addr)
{
	unsigned long __res;

	__asm__("ulw\t%0,(%1)"
		:"=&r" (__res)
		:"r" (__addr));

	return __res;
}

/*
 * Load word unaligned.
 */
extern __inline__ unsigned long ldw_u(const unsigned short * __addr)
{
	unsigned long __res;

	__asm__("ulh\t%0,(%1)"
		:"=&r" (__res)
		:"r" (__addr));

	return __res;
}

/*
 * Store quad ununaligned.
 */
extern __inline__ void stq_u(unsigned long __val, unsigned long long * __addr)
{
	__asm__ __volatile__(
		"usd\t%0,(%1)"
		: /* No results */
		:"r" (__val),
		 "r" (__addr));
}

/*
 * Store long ununaligned.
 */
extern __inline__ void stl_u(unsigned long __val, unsigned int * __addr)
{
	__asm__ __volatile__(
		"usw\t%0,(%1)"
		: /* No results */
		:"r" (__val),
		 "r" (__addr));
}

/*
 * Store word ununaligned.
 */
extern __inline__ void stw_u(unsigned long __val, unsigned short * __addr)
{
	__asm__ __volatile__(
		"ush\t%0,(%1)"
		: /* No results */
		:"r" (__val),
		 "r" (__addr));
}

extern inline unsigned long __get_unaligned(const void *ptr, size_t size)
{
	unsigned long val;
	switch (size) {
	case 1:
		val = *(const unsigned char *)ptr;
		break;
	case 2:
		val = ldw_u((const unsigned short *)ptr);
		break;
	case 4:
		val = ldl_u((const unsigned int *)ptr);
		break;
	case 8:
		val = ldq_u((const unsigned long long *)ptr);
		break;
	default:
		__get_unaligned_bad_length();
		break;
	}
	return val;
}

extern inline void __put_unaligned(unsigned long val, void *ptr, size_t size)
{
	switch (size) {
	case 1:
		*(unsigned char *)ptr = (val);
		break;
	case 2:
		stw_u(val, (unsigned short *)ptr);
		break;
	case 4:
		stl_u(val, (unsigned int *)ptr);
		break;
	case 8:
		stq_u(val, (unsigned long long *)ptr);
		break;
	default:
		__put_unaligned_bad_length();
		break;
	}
}

/* 
 * The main single-value unaligned transfer routines.
 */
#define get_unaligned(ptr) \
	((__typeof__(*(ptr)))__get_unaligned((ptr), sizeof(*(ptr))))
#define put_unaligned(x,ptr) \
	__put_unaligned((unsigned long)(x), (ptr), sizeof(*(ptr)))

#endif /* _ASM_UNALIGNED_H */
