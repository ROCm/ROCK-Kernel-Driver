/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1999, 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_UNALIGNED_H
#define _ASM_UNALIGNED_H

extern void __get_unaligned_bad_length(void);
extern void __put_unaligned_bad_length(void);

/*
 * Load quad unaligned.
 */
extern inline unsigned long __ldq_u(const unsigned long * __addr)
{
	unsigned long __res;

	__asm__("uld\t%0,%1"
		: "=&r" (__res)
		: "m" (*__addr));

	return __res;
}

/*
 * Load long unaligned.
 */
extern inline unsigned long __ldl_u(const unsigned int * __addr)
{
	unsigned long __res;

	__asm__("ulw\t%0,%1"
		: "=&r" (__res)
		: "m" (*__addr));

	return __res;
}

/*
 * Load word unaligned.
 */
extern inline unsigned long __ldw_u(const unsigned short * __addr)
{
	unsigned long __res;

	__asm__("ulh\t%0,%1"
		: "=&r" (__res)
		: "m" (*__addr));

	return __res;
}

/*
 * Store quad ununaligned.
 */
extern inline void __stq_u(unsigned long __val, unsigned long * __addr)
{
	__asm__("usd\t%1, %0"
		: "=m" (*__addr)
		: "r" (__val));
}

/*
 * Store long ununaligned.
 */
extern inline void __stl_u(unsigned long __val, unsigned int * __addr)
{
	__asm__("usw\t%1, %0"
		: "=m" (*__addr)
		: "r" (__val));
}

/*
 * Store word ununaligned.
 */
extern inline void __stw_u(unsigned long __val, unsigned short * __addr)
{
	__asm__("ush\t%1, %0"
		: "=m" (*__addr)
		: "r" (__val));
}

/* 
 * The main single-value unaligned transfer routines.
 */
#define get_unaligned(ptr)						\
({									\
	__typeof__(*(ptr)) __val;					\
									\
	switch (sizeof(*(ptr))) {					\
	case 1:								\
		__val = *(const unsigned char *)(ptr);			\
		break;							\
	case 2:								\
		__val = __ldw_u((const unsigned short *)(ptr));		\
		break;							\
	case 4:								\
		__val = __ldl_u((const unsigned int *)(ptr));		\
		break;							\
	case 8:								\
		__val = __ldq_u((const unsigned long long *)(ptr));	\
		break;							\
	default:							\
		__get_unaligned_bad_length();				\
		break;							\
	}								\
									\
	__val;								\
})

#define put_unaligned(val,ptr)						\
do {									\
	switch (sizeof(*(ptr))) {					\
	case 1:								\
		*(unsigned char *)(ptr) = (val);			\
		break;							\
	case 2:								\
		__stw_u((val), (unsigned short *)(ptr));		\
		break;							\
	case 4:								\
		__stl_u((val), (unsigned int *)(ptr));			\
		break;							\
	case 8:								\
		__stq_u((val), (unsigned long long *)(ptr));		\
		break;							\
	default:							\
		__put_unaligned_bad_length();				\
		break;							\
	}								\
} while(0)

#endif /* _ASM_UNALIGNED_H */
