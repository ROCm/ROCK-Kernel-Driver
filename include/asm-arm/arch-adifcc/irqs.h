/*
 * linux/include/asm-arm/arch-80200fcc/irqs.h
 *
 * Author:	Deepak Saxena <dsaxena@mvista.com>
 * Copyright:	(C) 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define IRQ_XS80200_BCU		0	/* Bus Control Unit */
#define IRQ_XS80200_PMU		1	/* Performance Monitoring Unit */
#define IRQ_XS80200_EXTIRQ	2	/* external IRQ signal */
#define IRQ_XS80200_EXTFIQ	3	/* external IRQ signal */

#define NR_XS80200_IRQS		4
#define NR_IRQS			NR_XS80200_IRQS

#define	IRQ_XSCALE_PMU		IRQ_XS80200_PMU
