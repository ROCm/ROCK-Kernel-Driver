/*
 *  linux/arch/arm/kernel/oldlatches.c
 *
 *  Copyright (C) David Alan Gilbert 1995/1996,2000
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Support for the latches on the old Archimedes which control the floppy,
 *  hard disc and printer
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>

#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/arch/oldlatches.h>

static unsigned char latch_a_copy;
static unsigned char latch_b_copy;

/* newval=(oldval & ~mask)|newdata */
void oldlatch_aupdate(unsigned char mask,unsigned char newdata)
{
	if (machine_is_archimedes()) {
		latch_a_copy = (latch_a_copy & ~mask) | newdata;

		printk("Latch: A = 0x%02x\n", latch_a_copy);

		__raw_writeb(latch_a_copy, LATCHA_BASE);
	} else
		BUG();
}


/* newval=(oldval & ~mask)|newdata */
void oldlatch_bupdate(unsigned char mask,unsigned char newdata)
{
	if (machine_is_archimedes()) {
		latch_b_copy = (latch_b_copy & ~mask) | newdata;

		printk("Latch: B = 0x%02x\n", latch_b_copy);

		__raw_writeb(latch_b_copy, LATCHB_BASE);
	} else
		BUG();
}

static void __init oldlatch_init(void)
{
	if (machine_is_archimedes()) {
		oldlatch_aupdate(0xff, 0xff);
		/* Thats no FDC reset...*/
		oldlatch_bupdate(0xff, LATCHB_FDCRESET);
	}
}

initcall(oldlatch_init);

EXPORT_SYMBOL(oldlatch_aupdate);
EXPORT_SYMBOL(oldlatch_bupdate);
