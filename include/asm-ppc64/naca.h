#ifndef _NACA_H
#define _NACA_H

/* 
 * c 2001 PPC 64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/types.h>
#include <asm/systemcfg.h>

#ifndef __ASSEMBLY__

struct naca_struct {
	/* Kernel only data - undefined for user space */
	void *xItVpdAreas;              /* VPD Data                  0x00 */
	void *xRamDisk;                 /* iSeries ramdisk           0x08 */
	u64   xRamDiskSize;		/* In pages                  0x10 */
	struct paca_struct *paca;	/* Ptr to an array of pacas  0x18 */
	u64 debug_switch;		/* Debug print control       0x20 */
	u64 banner;                     /* Ptr to banner string      0x28 */
	u64 log;                        /* Ptr to log buffer         0x30 */
	u64 serialPortAddr;		/* Phy addr of serial port   0x38 */
	u64 interrupt_controller;	/* Type of int controller    0x40 */ 
};

extern struct naca_struct *naca;

#endif /* __ASSEMBLY__ */

#define NACA_PAGE      0x4
#define NACA_PHYS_ADDR (NACA_PAGE<<PAGE_SHIFT)
#define NACA_VIRT_ADDR (KERNELBASE+NACA_PHYS_ADDR)

#endif /* _NACA_H */
