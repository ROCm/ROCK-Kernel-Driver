/*
 * PCI autoconfiguration library
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * Copyright 2000, 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

/*
 * Modified for MIPS by Jun Sun, jsun@mvista.com
 *
 * . Simplify the interface between pci_auto and the rest: a single function.
 * . Assign resources from low address to upper address.
 * . change most int to u32.
 *
 * Further modified to include it as mips generic code, ppopov@mvista.com.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>

#include <asm/pci_channel.h>

#define	DEBUG
#ifdef 	DEBUG
#define	DBG(x...)	printk(x)
#else
#define	DBG(x...)	
#endif

/* These are used for config access before all the PCI probing has been done. */
int early_read_config_byte(struct pci_channel *hose, int bus, int dev_fn, int where, u8 *val);
int early_read_config_word(struct pci_channel *hose, int bus, int dev_fn, int where, u16 *val);
int early_read_config_dword(struct pci_channel *hose, int bus, int dev_fn, int where, u32 *val);
int early_write_config_byte(struct pci_channel *hose, int bus, int dev_fn, int where, u8 val);
int early_write_config_word(struct pci_channel *hose, int bus, int dev_fn, int where, u16 val);
int early_write_config_dword(struct pci_channel *hose, int bus, int dev_fn, int where, u32 val);

static u32 pciauto_lower_iospc;
static u32 pciauto_upper_iospc;

static u32 pciauto_lower_memspc;
static u32 pciauto_upper_memspc;

void __init 
pciauto_setup_bars(struct pci_channel *hose,
		   int current_bus,
		   int pci_devfn)
{
	u32 bar_response, bar_size, bar_value;
	u32 bar, addr_mask, bar_nr = 0;
	u32 * upper_limit;
	u32 * lower_limit;
	int found_mem64 = 0;

	DBG("PCI Autoconfig: Found Bus %d, Device %d, Function %d\n",
	    current_bus, PCI_SLOT(pci_devfn), PCI_FUNC(pci_devfn) );

	for (bar = PCI_BASE_ADDRESS_0; bar <= PCI_BASE_ADDRESS_5; bar+=4) {
		/* Tickle the BAR and get the response */
		early_write_config_dword(hose,
					 current_bus,
					 pci_devfn,
					 bar,
					 0xffffffff);
		early_read_config_dword(hose,
					current_bus,
					pci_devfn,
					bar,
					&bar_response);

		/* If BAR is not implemented go to the next BAR */
		if (!bar_response)
			continue;

		/* Check the BAR type and set our address mask */
		if (bar_response & PCI_BASE_ADDRESS_SPACE) {
			addr_mask = PCI_BASE_ADDRESS_IO_MASK;
			upper_limit = &pciauto_upper_iospc;
			lower_limit = &pciauto_lower_iospc;
			DBG("PCI Autoconfig: BAR %d, I/O, ", bar_nr);
		} else {
			if ((bar_response & PCI_BASE_ADDRESS_MEM_TYPE_MASK) ==
			    PCI_BASE_ADDRESS_MEM_TYPE_64)
				found_mem64 = 1;

			addr_mask = PCI_BASE_ADDRESS_MEM_MASK;		
			upper_limit = &pciauto_upper_memspc;
			lower_limit = &pciauto_lower_memspc;
			DBG("PCI Autoconfig: BAR %d, Mem, ", bar_nr);
		}

		/* Calculate requested size */
		bar_size = ~(bar_response & addr_mask) + 1;

		/* Allocate a base address */
		bar_value = ((*lower_limit - 1) & ~(bar_size - 1)) + bar_size;

		/* Write it out and update our limit */
		early_write_config_dword(hose, current_bus, pci_devfn,
					 bar, bar_value);

		*lower_limit = bar_value + bar_size;

		/*
		 * If we are a 64-bit decoder then increment to the
		 * upper 32 bits of the bar and force it to locate
		 * in the lower 4GB of memory.
		 */ 
		if (found_mem64) {
			bar += 4;
			early_write_config_dword(hose,
						 current_bus,
						 pci_devfn,
						 bar,
						 0x00000000);
		}

		bar_nr++;

		DBG("size=0x%x, address=0x%x\n",
		    bar_size, bar_value);
	}

}

void __init
pciauto_prescan_setup_bridge(struct pci_channel *hose,
			     int current_bus,
			     int pci_devfn,
			     int sub_bus)
{
	int cmdstat;

	/* Configure bus number registers */
	early_write_config_byte(hose, current_bus, pci_devfn,
	                        PCI_PRIMARY_BUS, current_bus);
	early_write_config_byte(hose, current_bus, pci_devfn,
				PCI_SECONDARY_BUS, sub_bus + 1);
	early_write_config_byte(hose, current_bus, pci_devfn,
				PCI_SUBORDINATE_BUS, 0xff);

	/* Round memory allocator to 1MB boundary */
	pciauto_upper_memspc &= ~(0x100000 - 1);

	/* Round I/O allocator to 4KB boundary */
	pciauto_upper_iospc &= ~(0x1000 - 1);

	/* Set up memory and I/O filter limits, assume 32-bit I/O space */
	early_write_config_word(hose, current_bus, pci_devfn, PCI_MEMORY_LIMIT,
				((pciauto_upper_memspc - 1) & 0xfff00000) >> 16);
	early_write_config_byte(hose, current_bus, pci_devfn, PCI_IO_LIMIT,
				((pciauto_upper_iospc - 1) & 0x0000f000) >> 8);
	early_write_config_word(hose, current_bus, pci_devfn,
				PCI_IO_LIMIT_UPPER16,
				((pciauto_upper_iospc - 1) & 0xffff0000) >> 16);
			
	/* We don't support prefetchable memory for now, so disable */
	early_write_config_word(hose, current_bus, pci_devfn,
				PCI_PREF_MEMORY_BASE, 0x1000);
	early_write_config_word(hose, current_bus, pci_devfn,
				PCI_PREF_MEMORY_LIMIT, 0x1000);

	/* Enable memory and I/O accesses, enable bus master */
	early_read_config_dword(hose, current_bus, pci_devfn, PCI_COMMAND,
				&cmdstat);
	early_write_config_dword(hose, current_bus, pci_devfn, PCI_COMMAND,
				 cmdstat | PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
				 PCI_COMMAND_MASTER);
}

void __init
pciauto_postscan_setup_bridge(struct pci_channel *hose,
			      int current_bus,
			      int pci_devfn,
			      int sub_bus)
{
	/* Configure bus number registers */
	early_write_config_byte(hose, current_bus, pci_devfn,
				PCI_SUBORDINATE_BUS, sub_bus);

	/* Round memory allocator to 1MB boundary */
	pciauto_upper_memspc &= ~(0x100000 - 1);
	early_write_config_word(hose, current_bus, pci_devfn, PCI_MEMORY_BASE,
				pciauto_upper_memspc >> 16);

	/* Round I/O allocator to 4KB boundary */
	pciauto_upper_iospc &= ~(0x1000 - 1);
	early_write_config_byte(hose, current_bus, pci_devfn, PCI_IO_BASE,
				(pciauto_upper_iospc & 0x0000f000) >> 8);
	early_write_config_word(hose, current_bus, pci_devfn,
				PCI_IO_BASE_UPPER16, pciauto_upper_iospc >> 16);
}

#define      PCIAUTO_IDE_MODE_MASK           0x05

int __init
pciauto_bus_scan(struct pci_channel *hose, int current_bus)
{
	int sub_bus;
	u32 pci_devfn, pci_class, cmdstat, found_multi=0;
	unsigned short vid;
	unsigned char header_type;
	int devfn_start = 0;
	int devfn_stop = 0xff;

	sub_bus = current_bus;
	
	if (hose->first_devfn)
		devfn_start = hose->first_devfn;
	if (hose->last_devfn)
		devfn_stop = hose->last_devfn;
	
	for (pci_devfn=devfn_start; pci_devfn<devfn_stop; pci_devfn++) {

		if (PCI_FUNC(pci_devfn) && !found_multi)
			continue;

		early_read_config_byte(hose, current_bus, pci_devfn,
				       PCI_HEADER_TYPE, &header_type);

		if (!PCI_FUNC(pci_devfn))
			found_multi = header_type & 0x80;

		early_read_config_word(hose, current_bus, pci_devfn,
				       PCI_VENDOR_ID, &vid);

		if (vid == 0xffff) continue;

		early_read_config_dword(hose, current_bus, pci_devfn,
					PCI_CLASS_REVISION, &pci_class);
		if ((pci_class >> 16) == PCI_CLASS_BRIDGE_PCI) {
			DBG("PCI Autoconfig: Found P2P bridge, device %d\n", PCI_SLOT(pci_devfn));
			pciauto_prescan_setup_bridge(hose, current_bus,
						     pci_devfn, sub_bus);
			sub_bus = pciauto_bus_scan(hose, sub_bus+1);
			pciauto_postscan_setup_bridge(hose, current_bus,
						      pci_devfn, sub_bus);

		} else if ((pci_class >> 16) == PCI_CLASS_STORAGE_IDE) {

			unsigned char prg_iface;

			early_read_config_byte(hose, current_bus, pci_devfn,
					       PCI_CLASS_PROG, &prg_iface);
			if (!(prg_iface & PCIAUTO_IDE_MODE_MASK)) {
				DBG("PCI Autoconfig: Skipping legacy mode IDE controller\n");
				continue;
			}
		}

		/*
		 * Found a peripheral, enable some standard
		 * settings
		 */
		early_read_config_dword(hose, current_bus, pci_devfn,
					PCI_COMMAND, &cmdstat);
		early_write_config_dword(hose, current_bus, pci_devfn,
					 PCI_COMMAND, cmdstat | PCI_COMMAND_IO |
					 PCI_COMMAND_MEMORY |
					 PCI_COMMAND_MASTER);
		early_write_config_byte(hose, current_bus, pci_devfn,
					PCI_LATENCY_TIMER, 0x80);

		/* Allocate PCI I/O and/or memory space */
		pciauto_setup_bars(hose, current_bus, pci_devfn);
	}
	return sub_bus;
}

int __init
pciauto_assign_resources(int busno, struct pci_channel *hose)
{
	/* setup resource limits */
	pciauto_lower_iospc = hose->io_resource->start;
	pciauto_upper_iospc = hose->io_resource->end + 1;
	pciauto_lower_memspc = hose->mem_resource->start;
	pciauto_upper_memspc = hose->mem_resource->end + 1;

	return pciauto_bus_scan(hose, busno);
}


/*
 * These functions are used early on before PCI scanning is done
 * and all of the pci_dev and pci_bus structures have been created.
 */
static struct pci_dev *fake_pci_dev(struct pci_channel *hose, int busnr,
                                    int devfn)
{
	static struct pci_dev dev;
	static struct pci_bus bus;

	dev.bus = &bus;
	dev.sysdata = hose;
	dev.devfn = devfn;
	bus.number = busnr;
	bus.ops = hose->pci_ops;

	return &dev;
}

#define EARLY_PCI_OP(rw, size, type)					\
int early_##rw##_config_##size(struct pci_channel *hose, int bus,	\
                               int devfn, int offset, type value)	\
{									\
	return pci_##rw##_config_##size(fake_pci_dev(hose, bus, devfn),	\
	                                offset, value);			\
}

EARLY_PCI_OP(read, byte, u8 *)
EARLY_PCI_OP(read, word, u16 *)
EARLY_PCI_OP(read, dword, u32 *)
EARLY_PCI_OP(write, byte, u8)
EARLY_PCI_OP(write, word, u16)
EARLY_PCI_OP(write, dword, u32)
