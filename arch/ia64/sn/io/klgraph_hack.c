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

#include <linux/config.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <asm/sn/sgi.h>
#include <asm/sn/io.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/simulator.h>

extern u64 klgraph_addr[];
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

#define convert(a,b,c) temp = (u64 *)a; *temp = b; temp++; *temp = c

void
klgraph_hack_init(void)
{

	u64     *temp;

#ifdef CONFIG_IA64_SGI_SN1
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

	convert(0x0000000000030000, 0x00000000beedbabe, 0x0000004800000000);

#else

	if (IS_RUNNING_ON_SIMULATOR()) {
		printk("Creating FAKE Klconfig Structure for Embeded Kernel\n");
		klgraph_addr[0] = 0xe000003000030000;

        /*
         * klconfig entries initialization - mankato
         */
        convert(0xe000003000030000, 0x00000000beedbabe, 0x0000004800000000);
        convert(0xe000003000030010, 0x0003007000000018, 0x800002000f820178);
        convert(0xe000003000030020, 0x80000a000f024000, 0x800002000f800000);
        convert(0xe000003000030030, 0x0300fafa00012580, 0x00000000040f0000);
        convert(0xe000003000030040, 0x0000000000000000, 0x0003097000030070);
        convert(0xe000003000030050, 0x00030970000303b0, 0x0003181000033f70);
        convert(0xe000003000030060, 0x0003d51000037570, 0x0000000000038330);
        convert(0xe000003000030070, 0x0203110100030140, 0x0001000000000101);
        convert(0xe000003000030080, 0x0900000000000000, 0x000000004e465e67);
        convert(0xe000003000030090, 0x0003097000000000, 0x00030b1000030a40);
        convert(0xe0000030000300a0, 0x00030cb000030be0, 0x000315a0000314d0);
        convert(0xe0000030000300b0, 0x0003174000031670, 0x0000000000000000);
        convert(0xe000003000030100, 0x000000000000001a, 0x3350490000000000);
        convert(0xe000003000030110, 0x0000000000000037, 0x0000000000000000);
        convert(0xe000003000030140, 0x0002420100030210, 0x0001000000000101);
        convert(0xe000003000030150, 0x0100000000000000, 0xffffffffffffffff);
        convert(0xe000003000030160, 0x00030d8000000000, 0x0000000000030e50);
        convert(0xe0000030000301c0, 0x0000000000000000, 0x0000000000030070);
        convert(0xe0000030000301d0, 0x0000000000000025, 0x424f490000000000);
        convert(0xe0000030000301e0, 0x000000004b434952, 0x0000000000000000);
        convert(0xe000003000030210, 0x00027101000302e0, 0x00010000000e4101);
        convert(0xe000003000030220, 0x0200000000000000, 0xffffffffffffffff);
        convert(0xe000003000030230, 0x00030f2000000000, 0x0000000000030ff0);
        convert(0xe000003000030290, 0x0000000000000000, 0x0000000000030140);
        convert(0xe0000030000302a0, 0x0000000000000026, 0x7262490000000000);
        convert(0xe0000030000302b0, 0x00000000006b6369, 0x0000000000000000);
        convert(0xe0000030000302e0, 0x0002710100000000, 0x00010000000f3101);
        convert(0xe0000030000302f0, 0x0500000000000000, 0xffffffffffffffff);
        convert(0xe000003000030300, 0x000310c000000000, 0x0003126000031190);
        convert(0xe000003000030310, 0x0003140000031330, 0x0000000000000000);
        convert(0xe000003000030360, 0x0000000000000000, 0x0000000000030140);
        convert(0xe000003000030370, 0x0000000000000029, 0x7262490000000000);
        convert(0xe000003000030380, 0x00000000006b6369, 0x0000000000000000);
        convert(0xe000003000030970, 0x0000000002010102, 0x0000000000000000);
        convert(0xe000003000030980, 0x000000004e465e67, 0xffffffff00000000);
        /* convert(0x00000000000309a0, 0x0000000000037570, 0x0000000100000000); */
        convert(0xe0000030000309a0, 0x0000000000037570, 0xffffffff00000000);
        convert(0xe0000030000309b0, 0x0000000000030070, 0x0000000000000000);
        convert(0xe0000030000309c0, 0x000000000003f420, 0x0000000000000000);
        convert(0xe000003000030a40, 0x0000000002010125, 0x0000000000000000);
        convert(0xe000003000030a50, 0xffffffffffffffff, 0xffffffff00000000);
        convert(0xe000003000030a70, 0x0000000000037b78, 0x0000000000000000);
        convert(0xe000003000030b10, 0x0000000002010125, 0x0000000000000000);
        convert(0xe000003000030b20, 0xffffffffffffffff, 0xffffffff00000000);
        convert(0xe000003000030b40, 0x0000000000037d30, 0x0000000000000001);
        convert(0xe000003000030be0, 0x00000000ff010203, 0x0000000000000000);
        convert(0xe000003000030bf0, 0xffffffffffffffff, 0xffffffff000000ff);
        convert(0xe000003000030c10, 0x0000000000037ee8, 0x0100010000000200);
        convert(0xe000003000030cb0, 0x00000000ff310111, 0x0000000000000000);
        convert(0xe000003000030cc0, 0xffffffffffffffff, 0x0000000000000000);
        convert(0xe000003000030d80, 0x0000000002010104, 0x0000000000000000);
        convert(0xe000003000030d90, 0xffffffffffffffff, 0x00000000000000ff);
        convert(0xe000003000030db0, 0x0000000000037f18, 0x0000000000000000);
        convert(0xe000003000030dc0, 0x0000000000000000, 0x0003007000060000);
        convert(0xe000003000030de0, 0x0000000000000000, 0x0003021000050000);
        convert(0xe000003000030df0, 0x000302e000050000, 0x0000000000000000);
        convert(0xe000003000030e30, 0x0000000000000000, 0x000000000000000a);
        convert(0xe000003000030e50, 0x00000000ff00011a, 0x0000000000000000);
        convert(0xe000003000030e60, 0xffffffffffffffff, 0x0000000000000000);
        convert(0xe000003000030e80, 0x0000000000037fe0, 0x9e6e9e9e9e9e9e9e);
        convert(0xe000003000030e90, 0x000000000000bc6e, 0x0000000000000000);
        convert(0xe000003000030f20, 0x0000000002010205, 0x00000000d0020000);
        convert(0xe000003000030f30, 0xffffffffffffffff, 0x0000000e0000000e);
        convert(0xe000003000030f40, 0x000000000000000e, 0x0000000000000000);
        convert(0xe000003000030f50, 0x0000000000038010, 0x00000000000007ff);
        convert(0xe000003000030f70, 0x0000000000000000, 0x0000000022001077);
        convert(0xe000003000030fa0, 0x0000000000000000, 0x000000000003f4a8);
        convert(0xe000003000030ff0, 0x0000000000310120, 0x0000000000000000);
        convert(0xe000003000031000, 0xffffffffffffffff, 0xffffffff00000002);
        convert(0xe000003000031010, 0x000000000000000e, 0x0000000000000000);
        convert(0xe000003000031020, 0x0000000000038088, 0x0000000000000000);
        convert(0xe0000030000310c0, 0x0000000002010205, 0x00000000d0020000);
        convert(0xe0000030000310d0, 0xffffffffffffffff, 0x0000000f0000000f);
        convert(0xe0000030000310e0, 0x000000000000000f, 0x0000000000000000);
        convert(0xe0000030000310f0, 0x00000000000380b8, 0x00000000000007ff);
        convert(0xe000003000031120, 0x0000000022001077, 0x00000000000310a9);
        convert(0xe000003000031130, 0x00000000580211c1, 0x000000008009104c);
        convert(0xe000003000031140, 0x0000000000000000, 0x000000000003f4c0);
        convert(0xe000003000031190, 0x0000000000310120, 0x0000000000000000);
        convert(0xe0000030000311a0, 0xffffffffffffffff, 0xffffffff00000003);
        convert(0xe0000030000311b0, 0x000000000000000f, 0x0000000000000000);
        convert(0xe0000030000311c0, 0x0000000000038130, 0x0000000000000000);
        convert(0xe000003000031260, 0x0000000000110106, 0x0000000000000000);
        convert(0xe000003000031270, 0xffffffffffffffff, 0xffffffff00000004);
        convert(0xe000003000031270, 0xffffffffffffffff, 0xffffffff00000004);
        convert(0xe000003000031280, 0x000000000000000f, 0x0000000000000000);
        convert(0xe0000030000312a0, 0x00000000ff110013, 0x0000000000000000);
        convert(0xe0000030000312b0, 0xffffffffffffffff, 0xffffffff00000000);
        convert(0xe0000030000312c0, 0x000000000000000f, 0x0000000000000000);
        convert(0xe0000030000312e0, 0x0000000000110012, 0x0000000000000000);
        convert(0xe0000030000312f0, 0xffffffffffffffff, 0xffffffff00000000);
        convert(0xe000003000031300, 0x000000000000000f, 0x0000000000000000);
        convert(0xe000003000031310, 0x0000000000038160, 0x0000000000000000);
        convert(0xe000003000031330, 0x00000000ff310122, 0x0000000000000000);
        convert(0xe000003000031340, 0xffffffffffffffff, 0xffffffff00000005);
        convert(0xe000003000031350, 0x000000000000000f, 0x0000000000000000);
        convert(0xe000003000031360, 0x0000000000038190, 0x0000000000000000);
        convert(0xe000003000031400, 0x0000000000310121, 0x0000000000000000);
        convert(0xe000003000031400, 0x0000000000310121, 0x0000000000000000);
        convert(0xe000003000031410, 0xffffffffffffffff, 0xffffffff00000006);
        convert(0xe000003000031420, 0x000000000000000f, 0x0000000000000000);
        convert(0xe000003000031430, 0x00000000000381c0, 0x0000000000000000);
        convert(0xe0000030000314d0, 0x00000000ff010201, 0x0000000000000000);
        convert(0xe0000030000314e0, 0xffffffffffffffff, 0xffffffff00000000);
        convert(0xe000003000031500, 0x00000000000381f0, 0x000030430000ffff);
        convert(0xe000003000031510, 0x000000000000ffff, 0x0000000000000000);
        convert(0xe0000030000315a0, 0x00000020ff000201, 0x0000000000000000);
        convert(0xe0000030000315b0, 0xffffffffffffffff, 0xffffffff00000001);
        convert(0xe0000030000315d0, 0x0000000000038240, 0x00003f3f0000ffff);
        convert(0xe0000030000315e0, 0x000000000000ffff, 0x0000000000000000);
        convert(0xe000003000031670, 0x00000000ff010201, 0x0000000000000000);
        convert(0xe000003000031680, 0xffffffffffffffff, 0x0000000100000002);
        convert(0xe0000030000316a0, 0x0000000000038290, 0x000030430000ffff);
        convert(0xe0000030000316b0, 0x000000000000ffff, 0x0000000000000000);
        convert(0xe000003000031740, 0x00000020ff000201, 0x0000000000000000);
        convert(0xe000003000031750, 0xffffffffffffffff, 0x0000000500000003);
        convert(0xe000003000031770, 0x00000000000382e0, 0x00003f3f0000ffff);
        convert(0xe000003000031780, 0x000000000000ffff, 0x0000000000000000);
}

#endif

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
