/*
 *  linux/include/asm-arm/pgalloc.h
 *
 *  Copyright (C) 2000-2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _ASMARM_PGALLOC_H
#define _ASMARM_PGALLOC_H

#include <asm/processor.h>
#include <asm/proc/pgalloc.h>

/*
 * Since we have only two-level page tables, these are trivial
 */
#define pmd_alloc_one(mm,addr)		({ BUG(); ((pmd_t *)2); })
#define pmd_free(pmd)			do { } while (0)
#define pgd_populate(mm,pmd,pte)	BUG()

extern pgd_t *get_pgd_slow(struct mm_struct *mm);
extern void free_pgd_slow(pgd_t *pgd);

#define pgd_alloc(mm)			get_pgd_slow(mm)
#define pgd_free(pgd)			free_pgd_slow(pgd)

#define check_pgt_cache()		do { } while (0)

#endif
