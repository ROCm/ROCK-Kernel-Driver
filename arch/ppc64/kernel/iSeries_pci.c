#define PCIFR(...)
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
#include <linux/module.h>
#include <linux/ide.h>
#include <linux/pci.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/ppcdebug.h>
#include <asm/naca.h>
#include <asm/iommu.h>

#include <asm/iSeries/HvCallPci.h>
#include <asm/iSeries/HvCallSm.h>
#include <asm/iSeries/HvCallXm.h>
#include <asm/iSeries/LparData.h>
#include <asm/iSeries/iSeries_irq.h>
#include <asm/iSeries/iSeries_pci.h>
#include <asm/iSeries/mf.h>

#include "iSeries_IoMmTable.h"
#include "pci.h"

extern int panic_timeout;

extern unsigned long iSeries_Base_Io_Memory;    

extern struct iommu_table *tceTables[256];

extern void iSeries_MmIoTest(void);

/*
 * Forward declares of prototypes. 
 */
static struct iSeries_Device_Node *find_Device_Node(int bus, int devfn);
static void iSeries_Scan_PHBs_Slots(struct pci_controller *Phb);
static void iSeries_Scan_EADs_Bridge(HvBusNumber Bus, HvSubBusNumber SubBus,
		int IdSel);
static int iSeries_Scan_Bridge_Slot(HvBusNumber Bus,
		struct HvCallPci_BridgeInfo *Info);

LIST_HEAD(iSeries_Global_Device_List);

static int DeviceCount;

/* Counters and control flags. */
static long Pci_Io_Read_Count;
static long Pci_Io_Write_Count;
#if 0
static long Pci_Cfg_Read_Count;
static long Pci_Cfg_Write_Count;
#endif
static long Pci_Error_Count;

static int Pci_Retry_Max = 3;	/* Only retry 3 times  */	
static int Pci_Error_Flag = 1;	/* Set Retry Error on. */

static struct pci_ops iSeries_pci_ops;

/*
 * Log Error infor in Flight Recorder to system Console.
 * Filter out the device not there errors.
 * PCI: EADs Connect Failed 0x18.58.10 Rc: 0x00xx
 * PCI: Read Vendor Failed 0x18.58.10 Rc: 0x00xx
 * PCI: Connect Bus Unit Failed 0x18.58.10 Rc: 0x00xx
 */
static void pci_Log_Error(char *Error_Text, int Bus, int SubBus,
		int AgentId, int HvRc)
{
	if (HvRc == 0x0302)
		return;

	printk(KERN_ERR "PCI: %s Failed: 0x%02X.%02X.%02X Rc: 0x%04X",
	       Error_Text, Bus, SubBus, AgentId, HvRc);
}

/*
 * build_device_node(u16 Bus, int SubBus, u8 DevFn)
 */
static struct iSeries_Device_Node *build_device_node(HvBusNumber Bus,
		HvSubBusNumber SubBus, int AgentId, int Function)
{
	struct iSeries_Device_Node *node;

	PPCDBG(PPCDBG_BUSWALK,
			"-build_device_node 0x%02X.%02X.%02X Function: %02X\n",
			Bus, SubBus, AgentId, Function);

	node = kmalloc(sizeof(struct iSeries_Device_Node), GFP_KERNEL);
	if (node == NULL)
		return NULL;

	memset(node, 0, sizeof(struct iSeries_Device_Node));
	list_add_tail(&node->Device_List, &iSeries_Global_Device_List);
#if 0
	node->DsaAddr = ((u64)Bus << 48) + ((u64)SubBus << 40) + ((u64)0x10 << 32);
#endif
	node->DsaAddr.DsaAddr = 0;
	node->DsaAddr.Dsa.busNumber = Bus;
	node->DsaAddr.Dsa.subBusNumber = SubBus;
	node->DsaAddr.Dsa.deviceId = 0x10;
	node->AgentId = AgentId;
	node->DevFn = PCI_DEVFN(ISERIES_ENCODE_DEVICE(AgentId), Function);
	node->IoRetry = 0;
	iSeries_Get_Location_Code(node);
	PCIFR("Device 0x%02X.%2X, Node:0x%p ", ISERIES_BUS(node),
			ISERIES_DEVFUN(node), node);
	return node;
}

/*
 * unsigned long __init find_and_init_phbs(void)
 *
 * Description:
 *   This function checks for all possible system PCI host bridges that connect
 *   PCI buses.  The system hypervisor is queried as to the guest partition
 *   ownership status.  A pci_controller is built for any bus which is partially
 *   owned or fully owned by this guest partition.
 */
unsigned long __init find_and_init_phbs(void)
{
	struct pci_controller *phb;
	HvBusNumber bus;

	PPCDBG(PPCDBG_BUSWALK, "find_and_init_phbs Entry\n");

	/* Check all possible buses. */
	for (bus = 0; bus < 256; bus++) {
		int ret = HvCallXm_testBus(bus);
		if (ret == 0) {
			printk("bus %d appears to exist\n", bus);
			phb = pci_alloc_pci_controller(phb_type_hypervisor);
			if (phb == NULL) {
				PCIFR("Allocate pci_controller failed.");
				return -1;
			}
			phb->pci_mem_offset = phb->local_number = bus;
			phb->first_busno = bus;
			phb->last_busno = bus;
			phb->ops = &iSeries_pci_ops;

			PPCDBG(PPCDBG_BUSWALK, "PCI:Create iSeries pci_controller(%p), Bus: %04X\n",
					phb, bus);
			PCIFR("Create iSeries PHB controller: %04X", bus);

			/* Find and connect the devices. */
			iSeries_Scan_PHBs_Slots(phb);
		}
		/*
		 * Check for Unexpected Return code, a clue that something
		 * has gone wrong.
		 */
		else if (ret != 0x0301)
			printk(KERN_ERR "Unexpected Return on Probe(0x%04X): 0x%04X",
			       bus, ret);
	}
	return 0;
}

/*
 * iSeries_pcibios_init
 *  
 * Chance to initialize and structures or variable before PCI Bus walk.
 */
void iSeries_pcibios_init(void)
{
	PPCDBG(PPCDBG_BUSWALK, "iSeries_pcibios_init Entry.\n"); 
	iSeries_IoMmTable_Initialize();
	find_and_init_phbs();
	/* pci_assign_all_busses = 0;		SFRXXX*/
	PPCDBG(PPCDBG_BUSWALK, "iSeries_pcibios_init Exit.\n"); 
}

/*
 * iSeries_pci_final_fixup(void)  
 */
void __init iSeries_pci_final_fixup(void)
{
	struct pci_dev *pdev = NULL;
	struct iSeries_Device_Node *node;
	char Buffer[256];
    	int DeviceCount = 0;

	PPCDBG(PPCDBG_BUSWALK, "iSeries_pcibios_fixup Entry.\n"); 

	/* Fix up at the device node and pci_dev relationship */
	mf_displaySrc(0xC9000100);

	printk("pcibios_final_fixup\n");
	while ((pdev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, pdev))
			!= NULL) {
		node = find_Device_Node(pdev->bus->number, pdev->devfn);
		printk("pci dev %p (%x.%x), node %p\n", pdev,
		       pdev->bus->number, pdev->devfn, node);

		if (node != NULL) {
			++DeviceCount;
			pdev->sysdata = (void *)node;
			node->PciDev = pdev;
			PPCDBG(PPCDBG_BUSWALK,
					"pdev 0x%p <==> DevNode 0x%p\n",
					pdev, node);
			iSeries_allocateDeviceBars(pdev);
			iSeries_Device_Information(pdev, Buffer,
					sizeof(Buffer));
			printk("%d. %s\n", DeviceCount, Buffer);
			iommu_devnode_init(node);
		} else
			printk("PCI: Device Tree not found for 0x%016lX\n",
					(unsigned long)pdev);
		pdev->irq = node->Irq;
	}
	iSeries_IoMmTable_Status();
	iSeries_activate_IRQs();
	mf_displaySrc(0xC9000200);
}

void pcibios_fixup_bus(struct pci_bus *PciBus)
{
	PPCDBG(PPCDBG_BUSWALK, "iSeries_pcibios_fixup_bus(0x%04X) Entry.\n",
			PciBus->number); 
}

void pcibios_fixup_resources(struct pci_dev *pdev)
{
	PPCDBG(PPCDBG_BUSWALK, "fixup_resources pdev %p\n", pdev);
}   

/*
 * Loop through each node function to find usable EADs bridges.  
 */
static void iSeries_Scan_PHBs_Slots(struct pci_controller *Phb)
{
	struct HvCallPci_DeviceInfo *DevInfo;
	HvBusNumber bus = Phb->local_number;	/* System Bus */	
	const HvSubBusNumber SubBus = 0;	/* EADs is always 0. */
	int HvRc = 0;
	int IdSel;	
	const int MaxAgents = 8;

	DevInfo = (struct HvCallPci_DeviceInfo*)
		kmalloc(sizeof(struct HvCallPci_DeviceInfo), GFP_KERNEL);
	if (DevInfo == NULL)
		return;

	/*
	 * Probe for EADs Bridges      
	 */
	for (IdSel = 1; IdSel < MaxAgents; ++IdSel) {
    		HvRc = HvCallPci_getDeviceInfo(bus, SubBus, IdSel,
				ISERIES_HV_ADDR(DevInfo),
				sizeof(struct HvCallPci_DeviceInfo));
		if (HvRc == 0) {
			if (DevInfo->deviceType == HvCallPci_NodeDevice)
				iSeries_Scan_EADs_Bridge(bus, SubBus, IdSel);
			else
				printk("PCI: Invalid System Configuration(0x%02X)"
				       " for bus 0x%02x id 0x%02x.\n",
				       DevInfo->deviceType, bus, IdSel);
		}
		else
			pci_Log_Error("getDeviceInfo", bus, SubBus, IdSel, HvRc);
	}
	kfree(DevInfo);
}

static void iSeries_Scan_EADs_Bridge(HvBusNumber bus, HvSubBusNumber SubBus,
		int IdSel)
{
	struct HvCallPci_BridgeInfo *BridgeInfo;
	HvAgentId AgentId;
	int Function;
	int HvRc;

	BridgeInfo = (struct HvCallPci_BridgeInfo *)
		kmalloc(sizeof(struct HvCallPci_BridgeInfo), GFP_KERNEL);
	if (BridgeInfo == NULL)
		return;

	/* Note: hvSubBus and irq is always be 0 at this level! */
	for (Function = 0; Function < 8; ++Function) {
	  	AgentId = ISERIES_PCI_AGENTID(IdSel, Function);
		HvRc = HvCallXm_connectBusUnit(bus, SubBus, AgentId, 0);
 		if (HvRc == 0) {
			printk("found device at bus %d idsel %d func %d (AgentId %x)\n",
			       bus, IdSel, Function, AgentId);
  			/*  Connect EADs: 0x18.00.12 = 0x00 */
			PPCDBG(PPCDBG_BUSWALK,
					"PCI:Connect EADs: 0x%02X.%02X.%02X\n",
					bus, SubBus, AgentId);
	    		HvRc = HvCallPci_getBusUnitInfo(bus, SubBus, AgentId,
					ISERIES_HV_ADDR(BridgeInfo),
					sizeof(struct HvCallPci_BridgeInfo));
	 		if (HvRc == 0) {
				printk("bridge info: type %x subbus %x maxAgents %x maxsubbus %x logslot %x\n",
					BridgeInfo->busUnitInfo.deviceType,
					BridgeInfo->subBusNumber,
					BridgeInfo->maxAgents,
					BridgeInfo->maxSubBusNumber,
					BridgeInfo->logicalSlotNumber);
				PPCDBG(PPCDBG_BUSWALK,
					"PCI: BridgeInfo, Type:0x%02X, SubBus:0x%02X, MaxAgents:0x%02X, MaxSubBus: 0x%02X, LSlot: 0x%02X\n",
					BridgeInfo->busUnitInfo.deviceType,
					BridgeInfo->subBusNumber,
					BridgeInfo->maxAgents,
					BridgeInfo->maxSubBusNumber,
					BridgeInfo->logicalSlotNumber);

				if (BridgeInfo->busUnitInfo.deviceType ==
						HvCallPci_BridgeDevice)  {
					/* Scan_Bridge_Slot...: 0x18.00.12 */
					iSeries_Scan_Bridge_Slot(bus, BridgeInfo);
				} else
					printk("PCI: Invalid Bridge Configuration(0x%02X)",
						BridgeInfo->busUnitInfo.deviceType);
			}
    		} else if (HvRc != 0x000B)
			pci_Log_Error("EADs Connect",
					bus, SubBus, AgentId, HvRc);
	}
	kfree(BridgeInfo);
}

/*
 * This assumes that the node slot is always on the primary bus!
 */
static int iSeries_Scan_Bridge_Slot(HvBusNumber Bus,
		struct HvCallPci_BridgeInfo *BridgeInfo)
{
	struct iSeries_Device_Node *node;
	HvSubBusNumber SubBus = BridgeInfo->subBusNumber;
	u16 VendorId = 0;
	int HvRc = 0;
	u8 Irq = 0;
	int IdSel = ISERIES_GET_DEVICE_FROM_SUBBUS(SubBus);
	int Function = ISERIES_GET_FUNCTION_FROM_SUBBUS(SubBus);
	HvAgentId EADsIdSel = ISERIES_PCI_AGENTID(IdSel, Function);

	/* iSeries_allocate_IRQ.: 0x18.00.12(0xA3) */
  	Irq = iSeries_allocate_IRQ(Bus, 0, EADsIdSel);
	PPCDBG(PPCDBG_BUSWALK,
		"PCI:- allocate and assign IRQ 0x%02X.%02X.%02X = 0x%02X\n",
		Bus, 0, EADsIdSel, Irq);

	/*
	 * Connect all functions of any device found.  
	 */
  	for (IdSel = 1; IdSel <= BridgeInfo->maxAgents; ++IdSel) {
    		for (Function = 0; Function < 8; ++Function) {
			HvAgentId AgentId = ISERIES_PCI_AGENTID(IdSel, Function);
			HvRc = HvCallXm_connectBusUnit(Bus, SubBus,
					AgentId, Irq);
			if (HvRc != 0) {
				pci_Log_Error("Connect Bus Unit",
					      Bus, SubBus, AgentId, HvRc);
				continue;
			}

			HvRc = HvCallPci_configLoad16(Bus, SubBus, AgentId,
						      PCI_VENDOR_ID, &VendorId);
			if (HvRc != 0) {
				pci_Log_Error("Read Vendor",
					      Bus, SubBus, AgentId, HvRc);
				continue;
			}
			printk("read vendor ID: %x\n", VendorId);

			/* FoundDevice: 0x18.28.10 = 0x12AE */
			PPCDBG(PPCDBG_BUSWALK,
			       "PCI:- FoundDevice: 0x%02X.%02X.%02X = 0x%04X, irq %d\n",
			       Bus, SubBus, AgentId, VendorId, Irq);
			HvRc = HvCallPci_configStore8(Bus, SubBus, AgentId,
						      PCI_INTERRUPT_LINE, Irq);  
			if (HvRc != 0)
				pci_Log_Error("PciCfgStore Irq Failed!",
					      Bus, SubBus, AgentId, HvRc);

			++DeviceCount;
			node = build_device_node(Bus, SubBus, EADsIdSel, Function);
			node->Vendor = VendorId;
			node->Irq = Irq;
			node->LogicalSlot = BridgeInfo->logicalSlotNumber;

		} /* for (Function = 0; Function < 8; ++Function) */
	} /* for (IdSel = 1; IdSel <= MaxAgents; ++IdSel) */
	return HvRc;
}

/*
 * I/0 Memory copy MUST use mmio commands on iSeries
 * To do; For performance, include the hv call directly
 */
void *iSeries_memset_io(void *dest, char c, size_t Count)
{
	u8 ByteValue = c;
	long NumberOfBytes = Count;
	char *IoBuffer = dest;

	while (NumberOfBytes > 0) {
		iSeries_Write_Byte(ByteValue, (void *)IoBuffer);
		++IoBuffer;
		-- NumberOfBytes;
	}
	return dest;
}
EXPORT_SYMBOL(iSeries_memset_io);

void *iSeries_memcpy_toio(void *dest, void *source, size_t count)
{
	char *dst = dest;
	char *src = source;
	long NumberOfBytes = count;

	while (NumberOfBytes > 0) {
		iSeries_Write_Byte(*src++, (void *)dst++);
		-- NumberOfBytes;
	}
	return dest;
}
EXPORT_SYMBOL(iSeries_memcpy_toio);

void *iSeries_memcpy_fromio(void *dest, void *source, size_t count)
{
	char *dst = dest;
	char *src = source;
	long NumberOfBytes = count;

	while (NumberOfBytes > 0) {
		*dst++ = iSeries_Read_Byte((void *)src++);
		-- NumberOfBytes;
	}
	return dest;
}
EXPORT_SYMBOL(iSeries_memcpy_fromio);

/*
 * Look down the chain to find the matching Device Device
 */
static struct iSeries_Device_Node *find_Device_Node(int bus, int devfn)
{
	struct list_head *pos;

	list_for_each(pos, &iSeries_Global_Device_List) {
		struct iSeries_Device_Node *node =
			list_entry(pos, struct iSeries_Device_Node, Device_List);

		if ((bus == ISERIES_BUS(node)) && (devfn == node->DevFn))
			return node;
	}
	return NULL;
}

#if 0
/*
 * Returns the device node for the passed pci_dev
 * Sanity Check Node PciDev to passed pci_dev
 * If none is found, returns a NULL which the client must handle.
 */
static struct iSeries_Device_Node *get_Device_Node(struct pci_dev *pdev)
{
	struct iSeries_Device_Node *node;

	node = pdev->sysdata;
	if (node == NULL || node->PciDev != pdev)
		node = find_Device_Node(pdev->bus->number, pdev->devfn);
	return node;
}
#endif

/*
 * Config space read and write functions.
 * For now at least, we look for the device node for the bus and devfn
 * that we are asked to access.  It may be possible to translate the devfn
 * to a subbus and deviceid more directly.
 */
static u64 hv_cfg_read_func[4]  = {
	HvCallPciConfigLoad8, HvCallPciConfigLoad16,
	HvCallPciConfigLoad32, HvCallPciConfigLoad32
};

static u64 hv_cfg_write_func[4] = {
	HvCallPciConfigStore8, HvCallPciConfigStore16,
	HvCallPciConfigStore32, HvCallPciConfigStore32
};

/*
 * Read PCI config space
 */
static int iSeries_pci_read_config(struct pci_bus *bus, unsigned int devfn,
		int offset, int size, u32 *val)
{
	struct iSeries_Device_Node *node = find_Device_Node(bus->number, devfn);
	u64 fn;
	struct HvCallPci_LoadReturn ret;

	if (node == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	fn = hv_cfg_read_func[(size - 1) & 3];
	HvCall3Ret16(fn, &ret, node->DsaAddr.DsaAddr, offset, 0);

	if (ret.rc != 0) {
		*val = ~0;
		return PCIBIOS_DEVICE_NOT_FOUND;	/* or something */
	}

	*val = ret.value;
	return 0;
}

/*
 * Write PCI config space
 */

static int iSeries_pci_write_config(struct pci_bus *bus, unsigned int devfn,
		int offset, int size, u32 val)
{
	struct iSeries_Device_Node *node = find_Device_Node(bus->number, devfn);
	u64 fn;
	u64 ret;

	if (node == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	fn = hv_cfg_write_func[(size - 1) & 3];
	ret = HvCall4(fn, node->DsaAddr.DsaAddr, offset, val, 0);

	if (ret != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return 0;
}

static struct pci_ops iSeries_pci_ops = {
	.read = iSeries_pci_read_config,
	.write = iSeries_pci_write_config
};

/*
 * Check Return Code
 * -> On Failure, print and log information.
 *    Increment Retry Count, if exceeds max, panic partition.
 * -> If in retry, print and log success 
 *
 * PCI: Device 23.90 ReadL I/O Error( 0): 0x1234
 * PCI: Device 23.90 ReadL Retry( 1)
 * PCI: Device 23.90 ReadL Retry Successful(1)
 */
static int CheckReturnCode(char *TextHdr, struct iSeries_Device_Node *DevNode,
		u64 ret)
{
	if (ret != 0)  {
		++Pci_Error_Count;
		++DevNode->IoRetry;
		printk("PCI: %s: Device 0x%04X:%02X  I/O Error(%2d): 0x%04X\n",
				TextHdr, DevNode->DsaAddr.Dsa.busNumber, DevNode->DevFn,
				DevNode->IoRetry, (int)ret);
		/*
		 * Bump the retry and check for retry count exceeded.
		 * If, Exceeded, panic the system.
		 */
		if ((DevNode->IoRetry > Pci_Retry_Max) &&
				(Pci_Error_Flag > 0)) {
			mf_displaySrc(0xB6000103);
			panic_timeout = 0; 
			panic("PCI: Hardware I/O Error, SRC B6000103, "
					"Automatic Reboot Disabled.\n");
		}
		return -1;	/* Retry Try */
	}
	/* If retry was in progress, log success and rest retry count */
	if (DevNode->IoRetry > 0) {
		PCIFR("%s: Device 0x%04X:%02X Retry Successful(%2d).",
				TextHdr, DevNode->DsaAddr.Dsa.busNumber, DevNode->DevFn,
				DevNode->IoRetry);
		DevNode->IoRetry = 0;
	}
	return 0; 
}

/*
 * Translate the I/O Address into a device node, bar, and bar offset.
 * Note: Make sure the passed variable end up on the stack to avoid
 * the exposure of being device global.
 */
static inline struct iSeries_Device_Node *xlateIoMmAddress(void *IoAddress,
		 u64 *dsaptr, u64 *BarOffsetPtr)
{
	unsigned long BaseIoAddr;
	unsigned long TableIndex;
	struct iSeries_Device_Node *DevNode;

	if (((unsigned long)IoAddress < iSeries_Base_Io_Memory) ||
			((unsigned long)IoAddress >= iSeries_Max_Io_Memory))
		return NULL;
	BaseIoAddr = (unsigned long)IoAddress - iSeries_Base_Io_Memory;
	TableIndex = BaseIoAddr / iSeries_IoMmTable_Entry_Size;
	DevNode = iSeries_IoMmTable[TableIndex];

	if (DevNode != NULL) {
		int barnum = iSeries_IoBarTable[TableIndex];
		*dsaptr = DevNode->DsaAddr.DsaAddr | (barnum << 24);
		*BarOffsetPtr = BaseIoAddr % iSeries_IoMmTable_Entry_Size;
	} else
		panic("PCI: Invalid PCI IoAddress detected!\n");
	return DevNode;
}

/*
 * Read MM I/O Instructions for the iSeries
 * On MM I/O error, all ones are returned and iSeries_pci_IoError is cal
 * else, data is returned in big Endian format.
 *
 * iSeries_Read_Byte = Read Byte  ( 8 bit)
 * iSeries_Read_Word = Read Word  (16 bit)
 * iSeries_Read_Long = Read Long  (32 bit)
 */
u8 iSeries_Read_Byte(void *IoAddress)
{
	u64 BarOffset;
	u64 dsa;
	struct HvCallPci_LoadReturn ret;
	struct iSeries_Device_Node *DevNode =
		xlateIoMmAddress(IoAddress, &dsa, &BarOffset);

	if (DevNode == NULL) {
		static unsigned long last_jiffies;
		static int num_printed;

		if ((jiffies - last_jiffies) > 60 * HZ) {
			last_jiffies = jiffies;
			num_printed = 0;
		}
		if (num_printed++ < 10)
			printk(KERN_ERR "iSeries_Read_Byte: invalid access at IO address %p\n", IoAddress);
		return 0xff;
	}
	do {
		++Pci_Io_Read_Count;
		HvCall3Ret16(HvCallPciBarLoad8, &ret, dsa, BarOffset, 0);
	} while (CheckReturnCode("RDB", DevNode, ret.rc) != 0);

	return (u8)ret.value;
}
EXPORT_SYMBOL(iSeries_Read_Byte);

u16 iSeries_Read_Word(void *IoAddress)
{
	u64 BarOffset;
	u64 dsa;
	struct HvCallPci_LoadReturn ret;
	struct iSeries_Device_Node *DevNode =
		xlateIoMmAddress(IoAddress, &dsa, &BarOffset);

	if (DevNode == NULL) {
		static unsigned long last_jiffies;
		static int num_printed;

		if ((jiffies - last_jiffies) > 60 * HZ) {
			last_jiffies = jiffies;
			num_printed = 0;
		}
		if (num_printed++ < 10)
			printk(KERN_ERR "iSeries_Read_Word: invalid access at IO address %p\n", IoAddress);
		return 0xffff;
	}
	do {
		++Pci_Io_Read_Count;
		HvCall3Ret16(HvCallPciBarLoad16, &ret, dsa,
				BarOffset, 0);
	} while (CheckReturnCode("RDW", DevNode, ret.rc) != 0);

	return swab16((u16)ret.value);
}
EXPORT_SYMBOL(iSeries_Read_Word);

u32 iSeries_Read_Long(void *IoAddress)
{
	u64 BarOffset;
	u64 dsa;
	struct HvCallPci_LoadReturn ret;
	struct iSeries_Device_Node *DevNode =
		xlateIoMmAddress(IoAddress, &dsa, &BarOffset);

	if (DevNode == NULL) {
		static unsigned long last_jiffies;
		static int num_printed;

		if ((jiffies - last_jiffies) > 60 * HZ) {
			last_jiffies = jiffies;
			num_printed = 0;
		}
		if (num_printed++ < 10)
			printk(KERN_ERR "iSeries_Read_Long: invalid access at IO address %p\n", IoAddress);
		return 0xffffffff;
	}
	do {
		++Pci_Io_Read_Count;
		HvCall3Ret16(HvCallPciBarLoad32, &ret, dsa,
				BarOffset, 0);
	} while (CheckReturnCode("RDL", DevNode, ret.rc) != 0);

	return swab32((u32)ret.value);
}
EXPORT_SYMBOL(iSeries_Read_Long);

/*
 * Write MM I/O Instructions for the iSeries
 *
 * iSeries_Write_Byte = Write Byte (8 bit)
 * iSeries_Write_Word = Write Word(16 bit)
 * iSeries_Write_Long = Write Long(32 bit)
 */
void iSeries_Write_Byte(u8 data, void *IoAddress)
{
	u64 BarOffset;
	u64 dsa;
	u64 rc;
	struct iSeries_Device_Node *DevNode =
		xlateIoMmAddress(IoAddress, &dsa, &BarOffset);

	if (DevNode == NULL) {
		static unsigned long last_jiffies;
		static int num_printed;

		if ((jiffies - last_jiffies) > 60 * HZ) {
			last_jiffies = jiffies;
			num_printed = 0;
		}
		if (num_printed++ < 10)
			printk(KERN_ERR "iSeries_Write_Byte: invalid access at IO address %p\n", IoAddress);
		return;
	}
	do {
		++Pci_Io_Write_Count;
		rc = HvCall4(HvCallPciBarStore8, dsa, BarOffset, data, 0);
	} while (CheckReturnCode("WWB", DevNode, rc) != 0);
}
EXPORT_SYMBOL(iSeries_Write_Byte);

void iSeries_Write_Word(u16 data, void *IoAddress)
{
	u64 BarOffset;
	u64 dsa;
	u64 rc;
	struct iSeries_Device_Node *DevNode =
		xlateIoMmAddress(IoAddress, &dsa, &BarOffset);

	if (DevNode == NULL) {
		static unsigned long last_jiffies;
		static int num_printed;

		if ((jiffies - last_jiffies) > 60 * HZ) {
			last_jiffies = jiffies;
			num_printed = 0;
		}
		if (num_printed++ < 10)
			printk(KERN_ERR "iSeries_Write_Word: invalid access at IO address %p\n", IoAddress);
		return;
	}
	do {
		++Pci_Io_Write_Count;
		rc = HvCall4(HvCallPciBarStore16, dsa, BarOffset, swab16(data), 0);
	} while (CheckReturnCode("WWW", DevNode, rc) != 0);
}
EXPORT_SYMBOL(iSeries_Write_Word);

void iSeries_Write_Long(u32 data, void *IoAddress)
{
	u64 BarOffset;
	u64 dsa;
	u64 rc;
	struct iSeries_Device_Node *DevNode =
		xlateIoMmAddress(IoAddress, &dsa, &BarOffset);

	if (DevNode == NULL) {
		static unsigned long last_jiffies;
		static int num_printed;

		if ((jiffies - last_jiffies) > 60 * HZ) {
			last_jiffies = jiffies;
			num_printed = 0;
		}
		if (num_printed++ < 10)
			printk(KERN_ERR "iSeries_Write_Long: invalid access at IO address %p\n", IoAddress);
		return;
	}
	do {
		++Pci_Io_Write_Count;
		rc = HvCall4(HvCallPciBarStore32, dsa, BarOffset, swab32(data), 0);
	} while (CheckReturnCode("WWL", DevNode, rc) != 0);
}
EXPORT_SYMBOL(iSeries_Write_Long);

void pcibios_name_device(struct pci_dev *dev)
{
}
