/*
 * include/asm-v850/rte_nb85e_cb.h -- Midas labs RTE-V850/NB85E-CB board
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_RTE_NB85E_CB_H__
#define __V850_RTE_NB85E_CB_H__

#include <asm/rte_cb.h>		/* Common defs for Midas RTE-CB boards.  */

#define PLATFORM	"rte-v850e/nb85e-cb"
#define PLATFORM_LONG	"Midas lab RTE-V850E/NB85E-CB"

#define CPU_CLOCK_FREQ	50000000 /* 50MHz */

/* 1MB of onboard SRAM.  Note that the monitor ROM uses parts of this
   for its own purposes, so care must be taken.  */
#define SRAM_ADDR	0x03C00000
#define SRAM_SIZE	0x00100000 /* 1MB */

/* 16MB of onbard SDRAM.  */
#define SDRAM_ADDR	0x01000000
#define SDRAM_SIZE	0x01000000 /* 16MB */


#ifdef CONFIG_ROM_KERNEL
/* Kernel is in ROM, starting at address 0.  */

#define INTV_BASE	0

#else /* !CONFIG_ROM_KERNEL */
/* We're using the ROM monitor.  */

/* The chip's real interrupt vectors are in ROM, but they jump to a
   secondary interrupt vector table in RAM.  */
#define INTV_BASE	0x004F8000

/* Scratch memory used by the ROM monitor, which shouldn't be used by
   linux (except for the alternate interrupt vector area, defined
   above).  */
#define MON_SCRATCH_ADDR	0x03CE8000
#define MON_SCRATCH_SIZE	0x00008000 /* 32KB */

#endif /* CONFIG_ROM_KERNEL */


/* Some misc. on-board devices.  */

/* Seven-segment LED display (two digits).  Write-only.  */
#define LED_ADDR(n)	(0x03802000 + (n))
#define LED(n)		(*(volatile unsigned char *)LED_ADDR(n))
#define LED_NUM_DIGITS	4


#endif /* __V850_RTE_NB85E_CB_H__ */
