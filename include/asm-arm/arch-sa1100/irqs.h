/*
 * linux/include/asm-arm/arch-sa1100/irqs.h
 *
 * Copyright (C) 1996 Russell King
 * Copyright (C) 1998 Deborah Wallach (updates for SA1100/Brutus).
 * Copyright (C) 1999 Nicolas Pitre (full GPIO irq isolation)
 */

#include <linux/config.h>

#define SA1100_IRQ(x)		(0 + (x))

#define	IRQ_GPIO0		SA1100_IRQ(0)
#define	IRQ_GPIO1		SA1100_IRQ(1)
#define	IRQ_GPIO2		SA1100_IRQ(2)
#define	IRQ_GPIO3		SA1100_IRQ(3)
#define	IRQ_GPIO4		SA1100_IRQ(4)
#define	IRQ_GPIO5		SA1100_IRQ(5)
#define	IRQ_GPIO6		SA1100_IRQ(6)
#define	IRQ_GPIO7		SA1100_IRQ(7)
#define	IRQ_GPIO8		SA1100_IRQ(8)
#define	IRQ_GPIO9		SA1100_IRQ(9)
#define	IRQ_GPIO10		SA1100_IRQ(10)
#define	IRQ_GPIO11_27		SA1100_IRQ(11)
#define	IRQ_LCD  		SA1100_IRQ(12)	/* LCD controller           */
#define	IRQ_Ser0UDC		SA1100_IRQ(13)	/* Ser. port 0 UDC          */
#define	IRQ_Ser1SDLC		SA1100_IRQ(14)	/* Ser. port 1 SDLC         */
#define	IRQ_Ser1UART		SA1100_IRQ(15)	/* Ser. port 1 UART         */
#define	IRQ_Ser2ICP		SA1100_IRQ(16)	/* Ser. port 2 ICP          */
#define	IRQ_Ser3UART		SA1100_IRQ(17)	/* Ser. port 3 UART         */
#define	IRQ_Ser4MCP		SA1100_IRQ(18)	/* Ser. port 4 MCP          */
#define	IRQ_Ser4SSP		SA1100_IRQ(19)	/* Ser. port 4 SSP          */
#define	IRQ_DMA0 		SA1100_IRQ(20)	/* DMA controller channel 0 */
#define	IRQ_DMA1 		SA1100_IRQ(21)	/* DMA controller channel 1 */
#define	IRQ_DMA2 		SA1100_IRQ(22)	/* DMA controller channel 2 */
#define	IRQ_DMA3 		SA1100_IRQ(23)	/* DMA controller channel 3 */
#define	IRQ_DMA4 		SA1100_IRQ(24)	/* DMA controller channel 4 */
#define	IRQ_DMA5 		SA1100_IRQ(25)	/* DMA controller channel 5 */
#define	IRQ_OST0 		SA1100_IRQ(26)	/* OS Timer match 0         */
#define	IRQ_OST1 		SA1100_IRQ(27)	/* OS Timer match 1         */
#define	IRQ_OST2 		SA1100_IRQ(28)	/* OS Timer match 2         */
#define	IRQ_OST3 		SA1100_IRQ(29)	/* OS Timer match 3         */
#define	IRQ_RTC1Hz		SA1100_IRQ(30)	/* RTC 1 Hz clock           */
#define	IRQ_RTCAlrm		SA1100_IRQ(31)	/* RTC Alarm                */

#define	IRQ_GPIO_11_27(x)	(32 + (x) - 11)

#define	IRQ_GPIO11		IRQ_GPIO_11_27(11)
#define	IRQ_GPIO12		IRQ_GPIO_11_27(12)
#define	IRQ_GPIO13		IRQ_GPIO_11_27(13)
#define	IRQ_GPIO14		IRQ_GPIO_11_27(14)
#define	IRQ_GPIO15		IRQ_GPIO_11_27(15)
#define	IRQ_GPIO16		IRQ_GPIO_11_27(16)
#define	IRQ_GPIO17		IRQ_GPIO_11_27(17)
#define	IRQ_GPIO18		IRQ_GPIO_11_27(18)
#define	IRQ_GPIO19		IRQ_GPIO_11_27(19)
#define	IRQ_GPIO20		IRQ_GPIO_11_27(20)
#define	IRQ_GPIO21		IRQ_GPIO_11_27(21)
#define	IRQ_GPIO22		IRQ_GPIO_11_27(22)
#define	IRQ_GPIO23		IRQ_GPIO_11_27(23)
#define	IRQ_GPIO24		IRQ_GPIO_11_27(24)
#define	IRQ_GPIO25		IRQ_GPIO_11_27(25)
#define	IRQ_GPIO26		IRQ_GPIO_11_27(26)
#define	IRQ_GPIO27		IRQ_GPIO_11_27(27)

#define SA1100_GPIO_TO_IRQ(i)	(((i) < 11) ? SA1100_IRQ(i) : IRQ_GPIO_11_27(i))

/* To get the GPIO number from an IRQ number */
#define GPIO_11_27_IRQ(i)	(11 + (i) - 32)
#define SA1100_IRQ_TO_GPIO(i) 	(((i) < 11) ? (i) : GPIO_11_27_IRQ(i))

#define	NR_IRQS		(IRQ_GPIO27 + 1)


#if defined(CONFIG_SA1100_GRAPHICSCLIENT) || defined(CONFIG_SA1100_THINCLIENT)
#define ADS_EXT_IRQ(x)	(IRQ_GPIO27 + 1 + (x))
#undef NR_IRQS
#define NR_IRQS		(ADS_EXT_IRQ(15) + 1)
#endif


#if defined(CONFIG_SA1111)

#define SA1111_IRQ(x)	(IRQ_GPIO27 + 1 + (x))

#define GPAIN0		SA1111_IRQ(0)
#define GPAIN1		SA1111_IRQ(1)
#define GPAIN2		SA1111_IRQ(2)
#define GPAIN3		SA1111_IRQ(3)
#define GPBIN0		SA1111_IRQ(4)
#define GPBIN1		SA1111_IRQ(5)
#define GPBIN2		SA1111_IRQ(6)
#define GPBIN3		SA1111_IRQ(7)
#define GPBIN4		SA1111_IRQ(8)
#define GPBIN5		SA1111_IRQ(9)
#define GPCIN0		SA1111_IRQ(10)
#define GPCIN1		SA1111_IRQ(11)
#define GPCIN2		SA1111_IRQ(12)
#define GPCIN3		SA1111_IRQ(13)
#define GPCIN4		SA1111_IRQ(14)
#define GPCIN5		SA1111_IRQ(15)
#define GPCIN6		SA1111_IRQ(16)
#define GPCIN7		SA1111_IRQ(17)
#define MSTXINT		SA1111_IRQ(18)
#define MSRXINT		SA1111_IRQ(19)
#define MSSTOPERRINT	SA1111_IRQ(20)
#define TPTXINT		SA1111_IRQ(21)
#define TPRXINT		SA1111_IRQ(22)
#define TPSTOPERRINT	SA1111_IRQ(23)
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
#define IRQHCIBUFFACC	SA1111_IRQ(45)
#define IRQHCIRMTWKP	SA1111_IRQ(46)
#define NHCIMFCIR	SA1111_IRQ(47)
#define USB_PORT_RESUME	SA1111_IRQ(48)
#define S0_READY_NINT	SA1111_IRQ(49)
#define S1_READY_NINT	SA1111_IRQ(50)
#define S0_CD_VALID	SA1111_IRQ(51)
#define S1_CD_VALID	SA1111_IRQ(52)
#define S0_BVD1_STSCHG	SA1111_IRQ(53)
#define S1_BVD1_STSCHG	SA1111_IRQ(54)

#define SA1111_IRQ_MAX	SA1111_IRQ(54)

#undef NR_IRQS
#define NR_IRQS		(SA1111_IRQ_MAX + 1)


#ifdef CONFIG_ASSABET_NEPONSET

#define MISC_IRQ0	SA1111_IRQ(55)
#define MISC_IRQ1	SA1111_IRQ(56)

#undef NR_IRQS
#define NR_IRQS		(SA1111_IRQ_MAX + 3)

#endif  /* CONFIG_ASSABET_NEPONSET */

#endif  /* CONFIG_SA1111 */
