/*
 * Various register offset definitions for debuggers, core file
 * examiners and whatnot.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1999 Ralf Baechle
 * Copyright (C) 1995, 1999 Silicon Graphics
 */
#ifndef _ASM_REG_H
#define _ASM_REG_H

/*
 * This defines/structures correspond to the register layout on stack -
 * if the order here is changed, it needs to be updated in
 * include/asm-mips/stackframe.h
 */
#define EF_REG0			8
#define EF_REG1			9
#define EF_REG2			10
#define EF_REG3			11
#define EF_REG4			12
#define EF_REG5			13
#define EF_REG6			14
#define EF_REG7			15
#define EF_REG8			16
#define EF_REG9			17
#define EF_REG10		18
#define EF_REG11		19
#define EF_REG12		20
#define EF_REG13		21
#define EF_REG14		22
#define EF_REG15		23
#define EF_REG16		24
#define EF_REG17		25
#define EF_REG18		26
#define EF_REG19		27
#define EF_REG20		28
#define EF_REG21		29
#define EF_REG22		30
#define EF_REG23		31
#define EF_REG24		32
#define EF_REG25		33
/*
 * k0/k1 unsaved
 */
#define EF_REG28		36
#define EF_REG29		37
#define EF_REG30		38
#define EF_REG31		39

/*
 * Saved special registers
 */
#define EF_LO			40
#define EF_HI			41

#define EF_CP0_EPC		42
#define EF_CP0_BADVADDR		43
#define EF_CP0_STATUS		44
#define EF_CP0_CAUSE		45

#define EF_SIZE			368	/* size in bytes */

#endif /* _ASM_REG_H */
