#ifndef _ASM_IA64_A_OUT_H
#define _ASM_IA64_A_OUT_H

/*
 * No a.out format has been (or should be) defined so this file is
 * just a dummy that allows us to get binfmt_elf compiled.  It
 * probably would be better to clean up binfmt_elf.c so it does not
 * necessarily depend on there being a.out support.
 *
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/types.h>

struct exec {
	unsigned long a_info;
	unsigned long a_text;
	unsigned long a_data;
	unsigned long a_bss;
	unsigned long a_entry;
};

#define N_TXTADDR(x)	0
#define N_DATADDR(x)	0
#define N_BSSADDR(x)	0
#define N_DRSIZE(x)	0
#define N_TRSIZE(x)	0
#define N_SYMSIZE(x)	0
#define N_TXTOFF(x)	0

#ifdef __KERNEL__
# define STACK_TOP	(0x8000000000000000UL + (1UL << (4*PAGE_SHIFT - 12)))
# define IA64_RBS_BOT	(STACK_TOP - 0x80000000L)	/* bottom of register backing store */
#endif

#endif /* _ASM_IA64_A_OUT_H */
