/************************************************************************/
/* This module supports the iSeries PCI bus device detection            */
/* Copyright (C) 20yy  <Robert L Holtorf> <IBM Corp>                    */
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
/*   Created, December 13, 2000 by Wayne Holm                           */ 
/* End Change Activity                                                  */
/************************************************************************/
#include <linux/pci.h>
#include <linux/ide.h>
#include <asm/pci-bridge.h>
#include <asm/iSeries/HvCallXm.h>
#include <asm/iSeries/HvCallPci.h>
#include <asm/iSeries/iSeries_fixup.h>
#include <asm/iSeries/LparData.h>
#include <asm/iSeries/iSeries_irq.h>
#include <asm/iSeries/iSeries_dma.h>
#include <asm/iSeries/iSeries_VpdInfo.h>
#include <asm/iSeries/iSeries_pci.h>
#include "iSeries_IoMmTable.h"
#include "iSeries_pci.h"



unsigned int __init iSeries_scan_slot(struct pci_dev* temp_dev,
					   u16 hvBus, u8 slotSubBus, u8 maxAgents)
{
    u8 hvIdSel, hvFunction, hvAgentId;
    u64 hvRc = 0;
    u64 noConnectRc = 0xFFFF;
    HvAgentId slotAgentId;
    int irq;

    slotAgentId = ISERIES_PCI_AGENTID(ISERIES_GET_DEVICE_FROM_SUBBUS(slotSubBus), ISERIES_GET_FUNCTION_FROM_SUBBUS(slotSubBus));
    irq = iSeries_allocate_IRQ(hvBus, 0, slotAgentId);
    for (hvIdSel = 1; hvIdSel <= maxAgents; ++hvIdSel) {
	/* Connect all functions of any device found.  However, only call pci_scan_slot
	   once for each idsel.  pci_scan_slot handles multifunction devices appropriately */
	for (hvFunction = 0; hvFunction < 8 && hvRc == 0; ++hvFunction) {
	    hvAgentId = ISERIES_PCI_AGENTID(hvIdSel, hvFunction);
	    hvRc = HvCallXm_connectBusUnit(hvBus, slotSubBus, hvAgentId, irq);
	    if (hvRc == 0) {
		noConnectRc = 0;
		HvCallPci_configStore8(hvBus, slotSubBus, hvAgentId, PCI_INTERRUPT_LINE, irq);  // Store the irq in the interrupt line register of the function config space
	    }
	}
	if (noConnectRc == 0) {
	    /* This assumes that the node slot is always on the primary bus! */
	    temp_dev->devfn = ISERIES_ENCODE_SUBBUS(ISERIES_GET_DEVICE_FROM_SUBBUS(slotSubBus),
						     ISERIES_GET_FUNCTION_FROM_SUBBUS(slotSubBus),
						     0);
	    iSeries_assign_IRQ(irq, hvBus, 0, slotAgentId);
	    pci_scan_slot(temp_dev);
	}
    }
    return 0;
}
	
void __init iSeries_fixup_bus(struct pci_bus* bus)
{
    struct pci_dev temp_dev;
    struct pci_controller* hose;
    u16 hvBus;
    u8 hvSubBus, hvIdSel, hvFunction, hvAgentId, maxAgents, irq;
    u64 hvRc, devInfoRealAdd, bridgeInfoRealAdd;
    struct HvCallPci_DeviceInfo* devInfo;
    struct HvCallPci_BridgeInfo* bridgeInfo;
    u64 noConnectRc = 0xFFFF;

    hose = (struct pci_controller*)(bus->sysdata);
    /* Get the hv bus number from the hose arch_data */
    hvBus = ISERIES_GET_HOSE_HV_BUSNUM(hose);
    /* Initialize the global bus number map for this bus */
    iSeries_GlobalBusMap[bus->number][_HVBUSNUMBER_] = hvBus;
    iSeries_GlobalBusMap[bus->number][_HVSUBBUSNUMBER_] = 0;
    maxAgents = 7;
    /* If not a primary bus, set the hypervisor subBus number of this bus into the map */
    if (bus->parent != NULL) { 
	bridgeInfo = kmalloc(sizeof(*bridgeInfo), GFP_KERNEL);
	/* Perform linux virtual address to iSeries PLIC real address translation */
	bridgeInfoRealAdd = virt_to_absolute((u32)bridgeInfo);
	bridgeInfoRealAdd = bridgeInfoRealAdd | 0x8000000000000000;  
	/* Find the Hypervisor address of the bridge which created this bus... */
	if (ISERIES_IS_SUBBUS_ENCODED_IN_DEVFN(bus->self->devfn)) {  // This assumes that the bus passed to this function is connected to an EADS slot.  The subbus encoding algorithm only applies to bridge cards attached directly to EADS.  Future TODO is to allow for n levels of bridging.
	    hvSubBus = ISERIES_DEVFN_DECODE_SUBBUS(bus->self->devfn);
	    hvIdSel = 1;
	}
	else {
	    /* The hose arch_data needs to map Linux bus number -> PHB bus/subBus number
               Could also use a global table, might be cheaper for multiple PHBs */
	    // hvSubBus = iSeries_GlobalBusMap[bus->parent->number][_HVSUBBUSNUMBER_];
	    hvSubBus = 0;  // The bridge card devfn is not subbus encoded and is directly attached to the PCI primary bus.  Its subbus number is 0 and the card config space is accessed via type 0 config cycles.
	    hvIdSel = PCI_SLOT(bus->self->devfn);
	}
	hvAgentId = ISERIES_PCI_AGENTID(hvIdSel, PCI_FUNC(bus->self->devfn));
	/* Now we know the HV bus/subbus/agent of the bridge creating this bus,
           go get the subbus number from HV */
	hvRc = HvCallPci_getBusUnitInfo(hvBus, hvSubBus, hvAgentId,
					bridgeInfoRealAdd, sizeof(*bridgeInfo));
	if (hvRc != 0 || bridgeInfo->busUnitInfo.deviceType != HvCallPci_BridgeDevice) {
	    kfree(bridgeInfo);
	    // return -1;
	}
	iSeries_GlobalBusMap[bus->number][_HVSUBBUSNUMBER_] = bridgeInfo->subBusNumber;
	maxAgents = bridgeInfo->maxAgents;
	kfree(bridgeInfo);
    }
    /* Bus number mapping is complete, from here on cfgIos should result in HvCalls */

    hvSubBus = iSeries_GlobalBusMap[bus->number][_HVSUBBUSNUMBER_];

    devInfo = kmalloc(sizeof(*devInfo), GFP_KERNEL);
    devInfoRealAdd = virt_to_absolute((u32)devInfo);
    devInfoRealAdd = devInfoRealAdd | 0x8000000000000000;
    memset(&temp_dev, 0, sizeof(temp_dev));
    temp_dev.bus = bus;
    temp_dev.sysdata = bus->sysdata;

    for (hvIdSel=1; hvIdSel <= maxAgents; ++hvIdSel) {
	hvRc = HvCallPci_getDeviceInfo(hvBus, hvSubBus, hvIdSel,
				       devInfoRealAdd, sizeof(*devInfo));
	if (hvRc == 0) {
	    switch(devInfo->deviceType) {
		case HvCallPci_NodeDevice:
		    /* bridgeInfo = kmalloc(HvCallPci_MaxBusUnitInfoSize, GFP_KERNEL); */
		    bridgeInfo = kmalloc(sizeof(*bridgeInfo), GFP_KERNEL);
		    /* Loop through each node function to find usable bridges.  Scan
		       the node bridge to create devices.  The devices will appear
		       as if they were connected to the primary bus. */
		    for (hvFunction=0; hvFunction < 8; ++hvFunction) {
			hvAgentId = ISERIES_PCI_AGENTID(hvIdSel, hvFunction);
			irq = 0;
			/* Note: hvSubBus should always be 0 here! */
			hvRc = HvCallXm_connectBusUnit(hvBus, hvSubBus, hvAgentId, irq);
			if (hvRc == 0) {
			    bridgeInfoRealAdd = virt_to_absolute((u32)bridgeInfo);
			    bridgeInfoRealAdd = bridgeInfoRealAdd | 0x8000000000000000;
			    hvRc = HvCallPci_getBusUnitInfo(hvBus, hvSubBus, hvAgentId,
							    bridgeInfoRealAdd, sizeof(*bridgeInfo));
			    if (hvRc == 0 && bridgeInfo->busUnitInfo.deviceType == HvCallPci_BridgeDevice)
			    {
				// scan any card plugged into this slot
				iSeries_scan_slot(&temp_dev, hvBus, bridgeInfo->subBusNumber,
						bridgeInfo->maxAgents);
			    }
			}
		    }
		    kfree(bridgeInfo);
		    break;

		case HvCallPci_MultiFunctionDevice:
		    /* Attempt to connect each device function, then use architecture independent
		       pci_scan_slot to build the device(s) */
		    irq = bus->self->irq;  // Assume that this multi-function device is attached to a secondary bus.  Get the irq from the dev struct for the bus and pass to Hv on the function connects as well as write it into the interrupt line registers of each function
		    for (hvFunction=0; hvFunction < 8 && hvRc == 0; ++hvFunction) {
			hvAgentId = ISERIES_PCI_AGENTID(hvIdSel, hvFunction);
			/* Try to connect each function. */
			hvRc = HvCallXm_connectBusUnit(hvBus, hvSubBus, hvAgentId, irq);
			if (hvRc == 0) {
			    noConnectRc = 0;
			    HvCallPci_configStore8(hvBus, hvSubBus, hvAgentId, PCI_INTERRUPT_LINE, irq);  // Store the irq in the interrupt line register of the function config space
			}
		    }
		    if (noConnectRc == 0) {
			noConnectRc = 0xFFFF;  // Reset to error value in case other multi-function devices are attached to this bus
			// Note: using hvIdSel assumes this device is on a secondary bus!
			temp_dev.devfn = PCI_DEVFN(hvIdSel, 0);
			pci_scan_slot(&temp_dev);
		    }
		    break;

		case HvCallPci_BridgeDevice:
		case HvCallPci_IoaDevice:
		    /* Single function devices, just try to connect and use pci_scan_slot to
		       build the device */
		    irq = bus->self->irq;
		    hvAgentId = ISERIES_PCI_AGENTID(hvIdSel, 0);
		    hvRc = HvCallXm_connectBusUnit(hvBus, hvSubBus, hvAgentId, irq);
		    if (hvRc == 0) {
			HvCallPci_configStore8(hvBus, hvSubBus, hvAgentId, PCI_INTERRUPT_LINE, irq);  // Store the irq in the interrupt line register of the device config space
			// Note: using hvIdSel assumes this device is on a secondary bus!
			temp_dev.devfn = PCI_DEVFN(hvIdSel, 0);
			pci_scan_slot(&temp_dev);
		    }
		    break;

		default :	/* Unrecognized device */
		    break;

	    };			/* end of switch */
	}
    }

    kfree(devInfo);
    // return 0;
}
/*  Initialize bar space base and limit addresses for each device in the tree   */
void __init iSeries_fixup( void ) {
    struct pci_dev *dev;
    u8     LinuxBus, iSeriesBus, LastBusNumber;
    char   DeviceInfoBuffer[256];

    /*********************************************************/
    /* PCI: Allocate Bars space for each device              */
    /*********************************************************/
    pci_for_each_dev(dev) {
	iSeries_allocateDeviceBars(dev);
    }
    /*********************************************************/
    /* Create the TCEs for each iSeries bus now that we know */
    /* how many buses there are.  Need only create TCE for   */
    /* for each iSeries bus.  Multiple linux buses could     */
    /* be on the same iSeries bus.                       AHT */
    /*********************************************************/
    LastBusNumber = 0xFF;			/* Invalid   */
    for( LinuxBus = 0; LinuxBus < 255; ++ LinuxBus) {
	iSeriesBus = ISERIES_GET_LPAR_BUS(LinuxBus);
	if(iSeriesBus == 0xFF) break;	        /* Done      */
	else if(LastBusNumber != iSeriesBus ){	/* New Bus   */
	    create_pci_bus_tce_table(iSeriesBus);
	    LastBusNumber = iSeriesBus;		/* Remember  */
	}
    }
    /*********************************************************/
    /* List out all the PCI devices found... This will go    */
    /* into the etc/proc/iSeries/pci info as well....        */
    /* This is to help service figure out who is who......   */
    /*********************************************************/
    pci_for_each_dev(dev) {
	struct resource* BarResource = &dev->resource[0];
	iSeries_Device_Information(dev,DeviceInfoBuffer,256);
	printk("%s\n",DeviceInfoBuffer);
	printk("PCI: Bus%3d, Device%3d, %s at 0x%08x, irq %3d\n",
	             dev->bus->number, PCI_SLOT(dev->devfn),dev->name,(int)BarResource->start,dev->irq);
    }
    /* */
    iSeries_activate_IRQs();  // Unmask all device interrupts for assigned IRQs
}

