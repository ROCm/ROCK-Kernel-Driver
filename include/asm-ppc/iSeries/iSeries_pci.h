#ifndef _ISERIES_32_PCI_H
#define _ISERIES_32_PCI_H
/************************************************************************/
/* File iSeries_pci.h created by Allan Trautman on Tue Feb 20, 2001.    */
/************************************************************************/
/* Define some useful macros for the iSeries pci routines.              */
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
/*   Created Feb 20, 2001                                               */
/*   Added device reset, March 22, 2001                                 */
/* End Change Activity                                                  */
/************************************************************************/
#include <linux/config.h>
#include <asm/iSeries/HvCallPci.h>

struct pci_dev;				/* For Reference                */
/************************************************************************/
/* Gets iSeries Bus, SubBus, of DevFn using pci_dev* structure          */
/************************************************************************/
#define ISERIES_GET_BUS(DevPtr)    iSeries_Get_Bus(DevPtr)
#define ISERIES_GET_SUBBUS(DevPtr) iSeries_Get_SubBus(DevPtr)
#define ISERIES_GET_DEVFUN(DevPtr) iSeries_Get_DevFn(DevPtr)
/************************************************************************/
/* Functions                                                            */
/************************************************************************/
extern u8 iSeries_Get_Bus(struct pci_dev*);
extern u8 iSeries_Get_SubBus(struct pci_dev*);
extern u8 iSeries_Get_DevFn(struct pci_dev*);
/************************************************************************/
/* Global Bus map                                                       */
/************************************************************************/
extern u8 iSeries_GlobalBusMap[256][2];	      /* Global iSeries Bus Map */

/************************************************************************/
/* iSeries Device Information                                           */
/************************************************************************/
struct iSeries_Device_Struct {
    struct pci_dev* PciDevPtr;		/* Pointer to pci_dev structure */
    HvBusNumber     BusNumber;		/* Hypervisor Bus Number        */
    HvSubBusNumber  SubBus;		/* Hypervisor SubBus Number     */
    HvAgentId       DevFn;		/* Hypervisor DevFn             */
    u8              BarNumber;		/* Bar number on Xlates         */
    u32             BarOffset;		/* Offset within Bar on Xlates  */
    int             RCode;		/* Return Code Holder           */
};
typedef struct iSeries_Device_Struct iSeries_Device;
extern void build_iSeries_Device(iSeries_Device* Device, struct pci_dev* DevPtr);

/************************************************************************/
/* Formatting device information.                                       */
/************************************************************************/
extern int  iSeries_Device_Information(struct pci_dev*,char*,int );

/************************************************************************/
/* Flight Recorder tracing                                              */
/************************************************************************/
extern void iSeries_Set_PciFilter(struct pci_dev*);
extern int  iSeries_Set_PciTraceFlag(int TraceFlag);
extern int  iSeries_Get_PciTraceFlag(void);
extern int  iSeries_Set_PciErpFlag(int ErpFlag);

/************************************************************************/
/* Structure to hold the data for PCI Register Save/Restore functions.  */
/************************************************************************/
struct pci_config_reg_save_area {       /*                              */
    u16    Flags;			/* Control & Info Flags         */
    u16    ByteCount;			/* Number of Register Bytes to S*/
    struct pci_dev* PciDev;		/* Pointer to device            */ 
    u32    RCode;			/* Holder for possible errors   */
    u32    FailReg;                     /* Failing Register on error    */
    u8     Regs[64];			/* Save Area                    */ 
};
typedef struct pci_config_reg_save_area PciReqsSaveArea;
/************************************************************************/
/* Various flavors of reset device functions.                           */
/************************************************************************/
/*                                                                      */
/* iSeries_Device_Reset_NoIrq                                           */
/*	IRQ is not disabled and default timings are used.               */
/* iSeries_Device_Reset_Generic                                         */
/*	A generic reset, IRQ is disable and re-enabled.  The assert and */
/*	wait timings will be the pci defaults.                          */
/* iSeries_Device_Reset                                                 */
/*	A device Reset interface that client can control the timing of  */
/*	the reset and wait delays.                                      */
/*                                                                      */
/* Parameters:                                                          */
/*    pci_dev    = Device to reset.                                     */
/*    AssertTime = Time in .1 seconds to hold the reset down.  The      */
/*                 default (and minimum) is .5 seconds.                 */
/*    DelayTime = Time in .1 seconds to wait for device to come ready   */
/*                after the reset.  The default is 3 seconds.           */
/*    IrgDisable = A non-zero will skip irq disable & enable.           */
/*                                                                      */
/* Return:                                                              */
/*    Zero return, reset is successful.                                 */
/*    Non-zero return code indicates failure.                           */
/************************************************************************/
extern int  iSeries_Device_Reset_NoIrq(struct pci_dev* PciDev);
extern int  iSeries_Device_Reset_Generic(struct pci_dev* PciDev);
extern int  iSeries_Device_Reset(struct pci_dev* PciDev, int AssertTime, int DelayTime, int IrqDisable);
extern int  iSeries_Device_ToggleReset(struct pci_dev* PciDev, int AssertTime, int DelayTime);
extern int  iSeries_Device_RestoreConfigRegs(PciReqsSaveArea* SaveArea);
extern PciReqsSaveArea* iSeries_Device_SaveConfigRegs(struct pci_dev* DevPtr);

#endif /* _ISERIES_32_PCI_H */
