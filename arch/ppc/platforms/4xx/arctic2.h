/*
 * arch/ppc/platforms/arctic2.h   Platform definitions for the IBM Arctic-II
 *				based on beech.h by Bishop Brock
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Copyright (C) 2002, International Business Machines Corporation
 * All Rights Reserved.
 *
 * Ken Inoue 
 * IBM Thomas J. Watson Research Center
 * keninoue@us.ibm.com
 *
 * David Gibson
 * IBM Ozlabs, Canberra, Australia
 * arctic@gibson.dropbear.id.au
 * 
 */

#ifdef __KERNEL__
#ifndef __ASM_ARCTIC2_H__
#define __ASM_ARCTIC2_H__

#include <platforms/4xx/subzero.h>

#ifndef __ASSEMBLY__

/* Physical address for the 8-bit peripheral bank */
#define ARCTIC2_FPGA8_PADDR	(0xf8000000)

/* Physical address for the 16-bit peripheral bank */
#define ARCTIC2_FPGA16_PADDR	(0xf9000000)

/* Virtual address of the FPGA control registers */
extern volatile u8 *arctic2_fpga_regs;

#define ARCTIC2_FPGA_REGS_EXTENT	(0xf)
#define ARCTIC2_FPGA_POWERDOWN		(arctic2_fpga_regs + 0x0)
#define ARCTIC2_FPGA_BUTTONS		(arctic2_fpga_regs + 0x1)
#define ARCTIC2_FPGA_MULTIWAY		(arctic2_fpga_regs + 0x2)
#define ARCTIC2_FPGA_IRQ_ENABLE		(arctic2_fpga_regs + 0x3)
#define ARCTIC2_FPGA_PCCF_POWER		(arctic2_fpga_regs + 0x4)
#define ARCTIC2_FPGA_JACKET		(arctic2_fpga_regs + 0x5)
#define ARCTIC2_FPGA_SDIO_CTRL		(arctic2_fpga_regs + 0x6)
#define ARCTIC2_FPGA_USB_CTRL		(arctic2_fpga_regs + 0x7)
#define ARCTIC2_FPGA_MDOC_CTRL		(arctic2_fpga_regs + 0x8)
#define ARCTIC2_FPGA_CHARGER		(arctic2_fpga_regs + 0x9)
#define ARCTIC2_FPGA_CRYO		(arctic2_fpga_regs + 0xa)
#define ARCTIC2_FPGA_LED_DATA_HI        (arctic2_fpga_regs + 0xc)
#define ARCTIC2_FPGA_LED_DATA_LOW       (arctic2_fpga_regs + 0xd)
#define ARCTIC2_FPGA_LED_ADDR           (arctic2_fpga_regs + 0xe)
#define ARCTIC2_FPGA_LED_CTRL           (arctic2_fpga_regs + 0xf)

#define ARCTIC2_FPGA_BTN_PWR		0x20
#define ARCTIC2_FPGA_BTN_MIC		0x10

#define ARCTIC2_FPGA_MULTIWAY_PUSH	0x01
#define ARCTIC2_FPGA_MULTIWAY_NE	0x02
#define ARCTIC2_FPGA_MULTIWAY_SE	0x04
#define ARCTIC2_FPGA_MULTIWAY_SW	0x08
#define ARCTIC2_FPGA_MULTIWAY_NW	0x10

#define ARCTIC2_FPGA_MULTIWAY_N		0x12
#define ARCTIC2_FPGA_MULTIWAY_E		0x06
#define ARCTIC2_FPGA_MULTIWAY_S		0x0c
#define ARCTIC2_FPGA_MULTIWAY_W		0x18

#define ARCTIC2_FPGA_IRQ_PWR		0x10
#define ARCTIC2_FPGA_IRQ_TCPA		0x08
#define ARCTIC2_FPGA_IRQ_JACKET		0x04
#define ARCTIC2_FPGA_IRQ_MIC		0x02
#define ARCTIC2_FPGA_IRQ_BTN		0x01

#define ARCTIC2_FPGA_PCCF_POWER_5V	0x01

/* Arctic II uses the internal clock for UART. Note that the OPB
   frequency must be more than 2x the UART clock frequency. At OPB
   frequencies less than this the serial port will not function due to
   the way that SerClk is sampled.  We use 11.1111MHz as the frequency
   because it can be generated from a wide range of OPB frequencies we
   want to use. */

#define PPC4xx_SERCLK_FREQ 11111111

#define BASE_BAUD (PPC4xx_SERCLK_FREQ / 16)

#define RTC_DVBITS	RTC_DVBITS_33KHZ	/* 33kHz RTC */

#define PPC4xx_MACHINE_NAME	"IBM Arctic II"

#include <asm/pccf_4xx.h>
#define _IO_BASE		(pccf_4xx_io_base)
#define _ISA_MEM_BASE		(pccf_4xx_mem_base)

void arctic2_poweroff(void) __attribute__ ((noreturn));
void arctic2_set_lcdpower(int on);

extern int arctic2_supports_apm;
extern int arctic2_supports_dvs;

/*****************************************************************************
 * Serial port definitions
 *****************************************************************************/

/*
 * Arctic UART1 is touchscreen handled by separate driver, not included in
 * standard serial defines.
 */

#define UART0_INT	UIC_IRQ_U0
#define UART1_INT	UIC_IRQ_U1
#define UART0_IO_BASE	0xEF600300
#define UART1_IO_BASE	0xEF600400

#define RS_TABLE_SIZE	2

#define STD_UART_OP(num)					\
	{ 0, BASE_BAUD, 0, UART##num##_INT,			\
		(ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST),	\
		iomem_base:(u8 *) UART##num##_IO_BASE,		\
		io_type: SERIAL_IO_MEM},

#define SERIAL_DEBUG_IO_BASE    UART0_IO_BASE
#define SERIAL_PORT_DFNS        \
        STD_UART_OP(0)

/* PM Button support */

#ifdef CONFIG_405LP_PM_BUTTON
#define IBM405LP_PM_IRQ      APM0_IRQ_WUI0
#define IBM405LP_PM_POLARITY 1
#endif

#endif /* !__ASSEMBLY__ */
#endif /* __ASM_ARCTIC2_H__ */
#endif /* __KERNEL__ */
