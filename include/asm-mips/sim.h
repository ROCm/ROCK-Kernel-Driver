/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999, 2000, 2003 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_SIM_H
#define _ASM_SIM_H

#include <linux/config.h>

#include <asm/offset.h>

#ifdef CONFIG_MIPS32

/* Used in declaration of save_static functions.  */
#define static_unused static __attribute__((unused))

#define __str2(x) #x
#define __str(x) __str2(x)

#define save_static_function(symbol)					\
__asm__ (								\
	".text\n\t"							\
	".globl\t" #symbol "\n\t"					\
	".align\t2\n\t"							\
	".type\t" #symbol ", @function\n\t"				\
	".ent\t" #symbol ", 0\n"					\
	#symbol":\n\t"							\
	".frame\t$29, 0, $31\n\t"					\
	"sw\t$16,"__str(PT_R16)"($29)\t\t\t# save_static_function\n\t"	\
	"sw\t$17,"__str(PT_R17)"($29)\n\t"				\
	"sw\t$18,"__str(PT_R18)"($29)\n\t"				\
	"sw\t$19,"__str(PT_R19)"($29)\n\t"				\
	"sw\t$20,"__str(PT_R20)"($29)\n\t"				\
	"sw\t$21,"__str(PT_R21)"($29)\n\t"				\
	"sw\t$22,"__str(PT_R22)"($29)\n\t"				\
	"sw\t$23,"__str(PT_R23)"($29)\n\t"				\
	"sw\t$30,"__str(PT_R30)"($29)\n\t"				\
	".end\t" #symbol "\n\t"						\
	".size\t" #symbol",. - " #symbol)

#define save_static(frame)	do { } while (0)

#define nabi_no_regargs

#endif /* CONFIG_MIPS32 */

#ifdef CONFIG_MIPS64

/* Used in declaration of save_static functions.  */
#define static_unused static __attribute__((unused))

#define __str2(x) #x
#define __str(x) __str2(x)

#define save_static_function(symbol)					\
__asm__ (								\
	".text\n\t"							\
	".globl\t" #symbol "\n\t"					\
	".align\t2\n\t"							\
	".type\t" #symbol ", @function\n\t"				\
	".ent\t" #symbol ", 0\n"					\
	#symbol":\n\t"							\
	".frame\t$29, 0, $31\n\t"					\
	".end\t" #symbol "\n\t"						\
	".size\t" #symbol",. - " #symbol)

#define save_static(frame)						\
	__asm__ __volatile__(						\
		"sd\t$16,"__str(PT_R16)"(%0)\n\t"			\
		"sd\t$17,"__str(PT_R17)"(%0)\n\t"			\
		"sd\t$18,"__str(PT_R18)"(%0)\n\t"			\
		"sd\t$19,"__str(PT_R19)"(%0)\n\t"			\
		"sd\t$20,"__str(PT_R20)"(%0)\n\t"			\
		"sd\t$21,"__str(PT_R21)"(%0)\n\t"			\
		"sd\t$22,"__str(PT_R22)"(%0)\n\t"			\
		"sd\t$23,"__str(PT_R23)"(%0)\n\t"			\
		"sd\t$30,"__str(PT_R30)"(%0)\n\t"			\
		: /* No outputs */					\
		: "r" (frame))

#define nabi_no_regargs							\
	unsigned long __dummy0,						\
	unsigned long __dummy1,						\
	unsigned long __dummy2,						\
	unsigned long __dummy3,						\
	unsigned long __dummy4,						\
	unsigned long __dummy5,						\
	unsigned long __dummy6,						\
	unsigned long __dummy7,

#endif /* CONFIG_MIPS64 */

#endif /* _ASM_SIM_H */
