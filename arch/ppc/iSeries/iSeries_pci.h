#ifdef CONFIG_PPC_ISERIES 
#ifndef _ISERIES_PCI_H
#define _ISERIES_PCI_H
/************************************************************************/
/* File iSeries_pci.h created by Allan Trautman on Tue Jan  9 2001.     */
/************************************************************************/
/* Define some useful macros for the iseries pci routines.              */
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
/*   Converted to iseries_pci.h Jan 25, 2001                            */
/* End Change Activity                                                  */
/************************************************************************/
#include <linux/config.h>
#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/iSeries_FlightRecorder.h>
#include <asm/iSeries/iSeries_pci.h>

/************************************************************************************/
/* Define some useful macros.                                                       */
/* These macros started with Wayne, Al renamed and refined.                         */
/************************************************************************************/
/* Encodes SubBus address(seddddfff), Works only for bridges under EADS 1 and 2.    */
/************************************************************************************/
/* #define ISERIES_ENCODE_SUBBUS(eads, bridge, device) \
             (0x80 | ((eads-1 & 0x01) << 6) | ((bridge & 0x0F) << 3) | (device & 0x07)) */
#define ISERIES_ENCODE_SUBBUS(e, ef, df) (((0x80 | ((e&0x02)<<5) | ((ef & 0x07)<<3)) & 0xF8) | (df & 0x03))  // Al - Please Review

/************************************************************************************/
/* Combines IdSel and Function into Iseries 4.4 format                              */
/* For Linux, see PCI_DEVFN(slot,func) in include/linux/pci.h                       */
/************************************************************************************/
// #define ISERIES_PCI_AGENTID(idsel,func)	((idsel & 0x0F) << 4) | (func  & 0x07)
#define ISERIES_PCI_AGENTID(idsel,func)	(((idsel & 0x0F) << 4) | (func & 0x0F))  // Al - Please Review

/************************************************************************************/
/* Converts DeviceFunction from Linux   5.3(dddddfff) to Iseries 4.4(dddd0fff)      */
/* Converts DeviceFunction from Iseries 4.4(dddd0fff) to Linux   5.3(dddddfff)      */
/************************************************************************************/
#define ISERIES_44_FORMAT(devfn53)          (((devfn53 & 0xF8) << 1) | (devfn53 & 0x07))
#define ISERIES_53_FORMAT(devfn44)          (((devfn44 & 0xF0) >> 1) | (devfn44 & 0x07))

/************************************************************************************/
/* Tests for encoded subbus.                                                        */
/************************************************************************************/
#define ISERIES_IS_SUBBUS_ENCODED_IN_DEVFN(devfn)   ((devfn & 0x80) == 0x80)

/************************************************************************************/
/* Decodes the Iseries subbus to devfn, ONLY Works for bus 0!! Use Table lookup.    */
/************************************************************************************/
/* #define ISERIES_DEVFN_DECODE_SUBBUS(devfn) \
                             ((((devfn & 0x40) >> 1) + 0x20)  | ((devfn >> 1) & 0x1C)) */
#define ISERIES_DEVFN_DECODE_SUBBUS(devfn) (((((devfn >> 6 ) & 0x1) + 1) << 5) | (((devfn >> 3) & 0x7) << 2))  // Al - Please Review

/************************************************************************************/
/* Decodes Linux DevFn to Iseries DevFn, bridge device, or function.                */
/* For Linux, see PCI_SLOT and PCI_FUNC in include/linux/pci.h                      */
/************************************************************************************/
#define ISERIES_DECODE_DEVFN(linuxdevfn)  (((linuxdevfn & 0x71) << 1) | (linuxdevfn & 0x07))
#define ISERIES_DECODE_DEVICE(linuxdevfn) (((linuxdevfn & 0x38) >> 3) |(((linuxdevfn & 0x40) >> 2) + 0x10))
#define ISERIES_DECODE_FUNCTION(linuxdevfn) (linuxdevfn & 0x07)

#define ISERIES_GET_DEVICE_FROM_SUBBUS(subbus) ((subbus >> 5) & 0x7)
#define ISERIES_GET_FUNCTION_FROM_SUBBUS(subbus) ((subbus >> 2) & 0x7)
#define ISERIES_GET_HOSE_HV_BUSNUM(hose) (((struct iSeries_hose_arch_data *)(hose->arch_data))->hvBusNumber)

/************************************************************************************/
/* Retreives Iseries Bus and SubBus from GlobalBusMap                               */
/************************************************************************************/
#define ISERIES_GET_LPAR_BUS(linux_bus)    iSeries_GlobalBusMap[linux_bus][_HVBUSNUMBER_]
#define ISERIES_GET_LPAR_SUBBUS(linux_bus) iSeries_GlobalBusMap[linux_bus][_HVSUBBUSNUMBER_]

#define ISERIES_ADD_BUS_GLOBALBUSMAP(linuxbus, iseriesbus, iseriessubbus) \
    iSeries_GlobalBusMap[linuxbus][_HVBUSNUMBER_]    = iseriesbus;      \
    iSeries_GlobalBusMap[linuxbus][_HVSUBBUSNUMBER_] = iseriessubbus;       

/************************************************************************************/
/* Global Bus map                                                                   */
/* Bus and Subbus index values into the global bus number map array.                */
/************************************************************************************/
#define ISERIES_GLOBALBUSMAP_SIZE 256
#define _HVBUSNUMBER_    0
#define _HVSUBBUSNUMBER_ 1
extern u8 iSeries_GlobalBusMap[ISERIES_GLOBALBUSMAP_SIZE][2];
void iSeries_Initialize_GlobalBusMap(void);
#define pci_assign_all_buses() 1  // Al - NEW

/************************************************************************************/
/* Converts Virtual Address to Real Address for Hypervisor calls                    */
/************************************************************************************/
#define REALADDR(virtaddr)  (0x8000000000000000 | (virt_to_absolute((u32)virtaddr) ))

/************************************************************************************/
/* Define TRUE and FALSE Values for Al                                              */
/************************************************************************************/
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

typedef struct pci_dev               pciDev; 
/************************************************************************/
/* Routines to build the iSeries_Device for the pci device.             */
/************************************************************************/
extern void build_iSeries_Device(iSeries_Device* Device, struct pci_dev* DevPtr);
extern int  build_iSeries_Device_From_IoAddress(iSeries_Device* Device, u32* IoAddress);
extern void iSeries_pci_Initialize(void);

/************************************************************************/
/* Flight Recorder Debug Support                                        */
/************************************************************************/
extern int          PciTraceFlag;               /* Conditional Trace    */
void   iSeries_Initialize_FlightRecorder(void);
int    iSeries_Set_PciTraceFlag(int Flag);	/* Sets flag, return old*/
int    iSeries_Get_PciTraceFlag(void);	/* Gets Flag.           */
void   iSeries_DumpDevice(char* Text, iSeries_Device* );

#endif  /* _ISERIES_PCI_H */
#endif  /*CONFIG_PPC_ISERIES  */

