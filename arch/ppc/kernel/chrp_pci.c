/*
 * CHRP pci routines.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/ide.h>
#include <linux/bootmem.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/hydra.h>
#include <asm/prom.h>
#include <asm/gg2.h>
#include <asm/machdep.h>
#include <asm/init.h>
#include <asm/pci-bridge.h>

#include "open_pic.h"
#include "pci.h"


#ifdef CONFIG_POWER4
extern unsigned long pci_address_offset(int, unsigned int);
#endif /* CONFIG_POWER4 */

/* LongTrail */
#define pci_config_addr(dev, offset) \
(GG2_PCI_CONFIG_BASE | ((dev->bus->number)<<16) | ((dev->devfn)<<8) | (offset))

volatile struct Hydra *Hydra = NULL;

/*
 * The VLSI Golden Gate II has only 512K of PCI configuration space, so we
 * limit the bus number to 3 bits
 */

#define cfg_read(val, addr, type, op)	*val = op((type)(addr))
#define cfg_write(val, addr, type, op)	op((type *)(addr), (val))

#define cfg_read_bad(val, size)		*val = bad_##size;
#define cfg_write_bad(val, size)

#define bad_byte	0xff
#define bad_word	0xffff
#define bad_dword	0xffffffffU

#define GG2_PCI_OP(rw, size, type, op)					    \
int __chrp gg2_##rw##_config_##size(struct pci_dev *dev, int off, type val) \
{									    \
	if (dev->bus->number > 7) {					    \
		cfg_##rw##_bad(val, size)				    \
		return PCIBIOS_DEVICE_NOT_FOUND;			    \
	}								    \
	cfg_##rw(val, pci_config_addr(dev, off), type, op);		    \
	return PCIBIOS_SUCCESSFUL;					    \
}

GG2_PCI_OP(read, byte, u8 *, in_8)
GG2_PCI_OP(read, word, u16 *, in_le16)
GG2_PCI_OP(read, dword, u32 *, in_le32)
GG2_PCI_OP(write, byte, u8, out_8)
GG2_PCI_OP(write, word, u16, out_le16)
GG2_PCI_OP(write, dword, u32, out_le32)

static struct pci_ops gg2_pci_ops =
{
	gg2_read_config_byte,
	gg2_read_config_word,
	gg2_read_config_dword,
	gg2_write_config_byte,
	gg2_write_config_word,
	gg2_write_config_dword
};

/*
 * Access functions for PCI config space on IBM "python" host bridges.
 */
#define PYTHON_CFA(b, d, o)	(0x80 | ((b) << 8) | ((d) << 16) \
				 | (((o) & ~3) << 24))

#define PYTHON_PCI_OP(rw, size, type, op, mask)			    	     \
int __chrp								     \
python_##rw##_config_##size(struct pci_dev *dev, int offset, type val) 	     \
{									     \
	struct pci_controller *hose = dev->sysdata;			     \
									     \
	out_be32(hose->cfg_addr,					     \
		 PYTHON_CFA(dev->bus->number, dev->devfn, offset));	     \
	cfg_##rw(val, hose->cfg_data + (offset & mask), type, op);   	     \
	return PCIBIOS_SUCCESSFUL;					     \
}

PYTHON_PCI_OP(read, byte, u8 *, in_8, 3)
PYTHON_PCI_OP(read, word, u16 *, in_le16, 2)
PYTHON_PCI_OP(read, dword, u32 *, in_le32, 0)
PYTHON_PCI_OP(write, byte, u8, out_8, 3)
PYTHON_PCI_OP(write, word, u16, out_le16, 2)
PYTHON_PCI_OP(write, dword, u32, out_le32, 0)

static struct pci_ops python_pci_ops =
{
	python_read_config_byte,
	python_read_config_word,
	python_read_config_dword,
	python_write_config_byte,
	python_write_config_word,
	python_write_config_dword
};

#ifdef CONFIG_POWER4
/*
 * Access functions for PCI config space using RTAS calls.
 */
#define RTAS_PCI_READ_OP(size, type, nbytes)			    	  \
int __chrp								  \
rtas_read_config_##size(struct pci_dev *dev, int offset, type val) 	  \
{									  \
	unsigned long addr = (offset & 0xff) | ((dev->devfn & 0xff) << 8) \
		| ((dev->bus->number & 0xff) << 16);			  \
	unsigned long ret = ~0UL;					  \
	int rval;							  \
									  \
	rval = call_rtas("read-pci-config", 2, 2, &ret, addr, nbytes);	  \
	*val = ret;							  \
	return rval? PCIBIOS_DEVICE_NOT_FOUND: PCIBIOS_SUCCESSFUL;    	  \
}

#define RTAS_PCI_WRITE_OP(size, type, nbytes)				  \
int __chrp								  \
rtas_write_config_##size(struct pci_dev *dev, int offset, type val)	  \
{									  \
	unsigned long addr = (offset & 0xff) | ((dev->devfn & 0xff) << 8) \
		| ((dev->bus->number & 0xff) << 16);			  \
	int rval;							  \
									  \
	rval = call_rtas("write-pci-config", 3, 1, NULL,		  \
			 addr, nbytes, (ulong)val);			  \
	return rval? PCIBIOS_DEVICE_NOT_FOUND: PCIBIOS_SUCCESSFUL;	  \
}

RTAS_PCI_READ_OP(byte, u8 *, 1)
RTAS_PCI_READ_OP(word, u16 *, 2)
RTAS_PCI_READ_OP(dword, u32 *, 4)
RTAS_PCI_WRITE_OP(byte, u8, 1)
RTAS_PCI_WRITE_OP(word, u16, 2)
RTAS_PCI_WRITE_OP(dword, u32, 4)

static struct pci_ops rtas_pci_ops =
{
	rtas_read_config_byte,
	rtas_read_config_word,
	rtas_read_config_dword,
	rtas_write_config_byte,
	rtas_write_config_word,
	rtas_write_config_dword
};
#endif /* CONFIG_POWER4 */

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
	OpenPIC_Addr = &Hydra->OpenPIC;
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
	struct device_node *np;

	/* PCI interrupts are controlled by the OpenPIC */
	pci_for_each_dev(dev) {
		np = pci_device_to_OF_node(dev);
		if ((np != 0) && (np->n_intrs > 0) && (np->intrs[0].line != 0))
			dev->irq = np->intrs[0].line;
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);

		/* the F50 identifies the amd as a trident */
		if ( (dev->vendor == PCI_VENDOR_ID_TRIDENT) &&
		      (dev->class>>8 == PCI_CLASS_NETWORK_ETHERNET) )
		{
			dev->vendor = PCI_VENDOR_ID_AMD;
			pci_write_config_word(dev, PCI_VENDOR_ID,
					      PCI_VENDOR_ID_AMD);
		}
#ifdef CONFIG_POWER4
		power4_fixup_dev(dev);
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

/* this is used by the pmac_pci code too... - paulus */
void process_bridge_ranges(struct pci_controller *hose,
			   struct device_node *dev, int primary)
{
	unsigned int *ranges;
	int rlen = 0;
	int memno = 0;
	struct resource *res;

	hose->io_base_phys = 0;
	ranges = (unsigned int *) get_property(dev, "ranges", &rlen);
	while ((rlen -= 6 * sizeof(unsigned int)) >= 0) {
		res = NULL;
		switch (ranges[0] >> 24) {
		case 1:		/* I/O space */
			if (ranges[2] != 0)
				break;
			hose->io_base_phys = ranges[3];
			hose->io_base_virt = ioremap(ranges[3], ranges[5]);
			if (primary)
				isa_io_base = (unsigned long) hose->io_base_virt;
			res = &hose->io_resource;
			res->flags = IORESOURCE_IO;
			res->start = ranges[2];
			break;
		case 2:		/* memory space */
			memno = 0;
			if (ranges[1] == 0 && ranges[2] == 0
			    && ranges[5] <= (16 << 20)) {
				/* 1st 16MB, i.e. ISA memory area */
				if (primary)
					isa_mem_base = ranges[3];
				memno = 1;
			}
			while (memno < 3 && hose->mem_resources[memno].flags)
				++memno;
			if (memno == 0)
				hose->pci_mem_offset = ranges[3] - ranges[2];
			if (memno < 3) {
				res = &hose->mem_resources[memno];
				res->flags = IORESOURCE_MEM;
				res->start = ranges[3];
			}
			break;
		}
		if (res != NULL) {
			res->name = dev->full_name;
			res->end = res->start + ranges[5] - 1;
			res->parent = NULL;
			res->sibling = NULL;
			res->child = NULL;
		}
		ranges += 6;
	}
}

/* this is largely modeled and stolen after the pmac_pci code -- tgall
 */

static void __init
ibm_add_bridges(struct device_node *dev)
{
	int *bus_range;
	int len, index = 0;
	struct pci_controller *hose;
	volatile unsigned char *cfg;
	unsigned int *dma;
#ifdef CONFIG_POWER3
	unsigned long *opprop = (unsigned long *)
		get_property(find_path_device("/"), "platform-open-pic", NULL);
#endif

	for(; dev != NULL; dev = dev->next, ++index) {
		if (dev->n_addrs < 1) {
			printk(KERN_WARNING "Can't use %s: no address\n",
			       dev->full_name);
			continue;
		}
		bus_range = (int *) get_property(dev, "bus-range", &len);
		if (bus_range == NULL || len < 2 * sizeof(int)) {
			printk(KERN_WARNING "Can't get bus-range for %s\n",
				dev->full_name);
			continue;
		}
		if (bus_range[1] == bus_range[0])
			printk(KERN_INFO "PCI bus %d", bus_range[0]);
		else
			printk(KERN_INFO "PCI buses %d..%d",
			       bus_range[0], bus_range[1]);
		printk(" controlled by %s at %x\n", dev->type,
		       dev->addrs[0].address);

		hose = pcibios_alloc_controller();
		if (!hose) {
			printk("Can't allocate PCI controller structure for %s\n",
				dev->full_name);
			continue;
		}
		hose->arch_data = dev;
		hose->first_busno = bus_range[0];
		hose->last_busno = bus_range[1];
		hose->ops = &python_pci_ops;

		cfg = ioremap(dev->addrs[0].address + 0xf8000, 0x20);
       		hose->cfg_addr = (volatile unsigned int *) cfg;
       		hose->cfg_data = cfg + 0x10;

		process_bridge_ranges(hose, dev, index == 0);

#ifdef CONFIG_POWER3
                openpic_setup_ISU(index, opprop[index+1]);
#endif /* CONFIG_POWER3 */

		/* check the first bridge for a property that we can
		   use to set pci_dram_offset */
		dma = (unsigned int *)
			get_property(dev, "ibm,dma-ranges", &len);
		if (index == 0 && dma != NULL && len >= 6 * sizeof(*dma)) {
			pci_dram_offset = dma[2] - dma[3];
			printk("pci_dram_offset = %lx\n", pci_dram_offset);
		}
	}
}

#ifdef CONFIG_POWER4
void __init
power4_add_bridge(void)
{
	struct pci_controller* hose;

	hose = pcibios_alloc_controller();
	if (!hose)
		return;
	hose->first_busno = 0;
	hose->last_busno = 0xff;
	
	hose->ops = &rtas_pci_ops;
	pci_dram_offset = 0;
}
#endif /* CONFIG_POWER4 */

void __init
chrp_find_bridges(void)
{
	struct device_node *py;
	char *model, *name;
	struct pci_controller* hose;

	ppc_md.pcibios_fixup = chrp_pcibios_fixup;

#ifdef CONFIG_POWER4
	power4_add_bridge();
#else /* CONFIG_POWER4 */
	model = get_property(find_path_device("/"), "model", NULL);
        if (!strncmp("MOT", model, 3)) {
		struct pci_controller* hose;

		hose = pcibios_alloc_controller();
		if (!hose)
			return;
		hose->first_busno = 0;
		hose->last_busno = 0xff;
        	/* Check that please. This must be the root of the OF
        	 * PCI tree (the root host bridge
        	 */
               	hose->arch_data = find_devices("pci");
                setup_grackle(hose, 0x20000);
		return;
        }

	if ((py = find_compatible_devices("pci", "IBM,python")))
	{
		/* XXX xmon_init_scc needs this set and the BAT
		   set up in MMU_init */
		ibm_add_bridges(find_devices("pci"));
		return;
	}


	hose = pcibios_alloc_controller();
	if (!hose)
		return;
	hose->first_busno = 0;
	hose->last_busno = 0xff;
	/* Check that please. This must be the root of the OF
	 * PCI tree (the root host bridge
	 */
	hose->arch_data = find_devices("pci");
	name = get_property(find_path_device("/"), "name", NULL);
	if (!strncmp("IBM,7043-150", name, 12) ||
	    !strncmp("IBM,7046-155", name, 12) ||
	    !strncmp("IBM,7046-B50", name, 12) ) {
		setup_grackle(hose, 0x01000000);
		isa_mem_base = 0x80000000;
		return;
	}

	/* LongTrail */
	hose->ops = &gg2_pci_ops;
	pci_dram_offset = 0;
	isa_mem_base = 0xf7000000;
	hose->io_base_phys = (unsigned long) 0xf8000000;
	hose->io_base_virt = ioremap(hose->io_base_phys, 0x10000);
	isa_io_base = (unsigned long) hose->io_base_virt;
	ppc_md.pcibios_fixup = gg2_pcibios_fixup;
	ppc_md.pcibios_fixup_bus = gg2_pcibios_fixup_bus;
#endif /* CONFIG_POWER4 */
}

#ifdef CONFIG_PPC64BRIDGE
#ifdef CONFIG_POWER4
/*
 * Hack alert!!!
 * 64-bit machines like POWER3 and POWER4 have > 32 bit
 * physical addresses.  For now we remap particular parts
 * of the 32-bit physical address space that the Linux
 * page table gives us into parts of the physical address
 * space above 4GB so we can access the I/O devices.
 */
unsigned long pci_address_offset(int busnr, unsigned int flags)
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
#endif /* CONFIG_POWER4 */
#endif /* CONFIG_PPC64BRIDGE */
