/*
 * Support for indirect PCI bridges.
 *
 * Copyright (C) 1998 Gabriel Paubert.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>

#ifdef CONFIG_PPC_INDIRECT_PCI_BE
#define PCI_CFG_OUT out_be32
#else
#define PCI_CFG_OUT out_le32
#endif

static int
indirect_read_config(struct pci_bus *bus, unsigned int devfn, int offset,
		     int len, u32 *val)
{
	struct pci_controller *hose = bus->sysdata;
	volatile unsigned char *cfg_data;
	u8 cfg_type = 0;

	if (ppc_md.pci_exclude_device)
		if (ppc_md.pci_exclude_device(bus->number, devfn))
			return PCIBIOS_DEVICE_NOT_FOUND;
	
	if (hose->set_cfg_type)
		if (bus->number != hose->first_busno)
			cfg_type = 1;

	PCI_CFG_OUT(hose->cfg_addr, 					 
		 (0x80000000 | ((bus->number - hose->bus_offset) << 16)
		  | (devfn << 8) | ((offset & 0xfc) | cfg_type)));

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	cfg_data = hose->cfg_data + (offset & 3);
	switch (len) {
	case 1:
		*val = in_8((u8 *)cfg_data);
		break;
	case 2:
		*val = in_le16((u16 *)cfg_data);
		break;
	default:
		*val = in_le32((u32 *)cfg_data);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int
indirect_write_config(struct pci_bus *bus, unsigned int devfn, int offset,
		      int len, u32 val)
{
	struct pci_controller *hose = bus->sysdata;
	volatile unsigned char *cfg_data;
	u8 cfg_type = 0;

	if (ppc_md.pci_exclude_device)
		if (ppc_md.pci_exclude_device(bus->number, devfn))
			return PCIBIOS_DEVICE_NOT_FOUND;

	if (hose->set_cfg_type)
		if (bus->number != hose->first_busno)
			cfg_type = 1;

	PCI_CFG_OUT(hose->cfg_addr, 					 
		 (0x80000000 | ((bus->number - hose->bus_offset) << 16)
		  | (devfn << 8) | ((offset & 0xfc) | cfg_type)));

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	cfg_data = hose->cfg_data + (offset & 3);
	switch (len) {
	case 1:
		out_8((u8 *)cfg_data, val);
		break;
	case 2:
		out_le16((u16 *)cfg_data, val);
		break;
	default:
		out_le32((u32 *)cfg_data, val);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops indirect_pci_ops =
{
	indirect_read_config,
	indirect_write_config
};

void __init
setup_indirect_pci(struct pci_controller* hose, u32 cfg_addr, u32 cfg_data)
{
	unsigned long base = cfg_addr & PAGE_MASK;
	char *mbase;

	mbase = ioremap(base, PAGE_SIZE);
	hose->cfg_addr = (unsigned int *)(mbase + (cfg_addr & ~PAGE_MASK));
	if ((cfg_data & PAGE_MASK) != base)
		mbase = ioremap(cfg_data & PAGE_MASK, PAGE_SIZE);
	hose->cfg_data = (unsigned char *)(mbase + (cfg_data & ~PAGE_MASK));
	hose->ops = &indirect_pci_ops;
}
