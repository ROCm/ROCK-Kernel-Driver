#ifndef _ISERIES_IOMMTABLE_H
#define _ISERIES_IOMMTABLE_H
/************************************************************************/
/* File iSeries_IoMmTable.h created by Allan Trautman on Dec 12 2001.   */
/************************************************************************/
/* Interfaces for the write/read Io address translation table.          */
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
/*   Created December 12, 2000                                          */
/* End Change Activity                                                  */
/************************************************************************/

/************************************************************************/
/* iSeries_IoMmTable_Initialize                                         */
/************************************************************************/
/* - Initalizes the Address Translation Table and get it ready for use. */
/*   Must be called before any client calls any of the other methods.   */
/*                                                                      */
/* Parameters: None.                                                    */
/*                                                                      */
/* Return: None.                                                        */  
/************************************************************************/
extern  void iSeries_IoMmTable_Initialize(void);

/************************************************************************/
/* iSeries_allocateDeviceBars                                           */
/************************************************************************/
/* - Allocates ALL pci_dev BAR's and updates the resources with the BAR */
/*   value.  BARS with zero length will not have the resources.  The    */
/*   HvCallPci_getBarParms is used to get the size of the BAR space.    */
/*   It calls as400_IoMmTable_AllocateEntry to allocate each entry.     */
/*                                                                      */
/* Parameters:                                                          */
/* pci_dev = Pointer to pci_dev structure that will be mapped to pseudo */
/*           I/O Address.                                               */
/*                                                                      */
/* Return:                                                              */
/*   The pci_dev I/O resources updated with pseudo I/O Addresses.       */
/************************************************************************/
extern  void iSeries_allocateDeviceBars(struct pci_dev* Device);

/************************************************************************/
/* iSeries_IoMmTable_AllocateEntry                                      */ 
/************************************************************************/
/* - Allocates(adds) the pci_dev entry in the Address Translation Table */
/*   and updates the Resources for the device.                          */
/*                                                                      */
/* Parameters:                                                          */
/* pci_dev = Pointer to pci_dev structure that will be mapped to pseudo */
/*           I/O Address.                                               */
/*                                                                      */
/* BarNumber = Which Bar to be allocated.                               */
/*                                                                      */
/* Return:                                                              */
/*   The pseudo I/O Address in the resources that will map to the       */
/*   pci_dev on iSeries_xlateIoMmAddress call.                          */
/************************************************************************/
extern  void iSeries_IoMmTable_AllocateEntry(struct pci_dev* Device, u32 BarNumber);

/************************************************************************/
/* iSeries_xlateIoMmAddress                                             */
/************************************************************************/
/* - Translates an I/O Memory address to pci_dev that has been allocated*/
/*   the psuedo I/O Address.                                            */
/*                                                                      */
/* Parameters:                                                          */
/* IoAddress = I/O Memory Address.                                      */
/*                                                                      */
/* Return:                                                              */
/*   A pci_dev pointer to the device mapped to the I/O address.         */
/************************************************************************/
extern  struct pci_dev* iSeries_xlateIoMmAddress(u32* IoAddress);

/************************************************************************/
/* Helper Methods                                                       */
/************************************************************************/
extern  int  iSeries_IoMmTable_Bar(u32 *IoAddress);
extern  u32* iSeries_IoMmTable_BarBase(u32* IoAddress);
extern  u32  iSeries_IoMmTable_BarOffset(u32* IoAddress);
extern  int  iSeries_Is_IoMmAddress(unsigned long address);

/************************************************************************/
/* Helper Methods to get TableSize and TableSizeEntry.                  */
/************************************************************************/
extern  u32  iSeries_IoMmTable_TableEntrySize(void);
extern  u32  iSeries_IoMmTable_TableSize(void);

#endif /* _ISERIES_IOMMTABLE_H */
