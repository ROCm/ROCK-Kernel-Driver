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

struct naca_struct {
	void *xItVpdAreas;
	void *xRamDisk;
	u64 xRamDiskSize;		/* In pages */
	struct paca_struct *paca;	/* Ptr to an array of pacas */
	u64 debug_switch;		/* Bits to control debug printing */
	u16 processorCount;		/* # of physical processors */
	u16 dCacheL1LineSize;		/* Line size of L1 DCache in bytes */
	u16 dCacheL1LogLineSize;	/* Log-2 of DCache line size */
	u16 dCacheL1LinesPerPage;	/* DCache lines per page */
	u16 iCacheL1LineSize;		/* Line size of L1 ICache in bytes */
	u16 iCacheL1LogLineSize;	/* Log-2 of ICache line size */
	u16 iCacheL1LinesPerPage;	/* ICache lines per page */
	u16 slb_size;			/* SLB size in entries */
	u64 physicalMemorySize;		/* Size of real memory in bytes */
	u64 pftSize;			/* Log base 2 of page table size */
	u64 serialPortAddr;		/* Phyical address of serial port */
	u8 interrupt_controller;	/* Type of interrupt controller */ 
	u8 resv0;    			/* Type of interrupt controller */
	u16 platform;			/* Platform flags */
	u8 resv1[12];			/* Padding */
};

extern struct naca_struct *naca;

#endif /* _NACA_H */
