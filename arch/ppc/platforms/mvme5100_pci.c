/*
 * arch/ppc/platforms/mvme5100_pci.c
 *
 * PCI setup routines for the Motorola MVME5100.
 *
 * Author: Matt Porter <mporter@mvista.com>
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
#include <platforms/mvme5100.h>
#include <asm/pplus.h>

static inline int
mvme5100_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	int irq;

	static char pci_irq_table[][4] =
	/*
	 *	PCI IDSEL/INTPIN->INTLINE
	 * 	   A   B   C   D
	 */
	{
		{  0,  0,  0,  0 },	/* IDSEL 11 - Winbond */
		{  0,  0,  0,  0 },	/* IDSEL 12 - unused */
		{ 21, 22, 23, 24 },	/* IDSEL 13 - Universe II */
		{ 18,  0,  0,  0 },	/* IDSEL 14 - Enet 1 */
		{  0,  0,  0,  0 },	/* IDSEL 15 - unused */
		{ 25, 26, 27, 28 },	/* IDSEL 16 - PMC Slot 1 */
		{ 28, 25, 26, 27 },	/* IDSEL 17 - PMC Slot 2 */
		{  0,  0,  0,  0 },	/* IDSEL 18 - unused */
		{ 29,  0,  0,  0 },	/* IDSEL 19 - Enet 2 */
		{  0,  0,  0,  0 },	/* IDSEL 20 - PMCSPAN */
	};

	const long min_idsel = 11, max_idsel = 20, irqs_per_slot = 4;
	irq = PCI_IRQ_TABLE_LOOKUP;
	/* If lookup is zero, always return 0 */
	if (!irq)
		return 0;
	else
#ifdef CONFIG_MVME5100_IPMC761_PRESENT
	/* If IPMC761 present, return table value */
	return irq;
#else
	/* If IPMC761 not present, we don't have an i8259 so adjust */
	return (irq - NUM_8259_INTERRUPTS);
#endif
}

static void
mvme5100_pcibios_fixup_resources(struct pci_dev *dev)
{
	int i;

	if ((dev->vendor == PCI_VENDOR_ID_MOTOROLA) &&
			(dev->device == PCI_DEVICE_ID_MOTOROLA_HAWK))
		for (i=0; i<DEVICE_COUNT_RESOURCE; i++)
		{
			dev->resource[i].start = 0;
			dev->resource[i].end = 0;
		}
}

void __init
mvme5100_setup_bridge(void)
{
	struct pci_controller*	hose;

	hose = pcibios_alloc_controller();

	if (!hose)
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;
	hose->pci_mem_offset = MVME5100_PCI_MEM_OFFSET;

	pci_init_resource(&hose->io_resource,
			MVME5100_PCI_LOWER_IO,
			MVME5100_PCI_UPPER_IO,
			IORESOURCE_IO,
			"PCI host bridge");

	pci_init_resource(&hose->mem_resources[0],
			MVME5100_PCI_LOWER_MEM,
			MVME5100_PCI_UPPER_MEM,
			IORESOURCE_MEM,
			"PCI host bridge");

	hose->io_space.start = MVME5100_PCI_LOWER_IO;
	hose->io_space.end = MVME5100_PCI_UPPER_IO;
	hose->mem_space.start = MVME5100_PCI_LOWER_MEM;
	hose->mem_space.end = MVME5100_PCI_UPPER_MEM;
	hose->io_base_virt = (void *)MVME5100_ISA_IO_BASE;

	/* Use indirect method of Hawk */
	setup_indirect_pci(hose,
			   MVME5100_PCI_CONFIG_ADDR,
			   MVME5100_PCI_CONFIG_DATA);

	hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

	ppc_md.pcibios_fixup_resources = mvme5100_pcibios_fixup_resources;
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = mvme5100_map_irq;
}
