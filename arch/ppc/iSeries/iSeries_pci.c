/************************************************************************/
/* File iSeries_pci.c created by Allan Trautman on Tue Jan  9 2001.     */
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
/*   Created, Jan 9, 2001                                               */
/*   August, 10, 2001  Added PCI Retry code.                            */
/* End Change Activity                                                  */
/************************************************************************/
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>

/************************************************************************/
/* Arch specific's                                                      */
/************************************************************************/
#include <asm/io.h>                /* Has Io Instructions.              */
#include <asm/iSeries/iSeries_io.h>
#include <asm/pci-bridge.h>
#include <asm/iSeries/HvCallPci.h>
#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/mf.h>
#include <asm/iSeries/iSeries_proc.h>
#include <asm/iSeries/iSeries_FlightRecorder.h>
#include <asm/iSeries/iSeries_VpdInfo.h>
#include "iSeries_IoMmTable.h"
#include "iSeries_pci.h"

extern int panic_timeout;		       /* Panic Timeout reference      */

/************************************************************************/
/* /proc/iSeries/pci initialization.                                    */
/************************************************************************/
extern void iSeries_pci_proc_init(struct proc_dir_entry *iSeries_proc);
       void iSeries_pci_IoError(char* OpCode,iSeries_Device* Device);

/************************************************************************/
/* PCI Config Space Access functions for IBM iSeries Systems            */ 
/*                                                                      */
/* Modeled after the IBM Python                                         */
/* int pci_read_config_word(struct pci_dev*, int Offset, u16* ValuePtr) */
/************************************************************************/
/* Note: Normal these routines are defined by a macro and branch        */
/* table is created and stuffed in the pci_bus structure.               */
/* The following is for reference, if it was done this way.  However for*/
/* the first time out, the routines are individual coded.               */ 
/************************************************************************/
/* This is the low level architecture depend interface routines.        */     
/* 1. They must be in specific order, see <linux/pci.h> for base        */
/*    structure.                                                        */
/* 2. The code in not inline here, it calls specific routines in        */
/*    this module and HvCalls                                           */
/* 3. The common convention is to put a pointer to the structure in     */
/*    pci_bus->sysdata.                                                 */
/* 4. Direct call is the same as in <linux/pci.h                        */  
/************************************************************************/
int iSeries_pci_read_config_byte(  struct pci_dev* PciDev, int Offset, u8* ValuePtr) {
    iSeries_Device Device;
    build_iSeries_Device(&Device, PciDev);
    if(Device.BusNumber == 0xFF) Device.RCode = 0x301;	/* Trap out invalid bus. */
    else {
	Device.RCode = HvCallPci_configLoad8(Device.BusNumber, Device.SubBus, Device.DevFn, Offset, ValuePtr);
	if(Device.RCode != 0 || PciTraceFlag > 0) {
	    sprintf(PciFrBuffer,"RCB: %02X,%02X,%02X,%04X Rtn: %04X,%02X",
		                 Device.BusNumber, Device.SubBus, Device.DevFn, Offset,
		                 Device.RCode, *ValuePtr);
	    ISERIES_PCI_FR(PciFrBuffer);
	    if(Device.RCode != 0 ) printk("PCI: I/O Error %s",PciFrBuffer);
	}
    }
    return Device.RCode;
}                                                                            
int iSeries_pci_read_config_word(  struct pci_dev* PciDev, int Offset, u16* ValuePtr) {
    iSeries_Device Device;
    build_iSeries_Device(&Device, PciDev);
    if(Device.BusNumber == 0xFF) Device.RCode = 0x301;	/* Trap out invalid bus. */
    else {
	Device.RCode = HvCallPci_configLoad16(Device.BusNumber, Device.SubBus, Device.DevFn, Offset, ValuePtr);
	if(Device.RCode != 0 || PciTraceFlag > 0) {
	    sprintf(PciFrBuffer,"RCW: %02X,%02X,%02X,%04X Rtn: %04X,%04X",
		                 Device.BusNumber, Device.SubBus, Device.DevFn, Offset,
		                 Device.RCode, *ValuePtr);
	    ISERIES_PCI_FR(PciFrBuffer);
	    if(Device.RCode != 0 ) printk("PCI: I/O Error %s",PciFrBuffer);
	}
    }
    return Device.RCode;
}                                                                            
int iSeries_pci_read_config_dword( struct pci_dev* PciDev, int Offset, u32* ValuePtr) {
    iSeries_Device Device;
    build_iSeries_Device(&Device, PciDev);
    if(Device.BusNumber == 0xFF) Device.RCode = 0x301;	/* Trap out invalid bus. */
    else {
	Device.RCode = HvCallPci_configLoad32(Device.BusNumber, Device.SubBus, Device.DevFn, Offset, ValuePtr);
	if(Device.RCode != 0 || PciTraceFlag > 0) {
	    sprintf(PciFrBuffer,"RCL: %02X,%02X,%02X,%04X Rtn: %04X,%08X",
		                 Device.BusNumber, Device.SubBus, Device.DevFn, Offset,
		                 Device.RCode, *ValuePtr);
	    ISERIES_PCI_FR(PciFrBuffer);
	    if(Device.RCode != 0 ) printk("PCI: I/O Error %s",PciFrBuffer);
	}
    }
    return Device.RCode;
}                                                                            
int iSeries_pci_write_config_byte( struct pci_dev* PciDev, int Offset,  u8  Value) {
    iSeries_Device Device;
    build_iSeries_Device(&Device, PciDev);
    if(Device.BusNumber == 0xFF) Device.RCode = 0x301;	/* Trap out invalid bus. */
    else {
	Device.RCode = HvCallPci_configStore8(Device.BusNumber, Device.SubBus, Device.DevFn, Offset, Value);
	if(Device.RCode != 0 || PciTraceFlag > 0) {
	    sprintf(PciFrBuffer,"WCB: %02X,%02X,%02X,%04X Rtn: %04X,%02X",
		                 Device.BusNumber, Device.SubBus, Device.DevFn, Offset,
		                 Device.RCode, Value);
	    ISERIES_PCI_FR(PciFrBuffer);
	    if(Device.RCode != 0 ) printk("PCI: I/O Error %s",PciFrBuffer);
	}
    }
    return Device.RCode;
}                                                                            
int iSeries_pci_write_config_word( struct pci_dev* PciDev, int Offset, u16  Value) {
    iSeries_Device Device;
    build_iSeries_Device(&Device, PciDev);
    if(Device.BusNumber == 0xFF) Device.RCode = 0x301;	/* Trap out invalid bus. */
    else {
	Device.RCode = HvCallPci_configStore16(Device.BusNumber, Device.SubBus, Device.DevFn, Offset, Value);
	if(Device.RCode != 0 || PciTraceFlag > 0) {
	    sprintf(PciFrBuffer,"WCW: %02X,%02X,%02X,%04X Rtn: %04X,%04X",
		                 Device.BusNumber, Device.SubBus, Device.DevFn, Offset,
		                 Device.RCode, Value);
	    ISERIES_PCI_FR(PciFrBuffer);
	    if(Device.RCode != 0 ) printk("PCI: I/O Error %s",PciFrBuffer);
	}
    }
    return Device.RCode;
}                                                                            
int iSeries_pci_write_config_dword(struct pci_dev* PciDev, int Offset, u32  Value) {
    iSeries_Device Device;
    build_iSeries_Device(&Device, PciDev);
    if(Device.BusNumber == 0xFF) Device.RCode = 0x301;	/* Trap out invalid bus. */
    else {
	Device.RCode = HvCallPci_configStore32(Device.BusNumber, Device.SubBus, Device.DevFn, Offset, Value);
	if(Device.RCode != 0 || PciTraceFlag > 0) {
	    sprintf(PciFrBuffer,"WCL: %02X,%02X,%02X,%04X Rtn: %04X,%08X",
		                 Device.BusNumber, Device.SubBus, Device.DevFn, Offset,
		                 Device.RCode, Value);
	    ISERIES_PCI_FR(PciFrBuffer);
	    if(Device.RCode != 0 ) printk("PCI: I/O Error %s",PciFrBuffer);
	}
    }
    return Device.RCode;
}                                                                            
/************************************************************************/
/* Branch Table                                                         */
/************************************************************************/
struct pci_ops iSeries_pci_ops = {
	iSeries_pci_read_config_byte,
	iSeries_pci_read_config_word,
	iSeries_pci_read_config_dword,
	iSeries_pci_write_config_byte,
	iSeries_pci_write_config_word,
	iSeries_pci_write_config_dword
};

/************************************************************************/
/* Dump the device info into the Flight Recorder                        */
/* Call should have 3 char text to prefix FR Entry                      */
/************************************************************************/
void iSeries_DumpDevice(char* Text, iSeries_Device* Device) {
	sprintf(PciFrBuffer,"%s:%02X%02X%02X %04X",Text,Device->BusNumber,Device->SubBus,Device->DevFn,Device->RCode);
    ISERIES_PCI_FR(PciFrBuffer);
    if(Device->BarNumber != 0xFF) {
	sprintf(PciFrBuffer,"BAR:%02X %04X    ",Device->BarNumber,Device->BarOffset);
	ISERIES_PCI_FR(PciFrBuffer);
    }
    if(Device->RCode != 0) {
	/*****************************************************************/
	/* PCI I/O ERROR RDL: Bus: 0x01 Device: 0x06 ReturnCode: 0x000B  */
	/*****************************************************************/
	printk("PCI: I/O ERROR %s: Bus: 0x%02X Device: 0x%02X ReturnCode: 0x%04X\n",
	        Text,Device->PciDevPtr->bus->number,Device->PciDevPtr->devfn,Device->RCode);
    }
}

int IoCounts    = 0;
int RetryCounts = 0;
int IoRetry     = 0;

/************************************************************************
 * Check Return Code
 * -> On Failure, print and log information.
 *    Increment Retry Count, if exceeds max, panic partition.
 * -> If in retry, print and log success 
 ************************************************************************
 * PCI: ReadL: I/O Error( 0): 0x1234
 * PCI: Device..: 17:38.10  Bar: 0x00  Offset 0018
 * PCI: Device..: 17:38.10  Retry( 1) Operation ReadL:
 * PCI: Retry Successful on Device: 17:38.10
 ************************************************************************/
int  CheckReturnCode(char* TextHdr, iSeries_Device* Device) {
	++IoCounts;
	if(Device->RCode != 0)  {

		sprintf(PciFrBuffer,"%s: I/O Error(%2d): 0x%04X\n",	TextHdr, IoRetry,Device->RCode);
		ISERIES_PCI_FR(PciFrBuffer);
		printk("PCI: %s",PciFrBuffer);

		sprintf(PciFrBuffer,"Device..: %02X:%02X.%02X  Bar: 0x%02X  Offset %04X\n",
			                         Device->BusNumber, Device->SubBus, Device->DevFn,
		                              Device->BarNumber, Device->BarOffset); 
		ISERIES_PCI_FR(PciFrBuffer);
		printk("PCI: %s",PciFrBuffer);

		/* Bump the retry and check for max. */
		++RetryCounts;
		++IoRetry;

		/* Retry Count exceeded or RIO Bus went down. */
		if(IoRetry > 7 || Device->RCode == 0x206) {
			mf_displaySrc(0xB6000103);
			panic_timeout = 0; 
			panic("PCI: Hardware I/O Error, SRC B6000103, Automatic Reboot Disabled.");
		}
		else {
			sprintf(PciFrBuffer,"Device..: %02X:%02X.%02X  Retry(%2d) Operation: %s\n",
			                         Device->BusNumber, Device->SubBus, Device->DevFn,
			                         IoRetry,TextHdr);
			ISERIES_PCI_FR(PciFrBuffer);
			printk("PCI: %s\n",PciFrBuffer);
		}
	}
	else {
		if(IoRetry > 0 ) {
			sprintf(PciFrBuffer,"Retry Successful on Device: %02X:%02X.%02X\n",
			                         Device->BusNumber, Device->SubBus, Device->DevFn);
			ISERIES_PCI_FR(PciFrBuffer);
			printk("PCI: %s\n",PciFrBuffer);
		}
	}
	return Device->RCode; 
}

/************************************************************************/
/* Read MM I/O Instructions for the iSeries                             */
/* On MM I/O error, all ones are returned and iSeries_pci_IoError is cal*/
/* else, data is returned in big Endian format.                         */
/************************************************************************/
/* iSeries_Readb = Read Byte  ( 8 bit)                                  */
/* iSeries_Readw = Read Word  (16 bit)                                  */
/* iSeries_Readl = Read Long  (32 bit)                                  */
/************************************************************************/
u8   iSeries_Readb(u32* IoAddress)	{
	iSeries_Device Device;
	u8             IoData;
	u8             Data   = -1;
	IoRetry = 0;
	if(build_iSeries_Device_From_IoAddress(&Device, IoAddress) == 0) {
		do {
			Device.RCode = HvCallPci_barLoad8 (Device.BusNumber, Device.SubBus,Device.DevFn,
   	     	                                   Device.BarNumber, Device.BarOffset, &IoData);
        		Data = IoData;
			if(Device.RCode != 0) CheckReturnCode("ReadB",&Device);
		} while(Device.RCode != 0); 
    }
	return Data;
}
u16 iSeries_Readw(u32* IoAddress)	{
	iSeries_Device Device;
	u16            IoData;
    	u16            Data   = -1;
	IoRetry = 0;
	if(build_iSeries_Device_From_IoAddress(&Device, IoAddress) == 0) {
		do {
        		Device.RCode = HvCallPci_barLoad16(Device.BusNumber, Device.SubBus,Device.DevFn,
    	     	                                   Device.BarNumber, Device.BarOffset, &IoData);
			Data = swab16(IoData); 
			if(Device.RCode != 0) CheckReturnCode("ReadW",&Device);
		} while(Device.RCode != 0); 
	}
	return Data;
}
u32 iSeries_Readl(u32* IoAddress)	{
    iSeries_Device Device;
	u32            Data   = -1;
	u32            IoData;          
	IoRetry = 0;
	if(build_iSeries_Device_From_IoAddress(&Device, IoAddress) == 0) {
		IoRetry = 0;
		do {
			Device.RCode = HvCallPci_barLoad32(Device.BusNumber, Device.SubBus,Device.DevFn,
                                                   Device.BarNumber, Device.BarOffset, &IoData);
			Data = swab32(IoData);
			if(Device.RCode != 0) CheckReturnCode("ReadL",&Device);
		} while(Device.RCode != 0); 
	}
	return Data;
}
/************************************************************************/
/* Write MM I/O Instructions for the iSeries                            */
/* On MM I/O error, iSeries_pci_IoError is called                       */
/* Data is in big Endian format.                                        */
/************************************************************************/
/* iSeries_Writeb = Write Byte (8 bit)                                  */
/* iSeries_Writew = Write Word(16 bit)                                  */
/* iSeries_Writel = Write Long(32 bit)                                  */
/************************************************************************/
void iSeries_Writeb(u8  Data,u32* IoAddress) {
	iSeries_Device Device;
	u8             IoData = Data;
	IoRetry = 0;
	if(build_iSeries_Device_From_IoAddress(&Device, IoAddress) == 0) {
		do {
			Device.RCode = HvCallPci_barStore8 (Device.BusNumber, Device.SubBus,Device.DevFn,
                                                  Device.BarNumber, Device.BarOffset, IoData);
			if(Device.RCode != 0) CheckReturnCode("WrteB",&Device);
		} while(Device.RCode != 0); 
    }
}
void iSeries_Writew(u16 Data,u32* IoAddress)	{
	iSeries_Device Device;
	u16            IoData = swab16(Data);
	IoRetry = 0;
	if(build_iSeries_Device_From_IoAddress(&Device, IoAddress) == 0) {
		do {
			Device.RCode = HvCallPci_barStore16(Device.BusNumber, Device.SubBus,Device.DevFn,
     	                                       Device.BarNumber, Device.BarOffset, IoData);
			if(Device.RCode != 0) CheckReturnCode("WrteW",&Device);
		} while(Device.RCode != 0); 
    	}
}
void iSeries_Writel(u32 Data,u32* IoAddress)     {
	iSeries_Device Device;
	u32            IoData = swab32(Data);
	IoRetry = 0;
	if(build_iSeries_Device_From_IoAddress(&Device, IoAddress) == 0) {
		do {
			Device.RCode = HvCallPci_barStore32(Device.BusNumber, Device.SubBus,Device.DevFn,
			                                    Device.BarNumber, Device.BarOffset, IoData);
			if(Device.RCode != 0) CheckReturnCode("WrteL",&Device);
		} while(Device.RCode != 0); 
	}
}
/************************************************************************/
/* iSeries I/O Remap                                                    */
/* -> Check to see if one of ours                                       */
/* -> If not, return null to help find the bug.                         */
/************************************************************************/
void* iSeries_ioremap (unsigned long offset, unsigned long size) {
    if(iSeries_xlateIoMmAddress((u32*)offset) != 0) {
	return (void*)offset;
    }
    else {
	return NULL;
    }
}    
/************************************************************************/
/* Routine to build the iSeries_Device for the pci device.              */
/************************************************************************/
void build_iSeries_Device(iSeries_Device* Device, struct pci_dev* DevPtr) {
    Device->PciDevPtr = DevPtr; 
    Device->BusNumber = ISERIES_GET_LPAR_BUS(DevPtr->bus->number);
    if(ISERIES_GET_LPAR_SUBBUS(DevPtr->bus->number) == 0) {
	Device->SubBus = ISERIES_DEVFN_DECODE_SUBBUS(DevPtr->devfn);
	Device->DevFn  = 0x10 | ISERIES_DECODE_FUNCTION(DevPtr->devfn);
    }
    else {
	Device->SubBus = ISERIES_GET_LPAR_SUBBUS(DevPtr->bus->number);
	Device->DevFn  = ISERIES_44_FORMAT(DevPtr->devfn);
    }
    Device->BarNumber      = 0xFF;
    Device->BarOffset      = 0;
    Device->RCode          = 0;
}
/************************************************************************/
/* Routine to build the iSeries_Device for the IoAddress.               */
/************************************************************************/
int  build_iSeries_Device_From_IoAddress(iSeries_Device* Device, u32* IoAddress) {
    Device->PciDevPtr = iSeries_xlateIoMmAddress(IoAddress);
    if(Device->PciDevPtr != 0) {        /* Valid pci_dev?              */
		build_iSeries_Device(Device,Device->PciDevPtr);
		Device->BarNumber = iSeries_IoMmTable_Bar(IoAddress);
		if( Device->BarNumber != 0xFF) {
			Device->BarOffset = iSeries_IoMmTable_BarOffset(IoAddress);
			return 0;
		}
		else {
			sprintf(PciFrBuffer,"I/O BAR Address: 0x%08X",(int)IoAddress);
			ISERIES_PCI_FR(PciFrBuffer);
			printk("PCI: Invalid I/O Address 0x%08X\n",(int)IoAddress);
			return -1;
		}
	}
	printk("PCI: Invalid I/O Address 0x%08X\n",(int)IoAddress);
	return -1;
}
/************************************************************************/
/* Returns the iSeries bus value                                        */
/************************************************************************/
u8   iSeries_Get_Bus(struct pci_dev* DevPtr) {
    return iSeries_GlobalBusMap[DevPtr->bus->number][_HVBUSNUMBER_];
}
/************************************************************************/
/* Returns the iSeries subbus value                                     */
/************************************************************************/
u8   iSeries_Get_SubBus(struct pci_dev* DevPtr) {
    u8  SubBus = iSeries_GlobalBusMap[DevPtr->bus->number][_HVSUBBUSNUMBER_];
    if     (SubBus == 0xFF) SubBus = 0xFF;
    else if(SubBus == 0)    SubBus = ISERIES_DEVFN_DECODE_SUBBUS(DevPtr->devfn);
    else                    SubBus = ISERIES_GET_LPAR_SUBBUS(DevPtr->bus->number);
    return SubBus;
}
/************************************************************************/
/* Returns the iSeries Device and Function Number                       */
/************************************************************************/
u8   iSeries_Get_DevFn(struct pci_dev* DevPtr) {
    u8  SubBus = iSeries_GlobalBusMap[DevPtr->bus->number][_HVSUBBUSNUMBER_];
    u8  DevFn;
    if     (SubBus == 0xFF) DevFn  = 0;
    else if(SubBus == 0)    DevFn  = 0x10 | ISERIES_DECODE_FUNCTION(DevPtr->devfn);
    else                    DevFn  = ISERIES_44_FORMAT(DevPtr->devfn);
    return DevFn;
}

/************************************************************************/
/* This is provides the mapping from the Linux bus and devfn to ISeries */
/* Bus and Subbus.                                                      */
/* Initialize to all FFs, translations will fail if FF entry found.     */
/************************************************************************/
u8 iSeries_GlobalBusMap[256][2];	/* Global Bus Mapping           */	
void __init iSeries_Initialize_GlobalBusMap(void) {
    int Index;
    for(Index = 0; Index < 256; ++Index) {
	iSeries_GlobalBusMap[Index][_HVBUSNUMBER_]    = 0xFF;
	iSeries_GlobalBusMap[Index][_HVSUBBUSNUMBER_] = 0xFF;
    }
    ISERIES_PCI_FR("IntGlobalBusMap");
}
/************************************************************************/
/* Create the Flight Recorder                                           */
/************************************************************************/
FlightRecorder  PciFlightRecorder;		      /* Pci Flight Recorder  */
FlightRecorder* PciFr;				           /* Pointer to Fr        */
char            PciFrBufferData[128];		      /* Working buffer       */
char*           PciFrBuffer;			      /* Pointer to buffer    */
int             PciTraceFlag = 0;               /* Trace Level Flag     */
struct pci_dev* PciDeviceTrace;                 /* Device Tracing       */

void __init iSeries_Initialize_FlightRecorder(void) {
    PciFr       = &PciFlightRecorder;
    PciFrBuffer = &PciFrBufferData[0];
    iSeries_Fr_Initialize(PciFr, "PciFlightRecord");
    ISERIES_PCI_FR("August 10,2001.");
    PciTraceFlag = 0;
}
/************************************************************************/
/* Function to turn on and off the Flight Recorder Trace Flag           */
/************************************************************************/
int  iSeries_Set_PciTraceFlag(int TraceFlag) {
    int TempFlag = PciTraceFlag;
    PciTraceFlag = TraceFlag;
    return TempFlag;
}
int  iSeries_Get_PciTraceFlag(void) {
    return PciTraceFlag;
}
void iSeries_Set_PciFilter(struct pci_dev* PciDevice) {
    PciDeviceTrace = PciDevice;
}
/************************************************************************/
/* Initialize the I/O Tables and maps                                   */  
/************************************************************************/
void __init iSeries_pci_Initialize(void) {
    iSeries_Initialize_FlightRecorder();	     /* Flight Recorder 1st. */
    iSeries_Initialize_GlobalBusMap();		/* Global Bus Map       */
    iSeries_IoMmTable_Initialize();		     /* Io Translate table.  */
    iSeries_proc_callback(&iSeries_pci_proc_init);
    iSeries_Set_PciErpFlag(4);			     /* Erp Level set to 4   */
}    

/************************************************************************/
/* Erp level when PCI I/O Errors are detected.                          */
/*  0 =  Be quiet about the errors.                                     */
/*  1 >= Log error information and continue on.                         */
/*  2 =  Printk the error information and continue on.                  */
/*  3 =  Printk the error information and put task to sleep.            */
/*  4 =  Printk the error information and panic the kernel with no reboo*/        
/************************************************************************/
int        iSeries_pci_ErpLevel = 0;
/************************************************************************/
/* Allows clients to set the Erp State                                  */
/************************************************************************/
int  iSeries_Set_PciErpFlag(int ErpFlag) {
    int SavedState = iSeries_pci_ErpLevel;
    printk("PCI: PciERPFlag set to %d.\n", ErpFlag);
    iSeries_pci_ErpLevel = ErpFlag;
    return SavedState;
}
extern int panic_timeout;		       /* Panic Timeout reference      */
/************************************************************************/
/* Fatal I/O Error, crash the system.                                   */
/* PCI: Fatal I/O Error, Device 00/00, Error 0x0000, Status Reg 0x0000  */
/* PCI: Kernel Panic called with reboot disabled.                       */
/************************************************************************/
void iSeries_pci_IoError(char* OpCode,iSeries_Device* Device) {
    char            DeviceInfo[128];
    char            ErrorInfo[128];
    struct pci_dev* PciDev = Device->PciDevPtr;

    sprintf(ErrorInfo,"I/O Error Detected.  Op: %s, Bus%3d, Device%3d  Error: 0x%04X\n",
	                  OpCode,PciDev->bus->number, PCI_SLOT(PciDev->devfn),Device->RCode);
    iSeries_Device_Information(PciDev,DeviceInfo,128);

    /* Log the error in the flight recorder                            */
    if(iSeries_pci_ErpLevel > 0) {
	if( Device->RCode != 0x0102) {		/* Previous error      */
	    ISERIES_PCI_FR(ErrorInfo);
	    ISERIES_PCI_FR(DeviceInfo);
	}
    }
    if(iSeries_pci_ErpLevel > 1) {
	printk("PCI: %s",ErrorInfo);
	printk("PCI: %s\n",DeviceInfo);
    }
    if(iSeries_pci_ErpLevel == 3) {
	printk("PCI: Current process 0x%08X put to sleep for debug.\n",current->pid);
	{
	    DECLARE_WAITQUEUE(WaitQueue, current);
	    add_wait_queue(&current->wait_chldexit,&WaitQueue);
	    current->state = TASK_INTERRUPTIBLE;
	    schedule();
	    current->state = TASK_RUNNING;
	    remove_wait_queue(&current->wait_chldexit,&WaitQueue);
	}
    }
    else if(iSeries_pci_ErpLevel == 4) {
	mf_displaySrc(0xB6000103);
	panic_timeout = 0; /* Don't reboot. */
	/* printk("PCI: Hardware I/O Error, SRC B6000103, Kernel Panic called.\n");*/
	/* panic("Automatic Reboot Disabled.") */
	panic("PCI: Hardware I/O Error, SRC B6000103, Automatic Reboot Disabled.");
    }
}

/************************************************************************/
/* I/0 Memory copy MUST use mmio commands on iSeries                    */
/************************************************************************/
void* iSeries_memcpy_toio(void *dest, void *source, int n)
{
    char *dst = dest;
    char *src = source;

    while (n--) {
	writeb (*src++, dst++);
    }

    return dest;
}

void* iSeries_memcpy_fromio(void *dest, void *source, int n)
{
    char *dst = dest;
    char *src = source;

    while (n--) 
	*dst++ = readb (src++);

    return dest;
}
