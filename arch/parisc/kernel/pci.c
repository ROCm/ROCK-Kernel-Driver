/* $Id: pci.c,v 1.6 2000/01/29 00:12:05 grundler Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997, 1998 Ralf Baechle
 * Copyright (C) 1999 SuSE GmbH
 * Copyright (C) 1999 Hewlett-Packard Company
 * Copyright (C) 1999, 2000 Grant Grundler
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>		/* for __init and __devinit */
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/string.h>	/* for memcpy() */

#include <asm/system.h>

#ifdef CONFIG_PCI

#undef DEBUG_RESOURCES

#ifdef DEBUG_RESOURCES
#define DBG_RES(x...)	printk(x)
#else
#define DBG_RES(x...)
#endif

/* To be used as: mdelay(pci_post_reset_delay);
**
** post_reset is the time the kernel should stall to prevent anyone from
** accessing the PCI bus once #RESET is de-asserted. 
** PCI spec somewhere says 1 second but with multi-PCI bus systems,
** this makes the boot time much longer than necessary.
** 20ms seems to work for all the HP PCI implementations to date.
*/
int pci_post_reset_delay = 50;

struct pci_port_ops *pci_port;
struct pci_bios_ops *pci_bios;

struct pci_hba_data *hba_list = NULL;
int hba_count = 0;

/*
** parisc_pci_hba used by pci_port->in/out() ops to lookup bus data.
*/
#define PCI_HBA_MAX 32
static struct pci_hba_data *parisc_pci_hba[PCI_HBA_MAX];


/********************************************************************
**
** I/O port space support
**
*********************************************************************/

#define PCI_PORT_HBA(a) ((a)>>16)
#define PCI_PORT_ADDR(a) ((a) & 0xffffUL)

/* KLUGE : inb needs to be defined differently for PCI devices than
** for other bus interfaces. Doing this at runtime sucks but is the
** only way one driver binary can support devices on different bus types.
**
*/

#define PCI_PORT_IN(type, size) \
u##size in##type (int addr) \
{ \
	int b = PCI_PORT_HBA(addr); \
	u##size d = (u##size) -1; \
	ASSERT(pci_port); /* make sure services are defined */ \
	ASSERT(parisc_pci_hba[b]); /* make sure ioaddr are "fixed up" */ \
	if (parisc_pci_hba[b] == NULL) { \
		printk(KERN_WARNING "\nPCI Host Bus Adapter %d not registered. in" #size "(0x%x) returning -1\n", b, addr); \
	} else { \
		d = pci_port->in##type(parisc_pci_hba[b], PCI_PORT_ADDR(addr)); \
	} \
	return d; \
}

PCI_PORT_IN(b,  8)
PCI_PORT_IN(w, 16)
PCI_PORT_IN(l, 32)


#define PCI_PORT_OUT(type, size) \
void out##type (u##size d, int addr) \
{ \
	int b = PCI_PORT_HBA(addr); \
	ASSERT(pci_port); \
	pci_port->out##type(parisc_pci_hba[b], PCI_PORT_ADDR(addr), d); \
}

PCI_PORT_OUT(b,  8)
PCI_PORT_OUT(w, 16)
PCI_PORT_OUT(l, 32)



/*
 * BIOS32 replacement.
 */
void pcibios_init(void)
{
	ASSERT(pci_bios != NULL);

	if (pci_bios)
	{
		if (pci_bios->init) {
			(*pci_bios->init)();
		} else {
			printk(KERN_WARNING "pci_bios != NULL but init() is!\n");
		}
	}
}


/* Called from pci_do_scan_bus() *after* walking a bus but before walking PPBs. */
void pcibios_fixup_bus(struct pci_bus *bus)
{
	ASSERT(pci_bios != NULL);

        /* If this is a bridge, get the current bases */
	if (bus->self) {
		pci_read_bridge_bases(bus);
	}

	if (pci_bios) {
		if (pci_bios->fixup_bus) {
			(*pci_bios->fixup_bus)(bus);
		} else {
			printk(KERN_WARNING "pci_bios != NULL but fixup_bus() is!\n");
		}
	}
}


char *pcibios_setup(char *str)
{
	return str;
}

#endif /* defined(CONFIG_PCI) */



/* -------------------------------------------------------------------
** linux-2.4: NEW STUFF 
** --------------------
*/

/*
** Used in drivers/pci/quirks.c
*/
struct pci_fixup pcibios_fixups[] = { {0} };


/*
** called by drivers/pci/setup.c:pdev_fixup_irq()
*/
void __devinit pcibios_update_irq(struct pci_dev *dev, int irq)
{
/*
** updates IRQ_LINE cfg register to reflect PCI-PCI bridge skewing.
**
** Calling path for Alpha is:
**  alpha/kernel/pci.c:common_init_pci(swizzle_func, pci_map_irq_func )
**	drivers/pci/setup.c:pci_fixup_irqs()
**	    drivers/pci/setup.c:pci_fixup_irq()	(for each PCI device)
**		invoke swizzle and map functions
**	        alpha/kernel/pci.c:pcibios_update_irq()
**
** Don't need this for PA legacy PDC systems.
**
** On PAT PDC systems, We only support one "swizzle" for any number
** of PCI-PCI bridges deep. That's how bit3 PCI expansion chassis
** are implemented. The IRQ lines are "skewed" for all devices but
** *NOT* routed through the PCI-PCI bridge. Ie any device "0" will
** share an IRQ line. Legacy PDC is expecting this IRQ line routing
** as well.
**
** Unfortunately, PCI spec allows the IRQ lines to be routed
** around the PCI bridge as long as the IRQ lines are skewed
** based on the device number...<sigh>...
**
** Lastly, dino.c might be able to use pci_fixup_irq() to
** support RS-232 and PS/2 children. Not sure how but it's
** something to think about.
*/
}


/* ------------------------------------
**
** Program one BAR in PCI config space.
**
** ------------------------------------
** PAT PDC systems need this routine. PA legacy PDC does not.
**
** Used by alpha/arm: 
** alpha/kernel/pci.c:common_init_pci()
** (or arm/kernel/pci.c:pcibios_init())
**    drivers/pci/setup.c:pci_assign_unassigned_resources()
**        drivers/pci/setup.c:pdev_assign_unassigned_resources()
**            arch/<foo>/kernel/pci.c:pcibios_update_resource()
**
** When BAR's are configured by linux, this routine
** will update configuration space with the "normalized"
** address. "root" indicates where the range starts and res
** is some portion of that range.
**
** For all PA-RISC systems except V-class, root->start would be zero.
**
** PAT PDC can tell us which MMIO ranges are available or already in use.
** I/O port space and such are not memory mapped anyway for PA-Risc.
*/
void __devinit
pcibios_update_resource(
	struct pci_dev *dev,
	struct resource *root,
	struct resource *res,
	int barnum
	)
{
	int where;
	u32 barval = 0;

	DBG_RES("pcibios_update_resource(%s, ..., %d) [%lx,%lx]/%x\n",
		dev->slot_name,
		barnum, res->start, res->end, (int) res->flags);

	if (barnum >= PCI_BRIDGE_RESOURCES) {
		/* handled in pbus_set_ranges_data() */
		return;
	}

	if (barnum == PCI_ROM_RESOURCE) {
		where = PCI_ROM_ADDRESS;
	} else {
		/* 0-5  standard PCI "regions" */
		where = PCI_BASE_ADDRESS_0 + (barnum * 4);
	}

	if (res->flags & IORESOURCE_IO) {
		barval = PCI_PORT_ADDR(res->start);
	} else if (res->flags & IORESOURCE_MEM) {
		/* This should work for VCLASS too */
		barval = res->start & 0xffffffffUL;
	} else {
		panic("pcibios_update_resource() WTF? flags not IO or MEM");
	}

	pci_write_config_dword(dev, where, barval);

/* XXX FIXME - Elroy does support 64-bit (dual cycle) addressing.
** But at least one device (Symbios 53c896) which has 64-bit BAR
** doesn't actually work right with dual cycle addresses.
** So ignore the whole mess for now.
*/

	if ((res->flags & (PCI_BASE_ADDRESS_SPACE
			   | PCI_BASE_ADDRESS_MEM_TYPE_MASK))
	    == (PCI_BASE_ADDRESS_SPACE_MEMORY
		| PCI_BASE_ADDRESS_MEM_TYPE_64)) {
		pci_write_config_dword(dev, where+4, 0);
		printk(KERN_WARNING "PCI: dev %s type 64-bit\n", dev->name);
	}
}

/*
** Called by pci_set_master() - a driver interface.
**
** Legacy PDC guarantees to set:
**      Map Memory BAR's into PA IO space.
**      Map Expansion ROM BAR into one common PA IO space per bus.
**      Map IO BAR's into PCI IO space.
**      Command (see below)
**      Cache Line Size
**      Latency Timer
**      Interrupt Line
**	PPB: secondary latency timer, io/mmio base/limit,
**		bus numbers, bridge control
**
*/
void
pcibios_set_master(struct pci_dev *dev)
{
	u8 lat;
	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &lat);
	if (lat >= 16) return;

	/*
	** HP generally has fewer devices on the bus than other architectures.
	*/
	printk("PCIBIOS: Setting latency timer of %s to 128\n", dev->slot_name);
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x80);
}


/*
** called by drivers/pci/setup-res.c:pbus_set_ranges().
*/
void pcibios_fixup_pbus_ranges(
	struct pci_bus *bus,
	struct pbus_set_ranges_data *ranges
	)
{
	/*
	** I/O space may see busnumbers here. Something
	** in the form of 0xbbxxxx where bb is the bus num
	** and xxxx is the I/O port space address.
	** Remaining address translation are done in the
	** PCI Host adapter specific code - ie dino_out8.
	*/
	ranges->io_start = PCI_PORT_ADDR(ranges->io_start);
	ranges->io_end   = PCI_PORT_ADDR(ranges->io_end);

	DBG_RES("pcibios_fixup_pbus_ranges(%02x, [%lx,%lx %lx,%lx])\n", bus->number,
		ranges->io_start, ranges->io_end,
		ranges->mem_start, ranges->mem_end);
}

#define MAX(val1, val2)   ((val1) > (val2) ? (val1) : (val2))


/*
** pcibios align resources() is called everytime generic PCI code
** wants to generate a new address. The process of looking for
** an available address, each candidate is first "aligned" and
** then checked if the resource is available until a match is found.
**
** Since we are just checking candidates, don't use any fields other
** than res->start.
*/
void __devinit
pcibios_align_resource(void *data, struct resource *res, unsigned long size)
{
	unsigned long mask, align;

	DBG_RES("pcibios_align_resource(%s, (%p) [%lx,%lx]/%x, 0x%lx)\n",
		((struct pci_dev *) data)->slot_name,
		res->parent, res->start, res->end, (int) res->flags, size);

	/* has resource already been aligned/assigned? */
	if (res->parent)
		return;

	/* If it's not IO, then it's gotta be MEM */
	align = (res->flags & IORESOURCE_IO) ? PCIBIOS_MIN_IO : PCIBIOS_MIN_MEM;

	/* Align to largest of MIN or input size */
	mask = MAX(size, align) - 1;
	res->start += mask;
	res->start &= ~mask;

	/*
	** WARNING : caller is expected to update "end" field.
	** We can't since it might really represent the *size*.
	** The difference is "end = start + size" vs "end += size".
	*/
}


#define ROUND_UP(x, a)		(((x) + (a) - 1) & ~((a) - 1))

void __devinit
pcibios_size_bridge(struct pci_bus *bus, struct pbus_set_ranges_data *outer)
{
	struct pbus_set_ranges_data inner;
	struct pci_dev *dev;
	struct pci_dev *bridge = bus->self;
	struct list_head *ln;

	/* set reasonable default "window" for pcibios_align_resource */
	inner.io_start  = inner.io_end  = 0;
	inner.mem_start = inner.mem_end = 0;

	/* Collect information about how our direct children are layed out. */
	for (ln=bus->devices.next; ln != &bus->devices; ln=ln->next) {
		int i;
		dev = pci_dev_b(ln);

		/* Skip bridges here - we'll catch them below */
		if ((dev->class >> 8) == PCI_CLASS_BRIDGE_PCI)
			continue;

		for (i = 0; i < PCI_NUM_RESOURCES; i++) {
			struct resource res;
			unsigned long size;

			if (dev->resource[i].flags == 0)
				continue;

			memcpy(&res, &dev->resource[i], sizeof(res));
			size = res.end - res.start + 1;

			if (res.flags & IORESOURCE_IO) {
				res.start = inner.io_end;
				pcibios_align_resource(dev, &res, size);
				inner.io_end += res.start + size;
			} else if (res.flags & IORESOURCE_MEM) {
				res.start = inner.mem_end;
				pcibios_align_resource(dev, &res, size);
				inner.mem_end = res.start + size;
			}

		DBG_RES("    %s  inner size %lx/%x IO %lx MEM %lx\n",
			dev->slot_name,
			size, res.flags, inner.io_end, inner.mem_end);
		}
	}

	/* And for all of the subordinate busses. */
	for (ln=bus->children.next; ln != &bus->children; ln=ln->next)
		pcibios_size_bridge(pci_bus_b(ln), &inner);

	/* turn the ending locations into sizes (subtract start) */
	inner.io_end -= inner.io_start - 1;
	inner.mem_end -= inner.mem_start - 1;

	/* Align the sizes up by bridge rules */
	inner.io_end = ROUND_UP(inner.io_end, 4*1024) - 1;
	inner.mem_end = ROUND_UP(inner.mem_end, 1*1024*1024) - 1;

	/* PPB - PCI bridge Device will normaller also have "outer" != NULL. */
	if (bridge) {
		/* Adjust the bus' allocation requirements */
		/* PPB's pci device Bridge resources */

		bus->resource[0] = &bridge->resource[PCI_BRIDGE_RESOURCES];
		bus->resource[1] = &bridge->resource[PCI_BRIDGE_RESOURCES + 1];
		
		bus->resource[0]->start = bus->resource[1]->start  = 0;
		bus->resource[0]->parent= bus->resource[1]->parent = NULL;

		bus->resource[0]->end    = inner.io_end;
		bus->resource[0]->flags  = IORESOURCE_IO;

		bus->resource[1]->end    = inner.mem_end;
		bus->resource[1]->flags  = IORESOURCE_MEM;
	}

	/* adjust parent's resource requirements */
	if (outer) {
		outer->io_end = ROUND_UP(outer->io_end, 4*1024);
		outer->io_end += inner.io_end;

		outer->mem_end = ROUND_UP(outer->mem_end, 1*1024*1024);
		outer->mem_end += inner.mem_end;
	}
}

#undef ROUND_UP


int __devinit
pcibios_enable_device(struct pci_dev *dev)
{
	u16 cmd, old_cmd;
	int idx;

	/*
	** The various platform PDC's (aka "BIOS" for PCs) don't
	** enable all the same bits. We just make sure they are here.
	*/
	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;

	/*
	** See if any resources have been allocated
	*/
        for (idx=0; idx<6; idx++) {
		struct resource *r = &dev->resource[idx];
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}

	/*
	** System error and Parity Error reporting are enabled by default.
	** Devices that do NOT want those behaviors should clear them
	** (eg PCI graphics, possibly networking).
	** Interfaces like SCSI certainly should not. We want the
	** system to crash if a system or parity error is detected.
	** At least until the device driver can recover from such an error.
	*/
	cmd |= (PCI_COMMAND_SERR | PCI_COMMAND_PARITY);

	if (cmd != old_cmd) {
		printk("PCIBIOS: Enabling device %s (%04x -> %04x)\n",
			dev->slot_name, old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}

	return 0;
}


void __devinit
pcibios_assign_unassigned_resources(struct pci_bus *bus)
{
	struct list_head *ln;

        for (ln=bus->devices.next; ln != &bus->devices; ln=ln->next)
	{
		pdev_assign_unassigned_resources(pci_dev_b(ln));
	}

        /* And for all of the sub-busses.  */
	for (ln=bus->children.next; ln != &bus->children; ln=ln->next)
		pcibios_assign_unassigned_resources(pci_bus_b(ln));

}

/*
** PARISC specific (unfortunately)
*/
void pcibios_register_hba(struct pci_hba_data *hba)
{
	hba->next = hba_list;
	hba_list = hba;

	ASSERT(hba_count < PCI_HBA_MAX);

	/*
	** pci_port->in/out() uses parisc_pci_hba to lookup parameter.
	*/
	parisc_pci_hba[hba_count] = hba;
	hba->hba_num = hba_count++;
}
