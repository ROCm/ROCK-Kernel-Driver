/*
 * arch/ppc/platforms/prpmc800_pci.c
 *
 * PCI support for Motorola PrPMC800
 *
 * Author: Dale Farnsworth <dale.farnsworth@mvista.com>
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
#include <platforms/prpmc800.h>
#include <asm/harrier.h>

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

void __init
prpmc800_find_bridges(void)
{
	struct pci_controller* hose;
	int host_bridge;

	hose = pcibios_alloc_controller();
	if (!hose)
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;
	hose->pci_mem_offset = PRPMC800_PCI_PHY_MEM_OFFSET;

	pci_init_resource(&hose->io_resource,
		PRPMC800_PCI_IO_START,
		PRPMC800_PCI_IO_END,
		IORESOURCE_IO,
		"PCI host bridge");

	pci_init_resource(&hose->mem_resources[0],
		PRPMC800_PCI_MEM_START,
		PRPMC800_PCI_MEM_END,
		IORESOURCE_MEM,
		"PCI host bridge");

	hose->io_space.start = PRPMC800_PCI_IO_START;
	hose->io_space.end = PRPMC800_PCI_IO_END;
	hose->mem_space.start = PRPMC800_PCI_MEM_START;
	hose->mem_space.end = PRPMC800_PCI_MEM_END;
	hose->io_base_virt = (void *)PRPMC800_ISA_IO_BASE;

	setup_indirect_pci(hose,
			PRPMC800_PCI_CONFIG_ADDR,
			PRPMC800_PCI_CONFIG_DATA);

	/* Get host bridge vendor/dev id */
	early_read_config_dword(hose,
				0,
				PCI_DEVFN(0,0),
				PCI_VENDOR_ID,
				&host_bridge);

	switch (host_bridge) {
	case HARRIER_VEND_DEV_ID:
		if (harrier_init(hose,
				PRPMC800_HARRIER_XCSR_BASE,
				PRPMC800_PROC_PCI_MEM_START,
				PRPMC800_PROC_PCI_MEM_END,
				PRPMC800_PROC_PCI_IO_START,
				PRPMC800_PROC_PCI_IO_END,
				PRPMC800_HARRIER_MPIC_BASE) != 0) {
			printk("Could not initialize HARRIER bridge\n");
		}
		break;
	default:
		printk("Host bridge 0x%x not supported\n", host_bridge);
	}

	hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

	ppc_md.pcibios_fixup = NULL;
	ppc_md.pcibios_fixup_bus = NULL;
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = prpmc_map_irq;
}
