#ifndef _ASM_IA64_ASMMACRO_H
#define _ASM_IA64_ASMMACRO_H

/*
 * Copyright (C) 2000-2001, 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/config.h>

#define ENTRY(name)				\
	.align 32;				\
	.proc name;				\
name:

#define ENTRY_MIN_ALIGN(name)			\
	.align 16;				\
	.proc name;				\
name:

#define GLOBAL_ENTRY(name)			\
	.global name;				\
	ENTRY(name)

#define END(name)				\
	.endp name

/*
 * Helper macros to make unwind directives more readable:
 */

/* prologue_gr: */
#define ASM_UNW_PRLG_RP			0x8
#define ASM_UNW_PRLG_PFS		0x4
#define ASM_UNW_PRLG_PSP		0x2
#define ASM_UNW_PRLG_PR			0x1
#define ASM_UNW_PRLG_GRSAVE(ninputs)	(32+(ninputs))

/*
 * Helper macros for accessing user memory.
 */

	.section "__ex_table", "a"		// declare section & section attributes
	.previous

# define EX(y,x...)				\
	.xdata4 "__ex_table", 99f-., y-.;	\
  [99:]	x
# define EXCLR(y,x...)				\
	.xdata4 "__ex_table", 99f-., y-.+4;	\
  [99:]	x

/*
 * For now, we always put in the McKinley E9 workaround.  On CPUs that don't need it,
 * we'll patch out the work-around bundles with NOPs, so their impact is minimal.
 */
#define DO_MCKINLEY_E9_WORKAROUND
#ifdef DO_MCKINLEY_E9_WORKAROUND
	.section "__mckinley_e9_bundles", "a"
	.previous
/* workaround for Itanium 2 Errata 9: */
# define MCKINLEY_E9_WORKAROUND			\
	.xdata4 "__mckinley_e9_bundles", 1f-.;	\
1:{ .mib;					\
	nop.m 0;				\
	nop.i 0;				\
	br.call.sptk.many b7=1f;;		\
  };						\
1:
#else
# define MCKINLEY_E9_WORKAROUND
#endif

#endif /* _ASM_IA64_ASMMACRO_H */
