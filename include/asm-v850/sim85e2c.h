/*
 * include/asm-v850/sim85e2c.h -- Machine-dependent defs for
 *	V850E2 RTL simulator
 *
 *  Copyright (C) 2002  NEC Corporation
 *  Copyright (C) 2002  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_SIM85E2C_H__
#define __V850_SIM85E2C_H__


#define CPU_ARCH 	"v850e2"
#define CPU_MODEL	"v850e2"
#define CPU_MODEL_LONG	"NEC V850E2"
#define PLATFORM	"sim85e2c"
#define PLATFORM_LONG	"SIM85E2C V850E2 simulator"


/* Various memory areas supported by the simulator.
   These should match the corresponding definitions in the linker script.  */

/* `instruction RAM'; instruction fetches are much faster from IRAM than
   from DRAM.  */
#define IRAM_ADDR		0
#define IRAM_SIZE		0x00100000 /* 1MB */
/* `data RAM', below and contiguous with the I/O space.
   Data fetches are much faster from DRAM than from IRAM.  */
#define DRAM_ADDR		0xfff00000
#define DRAM_SIZE		0x000ff000 /* 1020KB */
/* `external ram'.  Unlike the above RAM areas, this memory is cached,
   so both instruction and data fetches should be (mostly) fast --
   however, currently only write-through caching is supported, so writes
   to ERAM will be slow.  */
#define ERAM_ADDR		0x00100000
#define ERAM_SIZE		0x07f00000 /* 127MB (max) */


/* CPU core control registers; these should be expanded and moved into
   separate header files when we support some other processors based on
   the same E2 core.  */
/* Bus Transaction Control Register */
#define NA85E2C_CACHE_BTSC_ADDR	0xfffff070
#define NA85E2C_CACHE_BTSC 	(*(volatile unsigned short *)NA85E2C_CACHE_BTSC_ADDR)
#define NA85E2C_CACHE_BTSC_ICM	0x1 /* icache enable */
#define NA85E2C_CACHE_BTSC_DCM0	0x4 /* dcache enable, bit 0 */
#define NA85E2C_CACHE_BTSC_DCM1	0x8 /* dcache enable, bit 1 */
/* Cache Configuration Register */
#define NA85E2C_BUSM_BHC_ADDR	0xfffff06a
#define NA85E2C_BUSM_BHC	(*(volatile unsigned short *)NA85E2C_BUSM_BHC_ADDR)

/* Simulator specific control registers.  */
/* NOTHAL controls whether the simulator will stop at a `halt' insn.  */
#define NOTHAL_ADDR		0xffffff22
#define NOTHAL			(*(volatile unsigned char *)NOTHAL_ADDR)
/* The simulator will stop N cycles after N is written to SIMFIN.  */
#define SIMFIN_ADDR		0xffffff24
#define SIMFIN			(*(volatile unsigned short *)SIMFIN_ADDR)


/* The simulator has an nb85e-style interrupt system.  */
#include <asm/nb85e_intc.h>

/* For <asm/irq.h> */
#define NUM_CPU_IRQS		64


/* For <asm/page.h> */
#define PAGE_OFFSET		DRAM_ADDR


/* For <asm/entry.h> */
/* `R0 RAM', used for a few miscellaneous variables that must be accessible
   using a load instruction relative to R0.  The sim85e2c simulator
   actually puts 1020K of RAM from FFF00000 to FFFFF000, so we arbitarily
   choose a small portion at the end of that.  */
#define R0_RAM_ADDR		0xFFFFE000


/* For <asm/param.h> */
#ifndef HZ
#define HZ			24	/* Minimum supported frequency.  */
#endif


#endif /* __V850_SIM85E2C_H__ */
