/* linux/arch/arm/mach-s3c2410/mach-bast.c
 *
 * Copyright (c) 2003 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * http://www.simtec.co.uk/products/EB2410ITX/
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

#include <asm/arch/bast-map.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

//#include <asm/debug-ll.h>
#include <asm/arch/regs-serial.h>

#include "s3c2410.h"

/* macros for virtual address mods for the io space entries */
#define VA_C5(item) ((item) + BAST_VAM_CS5)
#define VA_C4(item) ((item) + BAST_VAM_CS4)
#define VA_C3(item) ((item) + BAST_VAM_CS3)
#define VA_C2(item) ((item) + BAST_VAM_CS2)

/* macros to modify the physical addresses for io space */

#define PA_CS2(item) ((item) + S3C2410_CS2)
#define PA_CS3(item) ((item) + S3C2410_CS3)
#define PA_CS4(item) ((item) + S3C2410_CS4)
#define PA_CS5(item) ((item) + S3C2410_CS5)

static struct map_desc bast_iodesc[] __initdata = {
  /* ISA IO areas */

  { S3C2410_VA_ISA_BYTE, PA_CS2(BAST_PA_ISAIO),	   SZ_16M, MT_DEVICE },
  { S3C2410_VA_ISA_WORD, PA_CS3(BAST_PA_ISAIO),	   SZ_16M, MT_DEVICE },

  /* we could possibly compress the next set down into a set of smaller tables
   * pagetables, but that would mean using an L2 section, and it still means
   * we cannot actually feed the same register to an LDR due to 16K spacing
   */

  /* bast CPLD control registers, and external interrupt controls */
  { BAST_VA_CTRL1, BAST_PA_CTRL1,		   SZ_1M, MT_DEVICE },
  { BAST_VA_CTRL2, BAST_PA_CTRL2,		   SZ_1M, MT_DEVICE },
  { BAST_VA_CTRL3, BAST_PA_CTRL3,		   SZ_1M, MT_DEVICE },
  { BAST_VA_CTRL4, BAST_PA_CTRL4,		   SZ_1M, MT_DEVICE },

  /* PC104 IRQ mux */
  { BAST_VA_PC104_IRQREQ,  BAST_PA_PC104_IRQREQ,   SZ_1M, MT_DEVICE },
  { BAST_VA_PC104_IRQRAW,  BAST_PA_PC104_IRQRAW,   SZ_1M, MT_DEVICE },
  { BAST_VA_PC104_IRQMASK, BAST_PA_PC104_IRQMASK,  SZ_1M, MT_DEVICE },

  /* onboard 8bit lcd port */

  { BAST_VA_LCD_RCMD1,  BAST_PA_LCD_RCMD1,	   SZ_1M, MT_DEVICE },
  { BAST_VA_LCD_WCMD1,  BAST_PA_LCD_WCMD1,	   SZ_1M, MT_DEVICE },
  { BAST_VA_LCD_RDATA1, BAST_PA_LCD_RDATA1,	   SZ_1M, MT_DEVICE },
  { BAST_VA_LCD_WDATA1, BAST_PA_LCD_WDATA1,	   SZ_1M, MT_DEVICE },
  { BAST_VA_LCD_RCMD2,  BAST_PA_LCD_RCMD2,	   SZ_1M, MT_DEVICE },
  { BAST_VA_LCD_WCMD2,  BAST_PA_LCD_WCMD2,	   SZ_1M, MT_DEVICE },
  { BAST_VA_LCD_RDATA2, BAST_PA_LCD_RDATA2,	   SZ_1M, MT_DEVICE },
  { BAST_VA_LCD_WDATA2, BAST_PA_LCD_WDATA2,	   SZ_1M, MT_DEVICE },

  /* peripheral space... one for each of fast/slow/byte/16bit */
  /* note, ide is only decoded in word space, even though some registers
   * are only 8bit */

  /* slow, byte */
  { VA_C2(BAST_VA_ISAIO),   PA_CS2(BAST_PA_ISAIO),    SZ_16M, MT_DEVICE },
  { VA_C2(BAST_VA_ISAMEM),  PA_CS2(BAST_PA_ISAMEM),   SZ_16M, MT_DEVICE },
  { VA_C2(BAST_VA_ASIXNET), PA_CS3(BAST_PA_ASIXNET),  SZ_1M,  MT_DEVICE },
  { VA_C2(BAST_VA_SUPERIO), PA_CS2(BAST_PA_SUPERIO),  SZ_1M,  MT_DEVICE },
  { VA_C2(BAST_VA_DM9000),  PA_CS2(BAST_PA_DM9000),   SZ_1M,  MT_DEVICE },
  { VA_C2(BAST_VA_IDEPRI),  PA_CS3(BAST_PA_IDEPRI),   SZ_1M,  MT_DEVICE },
  { VA_C2(BAST_VA_IDESEC),  PA_CS3(BAST_PA_IDESEC),   SZ_1M,  MT_DEVICE },
  { VA_C2(BAST_VA_IDEPRIAUX), PA_CS3(BAST_PA_IDEPRIAUX), SZ_1M, MT_DEVICE },
  { VA_C2(BAST_VA_IDESECAUX), PA_CS3(BAST_PA_IDESECAUX), SZ_1M, MT_DEVICE },

  /* slow, word */
  { VA_C3(BAST_VA_ISAIO),   PA_CS3(BAST_PA_ISAIO),    SZ_16M, MT_DEVICE },
  { VA_C3(BAST_VA_ISAMEM),  PA_CS3(BAST_PA_ISAMEM),   SZ_16M, MT_DEVICE },
  { VA_C3(BAST_VA_ASIXNET), PA_CS3(BAST_PA_ASIXNET),  SZ_1M,  MT_DEVICE },
  { VA_C3(BAST_VA_SUPERIO), PA_CS3(BAST_PA_SUPERIO),  SZ_1M,  MT_DEVICE },
  { VA_C3(BAST_VA_DM9000),  PA_CS3(BAST_PA_DM9000),   SZ_1M,  MT_DEVICE },
  { VA_C3(BAST_VA_IDEPRI),  PA_CS3(BAST_PA_IDEPRI),   SZ_1M,  MT_DEVICE },
  { VA_C3(BAST_VA_IDESEC),  PA_CS3(BAST_PA_IDESEC),   SZ_1M,  MT_DEVICE },
  { VA_C3(BAST_VA_IDEPRIAUX), PA_CS3(BAST_PA_IDEPRIAUX), SZ_1M, MT_DEVICE },
  { VA_C3(BAST_VA_IDESECAUX), PA_CS3(BAST_PA_IDESECAUX), SZ_1M, MT_DEVICE },

  /* fast, byte */
  { VA_C4(BAST_VA_ISAIO),   PA_CS4(BAST_PA_ISAIO),    SZ_16M, MT_DEVICE },
  { VA_C4(BAST_VA_ISAMEM),  PA_CS4(BAST_PA_ISAMEM),   SZ_16M, MT_DEVICE },
  { VA_C4(BAST_VA_ASIXNET), PA_CS5(BAST_PA_ASIXNET),  SZ_1M,  MT_DEVICE },
  { VA_C4(BAST_VA_SUPERIO), PA_CS4(BAST_PA_SUPERIO),  SZ_1M,  MT_DEVICE },
  { VA_C4(BAST_VA_DM9000),  PA_CS4(BAST_PA_DM9000),   SZ_1M,  MT_DEVICE },
  { VA_C4(BAST_VA_IDEPRI),  PA_CS5(BAST_PA_IDEPRI),   SZ_1M,  MT_DEVICE },
  { VA_C4(BAST_VA_IDESEC),  PA_CS5(BAST_PA_IDESEC),   SZ_1M,  MT_DEVICE },
  { VA_C4(BAST_VA_IDEPRIAUX), PA_CS5(BAST_PA_IDEPRIAUX), SZ_1M, MT_DEVICE },
  { VA_C4(BAST_VA_IDESECAUX), PA_CS5(BAST_PA_IDESECAUX), SZ_1M, MT_DEVICE },

  /* fast, word */
  { VA_C5(BAST_VA_ISAIO),   PA_CS5(BAST_PA_ISAIO),    SZ_16M, MT_DEVICE },
  { VA_C5(BAST_VA_ISAMEM),  PA_CS5(BAST_PA_ISAMEM),   SZ_16M, MT_DEVICE },
  { VA_C5(BAST_VA_ASIXNET), PA_CS5(BAST_PA_ASIXNET),  SZ_1M,  MT_DEVICE },
  { VA_C5(BAST_VA_SUPERIO), PA_CS5(BAST_PA_SUPERIO),  SZ_1M,  MT_DEVICE },
  { VA_C5(BAST_VA_DM9000),  PA_CS5(BAST_PA_DM9000),   SZ_1M,  MT_DEVICE },
  { VA_C5(BAST_VA_IDEPRI),  PA_CS5(BAST_PA_IDEPRI),   SZ_1M,  MT_DEVICE },
  { VA_C5(BAST_VA_IDESEC),  PA_CS5(BAST_PA_IDESEC),   SZ_1M,  MT_DEVICE },
  { VA_C5(BAST_VA_IDEPRIAUX), PA_CS5(BAST_PA_IDEPRIAUX), SZ_1M, MT_DEVICE },
  { VA_C5(BAST_VA_IDESECAUX), PA_CS5(BAST_PA_IDESECAUX), SZ_1M, MT_DEVICE },
};

#define UCON S3C2410_UCON_DEFAULT | S3C2410_UCON_UCLK
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

/* base baud rate for all our UARTs */
static unsigned long bast_serial_clock = 24*1000*1000;

static struct s3c2410_uartcfg bast_uartcfgs[] = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.clock	     = &bast_serial_clock,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.clock	     = &bast_serial_clock,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	},
	/* port 2 is not actually used */
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.clock	     = &bast_serial_clock,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	}
};


void __init bast_map_io(void)
{
	s3c2410_map_io(bast_iodesc, ARRAY_SIZE(bast_iodesc));
	s3c2410_uartcfgs = bast_uartcfgs;
}

void __init bast_init_irq(void)
{
	//llprintk("bast_init_irq:\n");

	s3c2410_init_irq();

}

void __init bast_init_time(void)
{
	s3c2410_init_time();
}

MACHINE_START(BAST, "Simtec-BAST")
     MAINTAINER("Ben Dooks <ben@simtec.co.uk>")
     BOOT_MEM(S3C2410_SDRAM_PA, S3C2410_PA_UART, S3C2410_VA_UART)
     BOOT_PARAMS(S3C2410_SDRAM_PA + 0x100)
     MAPIO(bast_map_io)
     INITIRQ(bast_init_irq)
     INITTIME(bast_init_time)
MACHINE_END
