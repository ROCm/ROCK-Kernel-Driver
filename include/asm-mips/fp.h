/* $Id: fp.h,v 1.1 1998/07/16 19:10:04 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 by Ralf Baechle
 */

/*
 * Activate and deactive the floatingpoint accelerator.
 */
#define enable_cp1()							\
	__asm__ __volatile__(						\
		".set\tpush\n\t"					\
		".set\tnoat\n\t"					\
		".set\treorder\n\t"					\
		"mfc0\t$1,$12\n\t"					\
		"or\t$1,%0\n\t"						\
		"mtc0\t$1,$12\n\t"					\
		".set\tpop"						\
		: : "r" (ST0_CU1));

#define disable_cp1()							\
	__asm__ __volatile__(						\
		".set\tpush\n\t"					\
		".set\tnoat\n\t"					\
		".set\treorder\n\t"					\
		"mfc0\t$1,$12\n\t"					\
		"or\t$1,%0\n\t"						\
		"xor\t$1,%0\n\t"					\
		"mtc0\t$1,$12\n\t"					\
		".set\tpop"						\
		: : "r" (ST0_CU1));
