/*
 * linux/include/asm-arm/arch-omap/serial.h
 *
 * BRIEF MODULE DESCRIPTION
 * serial definitions
 *
 */

#ifndef __ASM_ARCH_SERIAL_H
#define __ASM_ARCH_SERIAL_H


#define OMAP1510_UART1_BASE	(unsigned char *)0xfffb0000
#define OMAP1510_UART2_BASE	(unsigned char *)0xfffb0800
#define OMAP1510_UART3_BASE	(unsigned char *)0xfffb9800

#define OMAP730_UART1_BASE	(unsigned char *)0xfffb0000
#define OMAP730_UART2_BASE	(unsigned char *)0xfffb0800

#if defined(CONFIG_ARCH_OMAP1510) || defined(CONFIG_ARCH_OMAP1610)
#define OMAP_SERIAL_REG_SHIFT 2
#else
#define OMAP_SERIAL_REG_SHIFT 0
#endif


#ifndef __ASSEMBLY__

#include <asm/arch/hardware.h>
#include <asm/irq.h>


/* UART3 Registers Maping through MPU bus */
#define OMAP_MPU_UART3_BASE	0xFFFB9800	/* UART3 through MPU bus */
#define UART3_RHR		(OMAP_MPU_UART3_BASE + 0)
#define UART3_THR		(OMAP_MPU_UART3_BASE + 0)
#define UART3_DLL		(OMAP_MPU_UART3_BASE + 0)
#define UART3_IER		(OMAP_MPU_UART3_BASE + 4)
#define UART3_DLH		(OMAP_MPU_UART3_BASE + 4)
#define UART3_IIR		(OMAP_MPU_UART3_BASE + 8)
#define UART3_FCR		(OMAP_MPU_UART3_BASE + 8)
#define UART3_EFR		(OMAP_MPU_UART3_BASE + 8)
#define UART3_LCR		(OMAP_MPU_UART3_BASE + 0x0C)
#define UART3_MCR		(OMAP_MPU_UART3_BASE + 0x10)
#define UART3_XON1_ADDR1	(OMAP_MPU_UART3_BASE + 0x10)
#define UART3_XON2_ADDR2	(OMAP_MPU_UART3_BASE + 0x14)
#define UART3_LSR		(OMAP_MPU_UART3_BASE + 0x14)
#define UART3_TCR		(OMAP_MPU_UART3_BASE + 0x18)
#define UART3_MSR		(OMAP_MPU_UART3_BASE + 0x18)
#define UART3_XOFF1		(OMAP_MPU_UART3_BASE + 0x18)
#define UART3_XOFF2		(OMAP_MPU_UART3_BASE + 0x1C)
#define UART3_SPR		(OMAP_MPU_UART3_BASE + 0x1C)
#define UART3_TLR		(OMAP_MPU_UART3_BASE + 0x1C)
#define UART3_MDR1		(OMAP_MPU_UART3_BASE + 0x20)
#define UART3_MDR2		(OMAP_MPU_UART3_BASE + 0x24)
#define UART3_SFLSR		(OMAP_MPU_UART3_BASE + 0x28)
#define UART3_TXFLL		(OMAP_MPU_UART3_BASE + 0x28)
#define UART3_RESUME		(OMAP_MPU_UART3_BASE + 0x2C)
#define UART3_TXFLH		(OMAP_MPU_UART3_BASE + 0x2C)
#define UART3_SFREGL		(OMAP_MPU_UART3_BASE + 0x30)
#define UART3_RXFLL		(OMAP_MPU_UART3_BASE + 0x30)
#define UART3_SFREGH		(OMAP_MPU_UART3_BASE + 0x34)
#define UART3_RXFLH		(OMAP_MPU_UART3_BASE + 0x34)
#define UART3_BLR		(OMAP_MPU_UART3_BASE + 0x38)
#define UART3_ACREG		(OMAP_MPU_UART3_BASE + 0x3C)
#define UART3_DIV16		(OMAP_MPU_UART3_BASE + 0x3C)
#define UART3_SCR		(OMAP_MPU_UART3_BASE + 0x40)
#define UART3_SSR		(OMAP_MPU_UART3_BASE + 0x44)
#define UART3_EBLR		(OMAP_MPU_UART3_BASE + 0x48)
#define UART3_OSC_12M_SEL	(OMAP_MPU_UART3_BASE + 0x4C)
#define UART3_MVR		(OMAP_MPU_UART3_BASE + 0x50)

#ifdef CONFIG_ARCH_OMAP1510
#define BASE_BAUD (12000000/16)
#endif

#ifdef CONFIG_ARCH_OMAP1610
#define BASE_BAUD (48000000/16)
#endif

#ifdef CONFIG_ARCH_OMAP730
#define BASE_BAUD (48000000/16)

#define RS_TABLE_SIZE		2

#define STD_COM_FLAGS	(ASYNC_SKIP_TEST)

#define STD_SERIAL_PORT_DEFNS	\
	{	\
		.uart =			PORT_OMAP,		\
		.baud_base =		BASE_BAUD,		\
		.iomem_base =		OMAP730_UART1_BASE,	\
		.iomem_reg_shift =	0,			\
		.io_type =		SERIAL_IO_MEM,		\
		.irq =			INT_UART1,		\
		.flags =			STD_COM_FLAGS,		\
	}, {	\
		.uart =			PORT_OMAP,		\
		.baud_base =		BASE_BAUD,		\
		.iomem_base =		OMAP730_UART2_BASE,	\
		.iomem_reg_shift =	0,			\
		.io_type =		SERIAL_IO_MEM,		\
		.irq =			INT_UART2,		\
		.flags =			STD_COM_FLAGS,		\
	}

#else

#define RS_TABLE_SIZE	3

#define STD_COM_FLAGS	(ASYNC_SKIP_TEST)

#define STD_SERIAL_PORT_DEFNS	\
	{	\
		.uart =			PORT_OMAP,		\
		.baud_base =		BASE_BAUD,		\
		.iomem_base =		OMAP1510_UART1_BASE,	\
		.iomem_reg_shift =	2,			\
		.io_type =		SERIAL_IO_MEM,		\
		.irq =			INT_UART1,		\
		.flags =		STD_COM_FLAGS,		\
	}, {	\
		.uart =			PORT_OMAP,		\
		.baud_base =		BASE_BAUD,		\
		.iomem_base =		OMAP1510_UART2_BASE,	\
		.iomem_reg_shift =	2,			\
		.io_type =		SERIAL_IO_MEM,		\
		.irq =			INT_UART2,		\
		.flags =		STD_COM_FLAGS,		\
	}, {	\
		.uart =			PORT_OMAP,		\
		.baud_base =		BASE_BAUD,		\
		.iomem_base =		OMAP1510_UART3_BASE,	\
		.iomem_reg_shift =	2,			\
		.io_type =		SERIAL_IO_MEM,		\
		.irq =			INT_UART3,		\
		.flags =		STD_COM_FLAGS,		\
	}
#endif				/* CONFIG_ARCH_OMAP730 */

#define EXTRA_SERIAL_PORT_DEFNS

/* OMAP FCR trigger  redefinitions */
#define UART_FCR_R_TRIGGER_8	0x00	/* Mask for receive trigger set at 8 */
#define UART_FCR_R_TRIGGER_16	0x40	/* Mask for receive trigger set at 16 */
#define UART_FCR_R_TRIGGER_56	0x80	/* Mask for receive trigger set at 56 */
#define UART_FCR_R_TRIGGER_60	0xC0	/* Mask for receive trigger set at 60 */

/* There is an error in the description of the transmit trigger levels of
   OMAP5910 TRM from January 2003. The transmit trigger level 56 is not
   56 but 32, the transmit trigger level 60 is not 60 but 56!
   Additionally, the descritption of these trigger levels is
   a little bit unclear. The trigger level define the number of EMPTY
   entries in the FIFO. Thus, if TRIGGER_8 is used, an interrupt is requested
   if 8 FIFO entries are empty (and 56 entries are still filled [the FIFO
   size is 64]). Or: If TRIGGER_56 is selected, everytime there are less than
   8 characters in the FIFO, an interrrupt is spawned. In other words: The
   trigger number is equal the number of characters which can be
   written without FIFO overrun */

#define UART_FCR_T_TRIGGER_8	0x00	/* Mask for transmit trigger set at 8 */
#define UART_FCR_T_TRIGGER_16	0x10	/* Mask for transmit trigger set at 16 */
#define UART_FCR_T_TRIGGER_32	0x20	/* Mask for transmit trigger set at 32 */
#define UART_FCR_T_TRIGGER_56	0x30	/* Mask for transmit trigger set at 56 */

#endif	/* __ASSEMBLY__ */
#endif	/* __ASM_ARCH_SERIAL_H */
