/*
 *  linux/include/asm-arm/arch-pxa/lubbock.h
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define LUBBOCK_FPGA_PHYS	PXA_CS2_PHYS
#define LUBBOCK_FPGA_VIRT	(0xf0000000)	/* phys 0x08000000 */
#define LUBBOCK_ETH_PHYS	PXA_CS3_PHYS
#define LUBBOCK_ETH_VIRT	(0xf1000000)

#define LUB_P2V(x)		((x) - LUBBOCK_FPGA_PHYS + LUBBOCK_FPGA_VIRT)
#define LUB_V2P(x)		((x) - LUBBOCK_FPGA_VIRT + LUBBOCK_FPGA_PHYS)

#ifndef __ASSEMBLY__
#  define __LUB_REG(x)		(*((volatile unsigned long *)LUB_P2V(x)))
#else
#  define __LUB_REG(x)		LUB_P2V(x)
#endif

/* board level registers in the CPLD: (offsets from CPLD_BASE) */

#define WHOAMI			0	// card ID's (see programmers manual)
#define HEX_LED			0x10	// R/W access to 8 7 segment displays
#define DISC_BLNK_LED		0x40 	// R/W [15-8] enables for hex leds, [7-0] discrete LEDs
#define CONF_SWITCHES		0x50	// RO [1] flash wrt prot, [0] 0= boot from rom, 1= flash
#define USER_SWITCHES		0x60	// RO [15-8] dip switches, [7-0] 2 hex encoding switches
#define MISC_WR			0x80	// R/W various system controls -see manual
#define MISC_RD			0x90	// RO various system status bits -see manual
//#define LUB_IRQ_MASK_EN		0xC0    // R/W 0= mask, 1= enable of TS, codec, ethernet, USB, SA1111, and card det. irq's
//#define LUB_IRQ_SET_CLR		0xD0	// R/W 1= set, 0 = clear IRQ's from TS, codec, etc...
//#define LUB_GP			0x100	// R/W [15-0] 16 bits of general purpose I/o for hacking


/* FPGA register physical addresses */
#define _LUB_WHOAMI		(LUBBOCK_FPGA_PHYS + 0x000)
#define _LUB_HEXLED		(LUBBOCK_FPGA_PHYS + 0x010)
#define _LUB_DISC_BLNK_LED	(LUBBOCK_FPGA_PHYS + 0x040)
#define _LUB_CONF_SWITCHES	(LUBBOCK_FPGA_PHYS + 0x050)
#define _LUB_USER_SWITCHES	(LUBBOCK_FPGA_PHYS + 0x060)
#define _LUB_MISC_WR		(LUBBOCK_FPGA_PHYS + 0x080)
#define _LUB_MISC_RD		(LUBBOCK_FPGA_PHYS + 0x090)
#define _LUB_IRQ_MASK_EN	(LUBBOCK_FPGA_PHYS + 0x0C0)
#define _LUB_IRQ_SET_CLR	(LUBBOCK_FPGA_PHYS + 0x0D0)
#define _LUB_GP			(LUBBOCK_FPGA_PHYS + 0x100)

/* FPGA register virtual addresses */
#define LUB_WHOAMI		__LUB_REG(_LUB_WHOAMI)
#define LUB_HEXLED		__LUB_REG(_LUB_HEXLED)
#define LUB_DISC_BLNK_LED	__LUB_REG(_LUB_DISC_BLNK_LED)
#define LUB_CONF_SWITCHES	__LUB_REG(_LUB_CONF_SWITCHES)
#define LUB_USER_SWITCHES	__LUB_REG(_LUB_USER_SWITCHES)
#define LUB_MISC_WR		__LUB_REG(_LUB_MISC_WR)
#define LUB_MISC_RD		__LUB_REG(_LUB_MISC_RD)
#define LUB_IRQ_MASK_EN		__LUB_REG(_LUB_IRQ_MASK_EN)
#define LUB_IRQ_SET_CLR		__LUB_REG(_LUB_IRQ_SET_CLR)
#define LUB_GP			__LUB_REG(_LUB_GP)

/* GPIOs */

#define GPIO_LUBBOCK_IRQ	0
#define IRQ_GPIO_LUBBOCK_IRQ	IRQ_GPIO0


/*
 * LED macros
 */

#define LEDS_BASE LUB_DISC_BLNK_LED

// 8 discrete leds available for general use:

#define D28	0x1
#define D27	0x2
#define D26	0x4
#define D25	0x8
#define D24	0x10
#define D23	0x20
#define D22	0x40
#define D21	0x80

/* Note: bits [15-8] are used to enable/blank the 8 7 segment hex displays so
*  be sure to not monkey with them here.
*/

#define HEARTBEAT_LED	D28
#define SYS_BUSY_LED    D27
#define HEXLEDS_BASE LUB_HEXLED

#define HEARTBEAT_LED_ON  (LEDS_BASE &= ~HEARTBEAT_LED)
#define HEARTBEAT_LED_OFF (LEDS_BASE |= HEARTBEAT_LED)
#define SYS_BUSY_LED_OFF  (LEDS_BASE |= SYS_BUSY_LED)
#define SYS_BUSY_LED_ON   (LEDS_BASE &= ~SYS_BUSY_LED)

// use x = D26-D21 for these, please...
#define DISCRETE_LED_ON(x) (LEDS_BASE &= ~(x))
#define DISCRETE_LED_OFF(x) (LEDS_BASE |= (x))

#ifndef __ASSEMBLY__

//extern int hexled_val = 0;

#endif

#define BUMP_COUNTER (HEXLEDS_BASE = hexled_val++)
#define DEC_COUNTER (HEXLEDS_BASE = hexled_val--)
