/*
 * linux/include/asm-arm/arch-iop310/irqs.h
 *
 * Author:	Nicolas Pitre
 * Copyright:	(C) 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 06/13/01: Added 80310 on-chip interrupt sources <dsaxena@mvista.com>
 *
 */
#include <linux/config.h>

/*
 * XS80200 specific IRQs
 */
#define IRQ_XS80200_BCU		0	/* Bus Control Unit */
#define IRQ_XS80200_PMU		1	/* Performance Monitoring Unit */
#define IRQ_XS80200_EXTIRQ	2	/* external IRQ signal */
#define IRQ_XS80200_EXTFIQ	3	/* external IRQ signal */

#define NR_XS80200_IRQS		4

#define XSCALE_PMU_IRQ		IRQ_XS80200_PMU

/*
 * IOP80310 chipset interrupts
 */
#define IOP310_IRQ_OFS		NR_XS80200_IRQS
#define IOP310_IRQ(x)		(IOP310_IRQ_OFS + (x))

/*
 * On FIQ1ISR register
 */
#define IRQ_IOP310_DMA0		IOP310_IRQ(0)	/* DMA Channel 0 */
#define IRQ_IOP310_DMA1		IOP310_IRQ(1)	/* DMA Channel 1 */
#define IRQ_IOP310_DMA2		IOP310_IRQ(2)	/* DMA Channel 2 */
#define IRQ_IOP310_PMON		IOP310_IRQ(3)	/* Bus performance Unit */
#define IRQ_IOP310_AAU		IOP310_IRQ(4)	/* Application Accelator Unit */

/*
 * On FIQ2ISR register
 */
#define IRQ_IOP310_I2C		IOP310_IRQ(5)	/* I2C unit */
#define IRQ_IOP310_MU		IOP310_IRQ(6)	/* messaging unit */

#define NR_IOP310_IRQS		(IOP310_IRQ(6) + 1)

#define NR_IRQS			NR_IOP310_IRQS


/*
 * Interrupts available on the Cyclone IQ80310 board
 */
#ifdef CONFIG_ARCH_IQ80310

#define IQ80310_IRQ_OFS		NR_IOP310_IRQS
#define IQ80310_IRQ(y)		((IQ80310_IRQ_OFS) + (y))

#define IRQ_IQ80310_TIMER	IQ80310_IRQ(0)	/* Timer Interrupt */
#define IRQ_IQ80310_I82559	IQ80310_IRQ(1)	/* I82559 Ethernet Interrupt */
#define IRQ_IQ80310_UART1	IQ80310_IRQ(2)	/* UART1 Interrupt */
#define IRQ_IQ80310_UART2	IQ80310_IRQ(3)	/* UART2 Interrupt */
#define IRQ_IQ80310_INTD	IQ80310_IRQ(4)	/* PCI INTD */


/*
 * ONLY AVAILABLE ON REV F OR NEWER BOARDS!
 */
#define	IRQ_IQ80310_INTA	IQ80310_IRQ(5)	/* PCI INTA */
#define	IRQ_IQ80310_INTB	IQ80310_IRQ(6)	/* PCI INTB */
#define	IRQ_IQ80310_INTC	IQ80310_IRQ(7)	/* PCI INTC */

#undef	NR_IRQS
#define NR_IRQS			(IQ80310_IRQ(7) + 1)

#endif // CONFIG_ARCH_IQ80310

