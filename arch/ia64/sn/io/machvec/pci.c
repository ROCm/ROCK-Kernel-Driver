/* 
 * SNI64 specific PCI support for SNI IO.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1997, 1998, 2000-2003 Silicon Graphics, Inc.  All rights reserved.
 */
#include <asm/sn/hcl.h>
#include <asm/sn/pci/pcibr_private.h>

/*
 * These routines are only used during sn_pci_init for probing each bus, and
 * can probably be removed with a little more cleanup now that the SAL routines
 * work on sn2.
 */

extern vertex_hdl_t devfn_to_vertex(unsigned char bus, unsigned char devfn);

int sn_read_config(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *val)
{
	unsigned long res = 0;
	vertex_hdl_t device_vertex;

	device_vertex = devfn_to_vertex(bus->number, devfn);

	if (!device_vertex)
		return PCIBIOS_DEVICE_NOT_FOUND;

	res = pciio_config_get(device_vertex, (unsigned)where, size);
	*val = (u32)res;
	return PCIBIOS_SUCCESSFUL;
}

int sn_write_config(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 val)
{
	vertex_hdl_t device_vertex;

	device_vertex = devfn_to_vertex(bus->number, devfn);

	if (!device_vertex)
		return PCIBIOS_DEVICE_NOT_FOUND;

	pciio_config_set(device_vertex, (unsigned)where, size, (uint64_t)val);
	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops sn_pci_ops = {
	.read = sn_read_config,
	.write = sn_write_config,
};
