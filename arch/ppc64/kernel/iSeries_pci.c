/*
 * iSeries_pci.c
 *
 * Copyright (C) 2001 Allan Trautman, IBM Corporation
 *
 * iSeries specific routines for PCI.
 * 
 * Based on code from pci.c and iSeries_pci.c 32bit
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
#include <linux/kernel.h>
#include <linux/list.h> 
#include <linux/string.h>
#include <linux/init.h>
#include <linux/ide.h>
#include <linux/pci.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/ppcdebug.h>
#include <asm/naca.h>
#include <asm/flight_recorder.h>
#include <asm/pci_dma.h>

#include <asm/iSeries/HvCallPci.h>
#include <asm/iSeries/HvCallSm.h>
#include <asm/iSeries/HvCallXm.h>
#include <asm/iSeries/LparData.h>
#include <asm/iSeries/iSeries_irq.h>
#include <asm/iSeries/iSeries_pci.h>
#include <asm/iSeries/mf.h>

#include "iSeries_IoMmTable.h"
#include "pci.h"

extern struct pci_controller* hose_head;
extern struct pci_controller** hose_tail;
extern int    global_phb_number;
extern int    panic_timeout;

extern struct device_node *allnodes;
extern unsigned long iSeries_Base_Io_Memory;    

extern struct pci_ops iSeries_pci_ops;
extern struct flightRecorder* PciFr;
extern struct TceTable* tceTables[256];

/*******************************************************************
 * Counters and control flags. 
 *******************************************************************/
extern long   Pci_Io_Read_Count;
extern long   Pci_Io_Write_Count;
extern long   Pci_Cfg_Read_Count;
extern long   Pci_Cfg_Write_Count;
extern long   Pci_Error_Count;

extern int    Pci_Retry_Max;	
extern int    Pci_Error_Flag;
extern int    Pci_Trace_Flag;

extern void iSeries_MmIoTest(void);


/*******************************************************************
 * Forward declares of prototypes. 
 *******************************************************************/
struct iSeries_Device_Node* find_Device_Node(struct pci_dev* PciDev);
struct iSeries_Device_Node* get_Device_Node(struct pci_dev* PciDev);

unsigned long find_and_init_phbs(void);
struct        pci_controller* alloc_phb(struct device_node *dev, char *model, unsigned int addr_size_words) ;

void  iSeries_Scan_PHBs_Slots(struct pci_controller* Phb);
void  iSeries_Scan_EADs_Bridge(HvBusNumber Bus, HvSubBusNumber SubBus, int IdSel);
int   iSeries_Scan_Bridge_Slot(HvBusNumber Bus, struct HvCallPci_BridgeInfo* Info);
void  list_device_nodes(void);

struct pci_dev;

LIST_HEAD(iSeries_Global_Device_List);

int DeviceCount = 0;


/* Counters and control flags. */
static long   Pci_Io_Read_Count  = 0;
static long   Pci_Io_Write_Count = 0;
static long   Pci_Cfg_Read_Count = 0;
static long   Pci_Cfg_Write_Count= 0;
static long   Pci_Error_Count    = 0;

static int    Pci_Retry_Max      = 3;	/* Only retry 3 times  */	
static int    Pci_Error_Flag     = 1;	/* Set Retry Error on. */
static int    Pci_Trace_Flag     = 0;


/**********************************************************************************
 * Log Error infor in Flight Recorder to system Console.
 * Filter out the device not there errors.
 * PCI: EADs Connect Failed 0x18.58.10 Rc: 0x00xx
 * PCI: Read Vendor Failed 0x18.58.10 Rc: 0x00xx
 * PCI: Connect Bus Unit Failed 0x18.58.10 Rc: 0x00xx
 **********************************************************************************/
void  pci_Log_Error(char* Error_Text, int Bus, int SubBus, int AgentId, int HvRc)
{
	if( HvRc != 0x0302) { 
		char ErrorString[128];
		sprintf(ErrorString,"%s Failed: 0x%02X.%02X.%02X Rc: 0x%04X",Error_Text,Bus,SubBus,AgentId,HvRc);
		PCIFR(ErrorString);
		printk("PCI: %s\n",ErrorString);
	}
}

/**********************************************************************************
 * Dump the iSeries Temp Device Node 
 *<4>buswalk [swapper : - DeviceNode: 0xC000000000634300
 *<4>00. Device Node   = 0xC000000000634300
 *<4>    - PciDev      = 0x0000000000000000
 *<4>    - tDevice     = 0x  17:01.00  0x1022 00
 *<4>  4. Device Node = 0xC000000000634480
 *<4>     - PciDev    = 0x0000000000000000
 *<4>     - Device    = 0x  18:38.16 Irq:0xA7 Vendor:0x1014  Flags:0x00
 *<4>     - Devfn     = 0xB0: 22.18
 **********************************************************************************/
void dumpDevice_Node(struct iSeries_Device_Node* DevNode)
{
	udbg_printf("Device Node      = 0x%p\n",DevNode);
	udbg_printf("     - PciDev    = 0x%p\n",DevNode->PciDev);
	udbg_printf("     - Device    = 0x%4X:%02X.%02X (0x%02X)\n",
		    ISERIES_BUS(DevNode),
		    ISERIES_SUBBUS(DevNode),
		    DevNode->AgentId,
		    DevNode->DevFn);
	udbg_printf("     - LSlot     = 0x%02X\n",DevNode->LogicalSlot);
	udbg_printf("     - TceTable  = 0x%p\n  ",DevNode->DevTceTable);

	udbg_printf("     - DSA       = 0x%04X\n",ISERIES_DSA(DevNode)>>32 );

	udbg_printf("                 = Irq:0x%02X Vendor:0x%04X  Flags:0x%02X\n",
		    DevNode->Irq,
		    DevNode->Vendor,
		    DevNode->Flags );
	udbg_printf("     - Location  = %s\n",DevNode->CardLocation);


}
/**********************************************************************************
 * Walk down the device node chain 
 **********************************************************************************/
void  list_device_nodes(void)
{
	struct list_head* Device_Node_Ptr = iSeries_Global_Device_List.next;
	while(Device_Node_Ptr != &iSeries_Global_Device_List) {
		dumpDevice_Node( (struct iSeries_Device_Node*)Device_Node_Ptr );
		Device_Node_Ptr = Device_Node_Ptr->next;
	}
}
	

/***********************************************************************
 * build_device_node(u16 Bus, int SubBus, u8 DevFn)
 *
 ***********************************************************************/
struct iSeries_Device_Node* build_device_node(HvBusNumber Bus, HvSubBusNumber  SubBus, int AgentId, int Function)
{
	struct iSeries_Device_Node*  DeviceNode;

	PPCDBG(PPCDBG_BUSWALK,"-build_device_node 0x%02X.%02X.%02X Function: %02X\n",Bus,SubBus,AgentId, Function);

	DeviceNode = kmalloc(sizeof(struct iSeries_Device_Node), GFP_KERNEL);
	if(DeviceNode == NULL) return NULL;

	memset(DeviceNode,0,sizeof(struct iSeries_Device_Node) );
	list_add_tail(&DeviceNode->Device_List,&iSeries_Global_Device_List);
	/*DeviceNode->DsaAddr      = ((u64)Bus<<48)+((u64)SubBus<<40)+((u64)0x10<<32); */
	ISERIES_BUS(DeviceNode)       = Bus;
	ISERIES_SUBBUS(DeviceNode)    = SubBus;
	DeviceNode->DsaAddr.deviceId  = 0x10;
        DeviceNode->DsaAddr.barNumber = 0;
	DeviceNode->AgentId           = AgentId;
	DeviceNode->DevFn             = PCI_DEVFN(ISERIES_ENCODE_DEVICE(AgentId),Function );
	DeviceNode->IoRetry           = 0;
	iSeries_Get_Location_Code(DeviceNode);
	PCIFR("Device 0x%02X.%2X, Node:0x%p ",ISERIES_BUS(DeviceNode),ISERIES_DEVFUN(DeviceNode),DeviceNode);
	return DeviceNode;
}
/****************************************************************************
* 
* Allocate pci_controller(phb) initialized common variables. 
* 
*****************************************************************************/
struct pci_controller* pci_alloc_pci_controllerX(char *model, enum phb_types controller_type)
{
	struct pci_controller *hose;
	hose = (struct pci_controller*)kmalloc(sizeof(struct pci_controller), GFP_KERNEL);
	if(hose == NULL) return NULL;

	memset(hose, 0, sizeof(struct pci_controller));
	if(strlen(model) < 8) strcpy(hose->what,model);
	else                  memcpy(hose->what,model,7);
	hose->type = controller_type;
	hose->global_number = global_phb_number;
	global_phb_number++;

	*hose_tail = hose;
	hose_tail = &hose->next;
	return hose;
}

/****************************************************************************
 *
 * unsigned int __init find_and_init_phbs(void)
 *
 * Description:
 *   This function checks for all possible system PCI host bridges that connect
 *   PCI buses.  The system hypervisor is queried as to the guest partition
 *   ownership status.  A pci_controller is build for any bus which is partially
 *   owned or fully owned by this guest partition.
 ****************************************************************************/
unsigned long __init find_and_init_phbs(void)
{
	struct      pci_controller* phb;
	HvBusNumber BusNumber;

	PPCDBG(PPCDBG_BUSWALK,"find_and_init_phbs Entry\n");

	/* Check all possible buses. */
	for (BusNumber = 0; BusNumber < 256; BusNumber++) {
		int RtnCode = HvCallXm_testBus(BusNumber);
		if (RtnCode == 0) {
			phb = pci_alloc_pci_controllerX("PHB HV", phb_type_hypervisor);
			if(phb == NULL) {
				printk("PCI: Allocate pci_controller failed.\n");
				PCIFR(      "Allocate pci_controller failed.");
				return -1;
			}
			phb->pci_mem_offset = phb->local_number = BusNumber;
			phb->first_busno  = BusNumber;
			phb->last_busno   = BusNumber;
			phb->ops          = &iSeries_pci_ops;

			PPCDBG(PPCDBG_BUSWALK, "PCI:Create iSeries pci_controller(%p), Bus: %04X\n",phb,BusNumber);
			PCIFR("Create iSeries PHB controller: %04X",BusNumber);

			/***************************************************/
			/* Find and connect the devices.                   */
			/***************************************************/
			iSeries_Scan_PHBs_Slots(phb);
		}
		/* Check for Unexpected Return code, a clue that something */
		/* has gone wrong.                                         */
		else if(RtnCode != 0x0301) {
			PCIFR("Unexpected Return on Probe(0x%04X): 0x%04X",BusNumber,RtnCode);
		}

	}
	return 0;
}
/*********************************************************************** 
 * iSeries_pcibios_init
 *  
 * Chance to initialize and structures or variable before PCI Bus walk.
 *  
 *<4>buswalk [swapper : iSeries_pcibios_init Entry.
 *<4>buswalk [swapper : IoMmTable Initialized 0xC00000000034BD30
 *<4>buswalk [swapper : find_and_init_phbs Entry
 *<4>buswalk [swapper : Create iSeries pci_controller:(0xC00000001F5C7000), Bus 0x0017
 *<4>buswalk [swapper : Connect EADs: 0x17.00.12 = 0x00
 *<4>buswalk [swapper : iSeries_assign_IRQ   0x0017.00.12 = 0x0091
 *<4>buswalk [swapper : - allocate and assign IRQ 0x17.00.12 = 0x91
 *<4>buswalk [swapper : - FoundDevice: 0x17.28.10 = 0x12AE
 *<4>buswalk [swapper : - build_device_node 0x17.28.12
 *<4>buswalk [swapper : iSeries_pcibios_init Exit.
 ***********************************************************************/
void iSeries_pcibios_init(void)
{
	PPCDBG(PPCDBG_BUSWALK,"iSeries_pcibios_init Entry.\n"); 

	iSeries_IoMmTable_Initialize();

	find_and_init_phbs();

	pci_assign_all_busses = 0;
	PPCDBG(PPCDBG_BUSWALK,"iSeries_pcibios_init Exit.\n"); 
}
/***********************************************************************
 * pcibios_final_fixup(void)  
 ***********************************************************************/
void __init pcibios_final_fixup(void)
{
	struct pci_dev* PciDev = NULL;
	struct iSeries_Device_Node* DeviceNode;
	char   Buffer[256];
    	int    DeviceCount = 0;

	PPCDBG(PPCDBG_BUSWALK,"iSeries_pcibios_fixup Entry.\n"); 
	/******************************************************/
	/* Fix up at the device node and pci_dev relationship */
	/******************************************************/
	mf_displaySrc(0xC9000100);

	while ((PciDev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, PciDev)) != NULL) {
		DeviceNode = find_Device_Node(PciDev);
		if(DeviceNode != NULL) {
			++DeviceCount;
			PciDev->sysdata    = (void*)DeviceNode;
			DeviceNode->PciDev = PciDev;

			PPCDBG(PPCDBG_BUSWALK,"PciDev 0x%p <==> DevNode 0x%p\n",PciDev,DeviceNode );

			iSeries_allocateDeviceBars(PciDev);

			iSeries_Device_Information(PciDev,Buffer, sizeof(Buffer) );
			printk("%d. %s\n",DeviceCount,Buffer);

			create_pci_bus_tce_table((unsigned long)DeviceNode);
		} else {
			printk("PCI: Device Tree not found for 0x%016lX\n",(unsigned long)PciDev);
		}
	}
	iSeries_IoMmTable_Status();

	iSeries_activate_IRQs();

	mf_displaySrc(0xC9000200);
}

void pcibios_fixup_bus(struct pci_bus* PciBus)
{
	PPCDBG(PPCDBG_BUSWALK,"iSeries_pcibios_fixup_bus(0x%04X) Entry.\n",PciBus->number); 

}


/***********************************************************************
 * pcibios_fixup_resources(struct pci_dev *dev) 
 *	
 ***********************************************************************/
void pcibios_fixup_resources(struct pci_dev *PciDev)
{
	PPCDBG(PPCDBG_BUSWALK,"pcibios_fixup_resources PciDev %p\n",PciDev);
}   


/********************************************************************************
* Loop through each node function to find usable EADs bridges.  
*********************************************************************************/
void  iSeries_Scan_PHBs_Slots(struct pci_controller* Phb)
{
	struct HvCallPci_DeviceInfo* DevInfo;
	HvBusNumber    Bus       = Phb->local_number;       /* System Bus        */	
	HvSubBusNumber SubBus    = 0;                       /* EADs is always 0. */
	int            HvRc      = 0;
	int            IdSel     = 1;	
	int            MaxAgents = 8;

	DevInfo    = (struct HvCallPci_DeviceInfo*)kmalloc(sizeof(struct HvCallPci_DeviceInfo), GFP_KERNEL);
	if(DevInfo == NULL) return;

	/********************************************************************************
	 * Probe for EADs Bridges      
	 ********************************************************************************/
	for (IdSel=1; IdSel < MaxAgents; ++IdSel) {
    		HvRc = HvCallPci_getDeviceInfo(Bus, SubBus, IdSel,REALADDR(DevInfo), sizeof(struct HvCallPci_DeviceInfo));
		if (HvRc == 0) {
			if(DevInfo->deviceType == HvCallPci_NodeDevice) {
				iSeries_Scan_EADs_Bridge(Bus, SubBus, IdSel);
			}
			else printk("PCI: Invalid System Configuration(0x%02X.\n",DevInfo->deviceType);
		}
		else pci_Log_Error("getDeviceInfo",Bus, SubBus, IdSel,HvRc);
	}
	kfree(DevInfo);
}

/********************************************************************************
* 
*********************************************************************************/
void  iSeries_Scan_EADs_Bridge(HvBusNumber Bus, HvSubBusNumber SubBus, int IdSel)
{
	struct HvCallPci_BridgeInfo* BridgeInfo;
	HvAgentId      AgentId;
	int            Function;
	int            HvRc;

	BridgeInfo = (struct HvCallPci_BridgeInfo*)kmalloc(sizeof(struct HvCallPci_BridgeInfo), GFP_KERNEL);
	if(BridgeInfo == NULL) return;

	/*********************************************************************
	 * Note: hvSubBus and irq is always be 0 at this level!
	 *********************************************************************/
	for (Function=0; Function < 8; ++Function) {
	  	AgentId = ISERIES_PCI_AGENTID(IdSel, Function);
		HvRc = HvCallXm_connectBusUnit(Bus, SubBus, AgentId, 0);
 		if (HvRc == 0) {
  			/*  Connect EADs: 0x18.00.12 = 0x00 */
			PPCDBG(PPCDBG_BUSWALK,"PCI:Connect EADs: 0x%02X.%02X.%02X\n",Bus, SubBus, AgentId);
			PCIFR(                    "Connect EADs: 0x%02X.%02X.%02X",  Bus, SubBus, AgentId);
	    		HvRc = HvCallPci_getBusUnitInfo(Bus, SubBus, AgentId, 
			                                REALADDR(BridgeInfo), sizeof(struct HvCallPci_BridgeInfo));
	 		if (HvRc == 0) {
				PPCDBG(PPCDBG_BUSWALK,"PCI: BridgeInfo, Type:0x%02X, SubBus:0x%02X, MaxAgents:0x%02X, MaxSubBus: 0x%02X, LSlot: 0x%02X\n",
				       BridgeInfo->busUnitInfo.deviceType,
				       BridgeInfo->subBusNumber,
				       BridgeInfo->maxAgents,
				       BridgeInfo->maxSubBusNumber,
				       BridgeInfo->logicalSlotNumber);
				PCIFR(                     "BridgeInfo, Type:0x%02X, SubBus:0x%02X, MaxAgents:0x%02X, MaxSubBus: 0x%02X, LSlot: 0x%02X",
				       BridgeInfo->busUnitInfo.deviceType,
				       BridgeInfo->subBusNumber,
				       BridgeInfo->maxAgents,
				       BridgeInfo->maxSubBusNumber,
				       BridgeInfo->logicalSlotNumber);

				if (BridgeInfo->busUnitInfo.deviceType == HvCallPci_BridgeDevice)  {
					/* Scan_Bridge_Slot...: 0x18.00.12 */
					iSeries_Scan_Bridge_Slot(Bus,BridgeInfo);
				}
				else printk("PCI: Invalid Bridge Configuration(0x%02X)",BridgeInfo->busUnitInfo.deviceType);
			}
    		}
		else if(HvRc != 0x000B) pci_Log_Error("EADs Connect",Bus,SubBus,AgentId,HvRc);
	}
	kfree(BridgeInfo);
}

/********************************************************************************
* 
* This assumes that the node slot is always on the primary bus!
*
*********************************************************************************/
int iSeries_Scan_Bridge_Slot(HvBusNumber Bus, struct HvCallPci_BridgeInfo* BridgeInfo)
{
	struct iSeries_Device_Node* DeviceNode;
	HvSubBusNumber SubBus = BridgeInfo->subBusNumber;
	u16       VendorId    = 0;
	int       HvRc        = 0;
	u8        Irq         = 0;
	int       IdSel       = ISERIES_GET_DEVICE_FROM_SUBBUS(SubBus);
	int       Function    = ISERIES_GET_FUNCTION_FROM_SUBBUS(SubBus);
	HvAgentId AgentId     = ISERIES_PCI_AGENTID(IdSel, Function);
	HvAgentId EADsIdSel   = ISERIES_PCI_AGENTID(IdSel, Function);
	int       FirstSlotId = 0; 	

	/**********************************************************/
	/* iSeries_allocate_IRQ.: 0x18.00.12(0xA3)                */
	/**********************************************************/
  	Irq   = iSeries_allocate_IRQ(Bus, 0, AgentId);
	iSeries_assign_IRQ(Irq, Bus, 0, AgentId);
	PPCDBG(PPCDBG_BUSWALK,"PCI:- allocate and assign IRQ 0x%02X.%02X.%02X = 0x%02X\n",Bus, 0, AgentId, Irq );

	/****************************************************************************
	 * Connect all functions of any device found.  
	 ****************************************************************************/
  	for (IdSel = 1; IdSel <= BridgeInfo->maxAgents; ++IdSel) {
    		for (Function = 0; Function < 8; ++Function) {
			AgentId = ISERIES_PCI_AGENTID(IdSel, Function);
			HvRc = HvCallXm_connectBusUnit(Bus, SubBus, AgentId, Irq);
			if( HvRc == 0) {
				HvRc = HvCallPci_configLoad16(Bus, SubBus, AgentId, PCI_VENDOR_ID, &VendorId);
				if( HvRc == 0) {
					/**********************************************************/
					/* FoundDevice: 0x18.28.10 = 0x12AE                       */
					/**********************************************************/
					PPCDBG(PPCDBG_BUSWALK,"PCI:- FoundDevice: 0x%02X.%02X.%02X = 0x%04X\n",
					                                       Bus, SubBus, AgentId, VendorId);

					HvRc = HvCallPci_configStore8(Bus, SubBus, AgentId, PCI_INTERRUPT_LINE, Irq);  
					if( HvRc != 0) {
						pci_Log_Error("PciCfgStore Irq Failed!",Bus,SubBus,AgentId,HvRc);
					}

					++DeviceCount;
					DeviceNode = build_device_node(Bus, SubBus, EADsIdSel, Function);
					DeviceNode->Vendor      = VendorId;
					DeviceNode->Irq         = Irq;
					DeviceNode->LogicalSlot = BridgeInfo->logicalSlotNumber;
					PCIFR("Device(%4d): 0x%02X.%02X.%02X 0x%02X 0x%04X",
					      DeviceCount,Bus, SubBus, AgentId,
					      DeviceNode->LogicalSlot,DeviceNode->Vendor);

					/***********************************************************
					 * On the first device/function, assign irq to slot
					 ***********************************************************/
					if(Function == 0) { 
						FirstSlotId = AgentId;
						// AHT iSeries_assign_IRQ(Irq, Bus, SubBus, AgentId);
    					}
				}
				else pci_Log_Error("Read Vendor",Bus,SubBus,AgentId,HvRc);
			}
			else pci_Log_Error("Connect Bus Unit",Bus,SubBus, AgentId,HvRc);
		} /* for (Function = 0; Function < 8; ++Function) */
	} /* for (IdSel = 1; IdSel <= MaxAgents; ++IdSel) */
	return HvRc;
}
/************************************************************************/
/* I/0 Memory copy MUST use mmio commands on iSeries                    */
/* To do; For performance, include the hv call directly                 */
/************************************************************************/
void* iSeries_memset_io(void* dest, char c, size_t Count)
{
	u8    ByteValue     = c;
	long  NumberOfBytes = Count;
	char* IoBuffer      = dest;
	while(NumberOfBytes > 0) {
		iSeries_Write_Byte( ByteValue, (void*)IoBuffer );
		++IoBuffer;
		-- NumberOfBytes;
	}
	return dest;
}	
void* iSeries_memcpy_toio(void *dest, void *source, size_t count)
{
	char *dst           = dest;
	char *src           = source;
	long  NumberOfBytes = count;
	while(NumberOfBytes > 0) {
		iSeries_Write_Byte(*src++, (void*)dst++);
		-- NumberOfBytes;
	}
	return dest;
}
void* iSeries_memcpy_fromio(void *dest, void *source, size_t count)
{
	char *dst = dest;
	char *src = source;
	long  NumberOfBytes = count;
	while(NumberOfBytes > 0) {
		*dst++ = iSeries_Read_Byte( (void*)src++);
		-- NumberOfBytes;
	}
	return dest;
}
/**********************************************************************************
 * Look down the chain to find the matching Device Device
 **********************************************************************************/
struct iSeries_Device_Node* find_Device_Node(struct pci_dev* PciDev)
{
	struct list_head* Device_Node_Ptr = iSeries_Global_Device_List.next;
	int Bus   = PciDev->bus->number;
	int DevFn = PciDev->devfn;
	
	while(Device_Node_Ptr != &iSeries_Global_Device_List) { 
		struct iSeries_Device_Node* DevNode = (struct iSeries_Device_Node*)Device_Node_Ptr;
		if(Bus == ISERIES_BUS(DevNode) && DevFn == DevNode->DevFn) {
			return DevNode;
		}
		Device_Node_Ptr = Device_Node_Ptr->next;
	}
	return NULL;
}
/******************************************************************/
/* Returns the device node for the passed pci_dev                 */
/* Sanity Check Node PciDev to passed pci_dev                     */
/* If none is found, returns a NULL which the client must handle. */
/******************************************************************/
struct iSeries_Device_Node* get_Device_Node(struct pci_dev* PciDev)
{
	struct iSeries_Device_Node* Node;
	Node = (struct iSeries_Device_Node*)PciDev->sysdata;
	if(Node == NULL ) {
		Node = find_Device_Node(PciDev);
	}
	else if(Node->PciDev != PciDev) { 
		Node = find_Device_Node(PciDev);
	}
	return Node;
}
/**********************************************************************************
 *
 * Read PCI Config Space Code 
 *
 **********************************************************************************/
/** BYTE  *************************************************************************/
int iSeries_Node_read_config_byte(struct iSeries_Device_Node* DevNode, int Offset, u8* ReadValue)
{
	u8  ReadData; 
	if(DevNode == NULL) { return 0x301; } 
	++Pci_Cfg_Read_Count;
	DevNode->ReturnCode = HvCallPci_configLoad8(ISERIES_BUS(DevNode),ISERIES_SUBBUS(DevNode),0x10,
	                                                Offset,&ReadData);
	if(Pci_Trace_Flag == 1) {
		PCIFR("RCB: 0x%04X.%02X 0x%04X = 0x%02X",ISERIES_BUS(DevNode),DevNode->DevFn,Offset,ReadData);
	}
	if(DevNode->ReturnCode != 0 ) { 
		printk("PCI: RCB: 0x%04X.%02X  Error: 0x%04X\n",ISERIES_BUS(DevNode),DevNode->DevFn,DevNode->ReturnCode);
		PCIFR(      "RCB: 0x%04X.%02X  Error: 0x%04X",  ISERIES_BUS(DevNode),DevNode->DevFn,DevNode->ReturnCode);
	}
	*ReadValue = ReadData; 
 	return DevNode->ReturnCode;
}
/** WORD  *************************************************************************/
int iSeries_Node_read_config_word(struct iSeries_Device_Node* DevNode, int Offset, u16* ReadValue)
{
	u16  ReadData; 
	if(DevNode == NULL) { return 0x301; } 
	++Pci_Cfg_Read_Count;
	DevNode->ReturnCode = HvCallPci_configLoad16(ISERIES_BUS(DevNode),ISERIES_SUBBUS(DevNode),0x10,
	                                                Offset,&ReadData);
	if(Pci_Trace_Flag == 1) {
		PCIFR("RCW: 0x%04X.%02X 0x%04X = 0x%04X",ISERIES_BUS(DevNode),DevNode->DevFn,Offset,ReadData);
	}
	if(DevNode->ReturnCode != 0 ) { 
		printk("PCI: RCW: 0x%04X.%02X  Error: 0x%04X\n",ISERIES_BUS(DevNode),DevNode->DevFn,DevNode->ReturnCode);
		PCIFR(      "RCW: 0x%04X.%02X  Error: 0x%04X",  ISERIES_BUS(DevNode),DevNode->DevFn,DevNode->ReturnCode);

	}
	*ReadValue = ReadData; 
 	return DevNode->ReturnCode;
}
/** DWORD *************************************************************************/
int iSeries_Node_read_config_dword(struct iSeries_Device_Node* DevNode, int Offset, u32* ReadValue)
{
 	u32  ReadData; 
	if(DevNode == NULL) { return 0x301; } 
	++Pci_Cfg_Read_Count;
	DevNode->ReturnCode = HvCallPci_configLoad32(ISERIES_BUS(DevNode),ISERIES_SUBBUS(DevNode),0x10,
	                                                Offset,&ReadData);
	if(Pci_Trace_Flag == 1) {
		PCIFR("RCL: 0x%04X.%02X 0x%04X = 0x%08X",ISERIES_BUS(DevNode),DevNode->DevFn,Offset,ReadData);
	}
	if(DevNode->ReturnCode != 0 ) { 
		printk("PCI: RCL: 0x%04X.%02X  Error: 0x%04X\n",ISERIES_BUS(DevNode),DevNode->DevFn,DevNode->ReturnCode);
		PCIFR(      "RCL: 0x%04X.%02X  Error: 0x%04X",  ISERIES_BUS(DevNode),DevNode->DevFn,DevNode->ReturnCode);
	}
	*ReadValue = ReadData; 
 	return DevNode->ReturnCode;
}
int iSeries_pci_read_config_byte(struct pci_dev* PciDev, int Offset, u8* ReadValue) { 
	struct iSeries_Device_Node* DevNode = get_Device_Node(PciDev);
	if(DevNode == NULL) return 0x0301;
	return iSeries_Node_read_config_byte( DevNode ,Offset,ReadValue);
}
int iSeries_pci_read_config_word(struct pci_dev* PciDev, int Offset, u16* ReadValue) { 
	struct iSeries_Device_Node* DevNode = get_Device_Node(PciDev);
	if(DevNode == NULL) return 0x0301;
	return iSeries_Node_read_config_word( DevNode ,Offset,ReadValue );
}
int iSeries_pci_read_config_dword(struct pci_dev* PciDev, int Offset, u32* ReadValue) { 
	struct iSeries_Device_Node* DevNode = get_Device_Node(PciDev);
	if(DevNode == NULL) return 0x0301;
	return iSeries_Node_read_config_dword(DevNode ,Offset,ReadValue  );
}
/**********************************************************************************/
/*                                                                                */
/* Write PCI Config Space                                                         */
/*                                                                                */
/** BYTE  *************************************************************************/
int iSeries_Node_write_config_byte(struct iSeries_Device_Node* DevNode, int Offset, u8 WriteData)
{
	++Pci_Cfg_Write_Count;
	DevNode->ReturnCode = HvCallPci_configStore8(ISERIES_BUS(DevNode),ISERIES_SUBBUS(DevNode),0x10,
	                                                  Offset,WriteData);
	if(Pci_Trace_Flag == 1) {
		PCIFR("WCB: 0x%04X.%02X 0x%04X = 0x%02X",ISERIES_BUS(DevNode),DevNode->DevFn,Offset,WriteData);
	}
	if(DevNode->ReturnCode != 0 ) { 
		printk("PCI: WCB: 0x%04X.%02X  Error: 0x%04X\n",ISERIES_BUS(DevNode),DevNode->DevFn,DevNode->ReturnCode);
		PCIFR(      "WCB: 0x%04X.%02X  Error: 0x%04X",  ISERIES_BUS(DevNode),DevNode->DevFn,DevNode->ReturnCode);
	}
 	return DevNode->ReturnCode;
}
/** WORD  *************************************************************************/
int iSeries_Node_write_config_word(struct iSeries_Device_Node* DevNode, int Offset, u16 WriteData)
{
	++Pci_Cfg_Write_Count;
	DevNode->ReturnCode = HvCallPci_configStore16(ISERIES_BUS(DevNode),ISERIES_SUBBUS(DevNode),0x10,
	                                                  Offset,WriteData);
	if(Pci_Trace_Flag == 1) {
		PCIFR("WCW: 0x%04X.%02X 0x%04X = 0x%04X",ISERIES_BUS(DevNode),DevNode->DevFn,Offset,WriteData);
	}
	if(DevNode->ReturnCode != 0 ) { 
		printk("PCI: WCW: 0x%04X.%02X  Error: 0x%04X\n",ISERIES_BUS(DevNode),DevNode->DevFn,DevNode->ReturnCode);
		PCIFR(      "WCW: 0x%04X.%02X  Error: 0x%04X",  ISERIES_BUS(DevNode),DevNode->DevFn,DevNode->ReturnCode);
	}
 	return DevNode->ReturnCode;
}
/** DWORD *************************************************************************/
int iSeries_Node_write_config_dword(struct iSeries_Device_Node* DevNode, int Offset, u32 WriteData)
{
	++Pci_Cfg_Write_Count;
	DevNode->ReturnCode = HvCallPci_configStore32(ISERIES_BUS(DevNode),ISERIES_SUBBUS(DevNode),0x10,
	                                                  Offset,WriteData);
	if(Pci_Trace_Flag == 1) {
		PCIFR("WCL: 0x%04X.%02X 0x%04X = 0x%08X",ISERIES_BUS(DevNode),DevNode->DevFn,Offset,WriteData);
	}
	if(DevNode->ReturnCode != 0 ) { 
		printk("PCI: WCL: 0x%04X.%02X  Error: 0x%04X\n",ISERIES_BUS(DevNode),DevNode->DevFn,DevNode->ReturnCode);
		PCIFR(      "WCL: 0x%04X.%02X  Error: 0x%04X",  ISERIES_BUS(DevNode),DevNode->DevFn,DevNode->ReturnCode);
	}
	return DevNode->ReturnCode;
}
int iSeries_pci_write_config_byte( struct pci_dev* PciDev,int Offset, u8 WriteValue)
{
	struct iSeries_Device_Node* DevNode = get_Device_Node(PciDev);
	if(DevNode == NULL) return 0x0301;
	return iSeries_Node_write_config_byte( DevNode,Offset,WriteValue);
}
int iSeries_pci_write_config_word( struct pci_dev* PciDev,int Offset,u16 WriteValue)
{
	struct iSeries_Device_Node* DevNode = get_Device_Node(PciDev);
	if(DevNode == NULL) return 0x0301;
	return iSeries_Node_write_config_word( DevNode,Offset,WriteValue);
}
int iSeries_pci_write_config_dword(struct pci_dev* PciDev,int Offset,u32 WriteValue)
{
	struct iSeries_Device_Node* DevNode = get_Device_Node(PciDev);
	if(DevNode == NULL) return 0x0301;
	return iSeries_Node_write_config_dword(DevNode,Offset,WriteValue);
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

/************************************************************************
 * Check Return Code
 * -> On Failure, print and log information.
 *    Increment Retry Count, if exceeds max, panic partition.
 * -> If in retry, print and log success 
 ************************************************************************
 * PCI: Device 23.90 ReadL I/O Error( 0): 0x1234
 * PCI: Device 23.90 ReadL Retry( 1)
 * PCI: Device 23.90 ReadL Retry Successful(1)
 ************************************************************************/
int  CheckReturnCode(char* TextHdr, struct iSeries_Device_Node* DevNode, u64 RtnCode)
{
	if(RtnCode != 0)  {
		++Pci_Error_Count;
		++DevNode->IoRetry;
		PCIFR(      "%s: Device 0x%04X:%02X  I/O Error(%2d): 0x%04X",
			    TextHdr,ISERIES_BUS(DevNode),DevNode->DevFn,DevNode->IoRetry,(int)RtnCode);
		printk("PCI: %s: Device 0x%04X:%02X  I/O Error(%2d): 0x%04X\n",
		            TextHdr,ISERIES_BUS(DevNode),DevNode->DevFn,DevNode->IoRetry,(int)RtnCode);
		/*******************************************************/
		/* Bump the retry and check for retry count exceeded.  */
		/* If, Exceeded, panic the system.                     */           
		/*******************************************************/
		if(DevNode->IoRetry > Pci_Retry_Max && Pci_Error_Flag > 0 ) {
			mf_displaySrc(0xB6000103);
			panic_timeout = 0; 
			panic("PCI: Hardware I/O Error, SRC B6000103, Automatic Reboot Disabled.\n");
		}
		return -1;	/* Retry Try */
	}
	/********************************************************************
	* If retry was in progress, log success and rest retry count        *
	*********************************************************************/
	else if(DevNode->IoRetry > 0) {
		PCIFR("%s: Device 0x%04X:%02X Retry Successful(%2d).",
		      TextHdr,ISERIES_BUS(DevNode),DevNode->DevFn,DevNode->IoRetry);
		DevNode->IoRetry = 0;
		return 0; 
	}
	return 0; 
}
/************************************************************************/
/* Translate the I/O Address into a device node, bar, and bar offset.   */
/* Note: Make sure the passed variable end up on the stack to avoid     */
/* the exposure of being device global.                                 */
/************************************************************************/
static inline struct iSeries_Device_Node* xlateIoMmAddress(void* IoAddress,
							    union HvDsaMap* DsaPtr,
							   u64* BarOffsetPtr) {

	unsigned long BaseIoAddr = (unsigned long)IoAddress-iSeries_Base_Io_Memory;
	long          TableIndex = BaseIoAddr/iSeries_IoMmTable_Entry_Size;
	struct iSeries_Device_Node* DevNode = *(iSeries_IoMmTable +TableIndex);
	if(DevNode != NULL) {
		DsaPtr->DsaAddr       = ISERIES_DSA(DevNode);
		DsaPtr->Dsa.barNumber = *(iSeries_IoBarTable+TableIndex);
		*BarOffsetPtr         = BaseIoAddr % iSeries_IoMmTable_Entry_Size;
	}
	else {
		panic("PCI: Invalid PCI IoAddress detected!\n");
	}
	return DevNode;
}

/************************************************************************/
/* Read MM I/O Instructions for the iSeries                             */
/* On MM I/O error, all ones are returned and iSeries_pci_IoError is cal*/
/* else, data is returned in big Endian format.                         */
/************************************************************************/
/* iSeries_Read_Byte = Read Byte  ( 8 bit)                              */
/* iSeries_Read_Word = Read Word  (16 bit)                              */
/* iSeries_Read_Long = Read Long  (32 bit)                              */
/************************************************************************/
u8  iSeries_Read_Byte(void* IoAddress)
{
	u64    BarOffset;
	union  HvDsaMap DsaData;
	struct HvCallPci_LoadReturn Return;
	struct iSeries_Device_Node* DevNode = xlateIoMmAddress(IoAddress,&DsaData,&BarOffset);

	do {
		++Pci_Io_Read_Count;
		HvCall3Ret16(HvCallPciBarLoad8, &Return, DsaData.DsaAddr,BarOffset, 0);
	} while (CheckReturnCode("RDB",DevNode, Return.rc) != 0);

	if(Pci_Trace_Flag == 1)	PCIFR("RDB: IoAddress 0x%p = 0x%02X",IoAddress, (u8)Return.value); 
	return (u8)Return.value;
}
u16  iSeries_Read_Word(void* IoAddress)
{
	u64    BarOffset;
	union  HvDsaMap DsaData;
	struct HvCallPci_LoadReturn Return;
	struct iSeries_Device_Node* DevNode = xlateIoMmAddress(IoAddress,&DsaData,&BarOffset);

	do {
		++Pci_Io_Read_Count;
		HvCall3Ret16(HvCallPciBarLoad16,&Return, DsaData.DsaAddr,BarOffset, 0);
	} while (CheckReturnCode("RDW",DevNode, Return.rc) != 0);

	if(Pci_Trace_Flag == 1) PCIFR("RDW: IoAddress 0x%p = 0x%04X",IoAddress, swab16((u16)Return.value));
	return swab16((u16)Return.value);
}
u32  iSeries_Read_Long(void* IoAddress)
{
	u64    BarOffset;
	union  HvDsaMap DsaData;
	struct HvCallPci_LoadReturn Return;
	struct iSeries_Device_Node* DevNode = xlateIoMmAddress(IoAddress,&DsaData,&BarOffset);

	do {
		++Pci_Io_Read_Count;
		HvCall3Ret16(HvCallPciBarLoad32,&Return, DsaData.DsaAddr,BarOffset, 0);
	} while (CheckReturnCode("RDL",DevNode, Return.rc) != 0);

	if(Pci_Trace_Flag == 1) PCIFR("RDL: IoAddress 0x%p = 0x%04X",IoAddress, swab32((u32)Return.value));
	return swab32((u32)Return.value);
}
/************************************************************************/
/* Write MM I/O Instructions for the iSeries                            */
/************************************************************************/
/* iSeries_Write_Byte = Write Byte (8 bit)                              */
/* iSeries_Write_Word = Write Word(16 bit)                              */
/* iSeries_Write_Long = Write Long(32 bit)                              */
/************************************************************************/
void iSeries_Write_Byte(u8 Data, void* IoAddress)
{
	u64    BarOffset;
	union  HvDsaMap DsaData;
	struct HvCallPci_LoadReturn Return;
	struct iSeries_Device_Node* DevNode = xlateIoMmAddress(IoAddress,&DsaData,&BarOffset);

	do {
		++Pci_Io_Write_Count;
		Return.rc = HvCall4(HvCallPciBarStore8, DsaData.DsaAddr,BarOffset, Data, 0);
	} while (CheckReturnCode("WWB",DevNode, Return.rc) != 0);
	if(Pci_Trace_Flag == 1) PCIFR("WWB: IoAddress 0x%p = 0x%02X",IoAddress,Data);
}
void iSeries_Write_Word(u16 Data, void* IoAddress)
{
	u64    BarOffset;
	union  HvDsaMap DsaData;
	struct HvCallPci_LoadReturn Return;
	struct iSeries_Device_Node* DevNode = xlateIoMmAddress(IoAddress,&DsaData,&BarOffset);

	do {
		++Pci_Io_Write_Count;
		Return.rc = HvCall4(HvCallPciBarStore16,DsaData.DsaAddr,BarOffset, swab16(Data), 0);
	} while (CheckReturnCode("WWW",DevNode, Return.rc) != 0);
	if(Pci_Trace_Flag == 1) PCIFR("WWW: IoAddress 0x%p = 0x%04X",IoAddress,Data);
}
void iSeries_Write_Long(u32 Data, void* IoAddress)
{
	u64    BarOffset;
	union  HvDsaMap DsaData;
	struct HvCallPci_LoadReturn Return;
	struct iSeries_Device_Node* DevNode = xlateIoMmAddress(IoAddress,&DsaData,&BarOffset);

	do {
		++Pci_Io_Write_Count;
		Return.rc = HvCall4(HvCallPciBarStore32,DsaData.DsaAddr,BarOffset, swab32(Data), 0);
	} while (CheckReturnCode("WWL",DevNode, Return.rc) != 0);
	if(Pci_Trace_Flag == 1) PCIFR("WWL: IoAddress 0x%p = 0x%08X",IoAddress, Data);
}
