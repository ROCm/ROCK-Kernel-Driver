/*
 *  linux/include/asm-arm/arch-omap/omap-perseus2.h
 *
 *  Copyright 2003 by Texas Instruments Incorporated
 *    OMAP730 / P2-sample additions
 *    Author: Jean Pihet
 *
 * Copyright (C) 2001 RidgeRun, Inc. (http://www.ridgerun.com)
 * Author: RidgeRun, Inc.
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef __ASM_ARCH_OMAP_P2SAMPLE_H
#define __ASM_ARCH_OMAP_P2SAMPLE_H

#if defined(CONFIG_ARCH_OMAP730) && defined (CONFIG_MACH_OMAP_PERSEUS2)

/*
 * NOTE:  ALL DEFINITIONS IN THIS FILE NEED TO BE PREFIXED BY IDENTIFIER
 *	  P2SAMPLE_ since they are specific to the EVM and not the chip.
 */

/* ---------------------------------------------------------------------------
 *  OMAP730 Debug Board FPGA
 * ---------------------------------------------------------------------------
 *
 */

/* maps in the FPGA registers and the ETHR registers */
#define OMAP730_FPGA_BASE		0xE8000000	/* VA */
#define OMAP730_FPGA_SIZE		SZ_4K		/* SIZE */
#define OMAP730_FPGA_START		0x04000000	/* PA */

#define OMAP730_FPGA_ETHR_START		OMAP730_FPGA_START
#define OMAP730_FPGA_ETHR_BASE		OMAP730_FPGA_BASE
#define OMAP730_FPGA_FPGA_REV		(OMAP730_FPGA_BASE + 0x10)	/* FPGA Revision */
#define OMAP730_FPGA_BOARD_REV		(OMAP730_FPGA_BASE + 0x12)	/* Board Revision */
#define OMAP730_FPGA_GPIO		(OMAP730_FPGA_BASE + 0x14)	/* GPIO outputs */
#define OMAP730_FPGA_LEDS		(OMAP730_FPGA_BASE + 0x16)	/* LEDs outputs */
#define OMAP730_FPGA_MISC_INPUTS	(OMAP730_FPGA_BASE + 0x18)	/* Misc inputs */
#define OMAP730_FPGA_LAN_STATUS		(OMAP730_FPGA_BASE + 0x1A)	/* LAN Status line */
#define OMAP730_FPGA_LAN_RESET		(OMAP730_FPGA_BASE + 0x1C)	/* LAN Reset line */

// LEDs definition on debug board (16 LEDs)
#define OMAP730_FPGA_LED_CLAIMRELEASE	(1 << 15)
#define OMAP730_FPGA_LED_STARTSTOP	(1 << 14)
#define OMAP730_FPGA_LED_HALTED		(1 << 13)
#define OMAP730_FPGA_LED_IDLE		(1 << 12)
#define OMAP730_FPGA_LED_TIMER		(1 << 11)
// cpu0 load-meter LEDs
#define OMAP730_FPGA_LOAD_METER		(1 << 0)	// A bit of fun on our board ...
#define OMAP730_FPGA_LOAD_METER_SIZE	11
#define OMAP730_FPGA_LOAD_METER_MASK	((1 << OMAP730_FPGA_LOAD_METER_SIZE) - 1)

#ifndef OMAP_SDRAM_DEVICE
#define OMAP_SDRAM_DEVICE		D256M_1X16_4B
#endif


/*
 * These definitions define an area of FLASH set aside
 * for the use of MTD/JFFS2. This is the area of flash
 * that a JFFS2 filesystem will reside which is mounted
 * at boot with the "root=/dev/mtdblock/0 rw"
 * command line option.
 */

/* Intel flash_0, partitioned as expected by rrload */
#define OMAP_FLASH_0_BASE	0xD8000000	/* VA */
#define OMAP_FLASH_0_START	0x00000000	/* PA */
#define OMAP_FLASH_0_SIZE	SZ_32M

/* 2.9.6 Traffic Controller Memory Interface Registers */
#define OMAP_FLASH_CFG_0		0xfffecc10
#define OMAP_FLASH_ACFG_0		0xfffecc50

#define OMAP_FLASH_CFG_1		0xfffecc14
#define OMAP_FLASH_ACFG_1		0xfffecc54

/*
 * Configuration Registers
 */
#define PERSEUS2_CONFIG_BASE	   0xfffe1000
#define PERSEUS2_IO_CONF_0	   0xfffe1070
#define PERSEUS2_IO_CONF_1	   0xfffe1074
#define PERSEUS2_IO_CONF_2	   0xfffe1078
#define PERSEUS2_IO_CONF_3	   0xfffe107c
#define PERSEUS2_IO_CONF_4	   0xfffe1080
#define PERSEUS2_IO_CONF_5	   0xfffe1084
#define PERSEUS2_IO_CONF_6	   0xfffe1088
#define PERSEUS2_IO_CONF_7	   0xfffe108c
#define PERSEUS2_IO_CONF_8	   0xfffe1090
#define PERSEUS2_IO_CONF_9	   0xfffe1094
#define PERSEUS2_IO_CONF_10	   0xfffe1098
#define PERSEUS2_IO_CONF_11	   0xfffe109c
#define PERSEUS2_IO_CONF_12	   0xfffe10a0
#define PERSEUS2_IO_CONF_13	   0xfffe10a4

#define PERSEUS2_MODE_1		   0xfffe1010
#define PERSEUS2_MODE_2		   0xfffe1014

/* CSMI specials: in terms of base + offset */
#define PERSEUS2_MODE2_OFFSET	   0x14

/* DSP control: ICR registers */
#define ICR_BASE		0xfffbb800
/* M_CTL */
#define DSP_M_CTL		((volatile __u16 *)0xfffbb804)
/* DSP control: MMU registers */
#define DSP_MMU_BASE		((volatile __u16 *)0xfffed200)

/* The Ethernet Controller IRQ is cascaded to MPU_EXT_nIRQ througb the FPGA */
#define INT_ETHER		INT_730_MPU_EXT_NIRQ

#define MAXIRQNUM		IH_BOARD_BASE
#define MAXFIQNUM		MAXIRQNUM
#define MAXSWINUM		MAXIRQNUM

#define NR_IRQS			(MAXIRQNUM + 1)

#ifndef __ASSEMBLY__
void fpga_write(unsigned char val, int reg);
unsigned char fpga_read(int reg);
#endif

/* PCC_UPLD control register: OMAP730 */
#define PCC_UPLD_CTRL_REG_BASE	(0xfffe0900)
#define PCC_UPLD_CTRL_REG	(volatile __u16 *)(PCC_UPLD_CTRL_REG_BASE + 0x00)

#else
#error "Only OMAP730 Perseus2 supported!"
#endif

#endif
