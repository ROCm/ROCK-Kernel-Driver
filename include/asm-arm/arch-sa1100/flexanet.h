/*
 * linux/include/asm-arm/arch-sa1100/flexanet.h
 *
 * Created 2001/05/04 by Jordi Colomer <jco@ict.es>
 *
 * This file contains the hardware specific definitions for FlexaNet
 *
 */

#ifndef __ASM_ARCH_HARDWARE_H
#error "include <asm/hardware.h> instead"
#endif


/* Board Control Register (virtual address) */
#define BCR_PHYS  0x10000000
#define BCR_VIRT  0xf0000000
#define BCR (*(volatile unsigned int *)(BCR_VIRT))

/* Power-up value */
#define BCR_POWERUP	0x00000000

/* Mandatory bits */
#define BCR_LED_GREEN	(1<<0)	/* General-purpose green LED (1 = on) */
#define BCR_GUI_NRST	(1<<4)	/* GUI board reset (0 = reset) */

/* Board Status Register (virtual address) */
#define BSR_BASE  BCR_BASE
#define BSR (*(volatile unsigned int *)(BSR_BASE))


#ifndef __ASSEMBLY__
extern unsigned long BCR_value;	/* Image of the BCR */
#define BCR_set( x )	BCR = (BCR_value |= (x))
#define BCR_clear( x )	BCR = (BCR_value &= ~(x))
#endif


/* GPIOs for which the generic definition doesn't say much */
#define GPIO_GUI_IRQ		GPIO_GPIO (23)	/* IRQ from GUI board (i.e., UCB1300) */
#define GPIO_ETH_IRQ		GPIO_GPIO (24)	/* IRQ from Ethernet controller */
#define GPIO_LED_RED		GPIO_GPIO (26)	/* General-purpose red LED */

/* IRQ sources from GPIOs */
#define IRQ_GPIO_GUI		IRQ_GPIO23
#define IRQ_GPIO_ETH		IRQ_GPIO24

/* On-Board Ethernet */
#define _FHH_ETH_IOBASE		0x18000000	/* I/O base (physical addr) */
#define _FHH_ETH_MMBASE		0x18800000	/* Attribute-memory base */
#define FHH_ETH_SIZE		0x01000000	/* total size */
#define FHH_ETH_VIRT		0xF1000000	/* Ethernet virtual address */

#define FHH_ETH_p2v( x )	((x) - _FHH_ETH_IOBASE + FHH_ETH_VIRT)
#define FHH_ETH_v2p( x )	((x) - FHH_ETH_VIRT + _FHH_ETH_IOBASE)

#define FHH_ETH_IOBASE		FHH_ETH_p2v(_FHH_ETH_IOBASE) /* Virtual base addr */
#define FHH_ETH_MMBASE		FHH_ETH_p2v(_FHH_ETH_MMBASE)


/* Types of GUI */
#ifndef __ASSEMBLY__
extern unsigned long GUI_type;
#endif

#define FHH_GUI_ERROR		0xFFFFFFFF
#define FHH_GUI_NONE		0x0000000F
#define FHH_GUI_TYPE_0		0
#define FHH_GUI_TYPE_1		1
#define FHH_GUI_TYPE_2		2

