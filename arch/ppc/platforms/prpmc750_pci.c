/*
 * arch/ppc/platforms/prpmc750_pci.c
 *
 * PCI support for Motorola PrPMC750
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
#include <platforms/prpmc750.h>

/*
 * Motorola PrPMC750/PrPMC800 in PrPMCBASE or PrPMC-Carrier
 * Combined irq tables.  Only Base has IDSEL 14, only Carrier has 21 and 22.
 */
static inline int
prpmc_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	/*
	 *      PCI IDSEL/INTPIN->INTLINE
	 *      A       B       C       D
	 */
	{
		{12,	0,	0,	0},  /* IDSEL 14 - Ethernet, base */
		{0,	0,	0,	0},  /* IDSEL 15 - unused */
		{10,	11,	12,	9},  /* IDSEL 16 - PMC A1, PMC1 */
		{10,	11,	12,	9},  /* IDSEL 17 - PrPMC-A-B, PMC2-B */
		{11,	12,	9,	10}, /* IDSEL 18 - PMC A1-B, PMC1-B */
		{0,	0,	0,	0},  /* IDSEL 19 - unused */
		{9,	10,	11,	12}, /* IDSEL 20 - P2P Bridge */
		{11,	12,	9,	10}, /* IDSEL 21 - PMC A2, carrier */
		{12,	9,	10,	11}, /* IDSEL 22 - PMC A2-B, carrier */
	};
	const long min_idsel = 14, max_idsel = 22, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
};

static void __init
prpmc750_pcibios_fixup(void)
{
	struct pci_dev *dev;
	unsigned short wtmp;

	/*
	 * Kludge to clean up after PPC6BUG which doesn't
	 * configure the CL5446 VGA card.  Also the
	 * resource subsystem doesn't fixup the
	 * PCI mem resources on the CL5446.
	 */
	if ((dev = pci_find_device(PCI_VENDOR_ID_CIRRUS,
				PCI_DEVICE_ID_CIRRUS_5446, 0)))
	{
		dev->resource[0].start += PRPMC750_PCI_PHY_MEM_BASE;
		dev->resource[0].end += PRPMC750_PCI_PHY_MEM_BASE;
		pci_read_config_word(dev,
				PCI_COMMAND,
				&wtmp);
		pci_write_config_word(dev,
				PCI_COMMAND,
				wtmp|3);
		/* Enable Color mode in MISC reg */
		outb(0x03, 0x3c2);
		/* Select DRAM config reg */
		outb(0x0f, 0x3c4);
		/* Set proper DRAM config */
		outb(0xdf, 0x3c5);
	}
}

void __init
prpmc750_find_bridges(void)
{
	struct pci_controller* hose;

	hose = pcibios_alloc_controller();
	if (!hose)
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;
	hose->pci_mem_offset = PRPMC750_PCI_PHY_MEM_BASE;

	pci_init_resource(&hose->io_resource,
			PRPMC750_PCI_LOWER_IO,
			PRPMC750_PCI_UPPER_IO,
			IORESOURCE_IO,
			"PCI host bridge");

	pci_init_resource(&hose->mem_resources[0],
			PRPMC750_PCI_LOWER_MEM + PRPMC750_PCI_PHY_MEM_BASE,
			PRPMC750_PCI_UPPER_MEM + PRPMC750_PCI_PHY_MEM_BASE,
			IORESOURCE_MEM,
			"PCI host bridge");

	hose->io_space.start = PRPMC750_PCI_LOWER_IO;
	hose->io_space.end = PRPMC750_PCI_UPPER_IO;
	hose->mem_space.start = PRPMC750_PCI_LOWER_MEM;
	hose->mem_space.end = PRPMC750_PCI_UPPER_MEM_AUTO;

	hose->io_base_virt = (void *)PRPMC750_ISA_IO_BASE;

	setup_indirect_pci(hose,
			PRPMC750_PCI_CONFIG_ADDR,
			PRPMC750_PCI_CONFIG_DATA);

	/*
	 * Disable MPIC response to PCI I/O space (BAR 0).
	 * Make MPIC respond to PCI Mem space at specified address.
	 * (BAR 1).
	 */
	early_write_config_dword(hose,
			         0,
			         PCI_DEVFN(0,0),
			         PCI_BASE_ADDRESS_0,
			         0x00000000 | 0x1);

	early_write_config_dword(hose,
			         0,
			         PCI_DEVFN(0,0),
			         PCI_BASE_ADDRESS_1,
			         (PRPMC750_HAWK_MPIC_BASE -
				 	PRPMC750_PCI_MEM_OFFSET) | 0x0);

	hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

	ppc_md.pcibios_fixup = prpmc750_pcibios_fixup;
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = prpmc_map_irq;
}
