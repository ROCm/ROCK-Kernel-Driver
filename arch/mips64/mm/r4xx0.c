/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * r4xx0.c: R4000 processor variant specific MMU/Cache routines.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997, 1998, 1999, 2000 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/bcache.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/bootinfo.h>
#include <asm/sgialib.h>
#include <asm/mmu_context.h>

/* CP0 hazard avoidance. */
#define BARRIER __asm__ __volatile__(".set noreorder\n\t" \
				     "nop; nop; nop; nop; nop; nop;\n\t" \
				     ".set reorder\n\t")

/* Primary cache parameters. */
static int icache_size, dcache_size; /* Size in bytes */
static int ic_lsize, dc_lsize;       /* LineSize in bytes */

/* Secondary cache (if present) parameters. */
static unsigned int scache_size, sc_lsize;	/* Again, in bytes */

#include <asm/r4kcacheops.h>
#include <asm/r4kcache.h>

#undef DEBUG_CACHE

/*
 * Dummy cache handling routines for machines without boardcaches
 */
static void no_sc_noop(void) {}

static struct bcache_ops no_sc_ops = {
	(void *)no_sc_noop, (void *)no_sc_noop,
	(void *)no_sc_noop, (void *)no_sc_noop
};

struct bcache_ops *bcops = &no_sc_ops;

/*
 * On processors with QED R4600 style two set assosicative cache
 * this is the bit which selects the way in the cache for the
 * indexed cachops.
 */
#define icache_waybit (icache_size >> 1)
#define dcache_waybit (dcache_size >> 1)

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

static void r4k_clear_page_d16(void * page)
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
		:"=r" (page)
		:"0" (page), "I" (PAGE_SIZE), "i" (Create_Dirty_Excl_D)
		:"$1", "memory");
}

static void r4k_clear_page_d32(void * page)
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
		:"=r" (page)
		:"0" (page), "I" (PAGE_SIZE), "i" (Create_Dirty_Excl_D)
		:"$1", "memory");
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
static void r4k_clear_page_r4600_v1(void * page)
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
		:"=r" (page)
		:"0" (page),
		 "I" (PAGE_SIZE),
		 "i" (Create_Dirty_Excl_D)
		:"$1", "memory");
}

/*
 * And this one is for the R4600 V2.0
 */
static void r4k_clear_page_r4600_v2(void * page)
{
	unsigned int flags;

	__save_and_cli(flags);
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
		:"=r" (page)
		:"0" (page), "I" (PAGE_SIZE), "i" (Create_Dirty_Excl_D)
		:"$1", "memory");
	__restore_flags(flags);
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
static void r4k_clear_page_s16(void * page)
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
		:"=r" (page)
		:"0" (page), "I" (PAGE_SIZE), "i" (Create_Dirty_Excl_SD)
		:"$1","memory");
}

static void r4k_clear_page_s32(void * page)
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
		:"=r" (page)
		:"0" (page), "I" (PAGE_SIZE), "i" (Create_Dirty_Excl_SD)
		:"$1","memory");
}

static void r4k_clear_page_s64(void * page)
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
		:"=r" (page)
		:"0" (page),
		 "I" (PAGE_SIZE),
		 "i" (Create_Dirty_Excl_SD)
		:"$1","memory");
}

static void r4k_clear_page_s128(void * page)
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
		:"=r" (page)
		:"0" (page),
		 "I" (PAGE_SIZE),
		 "i" (Create_Dirty_Excl_SD)
		:"$1", "memory");
}


/*
 * This is still inefficient.  We only can do better if we know the
 * virtual address where the copy will be accessed.
 */

static void r4k_copy_page_d16(void * to, void * from)
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
		" sd\t%4,-8(%0)\n\t"
		".set\tat\n\t"
		".set\treorder"
		:"=r" (dummy1), "=r" (dummy2), "=&r" (reg1), "=&r" (reg2)
		:"0" (to), "1" (from), "I" (PAGE_SIZE),
		 "i" (Create_Dirty_Excl_D));
}

static void r4k_copy_page_d32(void * to, void * from)
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
static void r4k_copy_page_r4600_v1(void * to, void * from)
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

static void r4k_copy_page_r4600_v2(void * to, void * from)
{
	unsigned long dummy1, dummy2, reg1, reg2;
	unsigned int flags;

	__save_and_cli(flags);
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
	__restore_flags(flags);
}

/*
 * These are for R4000SC / R4400MC
 */
static void r4k_copy_page_s16(void * to, void * from)
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

static void r4k_copy_page_s32(void * to, void * from)
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

static void r4k_copy_page_s64(void * to, void * from)
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

static void r4k_copy_page_s128(void * to, void * from)
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
 * If you think for one second that this stuff coming up is a lot
 * of bulky code eating too many kernel cache lines.  Think _again_.
 *
 * Consider:
 * 1) Taken branches have a 3 cycle penalty on R4k
 * 2) The branch itself is a real dead cycle on even R4600/R5000.
 * 3) Only one of the following variants of each type is even used by
 *    the kernel based upon the cache parameters we detect at boot time.
 *
 * QED.
 */

static inline void r4k_flush_cache_all_s16d16i16(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache16(); blast_icache16(); blast_scache16();
	__restore_flags(flags);
}

static inline void r4k_flush_cache_all_s32d16i16(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache16(); blast_icache16(); blast_scache32();
	__restore_flags(flags);
}

static inline void r4k_flush_cache_all_s64d16i16(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache16(); blast_icache16(); blast_scache64();
	__restore_flags(flags);
}

static inline void r4k_flush_cache_all_s128d16i16(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache16(); blast_icache16(); blast_scache128();
	__restore_flags(flags);
}

static inline void r4k_flush_cache_all_s32d32i32(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache32(); blast_icache32(); blast_scache32();
	__restore_flags(flags);
}

static inline void r4k_flush_cache_all_s64d32i32(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache32(); blast_icache32(); blast_scache64();
	__restore_flags(flags);
}

static inline void r4k_flush_cache_all_s128d32i32(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache32(); blast_icache32(); blast_scache128();
	__restore_flags(flags);
}

static inline void r4k_flush_cache_all_d16i16(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache16(); blast_icache16();
	__restore_flags(flags);
}

static inline void r4k_flush_cache_all_d32i32(void)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache32(); blast_icache32();
	__restore_flags(flags);
}

static void
r4k_flush_cache_range_s16d16i16(struct mm_struct *mm, unsigned long start,
                                unsigned long end)
{
	struct vm_area_struct *vma;
	unsigned long flags;

	if (CPU_CONTEXT(smp_processor_id(), mm) == 0)
		return;

	start &= PAGE_MASK;
#ifdef DEBUG_CACHE
	printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
	vma = find_vma(mm, start);
	if(vma) {
		if (CPU_CONTEXT(smp_processor_id(), mm) !=
				CPU_CONTEXT(smp_processor_id(), current->mm)) {
			r4k_flush_cache_all_s16d16i16();
		} else {
			pgd_t *pgd;
			pmd_t *pmd;
			pte_t *pte;

			__save_and_cli(flags);
			while(start < end) {
				pgd = pgd_offset(mm, start);
				pmd = pmd_offset(pgd, start);
				pte = pte_offset(pmd, start);

				if(pte_val(*pte) & _PAGE_VALID)
					blast_scache16_page(start);
				start += PAGE_SIZE;
			}
			__restore_flags(flags);
		}
	}
}

static void
r4k_flush_cache_range_s32d16i16(struct mm_struct *mm, unsigned long start,
                                unsigned long end)
{
	struct vm_area_struct *vma;
	unsigned long flags;

	if (CPU_CONTEXT(smp_processor_id(), mm) == 0)
		return;

	start &= PAGE_MASK;
#ifdef DEBUG_CACHE
	printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
	vma = find_vma(mm, start);
	if(vma) {
		if (CPU_CONTEXT(smp_processor_id(), mm) !=
				CPU_CONTEXT(smp_processor_id(), current->mm)) {
			r4k_flush_cache_all_s32d16i16();
		} else {
			pgd_t *pgd;
			pmd_t *pmd;
			pte_t *pte;

			__save_and_cli(flags);
			while(start < end) {
				pgd = pgd_offset(mm, start);
				pmd = pmd_offset(pgd, start);
				pte = pte_offset(pmd, start);

				if(pte_val(*pte) & _PAGE_VALID)
					blast_scache32_page(start);
				start += PAGE_SIZE;
			}
			__restore_flags(flags);
		}
	}
}

static void
r4k_flush_cache_range_s64d16i16(struct mm_struct *mm, unsigned long start,
                                unsigned long end)
{
	struct vm_area_struct *vma;
	unsigned long flags;

	if (CPU_CONTEXT(smp_processor_id(), mm) == 0)
		return;

	start &= PAGE_MASK;
#ifdef DEBUG_CACHE
	printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
	vma = find_vma(mm, start);
	if(vma) {
		if (CPU_CONTEXT(smp_processor_id(), mm) !=
				CPU_CONTEXT(smp_processor_id(), current->mm)) {
			r4k_flush_cache_all_s64d16i16();
		} else {
			pgd_t *pgd;
			pmd_t *pmd;
			pte_t *pte;

			__save_and_cli(flags);
			while(start < end) {
				pgd = pgd_offset(mm, start);
				pmd = pmd_offset(pgd, start);
				pte = pte_offset(pmd, start);

				if(pte_val(*pte) & _PAGE_VALID)
					blast_scache64_page(start);
				start += PAGE_SIZE;
			}
			__restore_flags(flags);
		}
	}
}

static void
r4k_flush_cache_range_s128d16i16(struct mm_struct *mm, unsigned long start,
                                 unsigned long end)
{
	struct vm_area_struct *vma;
	unsigned long flags;

	if (CPU_CONTEXT(smp_processor_id(), mm) == 0)
		return;

	start &= PAGE_MASK;
#ifdef DEBUG_CACHE
	printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
	vma = find_vma(mm, start);
	if(vma) {
		if (CPU_CONTEXT(smp_processor_id(), mm) !=
				CPU_CONTEXT(smp_processor_id(), current->mm)) {
			r4k_flush_cache_all_s128d16i16();
		} else {
			pgd_t *pgd;
			pmd_t *pmd;
			pte_t *pte;

			__save_and_cli(flags);
			while(start < end) {
				pgd = pgd_offset(mm, start);
				pmd = pmd_offset(pgd, start);
				pte = pte_offset(pmd, start);

				if(pte_val(*pte) & _PAGE_VALID)
					blast_scache128_page(start);
				start += PAGE_SIZE;
			}
			__restore_flags(flags);
		}
	}
}

static void
r4k_flush_cache_range_s32d32i32(struct mm_struct *mm, unsigned long start,
                                unsigned long end)
{
	struct vm_area_struct *vma;
	unsigned long flags;

	if (CPU_CONTEXT(smp_processor_id(), mm) == 0)
		return;

	start &= PAGE_MASK;
#ifdef DEBUG_CACHE
	printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
	vma = find_vma(mm, start);
	if(vma) {
		if (CPU_CONTEXT(smp_processor_id(), mm) !=
				CPU_CONTEXT(smp_processor_id(), current->mm)) {
			r4k_flush_cache_all_s32d32i32();
		} else {
			pgd_t *pgd;
			pmd_t *pmd;
			pte_t *pte;

			__save_and_cli(flags);
			while(start < end) {
				pgd = pgd_offset(mm, start);
				pmd = pmd_offset(pgd, start);
				pte = pte_offset(pmd, start);

				if(pte_val(*pte) & _PAGE_VALID)
					blast_scache32_page(start);
				start += PAGE_SIZE;
			}
			__restore_flags(flags);
		}
	}
}

static void
r4k_flush_cache_range_s64d32i32(struct mm_struct *mm, unsigned long start,
                                unsigned long end)
{
	struct vm_area_struct *vma;
	unsigned long flags;

	if (CPU_CONTEXT(smp_processor_id(), mm) == 0)
		return;

	start &= PAGE_MASK;
#ifdef DEBUG_CACHE
	printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
	vma = find_vma(mm, start);
	if(vma) {
		if (CPU_CONTEXT(smp_processor_id(), mm) !=
				CPU_CONTEXT(smp_processor_id(), current->mm)) {
			r4k_flush_cache_all_s64d32i32();
		} else {
			pgd_t *pgd;
			pmd_t *pmd;
			pte_t *pte;

			__save_and_cli(flags);
			while(start < end) {
				pgd = pgd_offset(mm, start);
				pmd = pmd_offset(pgd, start);
				pte = pte_offset(pmd, start);

				if(pte_val(*pte) & _PAGE_VALID)
					blast_scache64_page(start);
				start += PAGE_SIZE;
			}
			__restore_flags(flags);
		}
	}
}

static void
r4k_flush_cache_range_s128d32i32(struct mm_struct *mm, unsigned long start,
                                 unsigned long end)
{
	struct vm_area_struct *vma;
	unsigned long flags;

	if (CPU_CONTEXT(smp_processor_id(), mm) != 0) {
		return;

	start &= PAGE_MASK;
#ifdef DEBUG_CACHE
	printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
	vma = find_vma(mm, start);
	if(vma) {
		if (CPU_CONTEXT(smp_processor_id(), mm) !=
				CPU_CONTEXT(smp_processor_id(), current->mm)) {
			r4k_flush_cache_all_s128d32i32();
		} else {
			pgd_t *pgd;
			pmd_t *pmd;
			pte_t *pte;

			__save_and_cli(flags);
			while(start < end) {
				pgd = pgd_offset(mm, start);
				pmd = pmd_offset(pgd, start);
				pte = pte_offset(pmd, start);

				if(pte_val(*pte) & _PAGE_VALID)
					blast_scache128_page(start);
				start += PAGE_SIZE;
			}
			__restore_flags(flags);
		}
	}
}

static void
r4k_flush_cache_range_d16i16(struct mm_struct *mm, unsigned long start,
                             unsigned long end)
{
	if (CPU_CONTEXT(smp_processor_id(), mm) != 0) {
		unsigned long flags;

#ifdef DEBUG_CACHE
		printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
		__save_and_cli(flags);
		blast_dcache16(); blast_icache16();
		__restore_flags(flags);
	}
}

static void
r4k_flush_cache_range_d32i32(struct mm_struct *mm, unsigned long start,
                             unsigned long end)
{
	if (CPU_CONTEXT(smp_processor_id(), mm) != 0) {
		unsigned long flags;

#ifdef DEBUG_CACHE
		printk("crange[%d,%08lx,%08lx]", (int)mm->context, start, end);
#endif
		__save_and_cli(flags);
		blast_dcache32(); blast_icache32();
		__restore_flags(flags);
	}
}

/*
 * On architectures like the Sparc, we could get rid of lines in
 * the cache created only by a certain context, but on the MIPS
 * (and actually certain Sparc's) we cannot.
 */
static void r4k_flush_cache_mm_s16d16i16(struct mm_struct *mm)
{
	if (CPU_CONTEXT(smp_processor_id(), mm) != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r4k_flush_cache_all_s16d16i16();
	}
}

static void r4k_flush_cache_mm_s32d16i16(struct mm_struct *mm)
{
	if (CPU_CONTEXT(smp_processor_id(), mm) != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r4k_flush_cache_all_s32d16i16();
	}
}

static void r4k_flush_cache_mm_s64d16i16(struct mm_struct *mm)
{
	if (CPU_CONTEXT(smp_processor_id(), mm) != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r4k_flush_cache_all_s64d16i16();
	}
}

static void r4k_flush_cache_mm_s128d16i16(struct mm_struct *mm)
{
	if (CPU_CONTEXT(smp_processor_id(), mm) != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r4k_flush_cache_all_s128d16i16();
	}
}

static void r4k_flush_cache_mm_s32d32i32(struct mm_struct *mm)
{
	if (CPU_CONTEXT(smp_processor_id(), mm) != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r4k_flush_cache_all_s32d32i32();
	}
}

static void r4k_flush_cache_mm_s64d32i32(struct mm_struct *mm)
{
	if (CPU_CONTEXT(smp_processor_id(), mm) != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r4k_flush_cache_all_s64d32i32();
	}
}

static void r4k_flush_cache_mm_s128d32i32(struct mm_struct *mm)
{
	if (CPU_CONTEXT(smp_processor_id(), mm) != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r4k_flush_cache_all_s128d32i32();
	}
}

static void r4k_flush_cache_mm_d16i16(struct mm_struct *mm)
{
	if (CPU_CONTEXT(smp_processor_id(), mm) != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r4k_flush_cache_all_d16i16();
	}
}

static void r4k_flush_cache_mm_d32i32(struct mm_struct *mm)
{
	if (CPU_CONTEXT(smp_processor_id(), mm) != 0) {
#ifdef DEBUG_CACHE
		printk("cmm[%d]", (int)mm->context);
#endif
		r4k_flush_cache_all_d32i32();
	}
}

static void
r4k_flush_cache_page_s16d16i16(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (CPU_CONTEXT(smp_processor_id(), mm) == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/*
	 * If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if(!(pte_val(*ptep) & _PAGE_VALID))
		goto out;

	/* Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if (CPU_CONTEXT(smp_processor_id(), mm) != 
				CPU_CONTEXT(smp_processor_id(), current->mm)) {
		/* Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (scache_size - 1)));
		blast_dcache16_page_indexed(page);
		blast_scache16_page_indexed(page);
	} else
		blast_scache16_page(page);
out:
	__restore_flags(flags);
}

static void
r4k_flush_cache_page_s32d16i16(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (CPU_CONTEXT(smp_processor_id(), mm) == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/* If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if(!(pte_val(*ptep) & _PAGE_VALID))
		goto out;

	/* Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if (CPU_CONTEXT(smp_processor_id(), mm) != 
				CPU_CONTEXT(smp_processor_id(), current->mm)) {
		/* Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (scache_size - 1)));
		blast_dcache16_page_indexed(page);
		blast_scache32_page_indexed(page);
	} else
		blast_scache32_page(page);
out:
	__restore_flags(flags);
}

static void
r4k_flush_cache_page_s64d16i16(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (CPU_CONTEXT(smp_processor_id(), mm) == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/* If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if(!(pte_val(*ptep) & _PAGE_VALID))
		goto out;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if (CPU_CONTEXT(smp_processor_id(), mm) != 
				CPU_CONTEXT(smp_processor_id(), current->mm)) {
		/* Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (scache_size - 1)));
		blast_dcache16_page_indexed(page);
		blast_scache64_page_indexed(page);
	} else
		blast_scache64_page(page);
out:
	__restore_flags(flags);
}

static void
r4k_flush_cache_page_s128d16i16(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (CPU_CONTEXT(smp_processor_id(), mm) == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/*
	 * If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if(!(pte_val(*ptep) & _PAGE_VALID))
		goto out;

	/* Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if (CPU_CONTEXT(smp_processor_id(), mm) != 
				CPU_CONTEXT(smp_processor_id(), current->mm)) {
		/*
		 * Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (scache_size - 1)));
		blast_dcache16_page_indexed(page);
		blast_scache128_page_indexed(page);
	} else
		blast_scache128_page(page);
out:
	__restore_flags(flags);
}

static void
r4k_flush_cache_page_s32d32i32(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (CPU_CONTEXT(smp_processor_id(), mm) == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/*
	 * If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if(!(pte_val(*ptep) & _PAGE_VALID))
		goto out;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if (CPU_CONTEXT(smp_processor_id(), mm) != 
				CPU_CONTEXT(smp_processor_id(), current->mm)) {
		/*
		 * Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (scache_size - 1)));
		blast_dcache32_page_indexed(page);
		blast_scache32_page_indexed(page);
	} else
		blast_scache32_page(page);
out:
	__restore_flags(flags);
}

static void
r4k_flush_cache_page_s64d32i32(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (CPU_CONTEXT(smp_processor_id(), mm) == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/*
	 * If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if(!(pte_val(*ptep) & _PAGE_VALID))
		goto out;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if (CPU_CONTEXT(smp_processor_id(), mm) != 
				CPU_CONTEXT(smp_processor_id(), current->mm)) {
		/*
		 * Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (scache_size - 1)));
		blast_dcache32_page_indexed(page);
		blast_scache64_page_indexed(page);
	} else
		blast_scache64_page(page);
out:
	__restore_flags(flags);
}

static void
r4k_flush_cache_page_s128d32i32(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (CPU_CONTEXT(smp_processor_id(), mm) == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/* If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if(!(pte_val(*ptep) & _PAGE_VALID))
		goto out;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if (CPU_CONTEXT(smp_processor_id(), mm) != 
				CPU_CONTEXT(smp_processor_id(), current->mm)) {
		/* Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (scache_size - 1)));
		blast_dcache32_page_indexed(page);
		blast_scache128_page_indexed(page);
	} else
		blast_scache128_page(page);
out:
	__restore_flags(flags);
}

static void
r4k_flush_cache_page_d16i16(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (CPU_CONTEXT(smp_processor_id(), mm) == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/* If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if(!(pte_val(*ptep) & _PAGE_VALID))
		goto out;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if(mm == current->mm) {
		blast_dcache16_page(page);
	} else {
		/* Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (dcache_size - 1)));
		blast_dcache16_page_indexed(page);
	}
out:
	__restore_flags(flags);
}

static void
r4k_flush_cache_page_d32i32(struct vm_area_struct *vma, unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (CPU_CONTEXT(smp_processor_id(), mm) == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/*
	 * If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if(!(pte_val(*ptep) & _PAGE_PRESENT))
		goto out;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if((mm == current->mm) && (pte_val(*ptep) & _PAGE_VALID)) {
		blast_dcache32_page(page);
	} else {
		/*
		 * Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (dcache_size - 1)));
		blast_dcache32_page_indexed(page);
	}
out:
	__restore_flags(flags);
}

static void
r4k_flush_cache_page_d32i32_r4600(struct vm_area_struct *vma,
                                  unsigned long page)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;

	/*
	 * If ownes no valid ASID yet, cannot possibly have gotten
	 * this page into the cache.
	 */
	if (CPU_CONTEXT(smp_processor_id(), mm) == 0)
		return;

#ifdef DEBUG_CACHE
	printk("cpage[%d,%08lx]", (int)mm->context, page);
#endif
	__save_and_cli(flags);
	page &= PAGE_MASK;
	pgdp = pgd_offset(mm, page);
	pmdp = pmd_offset(pgdp, page);
	ptep = pte_offset(pmdp, page);

	/*
	 * If the page isn't marked valid, the page cannot possibly be
	 * in the cache.
	 */
	if(!(pte_val(*ptep) & _PAGE_PRESENT))
		goto out;

	/*
	 * Doing flushes for another ASID than the current one is
	 * too difficult since stupid R4k caches do a TLB translation
	 * for every cache flush operation.  So we do indexed flushes
	 * in that case, which doesn't overly flush the cache too much.
	 */
	if((mm == current->mm) && (pte_val(*ptep) & _PAGE_VALID)) {
		blast_dcache32_page(page);
	} else {
		/* Do indexed flush, too much work to get the (possible)
		 * tlb refills to work correctly.
		 */
		page = (KSEG0 + (page & (dcache_size - 1)));
		blast_dcache32_page_indexed(page);
		blast_dcache32_page_indexed(page ^ dcache_waybit);
	}
out:
	__restore_flags(flags);
}

static void r4k_flush_page_to_ram_s16(struct page *page)
{
	blast_scache16_page((unsigned long)page_address(page));
}

static void r4k_flush_page_to_ram_s32(struct page *page)
{
	blast_scache32_page((unsigned long)page_address(page));
}

static void r4k_flush_page_to_ram_s64(struct page *page)
{
	blast_scache64_page((unsigned long)page_address(page));
}

static void r4k_flush_page_to_ram_s128(struct page *page)
{
	blast_scache128_page((unsigned long)page_address(page));
}

static void r4k_flush_page_to_ram_d16(struct page *page)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache16_page((unsigned long)page_address(page));
	__restore_flags(flags);
}

static void r4k_flush_page_to_ram_d32(struct page *page)
{
	unsigned long flags;

	__save_and_cli(flags);
	blast_dcache32_page((unsigned long)page_address(page));
	__restore_flags(flags);
}

/*
 * Writeback and invalidate the primary cache dcache before DMA.
 *
 * R4600 v2.0 bug: "The CACHE instructions Hit_Writeback_Inv_D,
 * Hit_Writeback_D, Hit_Invalidate_D and Create_Dirty_Exclusive_D will only
 * operate correctly if the internal data cache refill buffer is empty.  These
 * CACHE instructions should be separated from any potential data cache miss
 * by a load instruction to an uncached address to empty the response buffer."
 * (Revision 2.0 device errata from IDT available on http://www.idt.com/
 * in .pdf format.)
 */
static void
r4k_dma_cache_wback_inv_pc(unsigned long addr, unsigned long size)
{
	unsigned long end, a;
	unsigned int flags;

	if (size >= dcache_size) {
		flush_cache_l1();
	} else {
		/* Workaround for R4600 bug.  See comment above. */
		__save_and_cli(flags);
		*(volatile unsigned long *)KSEG1;

		a = addr & ~(dc_lsize - 1);
		end = (addr + size) & ~(dc_lsize - 1);
		while (1) {
			flush_dcache_line(a); /* Hit_Writeback_Inv_D */
			if (a == end) break;
			a += dc_lsize;
		}
		__restore_flags(flags);
	}
	bc_wback_inv(addr, size);
}

static void
r4k_dma_cache_wback_inv_sc(unsigned long addr, unsigned long size)
{
	unsigned long end, a;

	if (size >= scache_size) {
		flush_cache_l1();
		return;
	}

	a = addr & ~(sc_lsize - 1);
	end = (addr + size) & ~(sc_lsize - 1);
	while (1) {
		flush_scache_line(a);	/* Hit_Writeback_Inv_SD */
		if (a == end) break;
		a += sc_lsize;
	}
}

static void
r4k_dma_cache_inv_pc(unsigned long addr, unsigned long size)
{
	unsigned long end, a;
	unsigned int flags;

	if (size >= dcache_size) {
		flush_cache_l1();
	} else {
		/* Workaround for R4600 bug.  See comment above. */
		__save_and_cli(flags);
		*(volatile unsigned long *)KSEG1;

		a = addr & ~(dc_lsize - 1);
		end = (addr + size) & ~(dc_lsize - 1);
		while (1) {
			flush_dcache_line(a); /* Hit_Writeback_Inv_D */
			if (a == end) break;
			a += dc_lsize;
		}
		__restore_flags(flags);
	}

	bc_inv(addr, size);
}

static void
r4k_dma_cache_inv_sc(unsigned long addr, unsigned long size)
{
	unsigned long end, a;

	if (size >= scache_size) {
		flush_cache_l1();
		return;
	}

	a = addr & ~(sc_lsize - 1);
	end = (addr + size) & ~(sc_lsize - 1);
	while (1) {
		flush_scache_line(a); /* Hit_Writeback_Inv_SD */
		if (a == end) break;
		a += sc_lsize;
	}
}

static void
r4k_dma_cache_wback(unsigned long addr, unsigned long size)
{
	panic("r4k_dma_cache called - should not happen.\n");
}

/*
 * While we're protected against bad userland addresses we don't care
 * very much about what happens in that case.  Usually a segmentation
 * fault will dump the process later on anyway ...
 */
static void r4k_flush_cache_sigtramp(unsigned long addr)
{
	unsigned long daddr, iaddr;

	daddr = addr & ~(dc_lsize - 1);
	__asm__ __volatile__("nop;nop;nop;nop");	/* R4600 V1.7 */
	protected_writeback_dcache_line(daddr);
	protected_writeback_dcache_line(daddr + dc_lsize);
	iaddr = addr & ~(ic_lsize - 1);
	protected_flush_icache_line(iaddr);
	protected_flush_icache_line(iaddr + ic_lsize);
}

static void r4600v20k_flush_cache_sigtramp(unsigned long addr)
{
	unsigned long daddr, iaddr;
	unsigned int flags;

	daddr = addr & ~(dc_lsize - 1);
	__save_and_cli(flags);

	/* Clear internal cache refill buffer */
	*(volatile unsigned int *)KSEG1;

	protected_writeback_dcache_line(daddr);
	protected_writeback_dcache_line(daddr + dc_lsize);
	iaddr = addr & ~(ic_lsize - 1);
	protected_flush_icache_line(iaddr);
	protected_flush_icache_line(iaddr + ic_lsize);
	__restore_flags(flags);
}

#undef DEBUG_TLB

#define NTLB_ENTRIES       48  /* Fixed on all R4XX0 variants... */

#define NTLB_ENTRIES_HALF  24  /* Fixed on all R4XX0 variants... */

static inline void r4k_flush_tlb_all(void)
{
	unsigned long flags;
	unsigned long old_ctx;
	int entry;

#ifdef DEBUG_TLB
	printk("[tlball]");
#endif

	__save_and_cli(flags);
	/* Save old context and create impossible VPN2 value */
	old_ctx = (get_entryhi() & 0xff);
	set_entryhi(KSEG0);
	set_entrylo0(0);
	set_entrylo1(0);
	BARRIER;

	entry = get_wired();

	/* Blast 'em all away. */
	while(entry < NTLB_ENTRIES) {
		set_index(entry);
		BARRIER;
		tlb_write_indexed();
		BARRIER;
		entry++;
	}
	BARRIER;
	set_entryhi(old_ctx);
	__restore_flags(flags);
}

static void r4k_flush_tlb_mm(struct mm_struct *mm)
{
	if (CPU_CONTEXT(smp_processor_id(), mm) != 0) {
		unsigned long flags;

#ifdef DEBUG_TLB
		printk("[tlbmm<%d>]", mm->context);
#endif
		__save_and_cli(flags);
		get_new_cpu_mmu_context(mm, smp_processor_id());
		if(mm == current->mm)
			set_entryhi(CPU_CONTEXT(smp_processor_id(), mm) & 0xff);
		__restore_flags(flags);
	}
}

static void r4k_flush_tlb_range(struct mm_struct *mm, unsigned long start,
				unsigned long end)
{
	if (CPU_CONTEXT(smp_processor_id(), mm) != 0) {
		unsigned long flags;
		int size;

#ifdef DEBUG_TLB
		printk("[tlbrange<%02x,%08lx,%08lx>]", (mm->context & 0xff),
		       start, end);
#endif
		__save_and_cli(flags);
		size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		size = (size + 1) >> 1;
		if(size <= NTLB_ENTRIES_HALF) {
			int oldpid = (get_entryhi() & 0xff);
			int newpid = (CPU_CONTEXT(smp_processor_id(), mm) & 0xff);

			start &= (PAGE_MASK << 1);
			end += ((PAGE_SIZE << 1) - 1);
			end &= (PAGE_MASK << 1);
			while(start < end) {
				int idx;

				set_entryhi(start | newpid);
				start += (PAGE_SIZE << 1);
				BARRIER;
				tlb_probe();
				BARRIER;
				idx = get_index();
				set_entrylo0(0);
				set_entrylo1(0);
				set_entryhi(KSEG0);
				BARRIER;
				if(idx < 0)
					continue;
				tlb_write_indexed();
				BARRIER;
			}
			set_entryhi(oldpid);
		} else {
			get_new_cpu_mmu_context(mm, smp_processor_id());
			if(mm == current->mm)
				set_entryhi(CPU_CONTEXT(smp_processor_id(), 
								mm) & 0xff);
		}
		__restore_flags(flags);
	}
}

static void r4k_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	if (CPU_CONTEXT(smp_processor_id(), vma->vm_mm) != 0) {
		unsigned long flags;
		int oldpid, newpid, idx;

#ifdef DEBUG_TLB
		printk("[tlbpage<%d,%08lx>]", vma->vm_mm->context, page);
#endif
		newpid = (CPU_CONTEXT(smp_processor_id(), vma->vm_mm) & 0xff);
		page &= (PAGE_MASK << 1);
		__save_and_cli(flags);
		oldpid = (get_entryhi() & 0xff);
		set_entryhi(page | newpid);
		BARRIER;
		tlb_probe();
		BARRIER;
		idx = get_index();
		set_entrylo0(0);
		set_entrylo1(0);
		set_entryhi(KSEG0);
		if(idx < 0)
			goto finish;
		BARRIER;
		tlb_write_indexed();

	finish:
		BARRIER;
		set_entryhi(oldpid);
		__restore_flags(flags);
	}
}

static void
r4k_flush_cache_l2(void)
{
}

/* We will need multiple versions of update_mmu_cache(), one that just
 * updates the TLB with the new pte(s), and another which also checks
 * for the R4k "end of page" hardware bug and does the needy.
 */
static void r4k_update_mmu_cache(struct vm_area_struct * vma,
				 unsigned long address, pte_t pte)
{
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	int idx, pid;

	/*
	 * Handle debugger faulting in for debugee.
	 */
	if (current->active_mm != vma->vm_mm)
		return;

	__save_and_cli(flags);
	pid = (get_entryhi() & 0xff);

#ifdef DEBUG_TLB
	if((pid != (CPU_CONTEXT(smp_processor_id(), vma->vm_mm) & 0xff)) ||
	   (CPU_CONTEXT(smp_processor_id(), vma->vm_mm) == 0)) {
		printk("update_mmu_cache: Wheee, bogus tlbpid mmpid=%d
			tlbpid=%d\n", (int) (CPU_CONTEXT(smp_processor_id(),
			vma->vm_mm) & 0xff), pid);
	}
#endif

	address &= (PAGE_MASK << 1);
	set_entryhi(address | (pid));
	pgdp = pgd_offset(vma->vm_mm, address);
	BARRIER;
	tlb_probe();
	BARRIER;
	pmdp = pmd_offset(pgdp, address);
	idx = get_index();
	ptep = pte_offset(pmdp, address);
	BARRIER;
	set_entrylo0(pte_val(*ptep++) >> 6);
	set_entrylo1(pte_val(*ptep) >> 6);
	set_entryhi(address | (pid));
	BARRIER;
	if(idx < 0) {
		tlb_write_random();
	} else {
		tlb_write_indexed();
	}
	BARRIER;
	set_entryhi(pid);
	BARRIER;
	__restore_flags(flags);
}

#if 0
static void r4k_update_mmu_cache_hwbug(struct vm_area_struct * vma,
				       unsigned long address, pte_t pte)
{
	unsigned long flags;
	pgd_t *pgdp;
	pmd_t *pmdp;
	pte_t *ptep;
	int idx;

	__save_and_cli(flags);
	address &= (PAGE_MASK << 1);
	set_entryhi(address | (get_entryhi() & 0xff));
	pgdp = pgd_offset(vma->vm_mm, address);
	tlb_probe();
	pmdp = pmd_offset(pgdp, address);
	idx = get_index();
	ptep = pte_offset(pmdp, address);
	set_entrylo0(pte_val(*ptep++) >> 6);
	set_entrylo1(pte_val(*ptep) >> 6);
	BARRIER;
	if(idx < 0)
		tlb_write_random();
	else
		tlb_write_indexed();
	BARRIER;
	__restore_flags(flags);
}
#endif

static void r4k_show_regs(struct pt_regs *regs)
{
	/* Saved main processor registers. */
	printk("$0      : %016lx %016lx %016lx %016lx\n",
	       0UL, regs->regs[1], regs->regs[2], regs->regs[3]);
	printk("$4      : %016lx %016lx %016lx %016lx\n",
               regs->regs[4], regs->regs[5], regs->regs[6], regs->regs[7]);
	printk("$8      : %016lx %016lx %016lx %016lx\n",
	       regs->regs[8], regs->regs[9], regs->regs[10], regs->regs[11]);
	printk("$12     : %016lx %016lx %016lx %016lx\n",
               regs->regs[12], regs->regs[13], regs->regs[14], regs->regs[15]);
	printk("$16     : %016lx %016lx %016lx %016lx\n",
	       regs->regs[16], regs->regs[17], regs->regs[18], regs->regs[19]);
	printk("$20     : %016lx %016lx %016lx %016lx\n",
               regs->regs[20], regs->regs[21], regs->regs[22], regs->regs[23]);
	printk("$24     : %016lx %016lx\n",
	       regs->regs[24], regs->regs[25]);
	printk("$28     : %016lx %016lx %016lx %016lx\n",
	       regs->regs[28], regs->regs[29], regs->regs[30], regs->regs[31]);
	printk("Hi      : %016lx\n", regs->hi);
	printk("Lo      : %016lx\n", regs->lo);

	/* Saved cp0 registers. */
	printk("epc     : %016lx\nbadvaddr: %016lx\n",
	       regs->cp0_epc, regs->cp0_badvaddr);
	printk("Status  : %08x\nCause   : %08x\n",
	       (unsigned int) regs->cp0_status, (unsigned int) regs->cp0_cause);
//{static int x = 3; x--; if(!x) while(1);}
}

/* Detect and size the various r4k caches. */
static void __init probe_icache(unsigned long config)
{
	icache_size = 1 << (12 + ((config >> 9) & 7));
	ic_lsize = 16 << ((config >> 5) & 1);

	printk("Primary instruction cache %dkb, linesize %d bytes)\n",
	       icache_size >> 10, ic_lsize);
}

static void __init probe_dcache(unsigned long config)
{
	dcache_size = 1 << (12 + ((config >> 6) & 7));
	dc_lsize = 16 << ((config >> 4) & 1);

	printk("Primary data cache %dkb, linesize %d bytes)\n",
	       dcache_size >> 10, dc_lsize);
}


/* If you even _breathe_ on this function, look at the gcc output
 * and make sure it does not pop things on and off the stack for
 * the cache sizing loop that executes in KSEG1 space or else
 * you will crash and burn badly.  You have been warned.
 */
static int __init probe_scache(unsigned long config)
{
	extern unsigned long stext;
	unsigned long flags, addr, begin, end, pow2;
	int tmp;

	tmp = ((config >> 17) & 1);
	if(tmp)
		return 0;
	tmp = ((config >> 22) & 3);
	switch(tmp) {
	case 0:
		sc_lsize = 16;
		break;
	case 1:
		sc_lsize = 32;
		break;
	case 2:
		sc_lsize = 64;
		break;
	case 3:
		sc_lsize = 128;
		break;
	}

	begin = (unsigned long) &stext;
	begin &= ~((4 * 1024 * 1024) - 1);
	end = begin + (4 * 1024 * 1024);

	/* This is such a bitch, you'd think they would make it
	 * easy to do this.  Away you daemons of stupidity!
	 */
	__save_and_cli(flags);

	/* Fill each size-multiple cache line with a valid tag. */
	pow2 = (64 * 1024);
	for(addr = begin; addr < end; addr = (begin + pow2)) {
		unsigned long *p = (unsigned long *) addr;
		__asm__ __volatile__("nop" : : "r" (*p)); /* whee... */
		pow2 <<= 1;
	}

	/* Load first line with zero (therefore invalid) tag. */
	set_taglo(0);
	set_taghi(0);
	__asm__ __volatile__("nop; nop; nop; nop;"); /* avoid the hazard */
	__asm__ __volatile__("\n\t.set noreorder\n\t"
			     "cache 8, (%0)\n\t"
			     ".set reorder\n\t" : : "r" (begin));
	__asm__ __volatile__("\n\t.set noreorder\n\t"
			     "cache 9, (%0)\n\t"
			     ".set reorder\n\t" : : "r" (begin));
	__asm__ __volatile__("\n\t.set noreorder\n\t"
			     "cache 11, (%0)\n\t"
			     ".set reorder\n\t" : : "r" (begin));

	/* Now search for the wrap around point. */
	pow2 = (128 * 1024);
	tmp = 0;
	for(addr = (begin + (128 * 1024)); addr < (end); addr = (begin + pow2)) {
		__asm__ __volatile__("\n\t.set noreorder\n\t"
				     "cache 7, (%0)\n\t"
				     ".set reorder\n\t" : : "r" (addr));
		__asm__ __volatile__("nop; nop; nop; nop;"); /* hazard... */
		if(!get_taglo())
			break;
		pow2 <<= 1;
	}
	__restore_flags(flags);
	addr -= begin;
	printk("Secondary cache sized at %dK linesize %d\n",
	       (int) (addr >> 10), sc_lsize);
	scache_size = addr;
	return 1;
}

static void __init setup_noscache_funcs(void)
{
	unsigned int prid;

	switch(dc_lsize) {
	case 16:
		_clear_page = r4k_clear_page_d16;
		_copy_page = r4k_copy_page_d16;
		_flush_cache_l1 = r4k_flush_cache_all_d16i16;
		_flush_cache_mm = r4k_flush_cache_mm_d16i16;
		_flush_cache_range = r4k_flush_cache_range_d16i16;
		_flush_cache_page = r4k_flush_cache_page_d16i16;
		break;
	case 32:
		prid = read_32bit_cp0_register(CP0_PRID) & 0xfff0;
		if (prid == 0x2010) {			/* R4600 V1.7 */
			_clear_page = r4k_clear_page_r4600_v1;
			_copy_page = r4k_copy_page_r4600_v1;
		} else if (prid == 0x2020) {		/* R4600 V2.0 */
			_clear_page = r4k_clear_page_r4600_v2;
			_copy_page = r4k_copy_page_r4600_v2;
		} else {
			_clear_page = r4k_clear_page_d32;
			_copy_page = r4k_copy_page_d32;
		}
		_flush_cache_l1 = r4k_flush_cache_all_d32i32;
		_flush_cache_mm = r4k_flush_cache_mm_d32i32;
		_flush_cache_range = r4k_flush_cache_range_d32i32;
		_flush_cache_page = r4k_flush_cache_page_d32i32;
		break;
	}

	switch(ic_lsize) {
	case 16:
		_flush_page_to_ram = r4k_flush_page_to_ram_d16;
		break;
	case 32:
		_flush_page_to_ram = r4k_flush_page_to_ram_d32;
		break;
	}

	_dma_cache_wback_inv = r4k_dma_cache_wback_inv_pc;
	_dma_cache_wback = r4k_dma_cache_wback;
	_dma_cache_inv = r4k_dma_cache_inv_pc;
}

static void __init setup_scache_funcs(void)
{
	switch(sc_lsize) {
	case 16:
		switch(dc_lsize) {
		case 16:
			_flush_cache_l1 = r4k_flush_cache_all_s16d16i16;
			_flush_cache_mm = r4k_flush_cache_mm_s16d16i16;
			_flush_cache_range = r4k_flush_cache_range_s16d16i16;
			_flush_cache_page = r4k_flush_cache_page_s16d16i16;
			break;
		case 32:
			panic("Invalid cache configuration detected");
		};
		_flush_page_to_ram = r4k_flush_page_to_ram_s16;
		_clear_page = r4k_clear_page_s16;
		_copy_page = r4k_copy_page_s16;
		break;
	case 32:
		switch(dc_lsize) {
		case 16:
			_flush_cache_l1 = r4k_flush_cache_all_s32d16i16;
			_flush_cache_mm = r4k_flush_cache_mm_s32d16i16;
			_flush_cache_range = r4k_flush_cache_range_s32d16i16;
			_flush_cache_page = r4k_flush_cache_page_s32d16i16;
			break;
		case 32:
			_flush_cache_l1 = r4k_flush_cache_all_s32d32i32;
			_flush_cache_mm = r4k_flush_cache_mm_s32d32i32;
			_flush_cache_range = r4k_flush_cache_range_s32d32i32;
			_flush_cache_page = r4k_flush_cache_page_s32d32i32;
			break;
		};
		_flush_page_to_ram = r4k_flush_page_to_ram_s32;
		_clear_page = r4k_clear_page_s32;
		_copy_page = r4k_copy_page_s32;
		break;
	case 64:
		switch(dc_lsize) {
		case 16:
			_flush_cache_l1 = r4k_flush_cache_all_s64d16i16;
			_flush_cache_mm = r4k_flush_cache_mm_s64d16i16;
			_flush_cache_range = r4k_flush_cache_range_s64d16i16;
			_flush_cache_page = r4k_flush_cache_page_s64d16i16;
			break;
		case 32:
			_flush_cache_l1 = r4k_flush_cache_all_s64d32i32;
			_flush_cache_mm = r4k_flush_cache_mm_s64d32i32;
			_flush_cache_range = r4k_flush_cache_range_s64d32i32;
			_flush_cache_page = r4k_flush_cache_page_s64d32i32;
			break;
		};
		_flush_page_to_ram = r4k_flush_page_to_ram_s64;
		_clear_page = r4k_clear_page_s64;
		_copy_page = r4k_copy_page_s64;
		break;
	case 128:
		switch(dc_lsize) {
		case 16:
			_flush_cache_l1 = r4k_flush_cache_all_s128d16i16;
			_flush_cache_mm = r4k_flush_cache_mm_s128d16i16;
			_flush_cache_range = r4k_flush_cache_range_s128d16i16;
			_flush_cache_page = r4k_flush_cache_page_s128d16i16;
			break;
		case 32:
			_flush_cache_l1 = r4k_flush_cache_all_s128d32i32;
			_flush_cache_mm = r4k_flush_cache_mm_s128d32i32;
			_flush_cache_range = r4k_flush_cache_range_s128d32i32;
			_flush_cache_page = r4k_flush_cache_page_s128d32i32;
			break;
		};
		_flush_page_to_ram = r4k_flush_page_to_ram_s128;
		_clear_page = r4k_clear_page_s128;
		_copy_page = r4k_copy_page_s128;
		break;
	}
	_dma_cache_wback_inv = r4k_dma_cache_wback_inv_sc;
	_dma_cache_wback = r4k_dma_cache_wback;
	_dma_cache_inv = r4k_dma_cache_inv_sc;
}

typedef int (*probe_func_t)(unsigned long);

static inline void __init setup_scache(unsigned int config)
{
	probe_func_t probe_scache_kseg1;
	int sc_present = 0;

	/* Maybe the cpu knows about a l2 cache? */
	probe_scache_kseg1 = (probe_func_t) (KSEG1ADDR(&probe_scache));
	sc_present = probe_scache_kseg1(config);

	if (sc_present) {
		setup_scache_funcs();
		return;
	}

	setup_noscache_funcs();
}

void __init ld_mmu_r4xx0(void)
{
	unsigned long config = read_32bit_cp0_register(CP0_CONFIG);

	printk("CPU revision is: %08x\n", read_32bit_cp0_register(CP0_PRID));

	set_cp0_config(CONF_CM_CMASK, CONF_CM_CACHABLE_NONCOHERENT);

	probe_icache(config);
	probe_dcache(config);
	setup_scache(config);

	switch(mips_cputype) {
	case CPU_R4600:			/* QED style two way caches? */
	case CPU_R4700:
	case CPU_R5000:
	case CPU_NEVADA:
		_flush_cache_page = r4k_flush_cache_page_d32i32_r4600;
	}

	_flush_cache_sigtramp = r4k_flush_cache_sigtramp;
	if ((read_32bit_cp0_register(CP0_PRID) & 0xfff0) == 0x2020) {
		_flush_cache_sigtramp = r4600v20k_flush_cache_sigtramp;
	}

	_flush_tlb_all = r4k_flush_tlb_all;
	_flush_tlb_mm = r4k_flush_tlb_mm;
	_flush_tlb_range = r4k_flush_tlb_range;
	_flush_tlb_page = r4k_flush_tlb_page;
	_flush_cache_l2 = r4k_flush_cache_l2;

	update_mmu_cache = r4k_update_mmu_cache;

	_show_regs = r4k_show_regs;

	flush_cache_l1();

	/*
	 * You should never change this register:
	 *   - On R4600 1.7 the tlbp never hits for pages smaller than
	 *     the value in the c0_pagemask register.
	 *   - The entire mm handling assumes the c0_pagemask register to
	 *     be set for 4kb pages.
	 */
	write_32bit_cp0_register(CP0_PAGEMASK, PM_4K);
	_flush_tlb_all();
}
