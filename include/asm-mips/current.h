/* $Id: current.h,v 1.5 1999/07/26 19:42:43 harald Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_CURRENT_H
#define _ASM_CURRENT_H

#ifdef _LANGUAGE_C

/* MIPS rules... */
register struct task_struct *current asm("$28");

#endif /* _LANGUAGE_C */
#ifdef _LANGUAGE_ASSEMBLY

/*
 * Special variant for use by exception handlers when the stack pointer
 * is not loaded.
 */
#define _GET_CURRENT(reg)			\
	lui	reg, %hi(kernelsp);		\
	.set	push;				\
	.set	reorder;			\
	lw	reg, %lo(kernelsp)(reg);	\
	.set	pop;				\
	ori	reg, 8191;			\
	xori	reg, 8191

#endif

#endif /* _ASM_CURRENT_H */
