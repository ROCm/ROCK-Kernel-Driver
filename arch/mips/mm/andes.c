/*
 * andes.c: MMU and cache operations for the R10000 (ANDES).
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/sgialib.h>
#include <asm/mmu_context.h>

/* page functions */
void andes_clear_page(void * page)
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
		:"=r" (page)
		:"0" (page),
		 "I" (PAGE_SIZE)
		:"$1","memory");
}

static void andes_copy_page(void * to, void * from)
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
		:"=r" (dummy1), "=r" (dummy2),
		 "=&r" (reg1), "=&r" (reg2), "=&r" (reg3), "=&r" (reg4)
		:"0" (to), "1" (from),
		 "I" (PAGE_SIZE));
}

/* Cache operations. XXX Write these dave... */
static inline void andes_flush_cache_all(void)
{
	/* XXX */
}

static void andes_flush_cache_mm(struct mm_struct *mm)
{
	/* XXX */
}

static void andes_flush_cache_range(struct mm_struct *mm,
				    unsigned long start,
				    unsigned long end)
{
	/* XXX */
}

static void andes_flush_cache_page(struct vm_area_struct *vma,
				   unsigned long page)
{
	/* XXX */
}

static void andes_flush_page_to_ram(struct page * page)
{
	/* XXX */
}

static void andes_flush_cache_sigtramp(unsigned long page)
{
	/* XXX */
}

/* TLB operations. XXX Write these dave... */
void flush_tlb_all(void)
{
	/* XXX */
}

void flush_tlb_mm(struct mm_struct *mm)
{
	/* XXX */
}

void flush_tlb_range(struct mm_struct *mm, unsigned long start,
				  unsigned long end)
{
	/* XXX */
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	/* XXX */
}

void load_pgd(unsigned long pg_dir)
{
}

void pgd_init(unsigned long page)
{
}

void add_wired_entry(unsigned long entrylo0, unsigned long entrylo1,
				  unsigned long entryhi, unsigned long pagemask)
{
        /* XXX */
}

void __init ld_mmu_andes(void)
{
	_clear_page = andes_clear_page;
	_copy_page = andes_copy_page;

	_flush_cache_all = andes_flush_cache_all;
	_flush_cache_mm = andes_flush_cache_mm;
	_flush_cache_range = andes_flush_cache_range;
	_flush_cache_page = andes_flush_cache_page;
	_flush_cache_sigtramp = andes_flush_cache_sigtramp;
	_flush_page_to_ram = andes_flush_page_to_ram;

	flush_cache_all();
	flush_tlb_all();
}
