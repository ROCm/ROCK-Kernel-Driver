/*
 * linux/include/asm-arm/arch-sa1100/thinclient.h
 *
 * Created 2000/06/11 by Nicolas Pitre <nico@cam.org>
 *
 * This file contains the hardware specific definitions for the ADS
 * ThinClient/GraphicsClient boards.
 */

#ifndef __ASM_ARCH_HARDWARE_H
#error "include <asm/hardware.h> instead"
#endif


#define ADS_CPLD_BASE		(0x10000000)
#define ADS_p2v( x )		((x) - ADS_CPLD_BASE + 0xf0000000)
#define ADS_v2p( x )		((x) - 0xf0000000 + ADS_CPLD_BASE)


/* Parallel Port */

#define _ADS_PPDR		0x10020000	/* parallel port data reg */
#define _ADS_PPSR		0x10020004	/* parallel port status reg */


/* PCMCIA */

#define _ADS_CS_STATUS		0x10040000	/* PCMCIA status reg */
#define ADS_CS_ST_A_READY	(1 << 0)	/* Socket A Card Ready */
#define ADS_CS_ST_B_READY	(1 << 1)	/* Socket B Card Ready */
#define ADS_CS_ST_A_CD		(1 << 2)	/* Socket A Card Detect */
#define ADS_CS_ST_B_CD		(1 << 3)	/* Socket B Card Detect */
#define ADS_CS_ST_A_BUSY	(1 << 4)	/* Socket A Card Busy */
#define ADS_CS_ST_B_BUSY	(1 << 5)	/* Socket B Card Busy */
#define ADS_CS_ST_A_STS		(1 << 6)	/* Socket A Card STS */
#define ADS_CS_ST_B_STS		(1 << 7)	/* Socket B Card STS */

#define _ADS_CS_PR		0x10040004	/* PCMCIA Power/Reset */
#define ADS_CS_PR_A_POWER	(1 << 0)	/* Socket A Enable Power */
#define ADS_CS_PR_B_POWER	(1 << 1)	/* Socket B Enable Power */
#define ADS_CS_PR_A_RESET	(1 << 2)	/* Socket A Reset */
#define ADS_CS_PR_B_RESET	(1 << 3)	/* Socket B Reset */


#define _ADS_SW_SWITCHES	0x10060000	/* Software Switches */


/* Extra IRQ Controller */

#define _ADS_INT_ST1		0x10080000	/* IRQ Status #1 */
#define _ADS_INT_ST2		0x10080004	/* IRQ Status #2 */
#define _ADS_INT_EN1		0x10080008	/* IRQ Enable #1 */
#define _ADS_INT_EN2		0x1008000c	/* IRQ Enable #2 */


/* On-Board Ethernet */

#define _ADS_ETHERNET		0x100e0000	/* Ethernet */


/* Extra UARTs */

#define _ADS_UARTA		0x10100000	/* UART A */
#define _ADS_UARTB		0x10120000	/* UART B */
#define _ADS_UARTC		0x10140000	/* UART C */
#define _ADS_UARTD		0x10160000	/* UART D */


/* LEDs */

#define ADS_LED0	GPIO_GPIO20		/* on-board D22 */
#define ADS_LED1	GPIO_GPIO21		/* on-board D21 */
#define ADS_LED2	GPIO_GPIO22		/* on-board D20 */
#define ADS_LED3	GPIO_GPIO23		/* external */
#define ADS_LED4	GPIO_GPIO24		/* external */
#define ADS_LED5	GPIO_GPIO25		/* external */
#define ADS_LED6	GPIO_GPIO26		/* external */
#define ADS_LED7	GPIO_GPIO27		/* external */


/* Virtual register addresses */

#ifndef __ASSEMBLY__
#define ADS_INT_ST1	(*((volatile u_char *) ADS_p2v(_ADS_INT_ST1)))
#define ADS_INT_ST2	(*((volatile u_char *) ADS_p2v(_ADS_INT_ST2)))
#define ADS_INT_EN1	(*((volatile u_char *) ADS_p2v(_ADS_INT_EN1)))
#define ADS_INT_EN2	(*((volatile u_char *) ADS_p2v(_ADS_INT_EN2)))
#define ADS_ETHERNET	((int) ADS_p2v(_ADS_ETHERNET))
#endif
