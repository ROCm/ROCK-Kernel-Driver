/*
 *  linux/arch/arm/mm/copypage-v6.c
 *
 *  Copyright (C) 2002 Deep Blue Solutions Ltd, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/mm.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/shmparam.h>
#include <asm/tlbflush.h>

#if SHMLBA > 16384
#error FIX ME
#endif

#define from_address	(0xffff8000)
#define from_pgprot	PAGE_KERNEL
#define to_address	(0xffffc000)
#define to_pgprot	PAGE_KERNEL

static pte_t *from_pte;
static pte_t *to_pte;
static spinlock_t v6_lock = SPIN_LOCK_UNLOCKED;

#define DCACHE_COLOUR(vaddr) ((vaddr & (SHMLBA - 1)) >> PAGE_SHIFT)

/*
 * Copy the page, taking account of the cache colour.
 */
void v6_copy_user_page(void *kto, const void *kfrom, unsigned long vaddr)
{
	unsigned int offset = DCACHE_COLOUR(vaddr);
	unsigned long from, to;

	spin_lock(&v6_lock);

	set_pte(from_pte + offset, pfn_pte(__pa(kfrom) >> PAGE_SHIFT, from_pgprot));
	set_pte(to_pte + offset, pfn_pte(__pa(kto) >> PAGE_SHIFT, to_pgprot));

	from = from_address + (offset << PAGE_SHIFT);
	to   = to_address + (offset << PAGE_SHIFT);

	flush_tlb_kernel_page(from);
	flush_tlb_kernel_page(to);

	copy_page((void *)to, (void *)from);

	spin_unlock(&v6_lock);
}

void v6_clear_user_page(void *kaddr, unsigned long vaddr)
{
	unsigned int offset = DCACHE_COLOUR(vaddr);
	unsigned long to = to_address + (offset << PAGE_SHIFT);

	spin_lock(&v6_lock);

	set_pte(to_pte + offset, pfn_pte(__pa(kaddr) >> PAGE_SHIFT, to_pgprot));
	flush_tlb_kernel_page(to);
	clear_page((void *)to);

	spin_unlock(&v6_lock);
}

struct cpu_user_fns v6_user_fns __initdata = {
	.cpu_clear_user_page	= v6_clear_user_page,
	.cpu_copy_user_page	= v6_copy_user_page,
};

static int __init v6_userpage_init(void)
{
	pgd_t *pgd;
	pmd_t *pmd;

	pgd = pgd_offset_k(from_address);
	pmd = pmd_alloc(&init_mm, pgd, from_address);
	if (!pmd)
		BUG();
	from_pte = pte_alloc_kernel(&init_mm, pmd, from_address);
	if (!from_pte)
		BUG();

	to_pte = pte_alloc_kernel(&init_mm, pmd, to_address);
	if (!to_pte)
		BUG();

	return 0;
}

__initcall(v6_userpage_init);

