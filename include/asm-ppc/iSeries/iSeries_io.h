#ifdef CONFIG_PPC_ISERIES
#ifndef _ISERIES_IO_H
#define _ISERIES_IO_H
/************************************************************************/
/* File iSeries_io.h created by Allan Trautman on Thu Dec 28 2000.      */
/************************************************************************/
/* Remaps the io.h for the iSeries Io                                   */
/* Copyright (C) 20yy  Allan H Trautman, IBM Corporation                */
/*                                                                      */
/* This program is free software; you can redistribute it and/or modify */
/* it under the terms of the GNU General Public License as published by */
/* the Free Software Foundation; either version 2 of the License, or    */
/* (at your option) any later version.                                  */
/*                                                                      */
/* This program is distributed in the hope that it will be useful,      */ 
/* but WITHOUT ANY WARRANTY; without even the implied warranty of       */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        */
/* GNU General Public License for more details.                         */
/*                                                                      */
/* You should have received a copy of the GNU General Public License    */ 
/* along with this program; if not, write to the:                       */
/* Free Software Foundation, Inc.,                                      */ 
/* 59 Temple Place, Suite 330,                                          */ 
/* Boston, MA  02111-1307  USA                                          */
/************************************************************************/
/* Change Activity:                                                     */
/*   Created December 28, 2000                                          */
/* End Change Activity                                                  */
/************************************************************************/
extern u8   iSeries_Readb(u32* IoAddress);
extern u16  iSeries_Readw(u32* IoAddress);
extern u32  iSeries_Readl(u32* IoAddress);
extern void iSeries_Writeb(u8  IoData,u32* IoAddress);
extern void iSeries_Writew(u16 IoData,u32* IoAddress);
extern void iSeries_Writel(u32 IoData,u32* IoAddress);

extern void* iSeries_memcpy_toio(void *dest, void *source, int n);
extern void* iSeries_memcpy_fromio(void *dest, void *source, int n);

#endif /*  _ISERIES_IO_H         */
#endif /*  CONFIG_PPC_ISERIES  */

