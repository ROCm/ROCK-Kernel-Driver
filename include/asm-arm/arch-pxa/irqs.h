/*
 *  linux/include/asm-arm/arch-pxa/irqs.h
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define PXA_IRQ_SKIP	8	/* The first 8 IRQs are reserved */
#define PXA_IRQ(x)		((x) - PXA_IRQ_SKIP)

#define	IRQ_GPIO0	PXA_IRQ(8)	/* GPIO0 Edge Detect */
#define	IRQ_GPIO1	PXA_IRQ(9)	/* GPIO1 Edge Detect */
#define	IRQ_GPIO_2_80	PXA_IRQ(10)	/* GPIO[2-80] Edge Detect */
#define	IRQ_USB		PXA_IRQ(11)	/* USB Service */
#define	IRQ_PMU		PXA_IRQ(12)	/* Performance Monitoring Unit */
#define	IRQ_I2S		PXA_IRQ(13)	/* I2S Interrupt */
#define	IRQ_AC97	PXA_IRQ(14)	/* AC97 Interrupt */
#define	IRQ_LCD		PXA_IRQ(17)	/* LCD Controller Service Request */
#define	IRQ_I2C		PXA_IRQ(18)	/* I2C Service Request */
#define	IRQ_ICP		PXA_IRQ(19)	/* ICP Transmit/Receive/Error */
#define	IRQ_STUART	PXA_IRQ(20)	/* STUART Transmit/Receive/Error */
#define	IRQ_BTUART	PXA_IRQ(21)	/* BTUART Transmit/Receive/Error */
#define	IRQ_FFUART	PXA_IRQ(22)	/* FFUART Transmit/Receive/Error*/
#define	IRQ_MMC		PXA_IRQ(23)	/* MMC Status/Error Detection */
#define	IRQ_SSP		PXA_IRQ(24)	/* SSP Service Request */
#define	IRQ_DMA 	PXA_IRQ(25)	/* DMA Channel Service Request */
#define	IRQ_OST0 	PXA_IRQ(26)	/* OS Timer match 0 */
#define	IRQ_OST1 	PXA_IRQ(27)	/* OS Timer match 1 */
#define	IRQ_OST2 	PXA_IRQ(28)	/* OS Timer match 2 */
#define	IRQ_OST3 	PXA_IRQ(29)	/* OS Timer match 3 */
#define	IRQ_RTC1Hz	PXA_IRQ(30)	/* RTC HZ Clock Tick */
#define	IRQ_RTCAlrm	PXA_IRQ(31)	/* RTC Alarm */

#define GPIO_2_80_TO_IRQ(x)	\
			PXA_IRQ((x) - 2 + 32)
#define IRQ_GPIO(x)	(((x) < 2) ? (IRQ_GPIO0 + (x)) : GPIO_2_80_TO_IRQ(x))

#define IRQ_TO_GPIO_2_80(i)	\
			((i) - PXA_IRQ(32) + 2)
#define IRQ_TO_GPIO(i)	((i) - (((i) > IRQ_GPIO1) ? IRQ_GPIO(2) : IRQ_GPIO(0)))

#define	NR_IRQS		(IRQ_GPIO(80) + 1)

#if defined(CONFIG_SA1111)

#define IRQ_SA1111_START	(IRQ_GPIO(80) + 1)
#define SA1111_IRQ(x)		(IRQ_SA1111_START + (x))

#define IRQ_GPAIN0		SA1111_IRQ(0)
#define IRQ_GPAIN1		SA1111_IRQ(1)
#define IRQ_GPAIN2		SA1111_IRQ(2)
#define IRQ_GPAIN3		SA1111_IRQ(3)
#define IRQ_GPBIN0		SA1111_IRQ(4)
#define IRQ_GPBIN1		SA1111_IRQ(5)
#define IRQ_GPBIN2		SA1111_IRQ(6)
#define IRQ_GPBIN3		SA1111_IRQ(7)
#define IRQ_GPBIN4		SA1111_IRQ(8)
#define IRQ_GPBIN5		SA1111_IRQ(9)
#define IRQ_GPCIN0		SA1111_IRQ(10)
#define IRQ_GPCIN1		SA1111_IRQ(11)
#define IRQ_GPCIN2		SA1111_IRQ(12)
#define IRQ_GPCIN3		SA1111_IRQ(13)
#define IRQ_GPCIN4		SA1111_IRQ(14)
#define IRQ_GPCIN5		SA1111_IRQ(15)
#define IRQ_GPCIN6		SA1111_IRQ(16)
#define IRQ_GPCIN7		SA1111_IRQ(17)
#define IRQ_MSTXINT		SA1111_IRQ(18)
#define IRQ_MSRXINT		SA1111_IRQ(19)
#define IRQ_MSSTOPERRINT	SA1111_IRQ(20)
#define IRQ_TPTXINT		SA1111_IRQ(21)
#define IRQ_TPRXINT		SA1111_IRQ(22)
#define IRQ_TPSTOPERRINT	SA1111_IRQ(23)
#define SSPXMTINT	SA1111_IRQ(24)
#define SSPRCVINT	SA1111_IRQ(25)
#define SSPROR		SA1111_IRQ(26)
#define AUDXMTDMADONEA	SA1111_IRQ(32)
#define AUDRCVDMADONEA	SA1111_IRQ(33)
#define AUDXMTDMADONEB	SA1111_IRQ(34)
#define AUDRCVDMADONEB	SA1111_IRQ(35)
#define AUDTFSR		SA1111_IRQ(36)
#define AUDRFSR		SA1111_IRQ(37)
#define AUDTUR		SA1111_IRQ(38)
#define AUDROR		SA1111_IRQ(39)
#define AUDDTS		SA1111_IRQ(40)
#define AUDRDD		SA1111_IRQ(41)
#define AUDSTO		SA1111_IRQ(42)
#define USBPWR		SA1111_IRQ(43)
#define NIRQHCIM	SA1111_IRQ(44)
#define HCIBUFFACC	SA1111_IRQ(45)
#define HCIRMTWKP	SA1111_IRQ(46)
#define NHCIMFCIR	SA1111_IRQ(47)
#define PORT_RESUME	SA1111_IRQ(48)
#define S0_READY_NINT	SA1111_IRQ(49)
#define S1_READY_NINT	SA1111_IRQ(50)
#define S0_CD_VALID	SA1111_IRQ(51)
#define S1_CD_VALID	SA1111_IRQ(52)
#define S0_BVD1_STSCHG	SA1111_IRQ(53)
#define S1_BVD1_STSCHG	SA1111_IRQ(54)

#define SA1111_IRQ_MAX	SA1111_IRQ(54)

#undef NR_IRQS
#define NR_IRQS		(SA1111_IRQ_MAX + 1)

#endif	// defined(CONFIG_SA1111)

#if defined(CONFIG_ARCH_LUBBOCK) || defined(CONFIG_ARCH_PXA_IDP)
#if CONFIG_SA1111
#define LUBBOCK_IRQ(x)	(SA1111_IRQ_MAX + 1 + (x))
#else
#define LUBBOCK_IRQ(x)	(IRQ_GPIO(80) + 1 + (x))
#endif

#define LUBBOCK_SD_IRQ		LUBBOCK_IRQ(0)
#define LUBBOCK_SA1111_IRQ	LUBBOCK_IRQ(1)
#define LUBBOCK_USB_IRQ		LUBBOCK_IRQ(2)
#define LUBBOCK_ETH_IRQ		LUBBOCK_IRQ(3)
#define LUBBOCK_UCB1400_IRQ	LUBBOCK_IRQ(4)
#define LUBBOCK_BB_IRQ		LUBBOCK_IRQ(5)

#undef NR_IRQS
#define NR_IRQS		(LUBBOCK_IRQ(5) + 1)

#endif	// CONFIG_ARCH_LUBBOCK



