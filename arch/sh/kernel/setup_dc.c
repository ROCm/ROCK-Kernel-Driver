/*
 *	$Id: setup_dc.c,v 1.1 2001/04/01 15:02:00 yaegashi Exp $
 *	SEGA Dreamcast support
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/pci.h>

#include <asm/io.h>
#include <asm/irq.h>

#define	GAPSPCI_REGS		0x01001400
#define GAPSPCI_DMA_BASE	0x01840000
#define GAPSPCI_DMA_SIZE	32768
#define GAPSPCI_BBA_CONFIG	0x01001600

#define	GAPSPCI_IRQ		11
#define	GAPSPCI_INTC		0x005f6924

static int gapspci_dma_used;

static struct pci_bus *pci_root_bus;

static void disable_gapspci_irq(unsigned int irq)
{
	unsigned long flags;
	unsigned long intc;

	save_and_cli(flags);
	intc = inl(GAPSPCI_INTC);
	intc &= ~(1<<3);
	outl(intc, GAPSPCI_INTC);
	restore_flags(flags);
}


static void enable_gapspci_irq(unsigned int irq)
{
	unsigned long flags;
	unsigned long intc;

	save_and_cli(flags);
	intc = inl(GAPSPCI_INTC);
	intc |= (1<<3);
	outl(intc, GAPSPCI_INTC);
	restore_flags(flags);
}


static void mask_and_ack_gapspci_irq(unsigned int irq)
{
	unsigned long flags;
	unsigned long intc;

	save_and_cli(flags);
	intc = inl(GAPSPCI_INTC);
	intc &= ~(1<<3);
	outl(intc, GAPSPCI_INTC);
	restore_flags(flags);
}


static void end_gapspci_irq(unsigned int irq)
{
	enable_gapspci_irq(irq);
}


static unsigned int startup_gapspci_irq(unsigned int irq)
{ 
	enable_gapspci_irq(irq);
	return 0;
}


static void shutdown_gapspci_irq(unsigned int irq)
{
	disable_gapspci_irq(irq);
}


static struct hw_interrupt_type gapspci_irq_type = {
	"GAPSPCI-IRQ",
	startup_gapspci_irq,
	shutdown_gapspci_irq,
	enable_gapspci_irq,
	disable_gapspci_irq,
	mask_and_ack_gapspci_irq,
	end_gapspci_irq
};


static u8 __init no_swizzle(struct pci_dev *dev, u8 * pin)
{
	return PCI_SLOT(dev->devfn);
}

static int __init map_od_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	return GAPSPCI_IRQ;
}

int __init setup_dreamcast(void)
{
	gapspci_init();

	printk(KERN_INFO "SEGA Dreamcast support.\n");
#if 0
	printk(KERN_INFO "BCR1: 0x%08x\n", ctrl_inl(0xff800000));
	printk(KERN_INFO "BCR2: 0x%08x\n", ctrl_inw(0xff800004));
	printk(KERN_INFO "WCR1: 0x%08x\n", ctrl_inl(0xff800008));
	printk(KERN_INFO "WCR2: 0x%08x\n", ctrl_inl(0xff80000c));
	printk(KERN_INFO "WCR3: 0x%08x\n", ctrl_inl(0xff800010));
	printk(KERN_INFO "MCR: 0x%08x\n", ctrl_inl(0xff800014));
	printk(KERN_INFO "PCR: 0x%08x\n", ctrl_inw(0xff800018));
/*
 *	BCR1: 0xa3020008
 *	BCR2: 0x0001
 *	WCR1: 0x01110111
 *	WCR2: 0x618066d8
 *	WCR3: 0x07777777
 *	MCR: 0xc00a0e24
 *	PCR: 0x0000
 */
#endif
	return 0;
}


/*
 *	Dreamcast PCI: Supports SEGA Broadband Adaptor only.
 */

#define BBA_SELECTED(dev) (dev->bus->number==0 && dev->devfn==0)

static int gapspci_read_config_byte(struct pci_dev *dev, int where,
                                    u8 * val)
{
	if (BBA_SELECTED(dev))
		*val = inb(GAPSPCI_BBA_CONFIG+where);
	else
                *val = 0xff;

	return PCIBIOS_SUCCESSFUL;
}

static int gapspci_read_config_word(struct pci_dev *dev, int where,
                                    u16 * val)
{
        if (BBA_SELECTED(dev))
		*val = inw(GAPSPCI_BBA_CONFIG+where);
	else
                *val = 0xffff;

        return PCIBIOS_SUCCESSFUL;
}

static int gapspci_read_config_dword(struct pci_dev *dev, int where,
                                     u32 * val)
{
        if (BBA_SELECTED(dev))
		*val = inl(GAPSPCI_BBA_CONFIG+where);
	else
                *val = 0xffffffff;

        return PCIBIOS_SUCCESSFUL;
}

static int gapspci_write_config_byte(struct pci_dev *dev, int where,
                                     u8 val)
{
        if (BBA_SELECTED(dev))
		outb(val, GAPSPCI_BBA_CONFIG+where);

        return PCIBIOS_SUCCESSFUL;
}


static int gapspci_write_config_word(struct pci_dev *dev, int where,
                                     u16 val)
{
        if (BBA_SELECTED(dev))
		outw(val, GAPSPCI_BBA_CONFIG+where);

        return PCIBIOS_SUCCESSFUL;
}

static int gapspci_write_config_dword(struct pci_dev *dev, int where,
                                      u32 val)
{
        if (BBA_SELECTED(dev))
		outl(val, GAPSPCI_BBA_CONFIG+where);

        return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops pci_config_ops = {
        gapspci_read_config_byte,
        gapspci_read_config_word,
        gapspci_read_config_dword,
        gapspci_write_config_byte,
        gapspci_write_config_word,
        gapspci_write_config_dword
};


void pcibios_align_resource(void *data, struct resource *res,
			    unsigned long size)
{
}


void __init pcibios_update_irq(struct pci_dev *dev, int irq)
{
}


void __init pcibios_update_resource(struct pci_dev *dev, struct resource *root,
			     struct resource *res, int resource)
{
}


int pcibios_enable_device(struct pci_dev *dev)
{

	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for (idx = 0; idx < 6; idx++) {
		r = dev->resource + idx;
		if (!r->start && r->end) {
			printk(KERN_ERR
			       "PCI: Device %s not available because"
			       " of resource collisions\n",
			       dev->slot_name);
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (cmd != old_cmd) {
		printk("PCI: enabling device %s (%04x -> %04x)\n",
		       dev->slot_name, old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;

}


void *pci_alloc_consistent(struct pci_dev *hwdev, size_t size,
			   dma_addr_t * dma_handle)
{
	unsigned long buf;

	if (gapspci_dma_used+size > GAPSPCI_DMA_SIZE)
		return NULL;

	buf = GAPSPCI_DMA_BASE+gapspci_dma_used;

	gapspci_dma_used = PAGE_ALIGN(gapspci_dma_used+size);
	
	printk("pci_alloc_consistent: %d bytes at 0x%08x\n", size, buf);

	*dma_handle = (dma_addr_t)buf;

	return (void *)P2SEGADDR(buf);
}


void pci_free_consistent(struct pci_dev *hwdev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
}


void __init
pcibios_fixup_pbus_ranges(struct pci_bus *bus, struct pbus_set_ranges_data *ranges)
{
}                                                                                

void __init pcibios_fixup_bus(struct pci_bus *bus)
{
	struct list_head *ln;
	struct pci_dev *dev;

	for (ln=bus->devices.next; ln != &bus->devices; ln=ln->next) {
		dev = pci_dev_b(ln);
		if (!BBA_SELECTED(dev)) continue;

		printk("PCI: MMIO fixup to %s\n", dev->name);
		dev->resource[1].start=0x01001700;
		dev->resource[1].end=0x010017ff;
	}
}


void __init dreamcast_pcibios_init(void)
{
	pci_root_bus = pci_scan_bus(0, &pci_config_ops, NULL);
	/* pci_assign_unassigned_resources(); */
	pci_fixup_irqs(no_swizzle, map_od_irq);
}


static __init int gapspci_init(void)
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

	/*  */
	irq_desc[GAPSPCI_IRQ].handler = &gapspci_irq_type;

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
