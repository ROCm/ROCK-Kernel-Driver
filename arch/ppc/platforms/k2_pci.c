/*
 * arch/ppc/platforms/k2_pci.c
 *
 * PCI support for SBS K2
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
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>

#include <syslib/cpc710.h>

#include "k2.h"

#undef DEBUG
#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif /* DEBUG */

static inline int __init
k2_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	struct pci_controller *hose = pci_bus_to_hose(dev->bus->number);
	/*
	 * Check our hose index.  If we are zero then we are on the
	 * local PCI hose, otherwise we are on the cPCI hose.
	 */
	if (!hose->index)
	{
		static char pci_irq_table[][4] =
			/*
			 * 	PCI IDSEL/INTPIN->INTLINE
			 * 	A	B	C	D
			 */
		{
			{1, 	0,	0,	0},	/* Ethernet */
			{5,	5,	5,	5},	/* PMC Site 1 */
			{6,	6,	6,	6},	/* PMC Site 2 */
			{0,     0,      0,      0},     /* unused */
			{0,     0,      0,      0},     /* unused */
			{0,     0,      0,      0},     /* PCI-ISA Bridge */
			{0,     0,      0,      0},     /* unused */
			{0,     0,      0,      0},     /* unused */
			{0,     0,      0,      0},     /* unused */
			{0,     0,      0,      0},     /* unused */
			{0,     0,      0,      0},     /* unused */
			{0,     0,      0,      0},     /* unused */
			{0,     0,      0,      0},     /* unused */
			{0,     0,      0,      0},     /* unused */
			{15,	0,	0,	0},	/* M5229 IDE */
		};
		const long min_idsel = 3, max_idsel = 17, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	}
	else
	{
		static char pci_irq_table[][4] =
		/*
		 * 	PCI IDSEL/INTPIN->INTLINE
		 * 	A	B	C	D
		 */
		{
			{10, 	11,	12,	9},	/* cPCI slot 8 */
			{11, 	12,	9,	10},	/* cPCI slot 7 */
			{12, 	9,	10,	11},	/* cPCI slot 6 */
			{9, 	10,	11,	12},	/* cPCI slot 5 */
			{10, 	11,	12,	9},	/* cPCI slot 4 */
			{11, 	12,	9,	10},	/* cPCI slot 3 */
			{12, 	9,	10,	11},	/* cPCI slot 2 */
		};
		const long min_idsel = 15, max_idsel = 21, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	}
}

void k2_pcibios_fixup(void)
{
#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
	struct pci_dev *ide_dev;

	/*
	 * Enable DMA support on hdc
	 */
	ide_dev = pci_find_device(PCI_VENDOR_ID_AL,
			PCI_DEVICE_ID_AL_M5229,
			NULL);

	if (ide_dev) {

		unsigned long ide_dma_base;

		ide_dma_base = pci_resource_start(ide_dev, 4);
		outb(0x00, ide_dma_base+0x2);
		outb(0x20, ide_dma_base+0xa);
	}
#endif
}

void k2_pcibios_fixup_resources(struct pci_dev *dev)
{
	int i;

	if ((dev->vendor == PCI_VENDOR_ID_IBM) &&
			(dev->device == PCI_DEVICE_ID_IBM_CPC710_PCI64))
	{
		DBG("Fixup CPC710 resources\n");
		for (i=0; i<DEVICE_COUNT_RESOURCE; i++)
		{
			dev->resource[i].start = 0;
			dev->resource[i].end = 0;
		}
	}
}

void k2_setup_hoses(void)
{
	struct pci_controller *hose_a, *hose_b;

	/*
	 * Reconfigure CPC710 memory map so
	 * we have some more PCI memory space.
	 */

	/* Set FPHB mode */
	__raw_writel(0x808000e0, PGCHP);	/* Set FPHB mode */

	/* PCI32 mappings */
	__raw_writel(0x00000000, K2_PCI32_BAR+PIBAR);	/* PCI I/O base */
	__raw_writel(0x00000000, K2_PCI32_BAR+PMBAR);	/* PCI Mem base */
	__raw_writel(0xf0000000, K2_PCI32_BAR+MSIZE);	/* 256MB */
	__raw_writel(0xfff00000, K2_PCI32_BAR+IOSIZE);	/* 1MB */
	__raw_writel(0xc0000000, K2_PCI32_BAR+SMBAR);	/* Base@0xc0000000 */
	__raw_writel(0x80000000, K2_PCI32_BAR+SIBAR);	/* Base@0x80000000 */
	__raw_writel(0x000000c0, K2_PCI32_BAR+PSSIZE);	/* 1GB space */
	__raw_writel(0x000000c0, K2_PCI32_BAR+PPSIZE);	/* 1GB space */
	__raw_writel(0x00000000, K2_PCI32_BAR+BARPS);	/* Base@0x00000000 */
	__raw_writel(0x00000000, K2_PCI32_BAR+BARPP);	/* Base@0x00000000 */
	__raw_writel(0x00000080, K2_PCI32_BAR+PSBAR);	/* Base@0x80 */
	__raw_writel(0x00000000, K2_PCI32_BAR+PPBAR);

	__raw_writel(0xc0000000, K2_PCI32_BAR+BPMDLK);
	__raw_writel(0xd0000000, K2_PCI32_BAR+TPMDLK);
	__raw_writel(0x80000000, K2_PCI32_BAR+BIODLK);
	__raw_writel(0x80100000, K2_PCI32_BAR+TIODLK);
	__raw_writel(0xe0008000, K2_PCI32_BAR+DLKCTRL);
	__raw_writel(0xffffffff, K2_PCI32_BAR+DLKDEV);

	/* PCI64 mappings */
	__raw_writel(0x00100000, K2_PCI64_BAR+PIBAR);	/* PCI I/O base */
	__raw_writel(0x10000000, K2_PCI64_BAR+PMBAR);	/* PCI Mem base */
	__raw_writel(0xf0000000, K2_PCI64_BAR+MSIZE);	/* 256MB */
	__raw_writel(0xfff00000, K2_PCI64_BAR+IOSIZE);	/* 1MB */
	__raw_writel(0xd0000000, K2_PCI64_BAR+SMBAR);	/* Base@0xd0000000 */
	__raw_writel(0x80100000, K2_PCI64_BAR+SIBAR);	/* Base@0x80100000 */
	__raw_writel(0x000000c0, K2_PCI64_BAR+PSSIZE);	/* 1GB space */
	__raw_writel(0x000000c0, K2_PCI64_BAR+PPSIZE);	/* 1GB space */
	__raw_writel(0x00000000, K2_PCI64_BAR+BARPS);	/* Base@0x00000000 */
	__raw_writel(0x00000000, K2_PCI64_BAR+BARPP);	/* Base@0x00000000 */

	/* Setup PCI32 hose */
	hose_a = pcibios_alloc_controller();
	if (!hose_a)
		return;

	hose_a->first_busno = 0;
	hose_a->last_busno = 0xff;
	hose_a->pci_mem_offset = K2_PCI32_MEM_BASE;

	pci_init_resource(&hose_a->io_resource,
			K2_PCI32_LOWER_IO,
			K2_PCI32_UPPER_IO,
			IORESOURCE_IO,
			"PCI32 host bridge");

	pci_init_resource(&hose_a->mem_resources[0],
			K2_PCI32_LOWER_MEM + K2_PCI32_MEM_BASE,
			K2_PCI32_UPPER_MEM + K2_PCI32_MEM_BASE,
			IORESOURCE_MEM,
			"PCI32 host bridge");

	hose_a->io_space.start = K2_PCI32_LOWER_IO;
	hose_a->io_space.end = K2_PCI32_UPPER_IO;
	hose_a->mem_space.start = K2_PCI32_LOWER_MEM;
	hose_a->mem_space.end = K2_PCI32_UPPER_MEM;
	hose_a->io_base_virt = (void *)K2_ISA_IO_BASE;

	setup_indirect_pci(hose_a, K2_PCI32_CONFIG_ADDR, K2_PCI32_CONFIG_DATA);

	/* Initialize PCI32 bus registers */
	early_write_config_byte(hose_a,
			hose_a->first_busno,
			PCI_DEVFN(0, 0),
			CPC710_BUS_NUMBER,
			hose_a->first_busno);

	early_write_config_byte(hose_a,
			hose_a->first_busno,
			PCI_DEVFN(0, 0),
			CPC710_SUB_BUS_NUMBER,
			hose_a->last_busno);

	/* Enable PCI interrupt polling */
	early_write_config_byte(hose_a,
			hose_a->first_busno,
			PCI_DEVFN(8, 0),
			0x45,
			0x80);

	/* Route polled PCI interrupts */
	early_write_config_byte(hose_a,
			hose_a->first_busno,
			PCI_DEVFN(8, 0),
			0x48,
			0x58);

	early_write_config_byte(hose_a,
			hose_a->first_busno,
			PCI_DEVFN(8, 0),
			0x49,
			0x07);

	early_write_config_byte(hose_a,
			hose_a->first_busno,
			PCI_DEVFN(8, 0),
			0x4a,
			0x31);

	early_write_config_byte(hose_a,
			hose_a->first_busno,
			PCI_DEVFN(8, 0),
			0x4b,
			0xb9);

	/* route secondary IDE channel interrupt to IRQ 15 */
	early_write_config_byte(hose_a,
			hose_a->first_busno,
			PCI_DEVFN(8, 0),
			0x75,
			0x0f);

	/* enable IDE controller IDSEL */
	early_write_config_byte(hose_a,
			hose_a->first_busno,
			PCI_DEVFN(8, 0),
			0x58,
			0x48);

	/* Enable IDE function */
	early_write_config_byte(hose_a,
			hose_a->first_busno,
			PCI_DEVFN(17, 0),
			0x50,
			0x03);

	/* Set M5229 IDE controller to native mode */
	early_write_config_byte(hose_a,
			hose_a->first_busno,
			PCI_DEVFN(17, 0),
			PCI_CLASS_PROG,
			0xdf);

	hose_a->last_busno = pciauto_bus_scan(hose_a, hose_a->first_busno);

	/* Write out correct max subordinate bus number for hose A */
	early_write_config_byte(hose_a,
			hose_a->first_busno,
			PCI_DEVFN(0, 0),
			CPC710_SUB_BUS_NUMBER,
			hose_a->last_busno);

	/* Only setup PCI64 hose if we are in the system slot */
	if (!(readb(K2_MISC_REG) & K2_SYS_SLOT_MASK))
	{
		/* Setup PCI64 hose */
		hose_b = pcibios_alloc_controller();
		if (!hose_b)
			return;

		hose_b->first_busno = hose_a->last_busno + 1;
		hose_b->last_busno = 0xff;

		/* Reminder: quit changing the following, it is correct. */
		hose_b->pci_mem_offset = K2_PCI32_MEM_BASE;

		pci_init_resource(&hose_b->io_resource,
				K2_PCI64_LOWER_IO,
				K2_PCI64_UPPER_IO,
				IORESOURCE_IO,
				"PCI64 host bridge");

		pci_init_resource(&hose_b->mem_resources[0],
				K2_PCI64_LOWER_MEM + K2_PCI32_MEM_BASE,
				K2_PCI64_UPPER_MEM + K2_PCI32_MEM_BASE,
				IORESOURCE_MEM,
				"PCI64 host bridge");

		hose_b->io_space.start = K2_PCI64_LOWER_IO;
		hose_b->io_space.end = K2_PCI64_UPPER_IO;
		hose_b->mem_space.start = K2_PCI64_LOWER_MEM;
		hose_b->mem_space.end = K2_PCI64_UPPER_MEM;
		hose_b->io_base_virt = (void *)K2_ISA_IO_BASE;

		setup_indirect_pci(hose_b,
				K2_PCI64_CONFIG_ADDR,
				K2_PCI64_CONFIG_DATA);

		/* Initialize PCI64 bus registers */
		early_write_config_byte(hose_b,
				0,
				PCI_DEVFN(0, 0),
				CPC710_SUB_BUS_NUMBER,
				0xff);

		early_write_config_byte(hose_b,
				0,
				PCI_DEVFN(0, 0),
				CPC710_BUS_NUMBER,
				hose_b->first_busno);

		hose_b->last_busno = pciauto_bus_scan(hose_b,
				hose_b->first_busno);

		/* Write out correct max subordinate bus number for hose B */
		early_write_config_byte(hose_b,
				hose_b->first_busno,
				PCI_DEVFN(0, 0),
				CPC710_SUB_BUS_NUMBER,
				hose_b->last_busno);

		/* Configure PCI64 PSBAR */
		early_write_config_dword(hose_b,
				hose_b->first_busno,
				PCI_DEVFN(0, 0),
				PCI_BASE_ADDRESS_0,
				K2_PCI64_SYS_MEM_BASE);
	}

	/* Configure i8259 level/edge settings */
	outb(0x62, 0x4d0);
	outb(0xde, 0x4d1);

#ifdef CONFIG_CPC710_DATA_GATHERING
	{
		unsigned int tmp;
		tmp = __raw_readl(ABCNTL);
		/* Enable data gathering on both PCI interfaces */
		__raw_writel(tmp | 0x05000000, ABCNTL);
	}
#endif

	ppc_md.pcibios_fixup = k2_pcibios_fixup;
	ppc_md.pcibios_fixup_resources = k2_pcibios_fixup_resources;
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = k2_map_irq;
}
