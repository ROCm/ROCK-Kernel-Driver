/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
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

#ifdef CONFIG_PCI

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/mips-boards/generic.h>
#include <asm/gt64120.h>
#ifdef CONFIG_MIPS_MALTA
#include <asm/mips-boards/malta.h>
#endif

#define PCI_ACCESS_READ  0
#define PCI_ACCESS_WRITE 1

static int
mips_pcibios_config_access(unsigned char access_type, struct pci_dev *dev,
                           unsigned char where, u32 *data)
{
	unsigned char bus = dev->bus->number;
	unsigned char dev_fn = dev->devfn;
        u32 intr;

	if ((bus == 0) && (dev_fn >= PCI_DEVFN(31,0)))
	        return -1; /* Because of a bug in the galileo (for slot 31). */

	/* Clear cause register bits */
	GT_WRITE(GT_INTRCAUSE_OFS, ~(GT_INTRCAUSE_MASABORT0_BIT | 
	                             GT_INTRCAUSE_TARABORT0_BIT));

	/* Setup address */
	GT_WRITE(GT_PCI0_CFGADDR_OFS, 
		 (bus         << GT_PCI0_CFGADDR_BUSNUM_SHF)   |
		 (dev_fn      << GT_PCI0_CFGADDR_FUNCTNUM_SHF) |
		 ((where / 4) << GT_PCI0_CFGADDR_REGNUM_SHF)   |
		 GT_PCI0_CFGADDR_CONFIGEN_BIT);

	if (access_type == PCI_ACCESS_WRITE) {
	        if (bus == 0 && dev_fn == 0) {
		        /* 
			 * Galileo is acting differently than other devices. 
			 */
		        GT_WRITE(GT_PCI0_CFGDATA_OFS, *data);
		} else {
		        GT_PCI_WRITE(GT_PCI0_CFGDATA_OFS, *data);
		}
	} else {
	        if (bus == 0 && dev_fn == 0) {
		        /* 
			 * Galileo is acting differently than other devices. 
			 */
		        GT_READ(GT_PCI0_CFGDATA_OFS, *data);
		} else {
		        GT_PCI_READ(GT_PCI0_CFGDATA_OFS, *data);
		}
	}

	/* Check for master or target abort */
	GT_READ(GT_INTRCAUSE_OFS, intr);

	if (intr & (GT_INTRCAUSE_MASABORT0_BIT | GT_INTRCAUSE_TARABORT0_BIT))
	{
	        /* Error occured */

	        /* Clear bits */
	        GT_WRITE( GT_INTRCAUSE_OFS, ~(GT_INTRCAUSE_MASABORT0_BIT | 
					      GT_INTRCAUSE_TARABORT0_BIT) );

		return -1;
	}

	return 0;
}


/*
 * We can't address 8 and 16 bit words directly.  Instead we have to
 * read/write a 32bit word and mask/modify the data we actually want.
 */
static int
mips_pcibios_read_config_byte (struct pci_dev *dev, int where, u8 *val)
{
	u32 data = 0;

	if (mips_pcibios_config_access(PCI_ACCESS_READ, dev, where, &data))
		return -1;

	*val = (data >> ((where & 3) << 3)) & 0xff;

	return PCIBIOS_SUCCESSFUL;
}


static int
mips_pcibios_read_config_word (struct pci_dev *dev, int where, u16 *val)
{
	u32 data = 0;

	if (where & 1)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (mips_pcibios_config_access(PCI_ACCESS_READ, dev, where, &data))
	       return -1;

	*val = (data >> ((where & 3) << 3)) & 0xffff;

	return PCIBIOS_SUCCESSFUL;
}

static int
mips_pcibios_read_config_dword (struct pci_dev *dev, int where, u32 *val)
{
	u32 data = 0;

	if (where & 3)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	
	if (mips_pcibios_config_access(PCI_ACCESS_READ, dev, where, &data))
		return -1;

	*val = data;

	return PCIBIOS_SUCCESSFUL;
}


static int
mips_pcibios_write_config_byte (struct pci_dev *dev, int where, u8 val)
{
	u32 data = 0;
       
	if (mips_pcibios_config_access(PCI_ACCESS_READ, dev, where, &data))
		return -1;

	data = (data & ~(0xff << ((where & 3) << 3))) |
	       (val << ((where & 3) << 3));

	if (mips_pcibios_config_access(PCI_ACCESS_WRITE, dev, where, &data))
		return -1;

	return PCIBIOS_SUCCESSFUL;
}

static int
mips_pcibios_write_config_word (struct pci_dev *dev, int where, u16 val)
{
        u32 data = 0;

	if (where & 1)
		return PCIBIOS_BAD_REGISTER_NUMBER;
       
        if (mips_pcibios_config_access(PCI_ACCESS_READ, dev, where, &data))
	       return -1;

	data = (data & ~(0xffff << ((where & 3) << 3))) | 
	       (val << ((where & 3) << 3));

	if (mips_pcibios_config_access(PCI_ACCESS_WRITE, dev, where, &data))
	       return -1;


	return PCIBIOS_SUCCESSFUL;
}

static int
mips_pcibios_write_config_dword(struct pci_dev *dev, int where, u32 val)
{
	if (where & 3)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (mips_pcibios_config_access(PCI_ACCESS_WRITE, dev, where, &val))
	       return -1;

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops mips_pci_ops = {
	mips_pcibios_read_config_byte,
        mips_pcibios_read_config_word,
	mips_pcibios_read_config_dword,
	mips_pcibios_write_config_byte,
	mips_pcibios_write_config_word,
	mips_pcibios_write_config_dword
};

void __init pcibios_init(void)
{
#ifdef CONFIG_MIPS_MALTA
	struct pci_dev *pdev;
	unsigned char reg_val;
#endif

	printk("PCI: Probing PCI hardware on host bus 0.\n");
	pci_scan_bus(0, &mips_pci_ops, NULL);

	/* 
	 * Due to a bug in the Galileo system controller, we need to setup 
	 * the PCI BAR for the Galileo internal registers.
	 * This should be done in the bios/bootprom and will be fixed in
	 * a later revision of YAMON (the MIPS boards boot prom).
	 */
	GT_WRITE(GT_PCI0_CFGADDR_OFS,
		 (0 << GT_PCI0_CFGADDR_BUSNUM_SHF)   |  /* Local bus */
		 (0 << GT_PCI0_CFGADDR_DEVNUM_SHF)   |  /* GT64120 device */
		 (0 << GT_PCI0_CFGADDR_FUNCTNUM_SHF) |  /* Function 0 */
		 ((0x20/4) << GT_PCI0_CFGADDR_REGNUM_SHF) |  /* BAR 4 */
		 GT_PCI0_CFGADDR_CONFIGEN_BIT );

	/* Perform the write */
	GT_WRITE( GT_PCI0_CFGDATA_OFS, PHYSADDR(MIPS_GT_BASE)); 

#ifdef CONFIG_MIPS_MALTA
	pci_for_each_dev(pdev) {
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
}

int __init
pcibios_enable_device(struct pci_dev *dev)
{
	/* Not needed, since we enable all devices at startup.  */
	return 0;
}

void __init
pcibios_align_resource(void *data, struct resource *res, unsigned long size)
{
}

char * __init
pcibios_setup(char *str)
{
	/* Nothing to do for now.  */

	return str;
}

struct pci_fixup pcibios_fixups[] = {
	{ 0 }
};

void __init
pcibios_update_resource(struct pci_dev *dev, struct resource *root,
                        struct resource *res, int resource)
{
	unsigned long where, size;
	u32 reg;

	where = PCI_BASE_ADDRESS_0 + (resource * 4);
	size = res->end - res->start;
	pci_read_config_dword(dev, where, &reg);
	reg = (reg & size) | (((u32)(res->start - root->start)) & ~size);
	pci_write_config_dword(dev, where, reg);
}

/*
 *  Called after each bus is probed, but before its children
 *  are examined.
 */
void __init pcibios_fixup_bus(struct pci_bus *b)
{
	pci_read_bridge_bases(b);
}

unsigned __init int pcibios_assign_all_busses(void)
{
	return 1;
}

#endif /* CONFIG_PCI */
