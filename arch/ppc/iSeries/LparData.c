/*
 * LparData.c
 * Copyright (C) 2001  Mike Corrigan IBM Corporation
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#define __KERNEL__ 1
#include <asm/types.h>
#include <asm/page.h>
#include <stddef.h>
#include <linux/config.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/iSeries/Naca.h>
#include <asm/iSeries/ItLpNaca.h>
#include <asm/iSeries/ItLpPaca.h>
#include <asm/iSeries/ItLpRegSave.h>
#include <asm/iSeries/Paca.h>
#include <asm/iSeries/HvReleaseData.h>
#include <asm/iSeries/LparMap.h>
#include <asm/iSeries/LparData.h>
#include <asm/iSeries/ItVpdAreas.h>
#include <asm/iSeries/ItIplParmsReal.h>
#include <asm/iSeries/ItLpQueue.h>
#include <asm/iSeries/IoHriProcessorVpd.h>
#include <asm/iSeries/ItSpCommArea.h>

#include "ReleaseData.h"

// maxProcessors is the number of physical processors
// The number of logical processors is twice that 
// number to support hardware multi-threading.
// If CONFIG_SMP is not defined, then logical
// processors will be defined, but the other threads
// will spin forever in iSeries_head.S
#define maxProcessors 16 

extern char _start_boltedStacks[];
unsigned maxPacas = maxProcessors * 2;

// The LparMap is used by the hypervisor to map the load area.  
// This indicates that the load area should be mapped to VSID 
// 0x000000000000C and that this  should be made addressable at 
// ESID 0x00000000C.  On 32-bit machines this is equivalent to 
// loading segment register 12 with VSID 12.
// 8192 indicates to map 8192 pages (32 MB) of the load area.

struct LparMap xLparMap = { 	
		.xNumberEsids =4,		// Number ESID/VSID pairs
		.xNumberRanges =1,		// Number of memory ranges
		.xSegmentTableOffs =0,		// Segment Table Page (unused)
		.xKernelEsidC =0xC,		// ESID to map
		.xKernelVsidC =0xCCC,		// VSID to map
		.xKernelEsidD =0xD,		// ESID to map
		.xKernelVsidD =0xDDD,		// VSID to map
		.xKernelEsidE =0xE,		// ESID to map
		.xKernelVsidE =0xEEE,		// VSID to map
		.xKernelEsidF =0xF,		// ESID to map
		.xKernelVsidF =0xFFF,		// VSID to map

		.xPages =		HvPagesToMap,	// # of pages to map (8192)
		.xOffset =	0,		// Offset into load area
		.xVPN =	0xCCC0000	// VPN of first mapped page
};					

// The Naca has a pointer to the ItVpdAreas.  The hypervisor finds
// the Naca via the HvReleaseData area.  The HvReleaseData has the
// offset into the Naca of the pointer to the ItVpdAreas.

extern struct ItVpdAreas itVpdAreas;

struct Naca xNaca = {
			 0, (void *)&itVpdAreas,
			 0, 0,			// Ram Disk start
			 0, 0			// Ram Disk size
};

// The LpQueue is used to pass event data from the hypervisor to
// the partition.  This is where I/O interrupt events are communicated.
// The ItLpQueue must be initialized (even though only to all zeros)
// If it were uninitialized (in .bss) it would get zeroed after the
// kernel gets control.  The hypervisor will have filled in some fields
// before the kernel gets control.  By initializing it we keep it out
// of the .bss

struct ItLpQueue xItLpQueue = {};

// The Paca is an array with one entry per processor.  Each contains an 
// ItLpPaca, which contains the information shared between the 
// hypervisor and Linux.  Each also contains an ItLpRegSave area which
// is used by the hypervisor to save registers.
// On systems with hardware multi-threading, there are two threads
// per processor.  The Paca array must contain an entry for each thread.
// The VPD Areas will give a max logical processors = 2 * max physical
// processors.  The processor VPD array needs one entry per physical
// processor (not thread).

#define PacaInit( n, start, lpq ) \
	{ 0, (struct ItLpPaca *)(((char *)(&xPaca[(n)]))+offsetof(struct Paca, xLpPaca)), \
	  0, (struct ItLpRegSave *)(((char *)(&xPaca[(n)]))+offsetof(struct Paca, xRegSav)), \
	  0, 0, 0, 		\
	  (n), (start),  	\
	  0,       		\
	  0,	   		\
	  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0},\
	  (lpq),		\
	  0, 0, {0},		\
	  { /* LpPaca */		\
		.xDesc = 0xd397d781, /* "LpPa" */	\
		.xSize = sizeof(struct ItLpPaca),	\
		.xFPRegsInUse =1,		\
		.xDynProcStatus = 2,		\
		.xEndOfQuantum =0xffffffffffffffff	\
	  },			\
	  { /* LpRegSave */	\
		0xd397d9e2,	/* "LpRS" */	\
		sizeof(struct ItLpRegSave)	\
	  }			\
	}

struct Paca xPaca[maxProcessors*2]  = {
	 PacaInit(  0, 1, &xItLpQueue ) 
	,PacaInit(  1, 0, 0 )
	,PacaInit(  2, 0, 0 )
	,PacaInit(  3, 0, 0 )
	,PacaInit(  4, 0, 0 )
	,PacaInit(  5, 0, 0 )
	,PacaInit(  6, 0, 0 )
	,PacaInit(  7, 0, 0 )
	,PacaInit(  8, 0, 0 )
	,PacaInit(  9, 0, 0 )
	,PacaInit( 10, 0, 0 )
	,PacaInit( 11, 0, 0 )
	,PacaInit( 12, 0, 0 )
	,PacaInit( 13, 0, 0 )
	,PacaInit( 14, 0, 0 )
	,PacaInit( 15, 0, 0 )
	,PacaInit( 16, 0, 0 )
	,PacaInit( 17, 0, 0 )
	,PacaInit( 18, 0, 0 )
	,PacaInit( 19, 0, 0 )
	,PacaInit( 20, 0, 0 )
	,PacaInit( 21, 0, 0 )
	,PacaInit( 22, 0, 0 )
	,PacaInit( 23, 0, 0 )
	,PacaInit( 24, 0, 0 )
	,PacaInit( 25, 0, 0 )
	,PacaInit( 26, 0, 0 )
	,PacaInit( 27, 0, 0 )
	,PacaInit( 28, 0, 0 )
	,PacaInit( 29, 0, 0 )
	,PacaInit( 30, 0, 0 )
	,PacaInit( 31, 0, 0 )
};



// The HvReleaseData is the root of the information shared between 
// the hypervisor and Linux.  

struct HvReleaseData 
	   hvReleaseData = {	0xc8a5d9c4,	// desc = "HvRD" ebcdic
		   		sizeof(struct HvReleaseData),
				offsetof(struct Naca, xItVpdAreas64),
				0, &xNaca,	// 64-bit Naca address
				((u32)&xLparMap-KERNELBASE),
				0,
				1,	// tags inactive mode
				1,	// 32-bit mode
				0,	// shared processors
				0,	// hardware multithreading
				6,	// TEMP: set me back to 0
					// this allows non-ga 450 driver
				3,	// We are v5r1m0
				3,	// Min supported PLIC = v5r1m0
				3,	// Min usuable PLIC   = v5r1m0
				RELEASEDATA,
				{0}  
			};

	
struct ItLpNaca itLpNaca = {    0xd397d581,	// desc = "LpNa" ebcdic
				0x0400,		// size of ItLpNaca
				0x0300, 19,	// offset to int array, # ents
				0, 0, 0,	// Part # of primary, serv, me
				0, 0x100,	// # of LP queues, offset
				0, 0, 0,	// Piranha stuff
				{ 0,0,0,0,0 },	// reserved
				0,0,0,0,0,0,0,	// stuff
				{ 0,0,0,0,0 },	// reserved
				0,		// reserved
				0,		// VRM index of PLIC
				0, 0,		// min supported, compat SLIC
				0,		// 64-bit addr of load area
				0,		// chunks for load area
				0,		// reserved
				{ 0 },		// 72 reserved bytes
				{ 0 }, 		// 128 reserved bytes
				{ 0 }, 		// Old LP Queue
				{ 0 }, 		// 384 reserved bytes
				{
				0xc0000100,	// int 0x100
				0xc0000200,	// int 0x200
				0xc0000300,	// int 0x300
				0xc0000400,	// int 0x400
				0xc0000500,	// int 0x500
				0xc0000600,	// int 0x600
				0xc0000700,	// int 0x700
				0xc0000800,	// int 0x800
				0xc0000900,	// int 0x900
				0xc0000a00,	// int 0xa00
				0xc0000b00,	// int 0xb00
				0xc0000c00,	// int 0xc00
				0xc0000d00,	// int 0xd00
				0xc0000e00,	// int 0xe00
				0xc0000f00,	// int 0xf00
				0xc0001000,	// int 0x1000
				0xc0001010,	// int 0x1010
				0xc0001020,	// int 0x1020 CPU ctls
				0xc0000500 	// SC Ret Hdlr
						// int 0x380
						// int 0x480
				}
		};

// All of the data structures that will be filled in by the hypervisor
// must be initialized (even if only to zeroes) to keep them out of 
// the bss.  If in bss, they will be zeroed by the kernel startup code
// after the hypervisor has filled them in.

struct ItIplParmsReal xItIplParmsReal = {};

struct IoHriProcessorVpd xIoHriProcessorVpd[maxProcessors] = {
	{ 
	.xTimeBaseFreq = 50000000
	}
};
	

u64    xMsVpd[3400] = {};		// Space for Main Store Vpd 27,200 bytes

u64    xRecoveryLogBuffer[32] = {};	// Space for Recovery Log Buffer

struct SpCommArea xSpCommArea = {
    0xE2D7C3C2,
    1,
    {0},
    0, 0, 0, 0, {0}
};

struct ItVpdAreas itVpdAreas = {
		0xc9a3e5c1,	// "ItVA"
		sizeof( struct ItVpdAreas ),
		0, 0,
		26,		// # VPD array entries
		10,		// # DMA array entries
		maxProcessors*2, maxProcessors,	// Max logical, physical procs
		offsetof(struct ItVpdAreas,xPlicDmaToks),// offset to DMA toks
		offsetof(struct ItVpdAreas,xSlicVpdAdrs),// offset to VPD addrs
		offsetof(struct ItVpdAreas,xPlicDmaLens),// offset to DMA lens
		offsetof(struct ItVpdAreas,xSlicVpdLens),// offset to VPD lens
		0,		// max slot labels
		1,		// max LP queues
		{0}, {0},	// reserved
		{0},		// DMA lengths
		{0},		// DMA tokens
		{		// VPD lengths
		0,0,0,0,		//  0 -  3
		sizeof(struct Paca),	//       4 length of Paca 
		0,			//       5
		sizeof(struct ItIplParmsReal),// 6 length of IPL parms
		26992,			//	 7 length of MS VPD
		0,			//       8
		sizeof(struct ItLpNaca),//       9 length of LP Naca
		0,			//	10 
		256,			//	11 length of Recovery Log Buf
		sizeof(struct SpCommArea), //   12 length of SP Comm area
		0,0,0,			// 13 - 15 
		sizeof(struct IoHriProcessorVpd),// 16 length of Proc Vpd
		0,0,0,0,0,0,		// 17 - 22 
		sizeof(struct ItLpQueue),//     23 length of Lp Queue
		0,0			// 24 - 25
		},
		{		// VPD addresses
		{0},{0},{0},{0},	//  0 -  3
		{0, &xPaca[0]},		//       4 first Paca
		{0},			//       5
		{0, &xItIplParmsReal},	//       6 IPL parms
		{0, &xMsVpd},		//	 7 MS Vpd
		{0},			//       8
		{0, &itLpNaca},		//       9 LpNaca
		{0},			//	10
		{0, &xRecoveryLogBuffer},//	11 Recovery Log Buffer
		{0, &xSpCommArea}, 	//	12 Sp Comm Area
		{0},{0},{0},		// 13 - 15 
		{0, &xIoHriProcessorVpd},//     16 Proc Vpd
		{0},{0},{0},{0},{0},{0},// 17 - 22
		{0, &xItLpQueue},	//      23 Lp Queue
		{0},{0}
		}
		
};

// The size of this array determines how much main store can be
// configured for use in the partition.  16384 allows 16384 * 256KB
// which is 4GB.  This is enough for the 32-bit
// implementation, but the msChunks array will need to be dynamically
// allocated for really big partitions.
u32 msChunks[16384]  = {0};
u32 totalLpChunks   = 0;

// Data area used in flush_hash_page 
long long flush_hash_page_hpte[2]  = {0,0};

u64 virt_to_absolute_outline(u32 address)
{
    return virt_to_absolute(address);
}
