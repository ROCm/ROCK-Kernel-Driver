/*
 * CHRP pci routines.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/openpic.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/hydra.h>
#include <asm/prom.h>
#include <asm/gg2.h>
#include <asm/machdep.h>
#include <asm/init.h>

#include "pci.h"

#ifdef CONFIG_POWER4
static unsigned long pci_address_offset(int, unsigned int);
#endif /* CONFIG_POWER4 */

/* LongTrail */
#define pci_config_addr(bus, dev, offset) \
(GG2_PCI_CONFIG_BASE | ((bus)<<16) | ((dev)<<8) | (offset))

volatile struct Hydra *Hydra = NULL;

/*
 * The VLSI Golden Gate II has only 512K of PCI configuration space, so we
 * limit the bus number to 3 bits
 */

int __chrp gg2_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
				 unsigned char offset, unsigned char *val)
{
	if (bus > 7) {
		*val = 0xff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	*val = in_8((unsigned char *)pci_config_addr(bus, dev_fn, offset));
	return PCIBIOS_SUCCESSFUL;
}

int __chrp gg2_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
				 unsigned char offset, unsigned short *val)
{
	if (bus > 7) {
		*val = 0xffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	*val = in_le16((unsigned short *)pci_config_addr(bus, dev_fn, offset));
	return PCIBIOS_SUCCESSFUL;
}


int __chrp gg2_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned int *val)
{
	if (bus > 7) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	*val = in_le32((unsigned int *)pci_config_addr(bus, dev_fn, offset));
	return PCIBIOS_SUCCESSFUL;
}

int __chrp gg2_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned char val)
{
	if (bus > 7)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_8((unsigned char *)pci_config_addr(bus, dev_fn, offset), val);
	return PCIBIOS_SUCCESSFUL;
}

int __chrp gg2_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned short val)
{
	if (bus > 7)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_le16((unsigned short *)pci_config_addr(bus, dev_fn, offset), val);
	return PCIBIOS_SUCCESSFUL;
}

int __chrp gg2_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
				   unsigned char offset, unsigned int val)
{
	if (bus > 7)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_le32((unsigned int *)pci_config_addr(bus, dev_fn, offset), val);
	return PCIBIOS_SUCCESSFUL;
}

#define python_config_address(bus) (unsigned *)((0xfef00000+0xf8000)-(bus*0x100000))
#define python_config_data(bus) ((0xfef00000+0xf8010)-(bus*0x100000))
#define PYTHON_CFA(b, d, o)	(0x80 | ((b<<6) << 8) | ((d) << 16) \
				 | (((o) & ~3) << 24))
unsigned int python_busnr = 0;

int __chrp python_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
				    unsigned char offset, unsigned char *val)
{
	if (bus > python_busnr) {
		*val = 0xff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	out_be32( python_config_address( bus ), PYTHON_CFA(bus,dev_fn,offset));
	*val = in_8((unsigned char *)python_config_data(bus) + (offset&3));
	return PCIBIOS_SUCCESSFUL;
}

int __chrp python_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
				    unsigned char offset, unsigned short *val)
{
	if (bus > python_busnr) {
		*val = 0xffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	out_be32( python_config_address( bus ), PYTHON_CFA(bus,dev_fn,offset));
	*val = in_le16((unsigned short *)(python_config_data(bus) + (offset&3)));
	return PCIBIOS_SUCCESSFUL;
}


int __chrp python_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned int *val)
{
	if (bus > python_busnr) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	out_be32( python_config_address( bus ), PYTHON_CFA(bus,dev_fn,offset));
	*val = in_le32((unsigned *)python_config_data(bus));
	return PCIBIOS_SUCCESSFUL;
}

int __chrp python_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned char val)
{
	if (bus > python_busnr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_be32( python_config_address( bus ), PYTHON_CFA(bus,dev_fn,offset));
	out_8((volatile unsigned char *)python_config_data(bus) + (offset&3), val);
	return PCIBIOS_SUCCESSFUL;
}

int __chrp python_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned short val)
{
	if (bus > python_busnr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_be32( python_config_address( bus ), PYTHON_CFA(bus,dev_fn,offset));
	out_le16((volatile unsigned short *)python_config_data(bus) + (offset&3),
		 val);
	return PCIBIOS_SUCCESSFUL;
}

int __chrp python_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
				      unsigned char offset, unsigned int val)
{
	if (bus > python_busnr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_be32( python_config_address( bus ), PYTHON_CFA(bus,dev_fn,offset));
	out_le32((unsigned *)python_config_data(bus) + (offset&3), val);
	return PCIBIOS_SUCCESSFUL;
}


int __chrp rtas_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
				    unsigned char offset, unsigned char *val)
{
	unsigned long addr = (offset&0xff) | ((dev_fn&0xff)<<8) | ((bus & 0xff)<<16);
	unsigned long ret;

	if (call_rtas( "read-pci-config", 2, 2, &ret, addr, 1) != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	*val = ret;
	return PCIBIOS_SUCCESSFUL;
}

int __chrp rtas_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
				    unsigned char offset, unsigned short *val)
{
	unsigned long addr = (offset&0xff) | ((dev_fn&0xff)<<8) | ((bus & 0xff)<<16);
	unsigned long ret;

	if (call_rtas("read-pci-config", 2, 2, &ret, addr, 2) != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	*val = ret;
	return PCIBIOS_SUCCESSFUL;
}


int __chrp rtas_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned int *val)
{
	unsigned long addr = (offset&0xff) | ((dev_fn&0xff)<<8) | ((bus & 0xff)<<16);
	unsigned long ret;

	if (call_rtas("read-pci-config", 2, 2, &ret, addr, 4) != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	*val = ret;
	return PCIBIOS_SUCCESSFUL;
}

int __chrp rtas_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned char val)
{
	unsigned long addr = (offset&0xff) | ((dev_fn&0xff)<<8) | ((bus & 0xff)<<16);
	if ( call_rtas( "write-pci-config", 3, 1, NULL, addr, 1, (ulong)val ) != 0 )
		return PCIBIOS_DEVICE_NOT_FOUND;
	return PCIBIOS_SUCCESSFUL;
}

int __chrp rtas_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned short val)
{
	unsigned long addr = (offset&0xff) | ((dev_fn&0xff)<<8) | ((bus & 0xff)<<16);
	if ( call_rtas( "write-pci-config", 3, 1, NULL, addr, 2, (ulong)val ) != 0 )
		return PCIBIOS_DEVICE_NOT_FOUND;
	return PCIBIOS_SUCCESSFUL;
}

int __chrp rtas_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
				      unsigned char offset, unsigned int val)
{
	unsigned long addr = (offset&0xff) | ((dev_fn&0xff)<<8) | ((bus & 0xff)<<16);
	if ( call_rtas( "write-pci-config", 3, 1, NULL, addr, 4, (ulong)val ) != 0 )
		return PCIBIOS_DEVICE_NOT_FOUND;
	return PCIBIOS_SUCCESSFUL;
}

    /*
     *  Temporary fixes for PCI devices. These should be replaced by OF query
     *  code -- Geert
     */

static u_char hydra_openpic_initsenses[] __initdata = {
    1,	/* HYDRA_INT_SIO */
    0,	/* HYDRA_INT_SCSI_DMA */
    0,	/* HYDRA_INT_SCCA_TX_DMA */
    0,	/* HYDRA_INT_SCCA_RX_DMA */
    0,	/* HYDRA_INT_SCCB_TX_DMA */
    0,	/* HYDRA_INT_SCCB_RX_DMA */
    1,	/* HYDRA_INT_SCSI */
    1,	/* HYDRA_INT_SCCA */
    1,	/* HYDRA_INT_SCCB */
    1,	/* HYDRA_INT_VIA */
    1,	/* HYDRA_INT_ADB */
    0,	/* HYDRA_INT_ADB_NMI */
    	/* all others are 1 (= default) */
};

int __init
hydra_init(void)
{
	struct device_node *np;

	np = find_devices("mac-io");
	if (np == NULL || np->n_addrs == 0) {
		printk(KERN_WARNING "Warning: no mac-io found\n");
		return 0;
	}
	Hydra = ioremap(np->addrs[0].address, np->addrs[0].size);
	printk("Hydra Mac I/O at %x\n", np->addrs[0].address);
	out_le32(&Hydra->Feature_Control, (HYDRA_FC_SCC_CELL_EN |
					   HYDRA_FC_SCSI_CELL_EN |
					   HYDRA_FC_SCCA_ENABLE |
					   HYDRA_FC_SCCB_ENABLE |
					   HYDRA_FC_ARB_BYPASS |
					   HYDRA_FC_MPIC_ENABLE |
					   HYDRA_FC_SLOW_SCC_PCLK |
					   HYDRA_FC_MPIC_IS_MASTER));
	OpenPIC = (volatile struct OpenPIC *)&Hydra->OpenPIC;
	OpenPIC_InitSenses = hydra_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(hydra_openpic_initsenses);
	return 1;
}

#ifdef CONFIG_POWER4
static void
power4_fixup_dev(struct pci_dev *dev)
{
	int i;
	unsigned long offset;

	for (i = 0; i < 6; ++i) {
		if (dev->resource[i].start == 0)
			continue;
		offset = pci_address_offset(dev->bus->number,
					    dev->resource[i].flags);
		if (offset) {
			dev->resource[i].start += offset;
			dev->resource[i].end += offset;
			printk("device %x.%x[%d] now [%lx..%lx]\n",
			       dev->bus->number, dev->devfn, i,
			       dev->resource[i].start,
			       dev->resource[i].end);
		}
		/* zap the 2nd function of the winbond chip */
		if (dev->resource[i].flags & IORESOURCE_IO
		    && dev->bus->number == 0 && dev->devfn == 0x81)
			dev->resource[i].flags &= ~IORESOURCE_IO;
	}
}
#endif /* CONFIG_POWER4 */

void __init
chrp_pcibios_fixup(void)
{
	struct pci_dev *dev;
	int *brp;
	struct device_node *np;
	extern struct pci_ops generic_pci_ops;

#ifndef CONFIG_POWER4
	np = find_devices("device-tree");
	if (np != 0) {
		for (np = np->child; np != NULL; np = np->sibling) {
			if (np->type == NULL || strcmp(np->type, "pci") != 0)
				continue;
			if ((brp = (int *) get_property(np, "bus-range", NULL)) == 0)
				continue;
			if (brp[0] != 0)	/* bus 0 is already done */
				pci_scan_bus(brp[0], &generic_pci_ops, NULL);
		}
	}
#else
	/* XXX kludge for now because we can't properly handle
	   physical addresses > 4GB.  -- paulus */
	pci_scan_bus(0x1e, &generic_pci_ops, NULL);
#endif /* CONFIG_POWER4 */

	/* PCI interrupts are controlled by the OpenPIC */
	pci_for_each_dev(dev) {
		np = find_pci_device_OFnode(dev->bus->number, dev->devfn);
		if ((np != 0) && (np->n_intrs > 0) && (np->intrs[0].line != 0))
			dev->irq = np->intrs[0].line;
		/* these need to be absolute addrs for OF and Matrox FB -- Cort */
		if ( dev->vendor == PCI_VENDOR_ID_MATROX )
		{
			if ( dev->resource[0].start < isa_mem_base )
				dev->resource[0].start += isa_mem_base;
			if ( dev->resource[1].start < isa_mem_base )
				dev->resource[1].start += isa_mem_base;
		}
		/* the F50 identifies the amd as a trident */
		if ( (dev->vendor == PCI_VENDOR_ID_TRIDENT) &&
		      (dev->class>>8 == PCI_CLASS_NETWORK_ETHERNET) )
		{
			dev->vendor = PCI_VENDOR_ID_AMD;
			pcibios_write_config_word(dev->bus->number,
			  dev->devfn, PCI_VENDOR_ID, PCI_VENDOR_ID_AMD);
		}
#ifdef CONFIG_POWER4
		power4_fixup_dev(dev);
#else
		if (dev->bus->number > 0 && python_busnr > 0)
			dev->resource[0].start += dev->bus->number*0x01000000;
#endif
	}
}

static struct {
    /* parent is iomem */
    struct resource ram, pci_mem, isa_mem, pci_io, pci_cfg, rom_exp, flash;
    /* parent is isa_mem */
    struct resource nvram;
} gg2_resources = {
    ram:	{ "RAM", 0x00000000, 0xbfffffff, IORESOURCE_MEM },
    pci_mem:	{ "GG2 PCI mem", 0xc0000000, 0xf6ffffff, IORESOURCE_MEM },
    isa_mem:	{ "GG2 ISA mem", 0xf7000000, 0xf7ffffff },
    pci_io:	{ "GG2 PCI I/O", 0xf8000000, 0xf8ffffff },
    pci_cfg:	{ "GG2 PCI cfg", 0xfec00000, 0xfec7ffff },
    rom_exp:	{ "ROM exp", 0xff000000, 0xff7fffff, },
    flash:	{ "Flash ROM", 0xfff80000, 0xffffffff },
    nvram:	{ "NVRAM", 0xf70e0000, 0xf70e7fff },
};

static void __init gg2_pcibios_fixup(void)
{
	int i;
	extern unsigned long *end_of_DRAM;

	chrp_pcibios_fixup();
	gg2_resources.ram.end = (unsigned long)end_of_DRAM-PAGE_OFFSET;
	for (i = 0; i < 7; i++)
	    request_resource(&iomem_resource,
		    	     &((struct resource *)&gg2_resources)[i]);
	request_resource(&gg2_resources.isa_mem, &gg2_resources.nvram);
}

static void __init gg2_pcibios_fixup_bus(struct pci_bus *bus)
{
    	bus->resource[1] = &gg2_resources.pci_mem;
}

decl_config_access_method(grackle);
decl_config_access_method(indirect);
decl_config_access_method(rtas);

void __init
chrp_setup_pci_ptrs(void)
{
	struct device_node *py;

	ppc_md.pcibios_fixup = chrp_pcibios_fixup;
#ifdef CONFIG_POWER4
	set_config_access_method(rtas);
	pci_dram_offset = 0;
#else /* CONFIG_POWER4 */
        if ( !strncmp("MOT",
                      get_property(find_path_device("/"), "model", NULL),3) )
        {
                pci_dram_offset = 0;
                isa_mem_base = 0xf7000000;
                isa_io_base = 0xfe000000;
                set_config_access_method(grackle);
        }
        else
        {
		if ((py = find_compatible_devices("pci", "IBM,python")) != 0
		    || (py = find_compatible_devices("pci", "IBM,python3.0")) != 0)
		{
			char *name = get_property(find_path_device("/"), "name", NULL);

			/* find out how many pythons */
			while ( (py = py->next) ) python_busnr++;
			set_config_access_method(python);

			/*
			 * We base these values on the machine type but should
			 * try to read them from the python controller itself.
			 * -- Cort
			 */
			if ( !strncmp("IBM,7025-F50", name, 12) )
			{
				pci_dram_offset = 0x80000000;
				isa_mem_base = 0xa0000000;
				isa_io_base = 0x88000000;
			} else if ( !strncmp("IBM,7043-260", name, 12)
				    || !strncmp("IBM,7044-270", name, 12))
			{
				pci_dram_offset = 0x0;
				isa_mem_base = 0xc0000000;
				isa_io_base = 0xf8000000;
			}
                }
                else
                {
			if ( !strncmp("IBM,7043-150", get_property(find_path_device("/"), "name", NULL),12) ||
			     !strncmp("IBM,7046-155", get_property(find_path_device("/"), "name", NULL),12) ||
			     !strncmp("IBM,7046-B50", get_property(find_path_device("/"), "name", NULL),12) )
			{
				pci_dram_offset = 0;
				isa_mem_base = 0x80000000;
				isa_io_base = 0xfe000000;
				pci_config_address = (unsigned int *)0xfec00000;
				pci_config_data = (unsigned char *)0xfee00000;
				set_config_access_method(indirect);
			}
			else
			{
				/* LongTrail */
				pci_dram_offset = 0;
				isa_mem_base = 0xf7000000;
				isa_io_base = 0xf8000000;
				set_config_access_method(gg2);
				ppc_md.pcibios_fixup = gg2_pcibios_fixup;
				ppc_md.pcibios_fixup_bus = gg2_pcibios_fixup_bus;
			}
                }
        }
#endif /* CONFIG_POWER4 */
}

#ifdef CONFIG_PPC64BRIDGE
/*
 * Hack alert!!!
 * 64-bit machines like POWER3 and POWER4 have > 32 bit
 * physical addresses.  For now we remap particular parts
 * of the 32-bit physical address space that the Linux
 * page table gives us into parts of the physical address
 * space above 4GB so we can access the I/O devices.
 */

#ifdef CONFIG_POWER4
static unsigned long pci_address_offset(int busnr, unsigned int flags)
{
	unsigned long offset = 0;

	if (busnr >= 0x1e) {
		if (flags & IORESOURCE_IO)
			offset = -0x100000;
		else if (flags & IORESOURCE_MEM)
			offset = 0x38000000;
	} else if (busnr <= 0xf) {
		if (flags & IORESOURCE_MEM)
			offset = -0x40000000;
		else
	}
	return offset;
}

unsigned long phys_to_bus(unsigned long pa)
{
	if (pa >= 0xf8000000)
		pa -= 0x38000000;
	else if (pa >= 0x80000000 && pa < 0xc0000000)
		pa += 0x40000000;
	return pa;
}

unsigned long bus_to_phys(unsigned int ba, int busnr)
{
	return ba + pci_address_offset(busnr, IORESOURCE_MEM);
}

#else /* CONFIG_POWER4 */
/*
 * For now assume I/O addresses are < 4GB and PCI bridges don't
 * remap addresses on POWER3 machines.
 */
unsigned long phys_to_bus(unsigned long pa)
{
	return pa;
}

unsigned long bus_to_phys(unsigned int ba, int busnr)
{
	return ba;
}
#endif /* CONFIG_POWER4 */
#endif /* CONFIG_PPC64BRIDGE */
