#ifndef _ASM_IA64_ASMMACRO_H
#define _ASM_IA64_ASMMACRO_H

/*
 * Copyright (C) 2000 Hewlett-Packard Co
 * Copyright (C) 2000 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#if 1

/*
 * This is a hack that's necessary as long as we support old versions
 * of gas, that have no unwind support.
 */
#include <linux/config.h>

#ifdef CONFIG_IA64_NEW_UNWIND
# define UNW(args...)	args
#else
# define UNW(args...)
#endif

#endif

#define ENTRY(name)				\
	.align 32;				\
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

#endif /* _ASM_IA64_ASMMACRO_H */
