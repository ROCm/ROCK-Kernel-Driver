/*
 *  linux/include/asm-arm/proc-armv/page.h
 *
 *  Copyright (C) 1995-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_PROC_PAGE_H
#define __ASM_PROC_PAGE_H

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12

#define EXEC_PAGESIZE   4096

#ifndef __ASSEMBLY__
#ifdef STRICT_MM_TYPECHECKS

typedef struct {
	unsigned long pgd0;
	unsigned long pgd1;
} pgd_t;

#define pgd_val(x)	((x).pgd0)

#else

typedef unsigned long pgd_t[2];

#define pgd_val(x)	((x)[0])

#endif
#endif /* __ASSEMBLY__ */

#endif /* __ASM_PROC_PAGE_H */
