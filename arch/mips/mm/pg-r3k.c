/*
 * Copyright (C) 2001 Ralf Baechle (ralf@gnu.org)
 */
#include <linux/sched.h>
#include <linux/mm.h>

/* page functions */
void r3k_clear_page(void * page)
{
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"addiu\t$1,%0,%2\n"
		"1:\tsw\t$0,(%0)\n\t"
		"sw\t$0,4(%0)\n\t"
		"sw\t$0,8(%0)\n\t"
		"sw\t$0,12(%0)\n\t"
		"addiu\t%0,32\n\t"
		"sw\t$0,-16(%0)\n\t"
		"sw\t$0,-12(%0)\n\t"
		"sw\t$0,-8(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"sw\t$0,-4(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		: "=r" (page)
		: "0" (page), "I" (PAGE_SIZE)
		: "memory");
}

void r3k_copy_page(void * to, void * from)
{
	unsigned long dummy1, dummy2;
	unsigned long reg1, reg2, reg3, reg4;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"addiu\t$1,%0,%8\n"
		"1:\tlw\t%2,(%1)\n\t"
		"lw\t%3,4(%1)\n\t"
		"lw\t%4,8(%1)\n\t"
		"lw\t%5,12(%1)\n\t"
		"sw\t%2,(%0)\n\t"
		"sw\t%3,4(%0)\n\t"
		"sw\t%4,8(%0)\n\t"
		"sw\t%5,12(%0)\n\t"
		"lw\t%2,16(%1)\n\t"
		"lw\t%3,20(%1)\n\t"
		"lw\t%4,24(%1)\n\t"
		"lw\t%5,28(%1)\n\t"
		"sw\t%2,16(%0)\n\t"
		"sw\t%3,20(%0)\n\t"
		"sw\t%4,24(%0)\n\t"
		"sw\t%5,28(%0)\n\t"
		"addiu\t%0,64\n\t"
		"addiu\t%1,64\n\t"
		"lw\t%2,-32(%1)\n\t"
		"lw\t%3,-28(%1)\n\t"
		"lw\t%4,-24(%1)\n\t"
		"lw\t%5,-20(%1)\n\t"
		"sw\t%2,-32(%0)\n\t"
		"sw\t%3,-28(%0)\n\t"
		"sw\t%4,-24(%0)\n\t"
		"sw\t%5,-20(%0)\n\t"
		"lw\t%2,-16(%1)\n\t"
		"lw\t%3,-12(%1)\n\t"
		"lw\t%4,-8(%1)\n\t"
		"lw\t%5,-4(%1)\n\t"
		"sw\t%2,-16(%0)\n\t"
		"sw\t%3,-12(%0)\n\t"
		"sw\t%4,-8(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"sw\t%5,-4(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		: "=r" (dummy1), "=r" (dummy2),
		  "=&r" (reg1), "=&r" (reg2), "=&r" (reg3), "=&r" (reg4)
		: "0" (to), "1" (from),
		  "I" (PAGE_SIZE));
}

/*
 * Initialize new page directory with pointers to invalid ptes
 */
void pgd_init(unsigned long page)
{
	unsigned long dummy1, dummy2;

	/*
	 * The plain and boring version for the R3000.  No cache flushing
	 * stuff is implemented since the R3000 has physical caches.
	 */
	__asm__ __volatile__(
		".set\tnoreorder\n"
		"1:\tsw\t%2, (%0)\n\t"
		"sw\t%2, 4(%0)\n\t"
		"sw\t%2, 8(%0)\n\t"
		"sw\t%2, 12(%0)\n\t"
		"sw\t%2, 16(%0)\n\t"
		"sw\t%2, 20(%0)\n\t"
		"sw\t%2, 24(%0)\n\t"
		"sw\t%2, 28(%0)\n\t"
		"subu\t%1, 1\n\t"
		"bnez\t%1, 1b\n\t"
		"addiu\t%0, 32\n\t"
		".set\treorder"
		:"=r" (dummy1), "=r" (dummy2)
		:"r" ((unsigned long) invalid_pte_table), "0" (page),
		 "1" (USER_PTRS_PER_PGD / 8));
}
