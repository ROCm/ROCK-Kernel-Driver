/************************************************************************/
/* This module supports the iSeries I/O Address translation mapping     */
/* Copyright (C) 20yy  <Allan H Trautman> <IBM Corp>                    */
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
/*   Created, December 14, 2000                                         */
/*   Added Bar table for IoMm performance.                              */
/* End Change Activity                                                  */
/************************************************************************/
#include <asm/types.h>
#include <asm/resource.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <asm/iSeries/HvCallPci.h>
#include <asm/iSeries/iSeries_FlightRecorder.h>

#include "iSeries_IoMmTable.h"
#include "iSeries_pci.h"

void iSeries_allocateDeviceBars(struct pci_dev* PciDevPtr); 
/*******************************************************************/
/* Table defines                                                   */
/* Entry size is 4 MB * 1024 Entries = 4GB.                        */
/*******************************************************************/
#define iSeries_IoMmTable_Entry_Size   0x00400000  
#define iSeries_IoMmTable_Size         1024
#define iSeries_Base_Io_Memory         0xFFFFFFFF

/*******************************************************************/
/* Static and Global variables                                     */
/*******************************************************************/
struct pci_dev*   iSeries_IoMmTable[iSeries_IoMmTable_Size];
u8                iSeries_IoBarTable[iSeries_IoMmTable_Size];
static int        iSeries_CurrentIndex;
static char*      iSeriesPciIoText     = "iSeries PCI I/O";
static spinlock_t iSeriesIoMmTableLock = SPIN_LOCK_UNLOCKED;
/*******************************************************************/
/* iSeries_IoMmTable_Initialize                                    */
/*******************************************************************/
/* - Initalizes the Address Translation Table and get it ready for */
/*   use.  Must be called before any client calls any of the other */
/*   methods.                                                      */
/*******************************************************************/
void iSeries_IoMmTable_Initialize(void) {
    int Index;
    spin_lock(&iSeriesIoMmTableLock);
    for(Index=0;Index<iSeries_IoMmTable_Size ; ++Index) {
	iSeries_IoMmTable[Index]  = NULL;
	iSeries_IoBarTable[Index] = 0xFF;
    }
    spin_unlock(&iSeriesIoMmTableLock);
    iSeries_CurrentIndex = iSeries_IoMmTable_Size-1;
    ISERIES_PCI_FR("IoMmTable Init.");
}
/*******************************************************************/
/* iSeries_allocateDeviceBars                                      */
/*******************************************************************/
/* - Allocates ALL pci_dev BAR's and updates the resources with the*/
/*   BAR value.  BARS with zero length will have the resources     */
/*   The HvCallPci_getBarParms is used to get the size of the BAR  */
/*   space.  It calls iSeries_IoMmTable_AllocateEntry to allocate  */
/*   each entry.                                                   */
/* - Loops through The Bar resourses(0 - 5) including the the ROM  */
/*   is resource(6).                                               */
/*******************************************************************/
void iSeries_allocateDeviceBars(struct pci_dev* PciDevPtr) {
    struct resource* BarResource;
    int              BarNumber = 0;	        /* Current Bar Number      */
    if(PciTraceFlag > 0) {
	printk("PCI: iSeries_allocateDeviceBars 0x%08X\n",(int)PciDevPtr);
	sprintf(PciFrBuffer,"IoBars %08X",(int)PciDevPtr);
	ISERIES_PCI_FR(PciFrBuffer);
    }
    for(BarNumber = 0; BarNumber <= PCI_ROM_RESOURCE; ++BarNumber) {
	BarResource = &PciDevPtr->resource[BarNumber];
	iSeries_IoMmTable_AllocateEntry(PciDevPtr, BarNumber);
    }
}

/*******************************************************************/
/* iSeries_IoMmTable_AllocateEntry                                 */
/*******************************************************************/
/* Adds pci_dev entry in address translation table                 */
/*******************************************************************/
/* - Allocates the number of entries required in table base on BAR */
/*   size.                                                         */
/* - This version, allocates top down, starting at 4GB.            */
/* - The size is round up to be a multiple of entry size.          */
/* - CurrentIndex is decremented to keep track of the last entry.  */
/* - Builds the resource entry for allocated BARs.                 */
/*******************************************************************/
void iSeries_IoMmTable_AllocateEntry(struct pci_dev* PciDevPtr, u32 BarNumber) {
    struct resource* BarResource  = &PciDevPtr->resource[BarNumber];
    int              BarSize      = BarResource->end - BarResource->start;
    u32              BarStartAddr;
    u32              BarEndAddr;
    /***************************************************************/
    /* No space to allocate, skip Allocation.                      */
    /***************************************************************/
    if(BarSize == 0) return;		/* Quick stage exit        */

    /***************************************************************/
    /* Allocate the table entries needed.                          */
    /***************************************************************/
    spin_lock(&iSeriesIoMmTableLock);
    while(BarSize > 0) {
	iSeries_IoMmTable[iSeries_CurrentIndex]  = PciDevPtr;
	iSeries_IoBarTable[iSeries_CurrentIndex] = BarNumber;
	BarSize -= iSeries_IoMmTable_Entry_Size;
	--iSeries_CurrentIndex;		/* Next Free entry         */
    }
    spin_unlock(&iSeriesIoMmTableLock);
    BarStartAddr       = iSeries_IoMmTable_Entry_Size*(iSeries_CurrentIndex+1);
    BarEndAddr         = BarStartAddr + (u32)(BarResource->end - BarResource->start);
    /***************************************************************/
    /* Build Resource info                                         */
    /***************************************************************/
    BarResource->name  = iSeriesPciIoText;
    BarResource->start = (long)BarStartAddr;
    BarResource->end   = (long)BarEndAddr;

    /***************************************************************/
    /* Tracing                                                     */
    /***************************************************************/
    if(PciTraceFlag > 0) {
	printk("PCI: BarAloc %04X-%08X-%08X\n",iSeries_CurrentIndex+1,(int)BarStartAddr, (int)BarEndAddr);
	sprintf(PciFrBuffer,"IoMmAloc %04X-%08X-%08X",
		iSeries_CurrentIndex+1,(int)BarStartAddr, (int)BarEndAddr);
	ISERIES_PCI_FR(PciFrBuffer);
    }
}
/*******************************************************************/
/* Translates an I/O Memory address to pci_dev*                    */
/*******************************************************************/
struct pci_dev* iSeries_xlateIoMmAddress(u32* IoAddress) {
    int             PciDevIndex;
    struct pci_dev* PciDevPtr;
    PciDevIndex = (u32)IoAddress/iSeries_IoMmTable_Entry_Size;
    PciDevPtr   = iSeries_IoMmTable[PciDevIndex];
    if(PciDevPtr == 0) {
	printk("PCI: Invalid I/O Address: 0x%08X\n",(int)IoAddress);
	sprintf(PciFrBuffer,"Invalid MMIO Address 0x%08X",(int)IoAddress); 
    	ISERIES_PCI_FR(PciFrBuffer);
    }
    return PciDevPtr;
}
/************************************************************************/
/* Returns the Bar number of Address                                    */
/************************************************************************/
int  iSeries_IoMmTable_Bar(u32 *IoAddress) {
    int BarIndex  = (u32)IoAddress/iSeries_IoMmTable_Entry_Size;
    int BarNumber = iSeries_IoBarTable[BarIndex];
    return BarNumber;
}
/************************************************************************/
/* Return the Bar Base Address or 0.                                    */
/************************************************************************/
u32* iSeries_IoMmTable_BarBase(u32 *IoAddress) {
    u32  BaseAddr  = -1;
    pciDev* PciDev    = iSeries_xlateIoMmAddress(IoAddress);
    if(PciDev != 0) { 
	int BarNumber = iSeries_IoMmTable_Bar(IoAddress);
	if(BarNumber != -1) {
	    BaseAddr = (&PciDev->resource[BarNumber])->start;
	}
    }
    return (u32*)BaseAddr;
}
/************************************************************************/
/* Return the Bar offset within the Bar Space                           */
/* Note: Assumes that address is valid.                                 */
/************************************************************************/
u32  iSeries_IoMmTable_BarOffset(u32* IoAddress) {
    u32 BaseAddr  = (u32)iSeries_IoMmTable_BarBase(IoAddress);
    return (u32)IoAddress-BaseAddr;
}
/************************************************************************/
/* Return 0 if Address is valid I/O Address                             */
/************************************************************************/
int  iSeries_Is_IoMmAddress(unsigned long IoAddress) {
    if( iSeries_IoMmTable_Bar((u32*)IoAddress) == -1) return 1;
    else                                              return 0;
}
/************************************************************************/
/* Helper Methods to get TableSize and TableSizeEntry.                  */
/************************************************************************/
u32  iSeries_IoMmTable_TableEntrySize(void)	{ return iSeries_IoMmTable_Entry_Size;	}
u32  iSeries_IoMmTable_TableSize(void)	{ return iSeries_IoMmTable_Size;	}


