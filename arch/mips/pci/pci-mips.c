/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999, 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * MIPS boards specific PCI support.
 *
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/mips-boards/generic.h>
#include <asm/gt64120.h>
#include <asm/mips-boards/bonito64.h>
#ifdef CONFIG_MIPS_MALTA
#include <asm/mips-boards/malta.h>
#endif
#include <asm/mips-boards/msc01_pci.h>

#define PCI_ACCESS_READ  0
#define PCI_ACCESS_WRITE 1

/*
 *  PCI configuration cycle AD bus definition
 */
/* Type 0 */
#define PCI_CFG_TYPE0_REG_SHF           0
#define PCI_CFG_TYPE0_FUNC_SHF          8

/* Type 1 */
#define PCI_CFG_TYPE1_REG_SHF           0
#define PCI_CFG_TYPE1_FUNC_SHF          8
#define PCI_CFG_TYPE1_DEV_SHF           11
#define PCI_CFG_TYPE1_BUS_SHF           16

static int mips_pcibios_config_access(unsigned char access_type,
				      struct pci_bus *bus,
				      unsigned int devfn, int where,
				      u32 * data)
{
	unsigned char busnum = bus->number;
	unsigned char type;
	u32 intr, dummy;
	u64 pci_addr;

	switch (mips_revision_corid) {
	case MIPS_REVISION_CORID_QED_RM5261:
	case MIPS_REVISION_CORID_CORE_LV:
	case MIPS_REVISION_CORID_CORE_FPGA:
		/* Galileo GT64120 system controller. */

		if ((busnum == 0) && (devfn >= PCI_DEVFN(31, 0)))
			return -1;	/* Because of a bug in the galileo (for slot 31). */

		/* Clear cause register bits */
		GT_READ(GT_INTRCAUSE_OFS, intr);
		GT_WRITE(GT_INTRCAUSE_OFS, intr &
			 ~(GT_INTRCAUSE_MASABORT0_BIT |
			   GT_INTRCAUSE_TARABORT0_BIT));

		/* Setup address */
		GT_WRITE(GT_PCI0_CFGADDR_OFS,
			 (busnum << GT_PCI0_CFGADDR_BUSNUM_SHF) |
			 (devfn << GT_PCI0_CFGADDR_FUNCTNUM_SHF) |
			 ((where / 4) << GT_PCI0_CFGADDR_REGNUM_SHF) |
			 GT_PCI0_CFGADDR_CONFIGEN_BIT);

		if (access_type == PCI_ACCESS_WRITE) {
			if (busnum == 0 && devfn == 0) {
				/*
				 * The Galileo system controller is acting
				 * differently than other devices.
				 */
				GT_WRITE(GT_PCI0_CFGDATA_OFS, *data);
			} else {
				GT_PCI_WRITE(GT_PCI0_CFGDATA_OFS, *data);
			}
		} else {
			if (busnum == 0 && devfn == 0) {
				/*
				 * The Galileo system controller is acting
				 * differently than other devices.
				 */
				GT_READ(GT_PCI0_CFGDATA_OFS, *data);
			} else {
				GT_PCI_READ(GT_PCI0_CFGDATA_OFS, *data);
			}
		}

		/* Check for master or target abort */
		GT_READ(GT_INTRCAUSE_OFS, intr);

		if (intr & (GT_INTRCAUSE_MASABORT0_BIT |
			    GT_INTRCAUSE_TARABORT0_BIT)) {
			/* Error occurred */

			/* Clear bits */
			GT_READ(GT_INTRCAUSE_OFS, intr);
			GT_WRITE(GT_INTRCAUSE_OFS, intr &
				 ~(GT_INTRCAUSE_MASABORT0_BIT |
				   GT_INTRCAUSE_TARABORT0_BIT));

			return -1;
		}

		break;

	case MIPS_REVISION_CORID_BONITO64:
	case MIPS_REVISION_CORID_CORE_20K:
		/* Algorithmics Bonito64 system controller. */

		if ((busnum == 0) && (PCI_SLOT(devfn) == 0)) {
			return -1;
		}

		/* Clear cause register bits */
		BONITO_PCICMD |= (BONITO_PCICMD_MABORT_CLR |
				  BONITO_PCICMD_MTABORT_CLR);

		/*
		 * Setup pattern to be used as PCI "address" for
		 * Type 0 cycle
		 */
		if (busnum == 0) {
			/* IDSEL */
			pci_addr = (u64) 1 << (PCI_SLOT(devfn) + 10);
		} else {
			/* Bus number */
			pci_addr = busnum << PCI_CFG_TYPE1_BUS_SHF;

			/* Device number */
			pci_addr |=
			    PCI_SLOT(devfn) << PCI_CFG_TYPE1_DEV_SHF;
		}

		/* Function (same for Type 0/1) */
		pci_addr |= PCI_FUNC(devfn) << PCI_CFG_TYPE0_FUNC_SHF;

		/* Register number (same for Type 0/1) */
		pci_addr |= (where & ~0x3) << PCI_CFG_TYPE0_REG_SHF;

		if (busnum == 0) {
			/* Type 0 */
			BONITO_PCIMAP_CFG = pci_addr >> 16;
		} else {
			/* Type 1 */
			BONITO_PCIMAP_CFG = (pci_addr >> 16) | 0x10000;
		}

		/* Flush Bonito register block */
		dummy = BONITO_PCIMAP_CFG;
		iob();		/* sync */

		/* Perform access */
		if (access_type == PCI_ACCESS_WRITE) {
			*(volatile u32 *) (KSEG1ADDR(BONITO_PCICFG_BASE +
						     (pci_addr & 0xffff)))
			    = *(u32 *) data;

			/* Wait till done */
			while (BONITO_PCIMSTAT & 0xF);
		} else {
			*(u32 *) data =
			    *(volatile u32
			      *) (KSEG1ADDR(BONITO_PCICFG_BASE +
					    (pci_addr & 0xffff)));
		}

		/* Detect Master/Target abort */
		if (BONITO_PCICMD & (BONITO_PCICMD_MABORT_CLR |
				     BONITO_PCICMD_MTABORT_CLR)) {
			/* Error occurred */

			/* Clear bits */
			BONITO_PCICMD |= (BONITO_PCICMD_MABORT_CLR |
					  BONITO_PCICMD_MTABORT_CLR);

			return -1;
		}
		break;

	case MIPS_REVISION_CORID_CORE_MSC:
		/* MIPS system controller. */

		if ((busnum == 0) && (PCI_SLOT(devfn) == 0)) {
			return -1;
		}

		/* Clear status register bits. */
		MSC_WRITE(MSC01_PCI_INTSTAT,
			  (MSC01_PCI_INTCFG_MA_BIT |
			   MSC01_PCI_INTCFG_TA_BIT));

		/* Setup address */
		if (busnum == 0)
			type = 0;	/* Type 0 */
		else
			type = 1;	/* Type 1 */

		MSC_WRITE(MSC01_PCI_CFGADDR,
			  ((busnum << MSC01_PCI_CFGADDR_BNUM_SHF) |
			   (PCI_SLOT(devfn) << MSC01_PCI_CFGADDR_DNUM_SHF)
			   | (PCI_FUNC(devfn) <<
			      MSC01_PCI_CFGADDR_FNUM_SHF) | ((where /
							      4) <<
							     MSC01_PCI_CFGADDR_RNUM_SHF)
			   | (type)));

		/* Perform access */
		if (access_type == PCI_ACCESS_WRITE) {
			MSC_WRITE(MSC01_PCI_CFGDATA, *data);
		} else {
			MSC_READ(MSC01_PCI_CFGDATA, *data);
		}

		/* Detect Master/Target abort */
		MSC_READ(MSC01_PCI_INTSTAT, intr);
		if (intr & (MSC01_PCI_INTCFG_MA_BIT |
			    MSC01_PCI_INTCFG_TA_BIT)) {
			/* Error occurred */

			/* Clear bits */
			MSC_READ(MSC01_PCI_INTSTAT, intr);
			MSC_WRITE(MSC01_PCI_INTSTAT,
				  (MSC01_PCI_INTCFG_MA_BIT |
				   MSC01_PCI_INTCFG_TA_BIT));

			return -1;
		}
		break;
	default:
		printk
		    ("Unknown Core card, don't know the system controller.\n");
		return -1;
	}

	return 0;
}


/*
 * We can't address 8 and 16 bit words directly.  Instead we have to
 * read/write a 32bit word and mask/modify the data we actually want.
 */
static int mips_pcibios_read(struct pci_bus *bus, unsigned int devfn,
			     int where, int size, u32 * val)
{
	u32 data = 0;

	if ((size == 2) && (where & 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;
	else if ((size == 4) && (where & 3))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (mips_pcibios_config_access(PCI_ACCESS_READ, bus, devfn, where,
				       &data))
		return -1;

	if (size == 1)
		*val = (data >> ((where & 3) << 3)) & 0xff;
	else if (size == 2)
		*val = (data >> ((where & 3) << 3)) & 0xffff;
	else
		*val = data;

	return PCIBIOS_SUCCESSFUL;
}

static int mips_pcibios_write(struct pci_bus *bus, unsigned int devfn,
			      int where, int size, u32 val)
{
	u32 data = 0;

	if ((size == 2) && (where & 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;
	else if ((size == 4) && (where & 3))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (size == 4)
		data = val;
	else {
		if (mips_pcibios_config_access(PCI_ACCESS_READ, bus, devfn,
		                               where, &data))
			return -1;

		if (size == 1)
			data = (data & ~(0xff << ((where & 3) << 3))) |
				(val << ((where & 3) << 3));
		else if (size == 2)
			data = (data & ~(0xffff << ((where & 3) << 3))) |
				(val << ((where & 3) << 3));
	}

	if (mips_pcibios_config_access(PCI_ACCESS_WRITE, bus, devfn, where,
				       &data))
		return -1;

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops mips_pci_ops = {
	.read = mips_pcibios_read,
	.write = mips_pcibios_write
};

int mips_pcibios_iack(void)
{
	int irq;
	u32 dummy;

	/*
	 * Determine highest priority pending interrupt by performing
	 * a PCI Interrupt Acknowledge cycle.
	 */
	switch (mips_revision_corid) {
	case MIPS_REVISION_CORID_QED_RM5261:
	case MIPS_REVISION_CORID_CORE_LV:
	case MIPS_REVISION_CORID_CORE_FPGA:
	case MIPS_REVISION_CORID_CORE_MSC:
		if (mips_revision_corid == MIPS_REVISION_CORID_CORE_MSC)
			MSC_READ(MSC01_PCI_IACK, irq);
		else
			GT_READ(GT_PCI0_IACK_OFS, irq);
		irq &= 0xff;
		break;
	case MIPS_REVISION_CORID_BONITO64:
	case MIPS_REVISION_CORID_CORE_20K:
		/* The following will generate a PCI IACK cycle on the
		 * Bonito controller. It's a little bit kludgy, but it
		 * was the easiest way to implement it in hardware at
		 * the given time.
		 */
		BONITO_PCIMAP_CFG = 0x20000;

		/* Flush Bonito register block */
		dummy = BONITO_PCIMAP_CFG;
		iob();		/* sync */

		irq = *(volatile u32 *) (KSEG1ADDR(BONITO_PCICFG_BASE));
		iob();		/* sync */
		irq &= 0xff;
		BONITO_PCIMAP_CFG = 0;
		break;
	default:
		printk
		    ("Unknown Core card, don't know the system controller.\n");
		return -1;
	}
	return irq;
}

static int __init pcibios_init(void)
{
#ifdef CONFIG_MIPS_MALTA
	struct pci_dev *pdev = NULL;
	unsigned char reg_val;
#endif

	printk("PCI: Probing PCI hardware on host bus 0.\n");
	pci_scan_bus(0, &mips_pci_ops, NULL);

	switch (mips_revision_corid) {
	case MIPS_REVISION_CORID_QED_RM5261:
	case MIPS_REVISION_CORID_CORE_LV:
	case MIPS_REVISION_CORID_CORE_FPGA:
		/*
		 * Due to a bug in the Galileo system controller, we need
		 * to setup the PCI BAR for the Galileo internal registers.
		 * This should be done in the bios/bootprom and will be
		 * fixed in a later revision of YAMON (the MIPS boards
		 * boot prom).
		 */
		GT_WRITE(GT_PCI0_CFGADDR_OFS, (0 << GT_PCI0_CFGADDR_BUSNUM_SHF) |	/* Local bus */
			 (0 << GT_PCI0_CFGADDR_DEVNUM_SHF) |	/* GT64120 dev */
			 (0 << GT_PCI0_CFGADDR_FUNCTNUM_SHF) |	/* Function 0 */
			 ((0x20 / 4) << GT_PCI0_CFGADDR_REGNUM_SHF) |	/* BAR 4 */
			 GT_PCI0_CFGADDR_CONFIGEN_BIT);

		/* Perform the write */
		GT_WRITE(GT_PCI0_CFGDATA_OFS, PHYSADDR(MIPS_GT_BASE));
		break;
	}

#ifdef CONFIG_MIPS_MALTA
	while ((pdev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, pdev)) != NULL) {
		if ((pdev->vendor == PCI_VENDOR_ID_INTEL)
		    && (pdev->device == PCI_DEVICE_ID_INTEL_82371AB)
		    && (PCI_SLOT(pdev->devfn) == 0x0a)) {
			/*
			 * IDE Decode enable.
			 */
			pci_read_config_byte(pdev, 0x41, &reg_val);
			pci_write_config_byte(pdev, 0x41, reg_val | 0x80);
			pci_read_config_byte(pdev, 0x43, &reg_val);
			pci_write_config_byte(pdev, 0x43, reg_val | 0x80);
		}

		if ((pdev->vendor == PCI_VENDOR_ID_INTEL)
		    && (pdev->device == PCI_DEVICE_ID_INTEL_82371AB_0)
		    && (PCI_SLOT(pdev->devfn) == 0x0a)) {
			/*
			 * Set top of main memory accessible by ISA or DMA
			 * devices to 16 Mb.
			 */
			pci_read_config_byte(pdev, 0x69, &reg_val);
			pci_write_config_byte(pdev, 0x69, reg_val | 0xf0);
		}
	}

	/*
	 * Activate Floppy Controller in the SMSC FDC37M817 Super I/O
	 * Controller.
	 * This should be done in the bios/bootprom and will be fixed in
	 * a later revision of YAMON (the MIPS boards boot prom).
	 */
	/* Entering config state. */
	SMSC_WRITE(SMSC_CONFIG_ENTER, SMSC_CONFIG_REG);

	/* Activate floppy controller. */
	SMSC_WRITE(SMSC_CONFIG_DEVNUM, SMSC_CONFIG_REG);
	SMSC_WRITE(SMSC_CONFIG_DEVNUM_FLOPPY, SMSC_DATA_REG);
	SMSC_WRITE(SMSC_CONFIG_ACTIVATE, SMSC_CONFIG_REG);
	SMSC_WRITE(SMSC_CONFIG_ACTIVATE_ENABLE, SMSC_DATA_REG);

	/* Exit config state. */
	SMSC_WRITE(SMSC_CONFIG_EXIT, SMSC_CONFIG_REG);
#endif

	return 0;
}

subsys_initcall(pcibios_init);

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	/* Not needed, since we enable all devices at startup.  */
	return 0;
}

void pcibios_align_resource(void *data, struct resource *res,
			    unsigned long size, unsigned long align)
{
}

char *__init pcibios_setup(char *str)
{
	/* Nothing to do for now.  */

	return str;
}

struct pci_fixup pcibios_fixups[] = {
	{0}
};

/*
 *  Called after each bus is probed, but before its children
 *  are examined.
 */
void __devinit pcibios_fixup_bus(struct pci_bus *b)
{
	pci_read_bridge_bases(b);
}

unsigned int pcibios_assign_all_busses(void)
{
	return 1;
}
