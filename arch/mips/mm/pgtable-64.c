/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999, 2000 by Silicon Graphics
 * Copyright (C) 2003 by Ralf Baechle
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>

void pgd_init(unsigned long page)
{
	unsigned long *p, *end;

 	p = (unsigned long *) page;
	end = p + PTRS_PER_PGD;

	while (p < end) {
		p[0] = (unsigned long) invalid_pmd_table;
		p[1] = (unsigned long) invalid_pmd_table;
		p[2] = (unsigned long) invalid_pmd_table;
		p[3] = (unsigned long) invalid_pmd_table;
		p[4] = (unsigned long) invalid_pmd_table;
		p[5] = (unsigned long) invalid_pmd_table;
		p[6] = (unsigned long) invalid_pmd_table;
		p[7] = (unsigned long) invalid_pmd_table;
		p += 8;
	}
}

void pmd_init(unsigned long addr, unsigned long pagetable)
{
	unsigned long *p, *end;

 	p = (unsigned long *) addr;
	end = p + PTRS_PER_PMD;

	while (p < end) {
		p[0] = (unsigned long)pagetable;
		p[1] = (unsigned long)pagetable;
		p[2] = (unsigned long)pagetable;
		p[3] = (unsigned long)pagetable;
		p[4] = (unsigned long)pagetable;
		p[5] = (unsigned long)pagetable;
		p[6] = (unsigned long)pagetable;
		p[7] = (unsigned long)pagetable;
		p += 8;
	}
}

void __init pagetable_init(void)
{
	pmd_t *pmd;
	pte_t *pte;
	int i;

	/* Initialize the entire pgd.  */
	pgd_init((unsigned long)swapper_pg_dir);
	pmd_init((unsigned long)invalid_pmd_table, (unsigned long)invalid_pte_table);
	memset((void *)invalid_pte_table, 0, sizeof(pte_t) * PTRS_PER_PTE);

	memset((void *)kptbl, 0, PAGE_SIZE << PGD_ORDER);
	memset((void *)kpmdtbl, 0, PAGE_SIZE);
	set_pgd(swapper_pg_dir, __pgd(kpmdtbl));

	/*
	 * The 64-bit kernel uses a flat pagetable for it's kernel mappings ...
	 */
	pmd = kpmdtbl;
	pte = kptbl;
	i = 0;
	while (i < (1 << PGD_ORDER)) {
		pmd_val(*pmd) = (unsigned long)pte;
		pte += PTRS_PER_PTE;
		pmd++;
		i++;
	}
}
