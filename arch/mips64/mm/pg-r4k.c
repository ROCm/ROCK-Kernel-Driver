/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997, 98, 99, 2000, 01, 02, 03 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 Kanoj Sarcar (kanoj@sgi.com)
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/cacheops.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/bootinfo.h>
#include <asm/mmu_context.h>
#include <asm/cpu.h>

/*
 * Zero an entire page.  Basically a simple unrolled loop should do the
 * job but we want more performance by saving memory bus bandwidth.  We
 * have five flavours of the routine available for:
 *
 * - 16byte cachelines and no second level cache
 * - 32byte cachelines second level cache
 * - a version which handles the buggy R4600 v1.x
 * - a version which handles the buggy R4600 v2.0
 * - Finally a last version without fancy cache games for the SC and MC
 *   versions of R4000 and R4400.
 */

void r4k_clear_page_d16(void * page)
{
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%2\n"
		"1:\tcache\t%3,(%0)\n\t"
		"sd\t$0,(%0)\n\t"
		"sd\t$0,8(%0)\n\t"
		"cache\t%3,16(%0)\n\t"
		"sd\t$0,16(%0)\n\t"
		"sd\t$0,24(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"cache\t%3,-32(%0)\n\t"
		"sd\t$0,-32(%0)\n\t"
		"sd\t$0,-24(%0)\n\t"
		"cache\t%3,-16(%0)\n\t"
		"sd\t$0,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"sd\t$0,-8(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		: "=r" (page)
		: "0" (page), "I" (PAGE_SIZE), "i" (Create_Dirty_Excl_D)
		: "memory");
}

void r4k_clear_page_d32(void * page)
{
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%2\n"
		"1:\tcache\t%3,(%0)\n\t"
		"sd\t$0,(%0)\n\t"
		"sd\t$0,8(%0)\n\t"
		"sd\t$0,16(%0)\n\t"
		"sd\t$0,24(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"cache\t%3,-32(%0)\n\t"
		"sd\t$0,-32(%0)\n\t"
		"sd\t$0,-24(%0)\n\t"
		"sd\t$0,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"sd\t$0,-8(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		: "=r" (page)
		: "0" (page), "I" (PAGE_SIZE), "i" (Create_Dirty_Excl_D)
		: "memory");
}


/*
 * This flavour of r4k_clear_page is for the R4600 V1.x.  Cite from the
 * IDT R4600 V1.7 errata:
 *
 *  18. The CACHE instructions Hit_Writeback_Invalidate_D, Hit_Writeback_D,
 *      Hit_Invalidate_D and Create_Dirty_Excl_D should only be
 *      executed if there is no other dcache activity. If the dcache is
 *      accessed for another instruction immeidately preceding when these
 *      cache instructions are executing, it is possible that the dcache
 *      tag match outputs used by these cache instructions will be
 *      incorrect. These cache instructions should be preceded by at least
 *      four instructions that are not any kind of load or store
 *      instruction.
 *
 *      This is not allowed:    lw
 *                              nop
 *                              nop
 *                              nop
 *                              cache       Hit_Writeback_Invalidate_D
 *
 *      This is allowed:        lw
 *                              nop
 *                              nop
 *                              nop
 *                              nop
 *                              cache       Hit_Writeback_Invalidate_D
 */
void r4k_clear_page_r4600_v1(void * page)
{
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%2\n"
		"1:\tnop\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		"cache\t%3,(%0)\n\t"
		"sd\t$0,(%0)\n\t"
		"sd\t$0,8(%0)\n\t"
		"sd\t$0,16(%0)\n\t"
		"sd\t$0,24(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		"cache\t%3,-32(%0)\n\t"
		"sd\t$0,-32(%0)\n\t"
		"sd\t$0,-24(%0)\n\t"
		"sd\t$0,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"sd\t$0,-8(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		: "=r" (page)
		: "0" (page), "I" (PAGE_SIZE), "i" (Create_Dirty_Excl_D)
		: "memory");
}

/*
 * And this one is for the R4600 V2.0
 */
void r4k_clear_page_r4600_v2(void * page)
{
	unsigned int flags;

	local_irq_save(flags);
	*(volatile unsigned int *)KSEG1;
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%2\n"
		"1:\tcache\t%3,(%0)\n\t"
		"sd\t$0,(%0)\n\t"
		"sd\t$0,8(%0)\n\t"
		"sd\t$0,16(%0)\n\t"
		"sd\t$0,24(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"cache\t%3,-32(%0)\n\t"
		"sd\t$0,-32(%0)\n\t"
		"sd\t$0,-24(%0)\n\t"
		"sd\t$0,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"sd\t$0,-8(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		: "=r" (page)
		: "0" (page), "I" (PAGE_SIZE), "i" (Create_Dirty_Excl_D)
		: "memory");
	local_irq_restore(flags);
}

/*
 * The next 4 versions are optimized for all possible scache configurations
 * of the SC / MC versions of R4000 and R4400 ...
 *
 * Todo: For even better performance we should have a routine optimized for
 * every legal combination of dcache / scache linesize.  When I (Ralf) tried
 * this the kernel crashed shortly after mounting the root filesystem.  CPU
 * bug?  Weirdo cache instruction semantics?
 */
void r4k_clear_page_s16(void * page)
{
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%2\n"
		"1:\tcache\t%3,(%0)\n\t"
		"sd\t$0,(%0)\n\t"
		"sd\t$0,8(%0)\n\t"
		"cache\t%3,16(%0)\n\t"
		"sd\t$0,16(%0)\n\t"
		"sd\t$0,24(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"cache\t%3,-32(%0)\n\t"
		"sd\t$0,-32(%0)\n\t"
		"sd\t$0,-24(%0)\n\t"
		"cache\t%3,-16(%0)\n\t"
		"sd\t$0,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"sd\t$0,-8(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		: "=r" (page)
		: "0" (page), "I" (PAGE_SIZE), "i" (Create_Dirty_Excl_SD)
		: "memory");
}

void r4k_clear_page_s32(void * page)
{
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%2\n"
		"1:\tcache\t%3,(%0)\n\t"
		"sd\t$0,(%0)\n\t"
		"sd\t$0,8(%0)\n\t"
		"sd\t$0,16(%0)\n\t"
		"sd\t$0,24(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"cache\t%3,-32(%0)\n\t"
		"sd\t$0,-32(%0)\n\t"
		"sd\t$0,-24(%0)\n\t"
		"sd\t$0,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"sd\t$0,-8(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		: "=r" (page)
		: "0" (page), "I" (PAGE_SIZE), "i" (Create_Dirty_Excl_SD)
		: "memory");
}

void r4k_clear_page_s64(void * page)
{
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%2\n"
		"1:\tcache\t%3,(%0)\n\t"
		"sd\t$0,(%0)\n\t"
		"sd\t$0,8(%0)\n\t"
		"sd\t$0,16(%0)\n\t"
		"sd\t$0,24(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"sd\t$0,-32(%0)\n\t"
		"sd\t$0,-24(%0)\n\t"
		"sd\t$0,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"sd\t$0,-8(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		: "=r" (page)
		: "0" (page), "I" (PAGE_SIZE), "i" (Create_Dirty_Excl_SD)
		: "memory");
}

void r4k_clear_page_s128(void * page)
{
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%2\n"
		"1:\tcache\t%3,(%0)\n\t"
		"sd\t$0,(%0)\n\t"
		"sd\t$0,8(%0)\n\t"
		"sd\t$0,16(%0)\n\t"
		"sd\t$0,24(%0)\n\t"
		"sd\t$0,32(%0)\n\t"
		"sd\t$0,40(%0)\n\t"
		"sd\t$0,48(%0)\n\t"
		"sd\t$0,56(%0)\n\t"
		"daddiu\t%0,128\n\t"
		"sd\t$0,-64(%0)\n\t"
		"sd\t$0,-56(%0)\n\t"
		"sd\t$0,-48(%0)\n\t"
		"sd\t$0,-40(%0)\n\t"
		"sd\t$0,-32(%0)\n\t"
		"sd\t$0,-24(%0)\n\t"
		"sd\t$0,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"sd\t$0,-8(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		: "=r" (page)
		: "0" (page), "I" (PAGE_SIZE), "i" (Create_Dirty_Excl_SD)
		: "memory");
}

/*
 * This version has been tuned on an Origin.  For other machines the arguments
 * of the pref instructin may have to be tuned differently.
 */
void andes_clear_page(void * page)
{
	__asm__ __volatile__(
		".set\tpush\n\t"
		".set\tmips4\n\t"
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%2\n"
		"1:\tpref 7,512(%0)\n\t"
		"sd\t$0,(%0)\n\t"
		"sd\t$0,8(%0)\n\t"
		"sd\t$0,16(%0)\n\t"
		"sd\t$0,24(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"sd\t$0,-32(%0)\n\t"
		"sd\t$0,-24(%0)\n\t"
		"sd\t$0,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		"sd\t$0,-8(%0)\n\t"
		".set\tpop"
		: "=r" (page)
		: "0" (page), "I" (PAGE_SIZE)
		: "memory");
}


/*
 * This is still inefficient.  We only can do better if we know the
 * virtual address where the copy will be accessed.
 */

void r4k_copy_page_d16(void * to, void * from)
{
	unsigned long dummy1, dummy2, reg1, reg2;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%6\n"
		"1:\tcache\t%7,(%0)\n\t"
		"ld\t%2,(%1)\n\t"
		"ld\t%3,8(%1)\n\t"
		"sd\t%2,(%0)\n\t"
		"sd\t%3,8(%0)\n\t"
		"cache\t%7,16(%0)\n\t"
		"ld\t%2,16(%1)\n\t"
		"ld\t%3,24(%1)\n\t"
		"sd\t%2,16(%0)\n\t"
		"sd\t%3,24(%0)\n\t"
		"cache\t%7,32(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"daddiu\t%1,64\n\t"
		"ld\t%2,-32(%1)\n\t"
		"ld\t%3,-24(%1)\n\t"
		"sd\t%2,-32(%0)\n\t"
		"sd\t%3,-24(%0)\n\t"
		"cache\t%7,-16(%0)\n\t"
		"ld\t%2,-16(%1)\n\t"
		"ld\t%3,-8(%1)\n\t"
		"sd\t%2,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		" sd\t%3,-8(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (dummy1), "=r" (dummy2), "=&r" (reg1), "=&r" (reg2)
		:"0" (to), "1" (from), "I" (PAGE_SIZE),
		 "i" (Create_Dirty_Excl_D));
}

void r4k_copy_page_d32(void * to, void * from)
{
	unsigned long dummy1, dummy2, reg1, reg2;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%6\n"
		"1:\tcache\t%7,(%0)\n\t"
		"ld\t%2,(%1)\n\t"
		"ld\t%3,8(%1)\n\t"
		"sd\t%2,(%0)\n\t"
		"sd\t%3,8(%0)\n\t"
		"ld\t%2,16(%1)\n\t"
		"ld\t%3,24(%1)\n\t"
		"sd\t%2,16(%0)\n\t"
		"sd\t%3,24(%0)\n\t"
		"cache\t%7,32(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"daddiu\t%1,64\n\t"
		"ld\t%2,-32(%1)\n\t"
		"ld\t%3,-24(%1)\n\t"
		"sd\t%2,-32(%0)\n\t"
		"sd\t%3,-24(%0)\n\t"
		"ld\t%2,-16(%1)\n\t"
		"ld\t%3,-8(%1)\n\t"
		"sd\t%2,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		" sd\t%3,-8(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (dummy1), "=r" (dummy2), "=&r" (reg1), "=&r" (reg2)
		:"0" (to), "1" (from), "I" (PAGE_SIZE),
		 "i" (Create_Dirty_Excl_D));
}

/*
 * Again a special version for the R4600 V1.x
 */
void r4k_copy_page_r4600_v1(void * to, void * from)
{
	unsigned long dummy1, dummy2, reg1, reg2;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%6\n"
		"1:\tnop\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		"\tcache\t%7,(%0)\n\t"
		"ld\t%2,(%1)\n\t"
		"ld\t%3,8(%1)\n\t"
		"sd\t%2,(%0)\n\t"
		"sd\t%3,8(%0)\n\t"
		"ld\t%2,16(%1)\n\t"
		"ld\t%3,24(%1)\n\t"
		"sd\t%2,16(%0)\n\t"
		"sd\t%3,24(%0)\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		"cache\t%7,32(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"daddiu\t%1,64\n\t"
		"ld\t%2,-32(%1)\n\t"
		"ld\t%3,-24(%1)\n\t"
		"sd\t%2,-32(%0)\n\t"
		"sd\t%3,-24(%0)\n\t"
		"ld\t%2,-16(%1)\n\t"
		"ld\t%3,-8(%1)\n\t"
		"sd\t%2,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		" sd\t%3,-8(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (dummy1), "=r" (dummy2), "=&r" (reg1), "=&r" (reg2)
		:"0" (to), "1" (from), "I" (PAGE_SIZE),
		 "i" (Create_Dirty_Excl_D));
}

void r4k_copy_page_r4600_v2(void * to, void * from)
{
	unsigned long dummy1, dummy2, reg1, reg2;
	unsigned int flags;

	local_irq_save(flags);
	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%6\n"
		"1:\tnop\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		"\tcache\t%7,(%0)\n\t"
		"ld\t%2,(%1)\n\t"
		"ld\t%3,8(%1)\n\t"
		"sd\t%2,(%0)\n\t"
		"sd\t%3,8(%0)\n\t"
		"ld\t%2,16(%1)\n\t"
		"ld\t%3,24(%1)\n\t"
		"sd\t%2,16(%0)\n\t"
		"sd\t%3,24(%0)\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		"cache\t%7,32(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"daddiu\t%1,64\n\t"
		"ld\t%2,-32(%1)\n\t"
		"ld\t%3,-24(%1)\n\t"
		"sd\t%2,-32(%0)\n\t"
		"sd\t%3,-24(%0)\n\t"
		"ld\t%2,-16(%1)\n\t"
		"ld\t%3,-8(%1)\n\t"
		"sd\t%2,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		" sd\t%3,-8(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (dummy1), "=r" (dummy2), "=&r" (reg1), "=&r" (reg2)
		:"0" (to), "1" (from), "I" (PAGE_SIZE),
		 "i" (Create_Dirty_Excl_D));
	local_irq_restore(flags);
}

/*
 * These are for R4000SC / R4400MC
 */
void r4k_copy_page_s16(void * to, void * from)
{
	unsigned long dummy1, dummy2, reg1, reg2;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%6\n"
		"1:\tcache\t%7,(%0)\n\t"
		"ld\t%2,(%1)\n\t"
		"ld\t%3,8(%1)\n\t"
		"sd\t%2,(%0)\n\t"
		"sd\t%3,8(%0)\n\t"
		"cache\t%7,16(%0)\n\t"
		"ld\t%2,16(%1)\n\t"
		"ld\t%3,24(%1)\n\t"
		"sd\t%2,16(%0)\n\t"
		"sd\t%3,24(%0)\n\t"
		"cache\t%7,32(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"daddiu\t%1,64\n\t"
		"ld\t%2,-32(%1)\n\t"
		"ld\t%3,-24(%1)\n\t"
		"sd\t%2,-32(%0)\n\t"
		"sd\t%3,-24(%0)\n\t"
		"cache\t%7,-16(%0)\n\t"
		"ld\t%2,-16(%1)\n\t"
		"ld\t%3,-8(%1)\n\t"
		"sd\t%2,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		" sd\t%3,-8(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (dummy1), "=r" (dummy2), "=&r" (reg1), "=&r" (reg2)
		:"0" (to), "1" (from), "I" (PAGE_SIZE),
		 "i" (Create_Dirty_Excl_SD));
}

void r4k_copy_page_s32(void * to, void * from)
{
	unsigned long dummy1, dummy2, reg1, reg2;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%6\n"
		"1:\tcache\t%7,(%0)\n\t"
		"ld\t%2,(%1)\n\t"
		"ld\t%3,8(%1)\n\t"
		"sd\t%2,(%0)\n\t"
		"sd\t%3,8(%0)\n\t"
		"ld\t%2,16(%1)\n\t"
		"ld\t%3,24(%1)\n\t"
		"sd\t%2,16(%0)\n\t"
		"sd\t%3,24(%0)\n\t"
		"cache\t%7,32(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"daddiu\t%1,64\n\t"
		"ld\t%2,-32(%1)\n\t"
		"ld\t%3,-24(%1)\n\t"
		"sd\t%2,-32(%0)\n\t"
		"sd\t%3,-24(%0)\n\t"
		"ld\t%2,-16(%1)\n\t"
		"ld\t%3,-8(%1)\n\t"
		"sd\t%2,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		" sd\t%3,-8(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (dummy1), "=r" (dummy2), "=&r" (reg1), "=&r" (reg2)
		:"0" (to), "1" (from), "I" (PAGE_SIZE),
		 "i" (Create_Dirty_Excl_SD));
}

void r4k_copy_page_s64(void * to, void * from)
{
	unsigned long dummy1, dummy2, reg1, reg2;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%6\n"
		"1:\tcache\t%7,(%0)\n\t"
		"ld\t%2,(%1)\n\t"
		"ld\t%3,8(%1)\n\t"
		"sd\t%2,(%0)\n\t"
		"sd\t%3,8(%0)\n\t"
		"ld\t%2,16(%1)\n\t"
		"ld\t%3,24(%1)\n\t"
		"sd\t%2,16(%0)\n\t"
		"sd\t%3,24(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"daddiu\t%1,64\n\t"
		"ld\t%2,-32(%1)\n\t"
		"ld\t%3,-24(%1)\n\t"
		"sd\t%2,-32(%0)\n\t"
		"sd\t%3,-24(%0)\n\t"
		"ld\t%2,-16(%1)\n\t"
		"ld\t%3,-8(%1)\n\t"
		"sd\t%2,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		" sd\t%3,-8(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (dummy1), "=r" (dummy2), "=&r" (reg1), "=&r" (reg2)
		:"0" (to), "1" (from), "I" (PAGE_SIZE),
		 "i" (Create_Dirty_Excl_SD));
}

void r4k_copy_page_s128(void * to, void * from)
{
	unsigned long dummy1, dummy2;
	unsigned long reg1, reg2, reg3, reg4;

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%8\n"
		"1:\tcache\t%9,(%0)\n\t"
		"ld\t%2,(%1)\n\t"
		"ld\t%3,8(%1)\n\t"
		"ld\t%4,16(%1)\n\t"
		"ld\t%5,24(%1)\n\t"
		"sd\t%2,(%0)\n\t"
		"sd\t%3,8(%0)\n\t"
		"sd\t%4,16(%0)\n\t"
		"sd\t%5,24(%0)\n\t"
		"ld\t%2,32(%1)\n\t"
		"ld\t%3,40(%1)\n\t"
		"ld\t%4,48(%1)\n\t"
		"ld\t%5,56(%1)\n\t"
		"sd\t%2,32(%0)\n\t"
		"sd\t%3,40(%0)\n\t"
		"sd\t%4,48(%0)\n\t"
		"sd\t%5,56(%0)\n\t"
		"daddiu\t%0,128\n\t"
		"daddiu\t%1,128\n\t"
		"ld\t%2,-64(%1)\n\t"
		"ld\t%3,-56(%1)\n\t"
		"ld\t%4,-48(%1)\n\t"
		"ld\t%5,-40(%1)\n\t"
		"sd\t%2,-64(%0)\n\t"
		"sd\t%3,-56(%0)\n\t"
		"sd\t%4,-48(%0)\n\t"
		"sd\t%5,-40(%0)\n\t"
		"ld\t%2,-32(%1)\n\t"
		"ld\t%3,-24(%1)\n\t"
		"ld\t%4,-16(%1)\n\t"
		"ld\t%5,-8(%1)\n\t"
		"sd\t%2,-32(%0)\n\t"
		"sd\t%3,-24(%0)\n\t"
		"sd\t%4,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		" sd\t%5,-8(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (dummy1), "=r" (dummy2),
		 "=&r" (reg1), "=&r" (reg2), "=&r" (reg3), "=&r" (reg4)
		:"0" (to), "1" (from),
		 "I" (PAGE_SIZE),
		 "i" (Create_Dirty_Excl_SD));
}

/*
 * This version has been tuned on an Origin.  For other machines the arguments
 * of the pref instructin may have to be tuned differently.
 */
void andes_copy_page(void * to, void * from)
{
	unsigned long dummy1, dummy2, reg1, reg2, reg3, reg4;

	__asm__ __volatile__(
		".set\tpush\n\t"
		".set\tmips4\n\t"
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"daddiu\t$1,%0,%8\n"
		"1:\tpref\t0,2*128(%1)\n\t"
		"pref\t1,2*128(%0)\n\t"
		"ld\t%2,(%1)\n\t"
		"ld\t%3,8(%1)\n\t"
		"ld\t%4,16(%1)\n\t"
		"ld\t%5,24(%1)\n\t"
		"sd\t%2,(%0)\n\t"
		"sd\t%3,8(%0)\n\t"
		"sd\t%4,16(%0)\n\t"
		"sd\t%5,24(%0)\n\t"
		"daddiu\t%0,64\n\t"
		"daddiu\t%1,64\n\t"
		"ld\t%2,-32(%1)\n\t"
		"ld\t%3,-24(%1)\n\t"
		"ld\t%4,-16(%1)\n\t"
		"ld\t%5,-8(%1)\n\t"
		"sd\t%2,-32(%0)\n\t"
		"sd\t%3,-24(%0)\n\t"
		"sd\t%4,-16(%0)\n\t"
		"bne\t$1,%0,1b\n\t"
		" sd\t%5,-8(%0)\n\t"
		".set\tpop\n\t"
		:"=r" (dummy1), "=r" (dummy2), "=&r" (reg1), "=&r" (reg2),
		 "=&r" (reg3), "=&r" (reg4)
		:"0" (to), "1" (from), "I" (PAGE_SIZE));
}
