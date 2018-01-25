/*
 * Supervisor Mode Access Prevention support
 *
 * Copyright (C) 2012 Intel Corporation
 * Author: H. Peter Anvin <hpa@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#ifndef _ASM_X86_SMAP_H
#define _ASM_X86_SMAP_H

#include <linux/stringify.h>
#include <asm/nops.h>
#include <asm/cpufeatures.h>

/* "Raw" instruction opcodes */
#define __ASM_CLAC	.byte 0x0f,0x01,0xca
#define __ASM_STAC	.byte 0x0f,0x01,0xcb

#ifdef __ASSEMBLY__

#include <asm/alternative-asm.h>

/*
 * MASK_NOSPEC - sanitize the value of a user controlled value with
 * respect to speculation
 *
 * In the get_user path once we have determined that the pointer is
 * below the current address limit sanitize its value with respect to
 * speculation. In the case when the pointer is above the address limit
 * this directs the cpu to speculate with a NULL ptr rather than
 * something targeting kernel memory.
 *
 * In the syscall entry path it is possible to speculate past the
 * validation of the system call number. Use MASK_NOSPEC to sanitize the
 * syscall array index to zero (sys_read) rather than an arbitrary
 * target.
 *
 * assumes CF is set from a previous 'cmp' i.e.:
 *     cmp TASK_addr_limit, %ptr
 *     cmp __NR_syscall_max, %idx
 */
.macro MASK_NOSPEC mask val
	sbb \mask, \mask
	and \mask, \val
.endm

#ifdef CONFIG_X86_SMAP

#define ASM_CLAC \
	ALTERNATIVE "", __stringify(__ASM_CLAC), X86_FEATURE_SMAP

#define ASM_STAC \
	ALTERNATIVE "", __stringify(__ASM_STAC), X86_FEATURE_SMAP

#else /* CONFIG_X86_SMAP */

#define ASM_CLAC
#define ASM_STAC

#endif /* CONFIG_X86_SMAP */

#else /* __ASSEMBLY__ */

#include <asm/alternative.h>

#ifdef CONFIG_X86_SMAP

static __always_inline void clac(void)
{
	/* Note: a barrier is implicit in alternative() */
	alternative("", __stringify(__ASM_CLAC), X86_FEATURE_SMAP);
}

static __always_inline void stac(void)
{
	/* Note: a barrier is implicit in alternative() */
	alternative("", __stringify(__ASM_STAC), X86_FEATURE_SMAP);
}

/* These macros can be used in asm() statements */
#define ASM_CLAC \
	ALTERNATIVE("", __stringify(__ASM_CLAC), X86_FEATURE_SMAP)
#define ASM_STAC \
	ALTERNATIVE("", __stringify(__ASM_STAC), X86_FEATURE_SMAP)

#else /* CONFIG_X86_SMAP */

static inline void clac(void) { }
static inline void stac(void) { }

#define ASM_CLAC
#define ASM_STAC

#endif /* CONFIG_X86_SMAP */

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_SMAP_H */
