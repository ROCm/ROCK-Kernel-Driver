/*
 * arch/ppc/platforms/mcpn765_pci.c
 *
 * PCI setup routines for the Motorola MCG MCPN765 cPCI board.
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/pplus.h>

#include "mcpn765.h"


/*
 * Motorola MCG MCPN765 interrupt routing.
 */
static inline int
mcpn765_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	/*
	 *	PCI IDSEL/INTPIN->INTLINE
	 * 	   A   B   C   D
	 */
	{
		{ 14,  0,  0,  0 },	/* IDSEL 11 - have to manually set */
		{  0,  0,  0,  0 },	/* IDSEL 12 - unused */
		{  0,  0,  0,  0 },	/* IDSEL 13 - unused */
		{ 18,  0,  0,  0 },	/* IDSEL 14 - Enet 0 */
		{  0,  0,  0,  0 },	/* IDSEL 15 - unused */
		{ 25, 26, 27, 28 },	/* IDSEL 16 - PMC Slot 1 */
		{ 28, 25, 26, 27 },	/* IDSEL 17 - PMC Slot 2 */
		{  0,  0,  0,  0 },	/* IDSEL 18 - PMC 2B Connector XXXX */
		{ 29,  0,  0,  0 },	/* IDSEL 19 - Enet 1 */
		{ 20,  0,  0,  0 },	/* IDSEL 20 - 21554 cPCI bridge */
	};

	const long min_idsel = 11, max_idsel = 20, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}

void __init
mcpn765_find_bridges(void)
{
	struct pci_controller	*hose;

	hose = pcibios_alloc_controller();

	if (!hose)
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;
	hose->pci_mem_offset = MCPN765_PCI_PHY_MEM_OFFSET;

	pci_init_resource(&hose->io_resource,
			MCPN765_PCI_IO_START,
			MCPN765_PCI_IO_END,
			IORESOURCE_IO,
			"PCI host bridge");

	pci_init_resource(&hose->mem_resources[0],
			MCPN765_PCI_MEM_START,
			MCPN765_PCI_MEM_END,
			IORESOURCE_MEM,
			"PCI host bridge");

	hose->io_space.start = MCPN765_PCI_IO_START;
	hose->io_space.end = MCPN765_PCI_IO_END;
	hose->mem_space.start = MCPN765_PCI_MEM_START;
	hose->mem_space.end = MCPN765_PCI_MEM_END - PPLUS_MPIC_SIZE;

	if (pplus_init(hose,
		       MCPN765_HAWK_PPC_REG_BASE,
		       MCPN765_PROC_PCI_MEM_START,
		       MCPN765_PROC_PCI_MEM_END - PPLUS_MPIC_SIZE,
		       MCPN765_PROC_PCI_IO_START,
		       MCPN765_PROC_PCI_IO_END,
		       MCPN765_PCI_MEM_END - PPLUS_MPIC_SIZE + 1) != 0) {
		printk("Could not initialize HAWK bridge\n");
	}

	hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

	ppc_md.pcibios_fixup = NULL;
	ppc_md.pcibios_fixup_bus = NULL;
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = mcpn765_map_irq;

	return;
}
