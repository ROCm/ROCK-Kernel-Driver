/*
 $	$Id: pci.c,v 1.1.2.4.2.1 2003/03/31 14:33:18 lethal Exp $
 *	Dreamcast PCI: Supports SEGA Broadband Adaptor only.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/pci.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/dreamcast/sysasic.h>

#define	GAPSPCI_REGS		0x01001400
#define GAPSPCI_DMA_BASE	0x01840000
#define GAPSPCI_DMA_SIZE	32768
#define GAPSPCI_BBA_CONFIG	0x01001600

#define	GAPSPCI_IRQ		HW_EVENT_EXTERNAL

static int gapspci_dma_used;

/* XXX: Uh... */
static struct resource gapspci_io_resource = {
	"GAPSPCI IO",
	0x01001600,
	0x010016ff,
	IORESOURCE_IO
};

static struct resource gapspci_mem_resource = {
	"GAPSPCI mem",
	0x01840000,
	0x01847fff,
	IORESOURCE_MEM
};

static struct pci_ops gapspci_pci_ops;
struct pci_channel board_pci_channels[] = {
	{&gapspci_pci_ops, &gapspci_io_resource, &gapspci_mem_resource, 0, 1},
	{NULL, NULL, NULL, 0, 0},
};

struct pci_fixup pcibios_fixups[] = {
	{0, 0, 0, NULL}
};

#define BBA_SELECTED(bus,devfn) (bus->number==0 && devfn==0)

static int gapspci_read(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 * val)
{
	switch (size) {
	case 1:
		if (BBA_SELECTED(bus, devfn))
			*val = (u8)inb(GAPSPCI_BBA_CONFIG+where);
	else
			*val = (u8)0xff;
		break;
	case 2:
		if (BBA_SELECTED(bus, devfn))
			*val = (u16)inw(GAPSPCI_BBA_CONFIG+where);
	else
			*val = (u16)0xffff;
		break;
	case 4:
		if (BBA_SELECTED(bus, devfn))
		*val = inl(GAPSPCI_BBA_CONFIG+where);
	else
                *val = 0xffffffff;
		break;
	}	
        return PCIBIOS_SUCCESSFUL;
}

static int gapspci_write(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 val)
{
	if (BBA_SELECTED(bus, devfn)) {
		switch (size) {
		case 1:
			if (BBA_SELECTED(bus, devfn))
				outb((u8)val, GAPSPCI_BBA_CONFIG+where);
			break;
		case 2:
			if (BBA_SELECTED(bus, devfn))
				outw((u16)val, GAPSPCI_BBA_CONFIG+where);
			break;
		case 4:
			if (BBA_SELECTED(bus, devfn))
		outl(val, GAPSPCI_BBA_CONFIG+where);
			break;
		}
	}
        return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops gapspci_pci_ops = {
	.read = 	gapspci_read,
	.write = 	gapspci_write,
};


void *pci_alloc_consistent(struct pci_dev *hwdev, size_t size,
			   dma_addr_t * dma_handle)
{
	unsigned long buf;

	if (gapspci_dma_used+size > GAPSPCI_DMA_SIZE)
		return NULL;

	buf = GAPSPCI_DMA_BASE+gapspci_dma_used;

	gapspci_dma_used = PAGE_ALIGN(gapspci_dma_used+size);
	
	printk("pci_alloc_consistent: %ld bytes at 0x%lx\n", (long)size, buf);

	*dma_handle = (dma_addr_t)buf;

	return (void *)P2SEGADDR(buf);
}


void pci_free_consistent(struct pci_dev *hwdev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	/* XXX */
	gapspci_dma_used = 0;
}


void __init pcibios_fixup_bus(struct pci_bus *bus)
{
	struct list_head *ln;
	struct pci_dev *dev;

	for (ln=bus->devices.next; ln != &bus->devices; ln=ln->next) {
		dev = pci_dev_b(ln);
		if (!BBA_SELECTED(bus, dev->devfn)) continue;

		printk("PCI: MMIO fixup to %s\n", dev->dev.name);
		dev->resource[1].start=0x01001700;
		dev->resource[1].end=0x010017ff;
	}
}


static u8 __init no_swizzle(struct pci_dev *dev, u8 * pin)
{
	return PCI_SLOT(dev->devfn);
}


static int __init map_dc_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	return GAPSPCI_IRQ;
}

void __init pcibios_fixup(void) { /* Do nothing. */ }

void __init pcibios_fixup_irqs(void)
{
	pci_fixup_irqs(no_swizzle, map_dc_irq);
}

int __init gapspci_init(void)
{
	int i;
	char idbuf[16];

	for(i=0; i<16; i++)
		idbuf[i]=inb(GAPSPCI_REGS+i);

	if(strncmp(idbuf, "GAPSPCI_BRIDGE_2", 16))
		return -1;

	outl(0x5a14a501, GAPSPCI_REGS+0x18);

	for(i=0; i<1000000; i++);

	if(inl(GAPSPCI_REGS+0x18)!=1)
		return -1;

	outl(0x01000000, GAPSPCI_REGS+0x20);
	outl(0x01000000, GAPSPCI_REGS+0x24);

	outl(GAPSPCI_DMA_BASE, GAPSPCI_REGS+0x28);
	outl(GAPSPCI_DMA_BASE+GAPSPCI_DMA_SIZE, GAPSPCI_REGS+0x2c);

	outl(1, GAPSPCI_REGS+0x14);
	outl(1, GAPSPCI_REGS+0x34);

	gapspci_dma_used=0;

	/* Setting Broadband Adapter */
	outw(0xf900, GAPSPCI_BBA_CONFIG+0x06);
	outl(0x00000000, GAPSPCI_BBA_CONFIG+0x30);
	outb(0x00, GAPSPCI_BBA_CONFIG+0x3c);
	outb(0xf0, GAPSPCI_BBA_CONFIG+0x0d);
	outw(0x0006, GAPSPCI_BBA_CONFIG+0x04);
	outl(0x00002001, GAPSPCI_BBA_CONFIG+0x10);
	outl(0x01000000, GAPSPCI_BBA_CONFIG+0x14);

	return 0;
}

/* Haven't done anything here as yet */
char * __devinit pcibios_setup(char *str)
{
	return str;
}
