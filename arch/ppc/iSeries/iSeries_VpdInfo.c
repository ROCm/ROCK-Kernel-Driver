/************************************************************************/
/* File iSeries_vpdInfo.c created by Allan Trautman on Fri Feb  2 2001. */
/************************************************************************/
/* This code gets the card location of the hardware                     */
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
/*   Created, Feb 2, 2001                                               */
/* End Change Activity                                                  */
/************************************************************************/
#include <linux/config.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/types.h>
#include <asm/resource.h>

#include <asm/iSeries/HvCallPci.h>
#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/mf.h>
#include <asm/iSeries/LparData.h>
#include <asm/iSeries/HvCallPci.h>
#include <asm/iSeries/iSeries_VpdInfo.h>
#include "iSeries_pci.h"

/************************************************/
/* Size of Bus VPD data                         */
/************************************************/
#define BUS_VPDSIZE      1024
/************************************************/
/* Bus Vpd Tags                                 */
/************************************************/
#define  VpdEndOfDataTag   0x78
#define  VpdEndOfAreaTag   0x79
#define  VpdIdStringTag    0x82
#define  VpdVendorAreaTag  0x84
/************************************************/
/* Mfg Area Tags                                */
/************************************************/
#define  VpdAsmPartNumber 0x504E     // "PN"
#define  VpdFruFlag       0x4647     // "FG"
#define  VpdFruLocation   0x464C     // "FL"
#define  VpdFruFrameId    0x4649     // "FI"
#define  VpdFruPartNumber 0x464E     // "FN"
#define  VpdFruPowerData  0x5052     // "PR"
#define  VpdFruSerial     0x534E     // "SN"
#define  VpdFruType       0x4343     // "CC"
#define  VpdFruCcinExt    0x4345     // "CE"
#define  VpdFruRevision   0x5256     // "RV"
#define  VpdSlotMapFormat 0x4D46     // "MF"
#define  VpdSlotMap       0x534D     // "SM"

/************************************************/
/* Structures of the areas                      */
/************************************************/
struct BusVpdAreaStruct {
    u8  Tag;
    u8  LowLength;
    u8  HighLength;
};
typedef struct BusVpdAreaStruct BusVpdArea;
#define BUS_ENTRY_SIZE   3

struct MfgVpdAreaStruct {
    u16 Tag;
    u8  TagLength;
};
typedef struct MfgVpdAreaStruct MfgVpdArea;
#define MFG_ENTRY_SIZE   3

struct SlotMapFormatStruct {
    u16 Tag;
    u8  TagLength;
    u16 Format;
};

struct FrameIdMapStruct{
    u16 Tag;
    u8  TagLength;
    u8  FrameId;
};
typedef struct FrameIdMapStruct FrameIdMap;

struct SlotMapStruct {
    u8   AgentId;
    u8   SecondaryAgentId;
    u8   PhbId;
    char CardLocation[3];
    char Parms[8];
    char Reserved[2];
};
typedef struct SlotMapStruct SlotMap;
#define SLOT_ENTRY_SIZE   16

/****************************************************************/
/* Prototypes                                                   */
/****************************************************************/
static void  iSeries_Parse_Vpd(BusVpdArea*,    int, LocationData*);
static void  iSeries_Parse_MfgArea(MfgVpdArea*,int, LocationData*);
static void  iSeries_Parse_SlotArea(SlotMap*,  int, LocationData*);
static void  iSeries_Parse_PhbId(BusVpdArea*,  int, LocationData*);

/****************************************************************/
/*                                                              */
/*                                                              */
/*                                                              */
/****************************************************************/
LocationData* iSeries_GetLocationData(struct pci_dev* PciDev) {
    LocationData* LocationPtr = 0;
    BusVpdArea*   BusVpdPtr   = 0;
    int           BusVpdLen   = 0;
    BusVpdPtr  = (BusVpdArea*)kmalloc(BUS_VPDSIZE, GFP_KERNEL);
    BusVpdLen  = HvCallPci_getBusVpd(ISERIES_GET_LPAR_BUS(PciDev->bus->number),REALADDR(BusVpdPtr),BUS_VPDSIZE);
    /* printk("PCI: VpdBuffer 0x%08X \n",(int)BusVpdPtr);          */
    /***************************************************************/
    /* Need to set Agent Id, Bus in location info before the call  */
    /***************************************************************/
    LocationPtr = (LocationData*)kmalloc(LOCATION_DATA_SIZE, GFP_KERNEL);
    LocationPtr->Bus              = ISERIES_GET_LPAR_BUS(PciDev->bus->number);
    LocationPtr->Board            = 0;
    LocationPtr->FrameId          = 0;
    iSeries_Parse_PhbId(BusVpdPtr,BusVpdLen,LocationPtr);
    LocationPtr->Card             = PCI_SLOT(PciDev->devfn);
    strcpy(LocationPtr->CardLocation,"Cxx");
    LocationPtr->AgentId          = ISERIES_DECODE_DEVICE(PciDev->devfn);
    LocationPtr->SecondaryAgentId = 0x10;
    /* And for Reference only.                        */
    LocationPtr->LinuxBus         = PciDev->bus->number;
    LocationPtr->LinuxDevFn       = PciDev->devfn;
    /***************************************************************/
    /* Any data to process?                                        */
    /***************************************************************/
    if(BusVpdLen > 0) {
	iSeries_Parse_Vpd(BusVpdPtr, BUS_VPDSIZE, LocationPtr);
    }
    else {
	ISERIES_PCI_FR("No VPD Data....");
    }
    kfree(BusVpdPtr);

    return LocationPtr;
}
/****************************************************************/
/*                                                              */
/****************************************************************/
void  iSeries_Parse_Vpd(BusVpdArea* VpdData,int VpdLen, LocationData* LocationPtr) {
    MfgVpdArea* MfgVpdPtr = 0;
    int         BusTagLen = 0;
    BusVpdArea* BusVpdPtr = VpdData;
    int         BusVpdLen = VpdLen;
    /*************************************************************/
    /* Make sure this is what I think it is                      */
    /*************************************************************/
    if(BusVpdPtr->Tag != VpdIdStringTag) {            /*0x82     */
	ISERIES_PCI_FR("Not 0x82 start.");
	return;
    }
    BusTagLen  = (BusVpdPtr->HighLength*256)+BusVpdPtr->LowLength;
    BusTagLen += BUS_ENTRY_SIZE;
    BusVpdPtr  = (BusVpdArea*)((u32)BusVpdPtr+BusTagLen);
    BusVpdLen -= BusTagLen;
    /*************************************************************/
    /* Parse the Data                                            */
    /*************************************************************/
    while(BusVpdLen > 0 ) {
	BusTagLen = (BusVpdPtr->HighLength*256)+BusVpdPtr->LowLength;
	/*********************************************************/
	/* End of data Found                                     */
	/*********************************************************/
	if(BusVpdPtr->Tag == VpdEndOfAreaTag) {
	    BusVpdLen = 0;		/* Done, Make sure       */
	}
	/*********************************************************/
	/* Was Mfg Data Found                                    */
	/*********************************************************/
	else if(BusVpdPtr->Tag == VpdVendorAreaTag) {
	    MfgVpdPtr = (MfgVpdArea*)((u32)BusVpdPtr+BUS_ENTRY_SIZE);
	    iSeries_Parse_MfgArea(MfgVpdPtr,BusTagLen,LocationPtr);
	}
	/********************************************************/
	/* On to the next tag.                                  */
	/********************************************************/
	if(BusVpdLen > 0) {
	    BusTagLen += BUS_ENTRY_SIZE;
	    BusVpdPtr  = (BusVpdArea*)((u32)BusVpdPtr+BusTagLen);
	    BusVpdLen -= BusTagLen;
	}
    }
}    

/*****************************************************************/
/* Parse the Mfg Area                                            */
/*****************************************************************/
void  iSeries_Parse_MfgArea(MfgVpdArea* VpdDataPtr,int VpdLen, LocationData* LocationPtr) {
    SlotMap*    SlotMapPtr = 0;
    u16         SlotMapFmt = 0;
    int         MfgTagLen  = 0;
    MfgVpdArea* MfgVpdPtr  = VpdDataPtr;
    int         MfgVpdLen  = VpdLen;

    /*************************************************************/
    /* Parse Mfg Data                                            */
    /*************************************************************/
    while(MfgVpdLen > 0) {
	MfgTagLen = MfgVpdPtr->TagLength;
	if     (MfgVpdPtr->Tag == VpdFruFlag) {}	  /* FG  */
	else if(MfgVpdPtr->Tag == VpdFruSerial) {}	  /* SN  */
	else if(MfgVpdPtr->Tag == VpdAsmPartNumber){}     /* PN  */
	/*********************************************************/
	/* Frame ID                                              */
	/*********************************************************/
	if(MfgVpdPtr->Tag == VpdFruFrameId) {	        /* FI    */
	    LocationPtr->FrameId = ((FrameIdMap*)MfgVpdPtr)->FrameId;
	}
	/*********************************************************/
	/* Slot Map Format                                       */
	/*********************************************************/
	else if(MfgVpdPtr->Tag == VpdSlotMapFormat){       /* MF */
	    SlotMapFmt = ((struct SlotMapFormatStruct*)MfgVpdPtr)->Format;
        }  
	/*********************************************************/
	/* Slot Labels                                           */
	/*********************************************************/
	else if(MfgVpdPtr->Tag == VpdSlotMap){             /* SM */
 	    if(SlotMapFmt == 0x1004) SlotMapPtr = (SlotMap*)((u32)MfgVpdPtr+MFG_ENTRY_SIZE+1);
 	    else                     SlotMapPtr = (SlotMap*)((u32)MfgVpdPtr+MFG_ENTRY_SIZE);
	    iSeries_Parse_SlotArea(SlotMapPtr,MfgTagLen, LocationPtr);
	}
	/*********************************************************/
	/* Point to the next Mfg Area                            */
	/* Use defined size, sizeof give wrong answer            */
	/*********************************************************/
	MfgTagLen += MFG_ENTRY_SIZE;
	MfgVpdPtr  = (MfgVpdArea*)( (u32)MfgVpdPtr + MfgTagLen);
	MfgVpdLen -= MfgTagLen;  
    }	
}
/*****************************************************************/
/* Look for "BUS" Tag to set the PhbId.                          */
/*****************************************************************/
void  iSeries_Parse_PhbId(BusVpdArea* VpdData,int VpdLen,LocationData* LocationPtr) {
    int   PhbId   = 0xff;                    /* Not found flag   */
    char* PhbPtr  = (char*)VpdData+3;        /* Skip over 82 tag */
    int   DataLen = VpdLen;
    while(DataLen > 0) {
        if(*PhbPtr == 'B' && *(PhbPtr+1) == 'U' && *(PhbPtr+2) == 'S') {
            if(*(PhbPtr+3) == ' ') PhbPtr += 4;/* Skip white spac*/	
            else                   PhbPtr += 3;
            if     (*PhbPtr == '0') PhbId = 0; /* Don't convert, */  
            else if(*PhbPtr == '1') PhbId = 1; /* Sanity check   */
            else if(*PhbPtr == '2') PhbId = 2; /*  values        */ 
            DataLen = 0;                       /* Exit loop.     */ 
        }
    ++PhbPtr;
    --DataLen;
    }
    LocationPtr->PhbId = PhbId;
}
/*****************************************************************/
/* Parse the Slot Area                                           */
/*****************************************************************/
void  iSeries_Parse_SlotArea(SlotMap* MapPtr,int MapLen, LocationData* LocationPtr) {
    int      SlotMapLen = MapLen;
    SlotMap* SlotMapPtr = MapPtr;
    /*************************************************************/
    /* Parse Slot label until we find the one requrested         */
    /*************************************************************/
    while(SlotMapLen > 0) {
	if(SlotMapPtr->AgentId          == LocationPtr->AgentId && 
	   SlotMapPtr->SecondaryAgentId == LocationPtr->SecondaryAgentId) {
	    /*****************************************************/
            /* If Phb wasn't found, grab the first one found.    */ 
	    /*****************************************************/
            if(LocationPtr->PhbId == 0xff) LocationPtr->PhbId = SlotMapPtr->PhbId; 
            if( SlotMapPtr->PhbId == LocationPtr->PhbId ) {
	    /*****************************************************/
	    /* Found what we were looking for, extract the data. */
	    /*****************************************************/
	        memcpy(&LocationPtr->CardLocation,&SlotMapPtr->CardLocation,3);
	        LocationPtr->CardLocation[3]  = 0;  /* Null terminate*/
	        SlotMapLen = 0;			/* We are done   */
            }
	}
	/*********************************************************/
	/* Point to the next Slot                                */
	/* Use defined size, sizeof may give wrong answer        */
	/*********************************************************/
	SlotMapLen -= SLOT_ENTRY_SIZE;
	SlotMapPtr = (SlotMap*)((u32)SlotMapPtr+SLOT_ENTRY_SIZE);
    }	
}
/************************************************************************/
/* Formats the device information.                                      */
/* - Pass in pci_dev* pointer to the device.                            */
/* - Pass in buffer to place the data.  Danger here is the buffer must  */
/*   be as big as the client says it is.   Should be at least 128 bytes.*/
/* Return will the length of the string data put in the buffer.         */
/* Format:                                                              */
/* PCI: Bus  0, Device 26, Vendor 0x12AE  iSeries: Bus  2, Device 34, Fr*/
/* ame  1, Card  C10  Ethernet controller                               */
/************************************************************************/
int  iSeries_Device_Information(struct pci_dev* PciDev,char* Buffer, int BufferSize) {
    LocationData*  LocationPtr;		/* VPD Information             */
    char*          BufPtr  = Buffer;
    int            LineLen = 0;
    if(BufferSize >= 128) {
	LineLen =  sprintf(BufPtr+LineLen,"PCI: Bus%3d, Device%3d, Vendor %04X  ",
			   PciDev->bus->number, PCI_SLOT(PciDev->devfn),PciDev->vendor);

	LocationPtr = iSeries_GetLocationData(PciDev);
	LineLen += sprintf(BufPtr+LineLen,"iSeries: Bus%3d, Device%3d, Frame%3d, Card %4s  ",
			   LocationPtr->Bus,LocationPtr->AgentId,
			   LocationPtr->FrameId,LocationPtr->CardLocation);
	kfree(LocationPtr);

	if(pci_class_name(PciDev->class >> 8) == 0) {
	    LineLen += sprintf(BufPtr+LineLen,"0x%04X  ",(int)(PciDev->class >> 8));
	}
	else {
	    LineLen += sprintf(BufPtr+LineLen,"%s",pci_class_name(PciDev->class >> 8) );
	}
    }
    return LineLen;
}
