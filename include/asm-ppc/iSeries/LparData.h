/*
 * LparData.h
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

#ifndef	_PPC_TYPES_H
#include	<asm/types.h>
#endif

#ifndef _LPARDATA_H
#define _LPARDATA_H

#include <asm/page.h>

extern u32    msChunks[];

#define phys_to_absolute( x ) (((u64)msChunks[((x)>>18)])<<18)+((u64)((x)&0x3ffff))

#define physRpn_to_absRpn( x ) (((u64)msChunks[((x)>>6)])<<6)+((u64)((x)&0x3f))

#define virt_to_absolute( x ) (((u64)msChunks[(((x)-KERNELBASE)>>18)])<<18)+((u64)((x)&0x3ffff))

extern u64 virt_to_absolute_outline(u32 address);


#include <asm/iSeries/Naca.h>
#include <asm/iSeries/ItLpNaca.h>
#include <asm/iSeries/ItLpPaca.h>
#include <asm/iSeries/ItLpRegSave.h>
#include <asm/iSeries/Paca.h>
#include <asm/iSeries/HvReleaseData.h>
#include <asm/iSeries/LparMap.h>
#include <asm/iSeries/ItVpdAreas.h>
#include <asm/iSeries/ItIplParmsReal.h>
#include <asm/iSeries/ItLpQueue.h>
#include <asm/iSeries/IoHriProcessorVpd.h>
#include <asm/page.h>

extern struct LparMap	xLparMap;
extern struct Naca	xNaca;
extern struct Paca	xPaca[];
extern struct HvReleaseData hvReleaseData;
extern struct ItLpNaca	itLpNaca;
extern struct ItIplParmsReal xItIplParmsReal;
extern struct IoHriProcessorVpd xIoHriProcessorVpd[];
extern struct ItLpQueue xItLpQueue;
extern struct ItVpdAreas itVpdAreas;
extern u64    xMsVpd[];
extern u32    msChunks[];
extern u32    totalLpChunks;
extern unsigned maxPacas;

/*
#define phys_to_absolute( x ) (((u64)msChunks[((x)>>18)])<<18)+((u64)((x)&0x3ffff))

#define physRpn_to_absRpn( x ) (((u64)msChunks[((x)>>6)])<<6)+((u64)((x)&0x3f))

#define virt_to_absolute( x ) (((u64)msChunks[(((x)-KERNELBASE)>>18)])<<18)+((u64)((x)&0x3ffff))
*/

#endif /* _LPAR_DATA_H */
