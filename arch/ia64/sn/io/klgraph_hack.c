/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2002 Silicon Graphics, Inc. All rights reserved.
 */


/*
 * This is a temporary file that statically initializes the expected 
 * initial klgraph information that is normally provided by prom.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <asm/sn/sgi.h>
#include <asm/sn/io.h>
#include <asm/sn/klconfig.h>

void * real_port;
void * real_io_base;
void * real_addr;

char *BW0 = NULL;

kl_config_hdr_t *linux_klcfg;

#ifdef DEFINE_DUMP_RTNS
/* forward declarations */
static void dump_ii(void), dump_crossbow(void);
static void clear_ii_error(void);
#endif /* DEFINE_DUMP_RTNS */

#define SYNERGY_WIDGET          ((char *)0xc0000e0000000000)
#define SYNERGY_SWIZZLE         ((char *)0xc0000e0000000400)
#define HUBREG                  ((char *)0xc0000a0001e00000)
#define WIDGET0                 ((char *)0xc0000a0000000000)
#define WIDGET4                 ((char *)0xc0000a0000000004)

#define SYNERGY_WIDGET          ((char *)0xc0000e0000000000)
#define SYNERGY_SWIZZLE         ((char *)0xc0000e0000000400)
#define HUBREG                  ((char *)0xc0000a0001e00000)
#define WIDGET0                 ((char *)0xc0000a0000000000)

void
klgraph_hack_init(void)
{

	/*
	 * We need to know whether we are booting from PROM or 
	 * boot from disk.
	 */
	linux_klcfg = (kl_config_hdr_t *)0xe000000000030000;
	if (linux_klcfg->ch_magic == 0xbeedbabe) {
		return;
	} else {
		panic("klgraph_hack_init: Unable to locate KLCONFIG TABLE\n");
	}

}




	
#ifdef DEFINE_DUMP_RTNS
/* 
 * these were useful for printing out registers etc
 * during bringup  
 */

static void
xdump(long long *addr, int count)
{
	int ii;
	volatile long long *xx = addr;

	for ( ii = 0; ii < count; ii++, xx++ ) {
		printk("0x%p : 0x%p\n", (void *)xx, (void *)*xx);
	}
}

static void
xdump32(unsigned int *addr, int count)
{
	int ii;
	volatile unsigned int *xx = addr;

	for ( ii = 0; ii < count; ii++, xx++ ) {
		printk("0x%p : 0x%0x\n", (void *)xx, (int)*xx);
	}
}

static void
clear_ii_error(void)
{
	volatile long long *tmp;

	printk("... WSTAT ");
	xdump((long long *)0xc0000a0001c00008, 1);
	printk("... WCTRL ");
	xdump((long long *)0xc0000a0001c00020, 1);
	printk("... WLCSR ");
	xdump((long long *)0xc0000a0001c00128, 1);
	printk("... IIDSR ");
	xdump((long long *)0xc0000a0001c00138, 1);
        printk("... IOPRBs ");
	xdump((long long *)0xc0000a0001c00198, 9);
	printk("... IXSS ");
	xdump((long long *)0xc0000a0001c00210, 1);
	printk("... IBLS0 ");
	xdump((long long *)0xc0000a0001c10000, 1);
	printk("... IBLS1 ");
	xdump((long long *)0xc0000a0001c20000, 1);

        /* Write IOERR clear to clear the CRAZY bit in the status */
        tmp = (long long *)0xc0000a0001c001f8; *tmp = (long long)0xffffffff;

	/* dump out local block error registers */
	printk("... ");
	xdump((long long *)0xc0000a0001e04040, 1);	/* LB_ERROR_BITS */
	printk("... ");
	xdump((long long *)0xc0000a0001e04050, 1);	/* LB_ERROR_HDR1 */
	printk("... ");
	xdump((long long *)0xc0000a0001e04058, 1);	/* LB_ERROR_HDR2 */
	/* and clear the LB_ERROR_BITS */
	tmp = (long long *)0xc0000a0001e04040; *tmp = 0x0;
	printk("clr: ");
	xdump((long long *)0xc0000a0001e04040, 1);	/* LB_ERROR_BITS */
	tmp = (long long *)0xc0000a0001e04050; *tmp = 0x0;
	tmp = (long long *)0xc0000a0001e04058; *tmp = 0x0;
}


static void
dump_ii(void)
{
	printk("===== Dump the II regs =====\n");
	xdump((long long *)0xc0000a0001c00000, 2);
	xdump((long long *)0xc0000a0001c00020, 1);
	xdump((long long *)0xc0000a0001c00100, 37);
	xdump((long long *)0xc0000a0001c00300, 98);
	xdump((long long *)0xc0000a0001c10000, 6);
	xdump((long long *)0xc0000a0001c20000, 6);
	xdump((long long *)0xc0000a0001c30000, 2);

	xdump((long long *)0xc0000a0000000000, 1);
	xdump((long long *)0xc0000a0001000000, 1);
	xdump((long long *)0xc0000a0002000000, 1);
	xdump((long long *)0xc0000a0003000000, 1);
	xdump((long long *)0xc0000a0004000000, 1);
	xdump((long long *)0xc0000a0005000000, 1);
	xdump((long long *)0xc0000a0006000000, 1);
	xdump((long long *)0xc0000a0007000000, 1);
	xdump((long long *)0xc0000a0008000000, 1);
	xdump((long long *)0xc0000a0009000000, 1);
	xdump((long long *)0xc0000a000a000000, 1);
	xdump((long long *)0xc0000a000b000000, 1);
	xdump((long long *)0xc0000a000c000000, 1);
	xdump((long long *)0xc0000a000d000000, 1);
	xdump((long long *)0xc0000a000e000000, 1);
	xdump((long long *)0xc0000a000f000000, 1);
}

static void
dump_crossbow(void)
{
	printk("===== Dump the Crossbow regs =====\n");
	clear_ii_error();
	xdump32((unsigned int *)0xc0000a0000000004, 1);
	clear_ii_error();
	xdump32((unsigned int *)0xc0000a0000000000, 1);
	printk("and again..\n");
	xdump32((unsigned int *)0xc0000a0000000000, 1);
	xdump32((unsigned int *)0xc0000a0000000000, 1);


	clear_ii_error();

	xdump32((unsigned int *)0xc000020000000004, 1);
	clear_ii_error();
	xdump32((unsigned int *)0xc000020000000000, 1);
	clear_ii_error();

	xdump32((unsigned int *)0xc0000a0000800004, 1);
	clear_ii_error();
	xdump32((unsigned int *)0xc0000a0000800000, 1);
	clear_ii_error();

	xdump32((unsigned int *)0xc000020000800004, 1);
	clear_ii_error();
	xdump32((unsigned int *)0xc000020000800000, 1);
	clear_ii_error();


}
#endif /* DEFINE_DUMP_RTNS */
