/************************************************************************/
/* File iSeries_reset_device.c created by Allan Trautman on Mar 21 2001.*/
/************************************************************************/
/* This code supports the pci interface on the IBM iSeries systems.     */
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
/*   Created, March 20, 2001                                            */
/*   April 30, 2001, Added return codes on functions.                   */
/* End Change Activity                                                  */
/************************************************************************/
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/irq.h>
/************************************************************************/
/* Arch specific's                                                      */
/************************************************************************/
#include <asm/io.h>
#include <asm/iSeries/HvCallPci.h>
#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/mf.h>
#include <asm/iSeries/iSeries_FlightRecorder.h>
#include <asm/iSeries/iSeries_pci.h>
#include "iSeries_pci.h"

/************************************************************************/
/* Interface to Reset Device, see .h for parms and flavors.             */
/************************************************************************/
int  iSeries_Device_Reset_NoIrq(struct pci_dev* PciDev) {
    return iSeries_Device_Reset(PciDev,0,0,1);
}
int  iSeries_Device_Reset_Generic(struct pci_dev* PciDev) {
    return iSeries_Device_Reset(PciDev,0,0,0);
}
int  iSeries_Device_Reset(struct pci_dev* PciDev, int AssertTime, int DelayTime, int IrqState) {
    int              RCode       = 0;
    PciReqsSaveArea* RegSaveArea = NULL;
    if(PciDev != 0) {
	if(IrqState == 0) disable_irq(PciDev->irq);
	RegSaveArea = iSeries_Device_SaveConfigRegs(PciDev);
	if(RegSaveArea != NULL) {
	    RCode  = iSeries_Device_ToggleReset(PciDev, AssertTime, DelayTime);
	    if(RCode == 0) {
		RCode = iSeries_Device_RestoreConfigRegs(RegSaveArea);
	    }
	}
        else {
            RCode = -1;
        }
	if(IrqState == 0) enable_irq(PciDev->irq);
    }
    return RCode;
}
/************************************************************************/
/* Interface to toggle the reset line                                   */
/* Time is in .1 seconds, need for seconds.                             */
/************************************************************************/
int  iSeries_Device_ToggleReset(struct pci_dev* PciDev, int AssertTime, int DelayTime) {
    unsigned long AssertDelay = AssertTime;
    unsigned long WaitDelay   = DelayTime;
    u16           Bus         = ISERIES_GET_LPAR_BUS(PciDev->bus->number);
    u8            Slot        = ISERIES_DECODE_DEVICE(PciDev->devfn);
    int           RCode       = 0;

    /* Set defaults                                                     */
    if(AssertTime < 5) AssertDelay = 5;        /* Default is .5 second  */
    if(WaitDelay < 30) WaitDelay = 30;         /* Default is 3 seconds  */

    /* Assert reset for time specified                                  */
    AssertDelay  *= HZ;                       /* Convert to ticks.      */
    AssertDelay  /= 10;                       /* Adjust to whole count  */
    RCode  = HvCallPci_setSlotReset(Bus, 0x00, Slot, 1);
    set_current_state(TASK_UNINTERRUPTIBLE);  /* Only Wait.             */
    schedule_timeout(AssertDelay);            /* Sleep for the time     */
    RCode += HvCallPci_setSlotReset(Bus, 0x00, Slot, 0);

    /* Wait for device to reset                                         */
    WaitDelay  *= HZ;                         /* Ticks                  */
    WaitDelay  /= 10;                         /* Whole count            */
    set_current_state(TASK_UNINTERRUPTIBLE);  /* Only Wait.             */
    schedule_timeout(WaitDelay);              /* Sleep for the time     */

    if(RCode == 0) {
        sprintf(PciFrBuffer,"Slot Reset on Bus%3d, Device%3d!\n",Bus, Slot);
    }
    else {
        sprintf(PciFrBuffer,"Slot Reset on Bus%3d, Device%3d Failed!  RCode: %04X\n",Bus, Slot, RCode);
    }
    ISERIES_PCI_FR_TIME(PciFrBuffer);
    printk("PCI: %s\n",PciFrBuffer);
    return RCode;
}

/************************************************************************/
/* Allocates space and save the config registers for a device.          */
/************************************************************************/
/* Note: This does byte reads so the data may appear byte swapped       */
/* when compared to read word or dword.                                 */
/* The data returned is a structure and will be freed automatically on  */
/* the restore of the data.  The is checking so if the save fails, the  */
/* data will not be restore.  Yes I know, you are most likey toast.     */
/************************************************************************/
PciReqsSaveArea* iSeries_Device_SaveConfigRegs(struct pci_dev* DevPtr) {
    int              Register    = 0;
    struct pci_dev*  PciDev      = DevPtr;
    PciReqsSaveArea* RegSaveArea = (PciReqsSaveArea*)kmalloc(sizeof(PciReqsSaveArea), GFP_KERNEL);
    /*printk("PCI: Save Configuration Registers. 0x%08X\n",(int)RegSaveArea); */
    if(RegSaveArea == 0) {
	printk("PCI: Allocation failure in Save Configuration Registers.\n");
    }
    /********************************************************************/
    /* Initialize Area.                                                 */
    /********************************************************************/
    else {
	RegSaveArea->PciDev    = DevPtr;
	RegSaveArea->Flags     = 0x01;
	RegSaveArea->ByteCount = PCI_MAX_LAT+1;     /* Number of Bytes  */
	RegSaveArea->RCode     = 0;
	RegSaveArea->FailReg   = 0;
	/****************************************************************/
	/* Save All the Regs,  NOTE: restore skips the first 16 bytes.  */
	/****************************************************************/
	for(Register = 0;Register < RegSaveArea->ByteCount && RegSaveArea->RCode == 0; ++Register) {
	    RegSaveArea->RCode = pci_read_config_byte(PciDev, Register, &RegSaveArea->Regs[Register]);
	}
	/* Check for error during the save.                             */
	if(RegSaveArea->RCode != 0) {
	    printk("PCI: I/O Failure in Save Configuration Registers. 0x%02X, 0x%04X\n",
		                                              Register,RegSaveArea->RCode);
	    RegSaveArea->Flags  |= 0x80;		/* Ouch Flag.           */
	    RegSaveArea->FailReg = Register;	        /* Stuff this way       */
	}
    }
    return  RegSaveArea;
}
/************************************************************************/
/* Restores the registers saved via the save function.  See the save    */
/* function for details.                                                */
/************************************************************************/
int  iSeries_Device_RestoreConfigRegs(PciReqsSaveArea* SaveArea) {
    int RCode = 0;
    if(SaveArea == 0 || SaveArea->PciDev == 0 ||
      (SaveArea->Flags & 0x80) == 0x80 || SaveArea->RCode != 0) {
	printk("PCI: Invalid SaveArea passed to Restore Configuration Registers. 0x%08X\n",(int)SaveArea);
        RCode = -1;
    }
    else {
	int    Register;
	struct pci_dev* PciDev = SaveArea->PciDev;
        /***************************************************************/
	/* Don't touch the Cmd or BIST regs, user must restore those.  */
	/* Restore PCI_CACHE_LINE_SIZE & PCI_LATENCY_TIMER             */
        /* Restore Saved Regs from 0x10 to 0x3F                        */
        /***************************************************************/
	pci_write_config_byte(PciDev, PCI_CACHE_LINE_SIZE, SaveArea->Regs[PCI_CACHE_LINE_SIZE]);
	pci_write_config_byte(PciDev, PCI_LATENCY_TIMER,   SaveArea->Regs[PCI_LATENCY_TIMER]);

	for(Register = PCI_BASE_ADDRESS_0; Register < SaveArea->ByteCount && SaveArea->RCode == 0; ++Register) {
	    SaveArea->RCode = pci_write_config_byte(PciDev, Register, SaveArea->Regs[Register]);
	}
	if(SaveArea->RCode != 0) {
	    printk("PCI: I/O Failure in Restore Configuration Registers %d, %02X\n",Register,SaveArea->RCode);
	    SaveArea->FailReg = Register;
	    RCode = SaveArea->RCode;
	}
	else {
	    RCode = 0;
	}
	/***************************************************************/
        /* Is the Auto Free Flag on                                    */
        /***************************************************************/
	if(SaveArea->Flags && 0x01 == 0x01 ) {
	    /* printk("PCI: Auto Free Register Save Area.  0x%08X\n",(int)SaveArea); */
	    kfree(SaveArea);
	}
    }
    return RCode;
}
