/*
 *  linux/arch/arm/kernel/time-acorn.c
 *
 *  Copyright (c) 1996-2000 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   24-Sep-1996	RMK	Created
 *   10-Oct-1996	RMK	Brought up to date with arch-sa110eval
 *   04-Dec-1997	RMK	Updated for new arch/arm/time.c
 *   13-May-2003        IM      Brought over to ARM26
 */
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ioc.h>

extern unsigned long (*gettimeoffset)(void);

static unsigned long ioctime_gettimeoffset(void)
{
	unsigned int count1, count2, status;
        long offset;

        ioc_writeb (0, IOC_T0LATCH);
        barrier ();
        count1 = ioc_readb(IOC_T0CNTL) | (ioc_readb(IOC_T0CNTH) << 8);
        barrier ();
        status = ioc_readb(IOC_IRQREQA);
        barrier ();
        ioc_writeb (0, IOC_T0LATCH);
        barrier ();
        count2 = ioc_readb(IOC_T0CNTL) | (ioc_readb(IOC_T0CNTH) << 8);

        offset = count2;
        if (count2 < count1) {
                /*
                 * We have not had an interrupt between reading count1
                 * and count2.
                 */
                if (status & (1 << 5))
                        offset -= LATCH;
        } else if (count2 > count1) {
                /*
                 * We have just had another interrupt between reading
                 * count1 and count2.
                 */
                offset -= LATCH;
        }

        offset = (LATCH - offset) * (tick_nsec / 1000);
        return (offset + LATCH/2) / LATCH;
}

void __init ioctime_init(void)
{
	ioc_writeb(LATCH & 255, IOC_T0LTCHL);
	ioc_writeb(LATCH >> 8, IOC_T0LTCHH);
	ioc_writeb(0, IOC_T0GO);

	gettimeoffset = ioctime_gettimeoffset;
}
