/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996 by Ralf Baechle
 *
 * Definitions commonly used in SGI style code.
 */
#ifndef __ASM_SGIDEFS_H
#define __ASM_SGIDEFS_H

/*
 * There are compilers out there that don't define _MIPS_ISA, _MIPS_SIM,
 * _MIPS_SZINT, _MIPS_SZLONG, _MIPS_SZPTR.  So we notify the user about this
 * problem.  The kernel sources are aware of this problem, so we don't warn
 * when compiling the kernel.
 */
#if !defined(_MIPS_ISA) && !defined(__KERNEL__)
#warning "Macro _MIPS_ISA has not been defined by specs file"
#endif

#if !defined(_MIPS_SIM) && !defined(__KERNEL__)
#warning "Macro _MIPS_SIM has not been defined by specs file"
#endif

#if !defined(_MIPS_SZINT) && !defined(__KERNEL__)
#warning "Macro _MIPS_SZINT has not been defined by specs file"
#endif

#if !defined(_MIPS_SZLONG) && !defined(__KERNEL__)
#warning "Macro _MIPS_SZLONG has not been defined by specs file"
#endif

#if !defined(_MIPS_SZPTR) && !defined(__KERNEL__)
#warning "Macro _MIPS_SZPTR has not been defined by specs file"
#endif

#if (!defined(_MIPS_ISA) || \
     !defined(_MIPS_SIM) || \
     !defined(_MIPS_SZINT) || \
     !defined(_MIPS_SZLONG) || \
     !defined(_MIPS_SZPTR)) && !defined(__KERNEL__)
#warning "Please update your GCC to GCC 2.7.2-4 or newer"
#endif

/*
 * Now let's try our best to supply some reasonable default values for
 * whatever defines GCC didn't supply.  This cannot be done correct for
 * all possible combinations of options, so be careful with your options
 * to GCC.  Best bet is to keep your fingers off the a.out GCC and use
 * ELF GCC 2.7.2-3 where possible.
 */
#ifndef _MIPS_ISA
#if __mips == 1
#define _MIPS_ISA	_MIPS_ISA_MIPS1
/* It is impossible to handle the -mips2 case correct.  */
#elif __mips == 3
#define _MIPS_ISA	_MIPS_ISA_MIPS3
#elif __mips == 4
#define _MIPS_ISA	_MIPS_ISA_MIPS4
#else /* __mips must be 5 */
#define _MIPS_ISA	_MIPS_ISA_MIPS5
#endif
#endif
#ifndef _MIPS_SIM
#define _MIPS_SIM	_MIPS_SIM_ABI32
#endif
#ifndef _MIPS_SZINT
#define _MIPS_SZINT	32
#endif
#ifndef _MIPS_SZLONG
#define _MIPS_SZLONG	32
#endif
#ifndef _MIPS_SZPTR
#define _MIPS_SZPTR	32
#endif

/*
 * Definitions for the ISA level
 */
#define _MIPS_ISA_MIPS1 1
#define _MIPS_ISA_MIPS2 2
#define _MIPS_ISA_MIPS3 3
#define _MIPS_ISA_MIPS4 4
#define _MIPS_ISA_MIPS5 5

/*
 * Subprogram calling convention
 *
 * At the moment only _MIPS_SIM_ABI32 is in use.  This will change rsn.
 * Until GCC 2.8.0 is released don't rely on this definitions because the
 * 64bit code is essentially using the 32bit interface model just with
 * 64bit registers.
 */
#define _MIPS_SIM_ABI32		1
#define _MIPS_SIM_NABI32	2
#define _MIPS_SIM_ABI64		3

#endif /* __ASM_SGIDEFS_H */
