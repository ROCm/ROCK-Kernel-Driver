/* linux/arch/arm/mach-s3c2410/mach-ipaq.c
 *
 * Copyright (c) 2003 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * http://www.handhelds.org/projects/h1940.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *     16-May-2003 BJD  Created initial version
 *     16-Aug-2003 BJD  Fixed header files and copyright, added URL
 *     05-Sep-2003 BJD  Moved to v2.6 kernel
 *     06-Jan-2003 BJD  Updates for <arch/map.h>
 *     18-Jan-2003 BJD  Added serial port configuration
 *     17-Feb-2003 BJD  Copied to mach-ipaq.c
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware.h>
#include <asm/hardware/iomd.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

//#include <asm/debug-ll.h>
#include <asm/arch/regs-serial.h>

#include "s3c2410.h"

static struct map_desc ipaq_iodesc[] __initdata = {
	/* nothing here yet */
};

#define UCON S3C2410_UCON_DEFAULT | S3C2410_UCON_UCLK
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg ipaq_uartcfgs[] = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.clock	     = &s3c2410_pclk,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x51,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.clock	     = &s3c2410_pclk,
		.ucon	     = 0x245,
		.ulcon	     = 0x03,
		.ufcon	     = 0x00,
	},
	/* IR port */
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.clock	     = &s3c2410_pclk,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x43,
		.ufcon	     = 0x51,
	}
};


void __init ipaq_map_io(void)
{
	s3c2410_map_io(ipaq_iodesc, ARRAY_SIZE(ipaq_iodesc));
	s3c2410_uartcfgs = ipaq_uartcfgs;
}

void __init ipaq_init_irq(void)
{
	//llprintk("ipaq_init_irq:\n");

	s3c2410_init_irq();

}

void __init ipaq_init_time(void)
{
	s3c2410_init_time();
}

MACHINE_START(H1940, "IPAQ-H1940")
     MAINTAINER("Ben Dooks <ben@fluff.org>")
     BOOT_MEM(S3C2410_SDRAM_PA, S3C2410_PA_UART, S3C2410_VA_UART)
     BOOT_PARAMS(S3C2410_SDRAM_PA + 0x100)
     MAPIO(ipaq_map_io)
     INITIRQ(ipaq_init_irq)
     INITTIME(ipaq_init_time)
MACHINE_END
