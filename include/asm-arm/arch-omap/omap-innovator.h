/*
 * linux/include/asm-arm/arch-omap/omap-innovator.h
 *
 * Copyright (C) 2001 RidgeRun, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef __ASM_ARCH_OMAP_INNOVATOR_H
#define __ASM_ARCH_OMAP_INNOVATOR_H

#if defined (CONFIG_ARCH_OMAP1510)

/*
 * ---------------------------------------------------------------------------
 *  OMAP-1510 FPGA
 * ---------------------------------------------------------------------------
 */
#define OMAP1510P1_FPGA_BASE			0xE8000000	/* Virtual */
#define OMAP1510P1_FPGA_SIZE			SZ_4K
#define OMAP1510P1_FPGA_START			0x08000000	/* Physical */

/* Revision */
#define OMAP1510P1_FPGA_REV_LOW			(OMAP1510P1_FPGA_BASE + 0x0)
#define OMAP1510P1_FPGA_REV_HIGH		(OMAP1510P1_FPGA_BASE + 0x1)

#define OMAP1510P1_FPGA_LCD_PANEL_CONTROL	(OMAP1510P1_FPGA_BASE + 0x2)
#define OMAP1510P1_FPGA_LED_DIGIT		(OMAP1510P1_FPGA_BASE + 0x3)
#define INNOVATOR_FPGA_HID_SPI			(OMAP1510P1_FPGA_BASE + 0x4)
#define OMAP1510P1_FPGA_POWER			(OMAP1510P1_FPGA_BASE + 0x5)

/* Interrupt status */
#define OMAP1510P1_FPGA_ISR_LO			(OMAP1510P1_FPGA_BASE + 0x6)
#define OMAP1510P1_FPGA_ISR_HI			(OMAP1510P1_FPGA_BASE + 0x7)

/* Interrupt mask */
#define OMAP1510P1_FPGA_IMR_LO			(OMAP1510P1_FPGA_BASE + 0x8)
#define OMAP1510P1_FPGA_IMR_HI			(OMAP1510P1_FPGA_BASE + 0x9)

/* Reset registers */
#define OMAP1510P1_FPGA_HOST_RESET		(OMAP1510P1_FPGA_BASE + 0xa)
#define OMAP1510P1_FPGA_RST			(OMAP1510P1_FPGA_BASE + 0xb)

#define OMAP1510P1_FPGA_AUDIO			(OMAP1510P1_FPGA_BASE + 0xc)
#define OMAP1510P1_FPGA_DIP			(OMAP1510P1_FPGA_BASE + 0xe)
#define OMAP1510P1_FPGA_FPGA_IO			(OMAP1510P1_FPGA_BASE + 0xf)
#define OMAP1510P1_FPGA_UART1			(OMAP1510P1_FPGA_BASE + 0x14)
#define OMAP1510P1_FPGA_UART2			(OMAP1510P1_FPGA_BASE + 0x15)
#define OMAP1510P1_FPGA_OMAP1510_STATUS		(OMAP1510P1_FPGA_BASE + 0x16)
#define OMAP1510P1_FPGA_BOARD_REV		(OMAP1510P1_FPGA_BASE + 0x18)
#define OMAP1510P1_PPT_DATA			(OMAP1510P1_FPGA_BASE + 0x100)
#define OMAP1510P1_PPT_STATUS			(OMAP1510P1_FPGA_BASE + 0x101)
#define OMAP1510P1_PPT_CONTROL			(OMAP1510P1_FPGA_BASE + 0x102)

#define OMAP1510P1_FPGA_TOUCHSCREEN		(OMAP1510P1_FPGA_BASE + 0x204)

#define INNOVATOR_FPGA_INFO			(OMAP1510P1_FPGA_BASE + 0x205)
#define INNOVATOR_FPGA_LCD_BRIGHT_LO		(OMAP1510P1_FPGA_BASE + 0x206)
#define INNOVATOR_FPGA_LCD_BRIGHT_HI		(OMAP1510P1_FPGA_BASE + 0x207)
#define INNOVATOR_FPGA_LED_GRN_LO		(OMAP1510P1_FPGA_BASE + 0x208)
#define INNOVATOR_FPGA_LED_GRN_HI		(OMAP1510P1_FPGA_BASE + 0x209)
#define INNOVATOR_FPGA_LED_RED_LO		(OMAP1510P1_FPGA_BASE + 0x20a)
#define INNOVATOR_FPGA_LED_RED_HI		(OMAP1510P1_FPGA_BASE + 0x20b)
#define INNOVATOR_FPGA_CAM_USB_CONTROL		(OMAP1510P1_FPGA_BASE + 0x20c)
#define INNOVATOR_FPGA_EXP_CONTROL		(OMAP1510P1_FPGA_BASE + 0x20d)
#define INNOVATOR_FPGA_ISR2			(OMAP1510P1_FPGA_BASE + 0x20e)
#define INNOVATOR_FPGA_IMR2			(OMAP1510P1_FPGA_BASE + 0x210)

#define OMAP1510P1_FPGA_ETHR_START		(OMAP1510P1_FPGA_START + 0x300)
#define OMAP1510P1_FPGA_ETHR_BASE		(OMAP1510P1_FPGA_BASE + 0x300)

/*
 * Power up Giga UART driver, turn on HID clock.
 * Turn off BT power, since we're not using it and it
 * draws power.
 */
#define OMAP1510P1_FPGA_RESET_VALUE		0x42

#define OMAP1510P1_FPGA_PCR_IF_PD0		(1 << 7)
#define OMAP1510P1_FPGA_PCR_COM2_EN		(1 << 6)
#define OMAP1510P1_FPGA_PCR_COM1_EN		(1 << 5)
#define OMAP1510P1_FPGA_PCR_EXP_PD0		(1 << 4)
#define OMAP1510P1_FPGA_PCR_EXP_PD1		(1 << 3)
#define OMAP1510P1_FPGA_PCR_48MHZ_CLK		(1 << 2)
#define OMAP1510P1_FPGA_PCR_4MHZ_CLK		(1 << 1)
#define OMAP1510P1_FPGA_PCR_RSRVD_BIT0		(1 << 0)

/*
 * Innovator/OMAP1510 FPGA HID register bit definitions
 */
#define FPGA_HID_SCLK	(1<<0)	/* output */
#define FPGA_HID_MOSI	(1<<1)	/* output */
#define FPGA_HID_nSS	(1<<2)	/* output 0/1 chip idle/select */
#define FPGA_HID_nHSUS	(1<<3)	/* output 0/1 host active/suspended */
#define FPGA_HID_MISO	(1<<4)	/* input */
#define FPGA_HID_ATN	(1<<5)	/* input  0/1 chip idle/ATN */
#define FPGA_HID_rsrvd	(1<<6)
#define FPGA_HID_RESETn (1<<7)	/* output - 0/1 USAR reset/run */

#ifndef OMAP_SDRAM_DEVICE
#define OMAP_SDRAM_DEVICE			D256M_1X16_4B
#endif

#define OMAP1510P1_IMIF_PRI_VALUE		0x00
#define OMAP1510P1_EMIFS_PRI_VALUE		0x00
#define OMAP1510P1_EMIFF_PRI_VALUE		0x00

/*
 * These definitions define an area of FLASH set aside
 * for the use of MTD/JFFS2. This is the area of flash
 * that a JFFS2 filesystem will reside which is mounted
 * at boot with the "root=/dev/mtdblock/0 rw"
 * command line option. The flash address used here must
 * fall within the legal range defined by rrload for storing
 * the filesystem component. This address will be sufficiently
 * deep into the overall flash range to avoid the other
 * components also stored in flash such as the bootloader,
 * the bootloader params, and the kernel.
 * The SW2 settings for the map below are:
 * 1 off, 2 off, 3 on, 4 off.
 */

/* Intel flash_0, partitioned as expected by rrload */
#define OMAP_FLASH_0_BASE	0xD8000000
#define OMAP_FLASH_0_START	0x00000000
#define OMAP_FLASH_0_SIZE	SZ_16M

/* Intel flash_1, used for cramfs or other flash file systems */
#define OMAP_FLASH_1_BASE	0xD9000000
#define OMAP_FLASH_1_START	0x01000000
#define OMAP_FLASH_1_SIZE	SZ_16M

/* The FPGA IRQ is cascaded through GPIO_13 */
#define INT_FPGA		(IH_GPIO_BASE + 13)

/* IRQ Numbers for interrupts muxed through the FPGA */
#define IH_FPGA_BASE		IH_BOARD_BASE
#define INT_FPGA_ATN		(IH_FPGA_BASE + 0)
#define INT_FPGA_ACK		(IH_FPGA_BASE + 1)
#define INT_FPGA2		(IH_FPGA_BASE + 2)
#define INT_FPGA3		(IH_FPGA_BASE + 3)
#define INT_FPGA4		(IH_FPGA_BASE + 4)
#define INT_FPGA5		(IH_FPGA_BASE + 5)
#define INT_FPGA6		(IH_FPGA_BASE + 6)
#define INT_FPGA7		(IH_FPGA_BASE + 7)
#define INT_FPGA8		(IH_FPGA_BASE + 8)
#define INT_FPGA9		(IH_FPGA_BASE + 9)
#define INT_FPGA10		(IH_FPGA_BASE + 10)
#define INT_FPGA11		(IH_FPGA_BASE + 11)
#define INT_FPGA12		(IH_FPGA_BASE + 12)
#define INT_ETHER		(IH_FPGA_BASE + 13)
#define INT_FPGAUART1		(IH_FPGA_BASE + 14)
#define INT_FPGAUART2		(IH_FPGA_BASE + 15)
#define INT_FPGA_TS		(IH_FPGA_BASE + 16)
#define INT_FPGA17		(IH_FPGA_BASE + 17)
#define INT_FPGA_CAM		(IH_FPGA_BASE + 18)
#define INT_FPGA_RTC_A		(IH_FPGA_BASE + 19)
#define INT_FPGA_RTC_B		(IH_FPGA_BASE + 20)
#define INT_FPGA_CD		(IH_FPGA_BASE + 21)
#define INT_FPGA22		(IH_FPGA_BASE + 22)
#define INT_FPGA23		(IH_FPGA_BASE + 23)

#define NR_FPGA_IRQS		 24

#define MAXIRQNUM		(IH_FPGA_BASE + NR_FPGA_IRQS - 1)
#define MAXFIQNUM		MAXIRQNUM
#define MAXSWINUM		MAXIRQNUM

#define NR_IRQS			256

#ifndef __ASSEMBLY__
void fpga_write(unsigned char val, int reg);
unsigned char fpga_read(int reg);
#endif

#elif defined (CONFIG_ARCH_OMAP1610)

/* At OMAP1610 Innovator the Ethernet is directly connected to CS1 */
#define OMAP1610_ETHR_BASE		0xE8000000
#define OMAP1610_ETHR_SIZE		SZ_4K
#define OMAP1610_ETHR_START		0x04000000

/* Intel STRATA NOR flash at CS3 */
#define OMAP1610_NOR_FLASH_BASE		0xD8000000
#define OMAP1610_NOR_FLASH_SIZE		SZ_32M
#define OMAP1610_NOR_FLASH_START	0x0C000000

#define MAXIRQNUM			(IH_BOARD_BASE)
#define MAXFIQNUM			MAXIRQNUM
#define MAXSWINUM			MAXIRQNUM

#define NR_IRQS				(MAXIRQNUM + 1)

#else
#error "Only OMAP1510 and OMAP1610 Innovator supported!"
#endif
#endif
