/*
 *
 * By Dustin McIntire (dustin@sensoria.com) (c)2001
 * 
 * Setup and IRQ handling code for the HD64465 companion chip.
 * by Greg Banks <gbanks@pocketpenguins.com>
 * Copyright (c) 2000 PocketPenguins Inc
 *
 * Derived from setup_hd64465.c which bore the message:
 * Greg Banks <gbanks@pocketpenguins.com>
 * Copyright (c) 2000 PocketPenguins Inc and
 * Copyright (C) 2000 YAEGASHI Takeshi
 * and setup_cqreek.c which bore message:
 * Copyright (C) 2000  Niibe Yutaka
 * 
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Setup functions for a Hitachi Big Sur Evaluation Board.
 * 
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>

#include <asm/bigsur/io.h>
#include <asm/hd64465/hd64465.h>
#include <asm/bigsur/bigsur.h>

//#define BIGSUR_DEBUG 3
#undef BIGSUR_DEBUG

#ifdef BIGSUR_DEBUG
#define DPRINTK(args...)	printk(args)
#define DIPRINTK(n, args...)	if (BIGSUR_DEBUG>(n)) printk(args)
#else
#define DPRINTK(args...)
#define DIPRINTK(n, args...)
#endif /* BIGSUR_DEBUG */

/*===========================================================*/
//		Big Sur Init Routines	
/*===========================================================*/

const char *get_system_type(void)
{
	return "Big Sur";
}

int __init platform_setup(void)
{
	static int done = 0; /* run this only once */

	if (!MACH_BIGSUR || done) return 0;
	done = 1;

	/* Mask all 2nd level IRQ's */
	outb(-1,BIGSUR_IMR0);
	outb(-1,BIGSUR_IMR1);
	outb(-1,BIGSUR_IMR2);
	outb(-1,BIGSUR_IMR3);

	/* Mask 1st level interrupts */
	outb(-1,BIGSUR_IRLMR0);
	outb(-1,BIGSUR_IRLMR1);

#if defined (CONFIG_HD64465) && defined (CONFIG_SERIAL) 
	/* remap IO ports for first ISA serial port to HD64465 UART */
	bigsur_port_map(0x3f8, 8, CONFIG_HD64465_IOBASE + 0x8000, 1);
#endif /* CONFIG_HD64465 && CONFIG_SERIAL */
	/* TODO: setup IDE registers */
	bigsur_port_map(BIGSUR_IDECTL_IOPORT, 2, BIGSUR_ICTL, 8);
	/* Setup the Ethernet port to BIGSUR_ETHER_IOPORT */
	bigsur_port_map(BIGSUR_ETHER_IOPORT, 16, BIGSUR_ETHR+BIGSUR_ETHER_IOPORT, 0);
	/* set page to 1 */
	outw(1, BIGSUR_ETHR+0xe);
	/* set the IO port to BIGSUR_ETHER_IOPORT */
	outw(BIGSUR_ETHER_IOPORT<<3, BIGSUR_ETHR+0x2);

    return 0;
}

module_init(setup_bigsur);
